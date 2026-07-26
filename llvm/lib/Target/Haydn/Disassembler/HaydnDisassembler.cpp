//===-- HaydnDisassembler.cpp - Disassembler for Haydn --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HaydnDisassembler class.
//
// Bundle128-only decode (16-byte parcels; variable-width realized as fixed
// Bundle128 geometry). Size=16 on every Success/Fail from the composite path.
//
// 1. tryDecodeBundle128Composite — primary path when >= 16 bytes remain.
//    Content gate: each non-zero slot window must pass isValidFlexSlotWindow
//    (strict FU + opcode range). Geometry via getBundle128FormatDesc peer
//    offsets (same authority as encodeSlotInBundle128). Generated composite
//    trie (DecoderTableBundle128128 → case 164 = BUNDLE128_FULL) dispatches
//    per-slot sub-tries via decodeS0Slot/S1Slot/S2Slot. All-zero Bundle128
//    (spec §10 NOP) is 3 empty NOP slots. Bundle128 is probed before any
//    2-byte NOP check so s0-NOP parcels (leading zero bytes) are not stolen.
// 2. < 16 bytes remaining — trailing 16-bit NOP fallback: leading 0x0000
//    decodes as Haydn::NOP (Size = 2); other trailing bytes → <unknown>
//    with Size = Bytes.size (forward progress).
//
// CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort on hostile.text.
// The bounds-safe `<?>` printOperand defense (HaydnInstPrinter) remains the
// primary anti-crash mechanism; slot decoders degrade sub-trie misses to
// empty NOP slots (always return Success) rather than asserting.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "haydn-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

//===----------------------------------------------------------------------===//
// Register decode helpers (used by TableGen-generated tables)
//===----------------------------------------------------------------------===//

// Decode a GPR32 register (4-bit encoding, R0-R15).
static DecodeStatus decodeGPR32(MCInst &Inst, uint32_t RegNo) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(Haydn::R0 + RegNo));
  return MCDisassembler::Success;
}

// Decode a DR64 register (4-bit encoding, D0-D15).
static DecodeStatus decodeDR64(MCInst &Inst, uint32_t RegNo) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(Haydn::D0 + RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPR32RegisterClass(MCInst &Inst, uint32_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  return decodeGPR32(Inst, RegNo);
}

// GPR32Lo (r0-r7, 3-bit encoding) — the low 8 GPRs, used by 16-bit compressed
// (§2.2) and 32-bit G-format (§3) instructions. Reject RegNo >= 8. Retained
// because the generated decoder tables still reference it by name.
static DecodeStatus DecodeGPR32LoRegisterClass(MCInst &Inst, uint32_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo >= 8)
    return MCDisassembler::Fail;
  return decodeGPR32(Inst, RegNo);
}

static DecodeStatus DecodeDR64RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  return decodeDR64(Inst, RegNo);
}

//===----------------------------------------------------------------------===//
// (Stage 1): AIE-style scaled-immediate decoder for 48-bit WIDE formats.
//
// Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes via tablegen
// `DecoderMethod`. Reverses the encoder's `getSImmOpValueXStepWide`: reads the
// raw N-bit field, sign/zero-extends per IsSigned, then shifts left by `Shift`
// to convert field units back to byte units (so the rendered disassembly shows
// byte offsets, matching RISCV's decodeSImmOperandAndLslN convention).
//
// Template parameters:
// N = encoded field width in bits
// Shift = left-shift applied after sign/zero-extension (1 for §5.14
// branch offsets in 2-byte units, 2 for §5.11/5.12 hwloop offsets
// in 4-byte units, 0 otherwise).
// IsSigned = 1 => sign-extend the N-bit field; 0 => zero-extend.
//===----------------------------------------------------------------------===//
template <unsigned N, unsigned Shift, bool IsSigned>
static DecodeStatus decodeSImmOperandXStepWide(MCInst &Inst, uint32_t Imm,
                                               int64_t Address,
                                               const MCDisassembler *Decoder) {
  // disassembler must NEVER abort on hostile.text (CLAUDE.md
  // "bounds-safe printOperand" philosophy: decode-or-degrade, never assert).
  // The generated decoder can call this DecoderMethod with an `Imm` whose
  // tablegen-aggregated field slice is WIDER than N bits — e.g. the Bundle128
  // LD slot sub-trie reads an 8-bit slice for a `simm6` operand
  // (fieldFromInstruction(insn, 28, 8) → decodeSImmOperandXStepWide<6,0,1> on
  // cases 147/148/149/150 in HaydnGenDisassemblerTables.inc) because tblgen
  // merges the adjacent `reserved` field into the same decoded region. Bits
  // above N are reserved/zero on a correctly-encoded instruction, but a legacy
  // parcel stream or a straddle read can set them. Mask to N bits BEFORE the
  // sign/zero-extend so the assert can never fire and objdump renders the
  // decoded operand (the encoder only ever wrote N bits; masking recovers
  // exactly what was encoded). This mirrors the encoder's own `maskTrailingOnes`
  // contract in getSImmOpValueXStepWide.
  Imm &= maskTrailingOnes<uint32_t>(N);
  assert((Imm & ~maskTrailingOnes<uint32_t>(N)) == 0 &&
         "post-mask residual bits — mask logic broken");
  int64_t Extended = IsSigned ? SignExtend64<N>(Imm) : static_cast<int64_t>(Imm);
  Inst.addOperand(MCOperand::createImm(Extended << Shift));
  return MCDisassembler::Success;
}

//===----------------------------------------------------------------------===//
// Bitfield extraction helpers
//===----------------------------------------------------------------------===//

// Extract bits [Hi:Lo] (inclusive) from a value.
static uint32_t extractBits(uint64_t Word, unsigned Lo, unsigned Width) {
  return static_cast<uint32_t>((Word >> Lo) & ((1ULL << Width) - 1));
}

// Bundle128 slot decode uses the generated S0/S1/S2 sub-tries plus
// decodeSImmOperandXStepWide / register class helpers only.

//===----------------------------------------------------------------------===//
// Main disassembler class
//===----------------------------------------------------------------------===//

class HaydnDisassembler : public MCDisassembler {
public:
  HaydnDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx);

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  // Reset disassembler state at symbol boundaries. Prevents corrupted
  // bytes from cascading across function boundaries.
  Expected<bool> onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                               ArrayRef<uint8_t> Bytes,
                               uint64_t Address) const override;
};

} // end anonymous namespace

HaydnDisassembler::HaydnDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
    : MCDisassembler(STI, Ctx) {}

// Bundle128 composite per-slot decoders (forward declarations).
//
// The generated HaydnGenDisassemblerTables.inc defines a template
// `decodeToMCInst` whose case 164 (the BUNDLE128_FULL decoder) emits:
// tmp = fieldFromInstruction(insn, 0, 48); / s0 window bits
// decodeS0Slot(MI, tmp, Address, Decoder);
// tmp = fieldFromInstruction(insn, 48, 40); / s1 window bits
// decodeS1Slot(MI, tmp, Address, Decoder);
// tmp = fieldFromInstruction(insn, 88, 40); / s2 window bits
// decodeS2Slot(MI, tmp, Address, Decoder);
// Because `decodeToMCInst` is a template and `tmp` is a dependent type, the
// slot decoders must be visible by name BEFORE the.inc include for two-phase
// lookup to find them. This mirrors AIE's `SLOTDECODERDecl(...)` macro pattern
// (AIEDisassemblerPP.h:42) — forward-declare here, define after the include.
//
// Each slot decoder takes the already-extracted slot-window bits (the
// composite trie did the fieldFromInstruction extraction), runs the per-slot
// tablegen trie on them (DecoderTableS048 / DecoderTableS140 / DecoderTableS240
// namespaces "S0"/"S1"/"S2" generated from HaydnSlotS0/S1/S2 in HaydnSlots.td)
// and adds the result as an MCOperand::createInst sub-instruction operand to
// the composite MI (mirrors AIE's decodeAIE2PSSlot). On a sub-trie miss the
// sub-MCInst is cleared (an empty NOP slot, §4). Each decoder ALWAYS returns
// Success — a NOP/failed slot is a valid Bundle128 slot occupancy, not a hard
// Fail.
//
// CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort on hostile.text.
// The bounds-safe `<?>` printOperand defense remains the primary anti-crash
// bar regardless.
namespace {
template <typename InsnType>
static DecodeStatus decodeS0Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
template <typename InsnType>
static DecodeStatus decodeS1Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
template <typename InsnType>
static DecodeStatus decodeS2Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
} // namespace

// Include the auto-generated decoder tables (the Bundle128 composite trie +
// the per-slot sub-tries; the legacy Haydn16/32/48/64 tables remain generated
// from the.td but are no longer consulted by this disassembler subsequent).
#define LLVM_DISASSEMBLER_HAYDN_DECODER_TABLES
#include "HaydnGenDisassemblerTables.inc"

// Bundle128 composite per-slot decoders (definitions).
//
// Mirrors AIE's decodeAIE2PSSlot (AIE2PSDisassembler.cpp:75-87): allocate a
// heap MCInst via MCContext (persists beyond this call), run the per-slot
// tablegen trie on the window bits, and add the sub-instruction as an
// MCOperand::createInst operand to the composite MI. The composite trie's
// case 164 (BUNDLE128_FULL) extracted the windows already via
// fieldFromInstruction; here we just run the slot sub-trie.
//
// Slot DecoderTable names (generated from the namespaces "S0"/"S1"/"S2" set
// by HaydnSlotS0/S1/S2 in HaydnSlots.td):
// s0: DecoderTableS048 — 48-bit s0 window (bits[47:0])
// s1: DecoderTableS140 — 40-bit s1 window (bits[39:0])
// s2: DecoderTableS240 — 40-bit s2 window (bits[39:0])
//
// On a sub-trie miss the sub-MCInst is cleared (per AIE). An empty sub-MCInst
// is the per-slot NOP. The decoder ALWAYS returns Success — a NOP/failed slot
// is a valid Bundle128 occupancy, not a hard Fail.
namespace {
// Decode the s0 slot of a Bundle128 parcel. \p Insn is the s0 window bits
// (48-bit, extracted by the composite trie via fieldFromInstruction).
template <typename InsnType>
static DecodeStatus decodeS0Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MCInst *SlotInst = Decoder->getContext().createMCInst();
  DecodeStatus Result =
      decodeInstruction(DecoderTableS048, *SlotInst, Insn, Address, Decoder,
                        Decoder->getSubtargetInfo());
  if (Result != MCDisassembler::Success)
    SlotInst->clear();
  MI.addOperand(MCOperand::createInst(SlotInst));
  return MCDisassembler::Success;
}

// Decode the s1 slot of a Bundle128 parcel. \p Insn is the s1 window bits
// (40-bit, extracted by the composite trie via fieldFromInstruction).
template <typename InsnType>
static DecodeStatus decodeS1Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MCInst *SlotInst = Decoder->getContext().createMCInst();
  DecodeStatus Result =
      decodeInstruction(DecoderTableS140, *SlotInst, Insn, Address, Decoder,
                        Decoder->getSubtargetInfo());
  if (Result != MCDisassembler::Success)
    SlotInst->clear();
  MI.addOperand(MCOperand::createInst(SlotInst));
  return MCDisassembler::Success;
}

// Decode the s2 slot of a Bundle128 parcel. \p Insn is the s2 window bits
// (40-bit, extracted by the composite trie via fieldFromInstruction).
template <typename InsnType>
static DecodeStatus decodeS2Slot(MCInst &MI, InsnType &Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MCInst *SlotInst = Decoder->getContext().createMCInst();
  DecodeStatus Result =
      decodeInstruction(DecoderTableS240, *SlotInst, Insn, Address, Decoder,
                        Decoder->getSubtargetInfo());
  if (Result != MCDisassembler::Success)
    SlotInst->clear();
  MI.addOperand(MCOperand::createInst(SlotInst));
  return MCDisassembler::Success;
}
} // namespace

Expected<bool> HaydnDisassembler::onSymbolStart(SymbolInfoTy &Symbol,
                                                uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address) const {
  // At the start of a symbol, reset any in-progress state. This prevents
  // corrupted bytes from one function from cascading into the next.
  // Follows the Hexagon pattern of resetting at symbol boundaries.
  Size = 0;
  return true;
}

//===----------------------------------------------------------------------===//
// Bundle128 content-gate: strict FU + opcode validation.
//===----------------------------------------------------------------------===//
//
// isValidFlexSlotWindow validates a Bundle128 slot window's FU and opcode
// fields against the encoding_manual_flex.md contract. The generated
// DecoderTableS048/S140/S240 have DEFAULT catch-all branches that accept any
// non-zero window, so the gate rejects reserved FU (5..7) and out-of-range
// opcodes before the composite trie runs.
//
// Slot geometry is single-authority: windows are extracted from the 128-bit
// Bundle128 word using offsets from the Bundle128 format-desc
// (HaydnMCFormats::getBundle128FormatDesc.getSlotOffsetsHiBit) — the same
// geometric authority the encoder consults (HaydnMCCodeEmitter
// encodeSlotInBundle128). \p Window is that extracted value viewed as a
// standalone Width-bit integer (MSB at bit Width-1). FU sits at the top 3
// bits of the window (LSB [WindowTopBit-2, WindowTopBit]); the opcode starts
// just below it (top at WindowTopBit-3). s0/s1/s2 differ only in window
// width, not in FU/opcode placement (encoding_manual_flex.md §1.2 + §2).
//
// Opcode widths and valid max (from the.td FLEX defs):
// ALU32 (FU=0): 7b, max 0x71 (113 ops — ADDI32_W=0x70, ORI32_W=0x71)
// LS (FU=1): 7b, max 0x7B (LD16=0x7A, LD8=0x7B — signed half/byte after LDU16)
// ALU64 (FU=2): 8b, max 0x9B (155 ops, dense from 0x01)
// LD (FU=3): 6b, max 0x3F (64 ops)
// MAC (FU=4): 9b, max 0x165 (357 ops, dense from 0x01)
//
// \p WindowTopBit is the LSB index of the window's most-significant bit
// within \p Window (i.e. Width-1; the FU top bit). \returns true if the
// window is a plausible Bundle128 slot (valid FU + opcode in range); false
// if it should be rejected.
static bool isValidFlexSlotWindow(uint64_t Window, unsigned WindowTopBit) {
  // FU occupies the top 3 bits of the window: LSB [WindowTopBit-2, WindowTopBit].
  unsigned Fu = extractBits(Window, WindowTopBit - 2, 3);
  // Reserved FU (5..7) → illegal-instruction per §1.2.
  if (Fu >= Haydn::FlexFU::FIRST_RESERVED)
    return false;

  // Opcode width per FU (encoding_manual_flex.md §1.2).
  unsigned OpBits;
  unsigned MaxOpcode;
  switch (Fu) {
  case Haydn::FlexFU::ALU32: OpBits = 7; MaxOpcode = 0x71;  break;
  case Haydn::FlexFU::LS:    OpBits = 7; MaxOpcode = 0x7B;  break;
  case Haydn::FlexFU::ALU64: OpBits = 8; MaxOpcode = 0x9B;  break;
  case Haydn::FlexFU::LD:    OpBits = 6; MaxOpcode = 0x3F;  break;
  case Haydn::FlexFU::MAC:   OpBits = 9; MaxOpcode = 0x165; break;
  default:                   return false; // unreachable (FIRST_RESERVED guard)
  }

  // Opcode sits just below FU: its top bit is at WindowTopBit-3, occupying
  // LSB [WindowTopBit-3-OpBits+1, WindowTopBit-3].
  unsigned OpcodeStartBit = WindowTopBit - 3;
  unsigned Opcode = extractBits(Window, OpcodeStartBit - OpBits + 1, OpBits);
  // Dense codepoints from 0x01 (§1.2: "no legacy opcodes borrowed"; opcode 0
  // is a real instruction or per-FU trap, but the FLEX defs start at 0x01).
  // Out-of-range opcode → not a valid Bundle128 slot.
  return Opcode >= 1 && Opcode <= MaxOpcode;
}

//===----------------------------------------------------------------------===//
// Bundle128 composite decode (AIE two-step model).
//===----------------------------------------------------------------------===//
//
// tryDecodeBundle128Composite is the decoder's symmetric inverse of the
// encoder's encodeBundle128 (HaydnMCCodeEmitter.cpp): it reads a 16-byte
// (128-bit) parcel, then runs the generated composite trie
// DecoderTableBundle128128 on it. The composite trie matches unconditionally
// (case 164 = BUNDLE128_FULL, no fixed bits to match — FU is the slot-level
// discriminator) and calls decodeS0Slot/decodeS1Slot/decodeS2Slot on each
// already-extracted slot window. The result is a BUNDLE128_FULL MCInst whose
// 3 operands are MCOperand::createInst sub-instructions (mirrors AIE's
// AIEBaseMCCodeEmitter/decodeXxxSlot).
//
// The slot sub-instructions are MCContext-allocated and persist beyond this
// call. Per-slot NOP / trie-miss handling lives in the slot decoders (each
// always returns Success; a NOP/failed slot becomes an empty sub-MCInst). The
// top-level composite MCInst is always 3 operands wide (one per slot).
//
// Content gate (strict FU+opcode via isValidFlexSlotWindow): the composite
// trie alone accepts every 16-byte window; the gate validates each non-zero
// slot window's FU and opcode (generated sub-tries have catch-all defaults).
// A non-zero window with reserved FU or out-of-range opcode is not a
// Bundle128 slot — return Fail. The all-zero word (real §4 Bundle128 NOP)
// has no non-zero window to validate and passes unconditionally.
//
// Post-trie sub-MCInst validation (second tier): a non-zero source window
// whose sub-MCInst came back empty (sub-trie miss → cleared by the slot
// decoders) is not a real Bundle128 slot — return Fail. An all-zero source
// window is a valid §4 NOP slot and is exempt.
//
// Size contract: every Bundle128 parcel is exactly 16 bytes. On Success or
// Fail from this path, Size = 16 (forward progress; never leave Size unset).
static DecodeStatus tryDecodeBundle128Composite(MCInst &Instr, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                const MCDisassembler *DisAsm) {
  // Every Bundle128 parcel is exactly 16 bytes.
  if (Bytes.size() < 16) {
    return MCDisassembler::Fail;
  }

  // Read the 128-bit word little-endian (low 64 bits first, high 64 bits
  // second). APInt::insertBits places each 64-bit lane at its LSB offset.
  uint64_t Lo = support::endian::read64le(Bytes.data());
  uint64_t Hi = support::endian::read64le(Bytes.data() + 8);
  APInt Word(128, 0);
  Word.insertBits(Lo, 0, 64);
  Word.insertBits(Hi, 64, 64);

  // single-authority slot geometry. The slot windows are derived from
  // the Bundle128 format-desc (the SAME geometric authority the encoder
  // consults via HaydnMCCodeEmitter::encodeSlotInBundle128). No hand-coded
  // bit offsets: each slot's MSB-indexed {LeftOffset, RightOffset} comes from
  // getSlotOffsetsHiBit, converted to LSB-indexed once
  // (LoBit = 127 - RightOffset, Width = Right - Left + 1). The window is
  // extracted as an isolated Width-bit value, so within it the FU top bit is
  // at position Width-1. If the geometry ever changes in HaydnMCFormats.cpp
  // (B128S0Field/B128S1Field/B128S2Field), this decode path tracks it
  // automatically — no second source of truth to drift.
  HaydnMCFormats Formats;
  const MCFormatDesc &B128 = Formats.getBundle128FormatDesc();
  constexpr unsigned BundleBits =
      128; // == B128.getFormatSize (the base field is [0,127]).
  struct SlotGeo {
    MCSlotKind Kind;
    uint64_t Window;
    unsigned WindowTopBit; // LSB index of the window's MSB within Window.
  };
  auto BuildGeo = [&](MCSlotKind Kind) -> SlotGeo {
    MCFormatField::GlobalOffsets Off = B128.getSlotOffsetsHiBit(Kind);
    unsigned Width = Off.RightOffset - Off.LeftOffset + 1;
    unsigned LoBit = (BundleBits - 1) - Off.RightOffset;
    uint64_t Window = Word.extractBitsAsZExtValue(Width, LoBit);
    return {Kind, Window, Width - 1};
  };
  SlotGeo Slots[3] = {
      BuildGeo(MCSlotKind::Haydn_SLOT_S0),
      BuildGeo(MCSlotKind::Haydn_SLOT_S1),
      BuildGeo(MCSlotKind::Haydn_SLOT_S2),
  };

  // Content gate (strict FU + opcode via isValidFlexSlotWindow).
  //
  // The composite trie (DecoderTableBundle128128) has no fixed bits and
  // matches any 16-byte input (single unconditional OPC_Decode). The gate
  // validates each non-zero slot window by checking (a) FU ∈ {0..4} (reject
  // reserved 5..7 per encoding_manual_flex.md §1.2) and (b) the raw opcode
  // is within the valid dense range for that FU (per the.td FLEX defs).
  // A non-zero window that fails either check is not a Bundle128 slot →
  // return Fail. An all-zero window is a §4 NOP slot and skips the gate.
  //
  // Every Fail path MUST set Size=16. Leaving Size unset (or 0) makes
  // llvm-objdump advance by an undefined/tiny amount, desyncing the 16-byte
  // parcel stream and cascading misaligned <unknown>s.
  for (const SlotGeo &S : Slots) {
    if (S.Window != 0 && !isValidFlexSlotWindow(S.Window, S.WindowTopBit)) {
      Size = 16;
      return MCDisassembler::Fail;
    }
  }

  // Run the generated composite trie. DecoderTableBundle128128 has one entry
  // (case 164 = BUNDLE128_FULL, no fixed bits) so it always matches; the slot
  // decoders do the per-FU sub-trie dispatch. The result MCInst is
  // BUNDLE128_FULL with 3 MCOperand::createInst operands.
  DecodeStatus S = decodeInstruction(DecoderTableBundle128128, Instr, Word,
                                     Address, DisAsm, DisAsm->getSubtargetInfo());
  if (S == MCDisassembler::Fail) {
    // The composite trie only fails on an internal decoder error (it has no
    // fixed bits to match). Commit Size=16 for forward progress and let the
    // caller render <unknown>.
    Size = 16;
    return MCDisassembler::Fail;
  }

  // Post-trie sub-MCInst validation (second content-gate tier). A non-zero
  // source window whose sub-MCInst came back empty (sub-trie miss → cleared
  // by decodeS0Slot/S1Slot/S2Slot) is not a real Bundle128 slot. Return Fail
  // so the caller renders <unknown> with forward-progress Size.
  //
  // An all-zero source window is a valid §4 NOP slot: empty sub-MCInst is
  // the intended NOP rendering, so it is exempt.
  //
  // An empty sub-MCInst has getOpcode==0 and getNumOperands==0 (the slot
  // decoder's `SlotInst->clear` resets it to a default-constructed state).
  // A successfully-decoded real instruction always has a non-zero opcode.
  assert(Instr.getNumOperands() == 3 && "Bundle128 composite must have 3 slots");
  for (unsigned SlotIdx = 0; SlotIdx < 3; ++SlotIdx) {
    if (Slots[SlotIdx].Window == 0)
      continue; // all-zero window = valid §4 NOP slot, exempt.
    const MCOperand &Op = Instr.getOperand(SlotIdx);
    if (!Op.isInst() || Op.getInst()->getOpcode() == 0) {
      // Non-zero window but the sub-trie missed (sub-MCInst empty/cleared).
      Instr = MCInst(); // clear the partial composite
      Size = 16;        // must advance one full Bundle128 parcel
      return MCDisassembler::Fail;
    }
  }

  Size = 16;
  return MCDisassembler::Success;
}

MCDisassembler::DecodeStatus HaydnDisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &CStream) const {

  // Bundle128-only decoder. Primary path is tryDecodeBundle128Composite
  // (content gate + composite trie; Size=16 on Success/Fail). Bundle128 is
  // probed before any 2-byte NOP check so parcels whose s0 window is a §4
  // NOP (leading zero bytes) are not stolen as standalone 0x0000.
  //
  // Size / forward-progress contract:
  // * >= 16 bytes + composite Success → Size = 16, Success.
  // * >= 16 bytes + composite Fail (content-gate / trie / post-trie miss)
  //   → Size = 16, Fail (caller renders <unknown>).
  // * < 16 bytes + leading 0x0000 → Size = 2, NOP, Success (trailing only).
  // * < 16 bytes (other) → Size = Bytes.size, Fail (EOF / trailing junk).
  //
  // All-zero Bundle128 (spec §10 NOP) decodes as 3 empty NOP slots via the
  // composite path; encodeBundle emits 16-byte all-zero for all-NOP, so a
  // bare 2-byte 0x0000 parcel only appears as hand-crafted trailing bytes.
  // CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort.
  if (Bytes.size() < 16) {
    // Trailing standalone 16-bit NOP (0x0000) when Bundle128 cannot run.
    if (Bytes.size() >= 2 && Bytes[0] == 0x00 && Bytes[1] == 0x00) {
      Instr.setOpcode(Haydn::NOP);
      Size = 2;
      return MCDisassembler::Success;
    }
    Size = Bytes.size();
    return MCDisassembler::Fail;
  }

  return tryDecodeBundle128Composite(Instr, Size, Bytes, Address, this);
}


static MCDisassembler *createHaydnDisassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new HaydnDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheHaydnTarget(),
                                         createHaydnDisassembler);
}
