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
// Decode strategy (— Bundle128-only; Bundle128-first ordering):
// 1. tryDecodeBundle128Composite — the primary decode path. Reads 16 bytes
// validates each non-zero slot window via isValidFlexSlotWindow (strict
// FU + opcode range contract, Option A) and runs the generated
// composite trie (DecoderTableBundle128128 → case 164 = BUNDLE128_FULL)
// which dispatches to the per-slot sub-tries via decodeS0Slot/S1Slot/S2Slot.
// The all-zero Bundle128 (spec §10 NOP) decodes here as 3 empty NOP slots.
// 2. < 16 bytes remaining — trailing 16-bit NOP parcel fallback: a
// leading 0x0000 with fewer than 16 bytes left decodes as Haydn::NOP
// (Size = 2); any other trailing bytes render <unknown> with
// Size = Bytes.size (forward progress).
//
// reordering: the 2-byte NOP check used to run BEFORE the Bundle128
// probe and intercepted any parcel whose first 2 bytes were 0x00 — including
// real Bundle128 parcels whose s0 window (bits[47:0]) is a §4 NOP slot (they
// start with 6 zero bytes). That misaligned the cursor 2 bytes at a time and
// caused the Bundle128 decoder regression (d304/flex-bytes-mac/move-instr all
// failed because their parcels started with 00 00). Bundle128 is now probed
// first; the 2-byte NOP fallback survives only for the sub-16-byte trailing
// case where Bundle128 cannot run.
//
// The §1.2 width tree (16/32/48/64-bit branches), the D-class classifier
// (decodeDClassBundle), the G-format / 48-bit / 16-bit TableGen paths, and the
// tier-2 content-gate fallback to the width tree were RETIRED in when
// the encoder was cut over to Bundle128-only emission (CLAUDE.md hard
// constraint #3 — variable-width bundles — is realized as 16-byte parcels).
//
// CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort on hostile.text.
// The bounds-safe `<?>` printOperand defense (HaydnInstPrinter) remains the
// primary anti-crash mechanism; the slot decoders here degrade sub-trie
// misses to empty NOP slots (always return Success) rather than asserting.
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

// Mode-0 DecoderMethods retired with the Mode-0 TableGen islands
// (legacy-retired). Bundle128 slot decode uses the generated S0/S1/S2
// sub-tries + decodeSImmOperandXStepWide / register class helpers only.
// Deleted: decodeM0S1ALU64Flat2RR3Copy, decodeLSPage1Imm5, decodeLSPage1Reg.

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

// P3 — Bundle128 composite per-slot decoders (forward declarations).
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

// P3 — Bundle128 composite per-slot decoders (definitions).
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
// Option A — Bundle128 content-gate: strict FU + opcode validation.
//===----------------------------------------------------------------------===//
//
// isValidFlexSlotWindow validates a Bundle128 slot window's FU and opcode
// fields against the encoding_manual_flex.md contract. This replaces
// the ineffective sub-trie probe (the generated DecoderTableS048/S140/S240
// have DEFAULT catch-all branches that accept any non-zero window). The
// check mirrors the pre- decodeFlexSlot strict contract (commit
// 2c9c00ce8eaa^): reject reserved FU (5..7) and out-of-range opcodes.
//
// single-authority slot geometry. The window's bit positions are NO
// LONGER hand-coded per slot (the old `IsS0 ? 45 : 37` FU position and
// `IsS0 ? 44 : 36` opcode start). The window is extracted from the 128-bit
// Bundle128 word using offsets derived from the Bundle128 format-desc
// (HaydnMCFormats::getBundle128FormatDesc.getSlotOffsetsHiBit) — the same
// geometric authority the ENCODER consults (HaydnMCCodeEmitter
// encodeSlotInBundle128). \p Window is that extracted value viewed as a
// standalone Width-bit integer (its MSB is at bit Width-1). FU sits at the
// top 3 bits of the window (LSB [WindowTopBit-2, WindowTopBit]); the opcode
// starts just below it (top at WindowTopBit-3). This holds for every slot
// s0/s1/s2 differ only in window width, not in FU/opcode placement
// (encoding_manual_flex.md §1.2 + §2).
//
// Opcode widths and valid max (from the.td FLEX defs):
// ALU32 (FU=0): 7b, max 0x71 (113 ops — ADDI32_W=0x70, ORI32_W=0x71)
// LS (FU=1): 7b, max 0x7B (LD16=0x7A, LD8=0x7B — signed half/byte after LDU16)
// ALU64 (FU=2): 8b, max 0x9B (155 ops, dense from 0x01)
// LD (FU=3): 6b, max 0x3F (64 ops — LD max raised from 0x38 to 0x3F)
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
// P3 — Bundle128 composite decode (the AIE two-step model).
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
// Option A — content-gated Bundle128 probe (strict FU+opcode contract
// restored from earlier prior revision decodeFlexSlot). The composite
// trie matching alone accepts EVERY 16-byte window; the gate validates each
// non-zero slot window's FU and opcode via isValidFlexSlotWindow (strict
// range check — the generated sub-tries have catch-all defaults that accept
// everything). A non-zero window with reserved FU or out-of-range opcode is
// NOT a Bundle128 slot — return Fail. The sole exception is the all-zero
// word (a real §4 Bundle128 NOP): it has no non-zero window to validate, so
// it passes unconditionally.
//
// post-trie sub-MCInst validation (the second tier of the
// content gate). A non-zero source window whose sub-MCInst came back EMPTY
// (sub-trie miss → cleared by the slot decoders) is by construction NOT a
// real Bundle128 slot. In that case return Fail. An all-zero source window
// is a valid §4 NOP slot: it has no sub-MCInst content to validate, so it is
// exempt.
//
// Size contract: every Bundle128 parcel is exactly 16 bytes. On Success
// Size = 16. On Fail, Size is left unset and the caller renders <unknown>
// with Size = min(Bytes.size, 16) (forward progress).
static DecodeStatus tryDecodeBundle128Composite(MCInst &Instr, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                const MCDisassembler *DisAsm) {
  // Stage-1 contract: every Bundle128 parcel is exactly 16 bytes.
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

  // Option A — content gate (strict FU + opcode validation, mirrors
  // earlier prior revision decodeFlexSlot contract).
  //
  // The composite trie (DecoderTableBundle128128) has no fixed bits and
  // matches any 16-byte input (single unconditional OPC_Decode). The gate
  // validates each non-zero slot window by checking (a) FU ∈ {0..4} (reject
  // reserved 5..7 per encoding_manual_flex.md §1.2) and (b) the raw opcode
  // is within the valid dense range for that FU (per the.td FLEX defs).
  // A non-zero window that fails either check is NOT a Bundle128 slot →
  // return Fail. An all-zero window is a §4 NOP slot and skips the gate.
  //
  // CRITICAL: every Fail path MUST set Size=16. Leaving Size unset (or 0)
  // makes llvm-objdump advance by an undefined/tiny amount, desyncing the
  // 16-byte parcel stream and turning every subsequent parcel into a cascade
  // of misaligned <unknown>s (seen as unknown at addr%16!=0, e.g. 0x21b4).
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

  // post-trie sub-MCInst validation (the second tier of the
  // content gate). A non-zero source window whose sub-MCInst came back EMPTY
  // (sub-trie miss → cleared by decodeS0Slot/S1Slot/S2Slot) is by
  // construction NOT a real Bundle128 slot. Return Fail so the caller renders
  // <unknown> with forward-progress Size.
  //
  // An all-zero source window is a valid §4 NOP slot: it has no sub-MCInst
  // content to validate (the slot decoder's empty-MCInst result is the
  // intended NOP rendering), so it is exempt from this check.
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

  // ======================================================================
  // Bundle128-only decoder (+ / NOP-parcel handling).
  //
  // The §1.2 width tree (16/32/48/64-bit branches), the D-class classifier
  // (decodeDClassBundle), the G-format / 48-bit / 16-bit TableGen paths, and
  // the tier-2 content-gate fallback to the width tree were RETIRED in
  // when the encoder was cut over to Bundle128-only emission (CLAUDE.md hard
  // constraint #3 — variable-width bundles — is realized as 16-byte parcels).
  // tryDecodeBundle128Composite is the primary decode path.
  //
  // Bundle128-first ordering. The standalone 16-bit NOP check
  // (Bytes[0:2] == 0x0000 -> Size=2 NOP) was originally placed BEFORE the
  // Bundle128 probe, which intercepted ANY parcel whose first 2 bytes were
  // 0x00. Real Bundle128 parcels whose s0 window (bits[47:0]) is a §4 NOP
  // slot start with 6 zero bytes; the check gobbled them 2 bytes at a
  // time, misaligning the cursor and rendering every subsequent slot
  // <unknown>. This was the Bundle128 decoder regression (d304 max64
  // flex-bytes-mac, move-instructions 0x40+ all failed because their parcels
  // started with 00 00). The fix probes Bundle128 FIRST when 16+ bytes are
  // available; the all-zero Bundle128 already decodes correctly via
  // tryDecodeBundle128Composite (3 empty NOP slots). The 2-byte standalone
  // NOP fallback is retained ONLY for the trailing sub-16-byte case.
  //
  // Size / forward-progress contract: 
  // * >= 16 bytes available + composite Success → Size = 16, return Success.
  // * >= 16 bytes available + composite Fail (content-gate miss OR
  // internal trie error OR post-trie sub-MCInst miss) → Size = 16
  // return Fail (caller renders <unknown>).
  // * < 16 bytes available + leading 0x0000 → Size = 2, NOP, Success
  // (trailing-parcel fallback).
  // * < 16 bytes available (other) → Size = Bytes.size, return Fail
  // (graceful <unknown> at EOF / trailing bytes; no infinite loop).
  //
  // CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort. The bounds-safe
  // `<?>` printOperand defense (HaydnInstPrinter) remains the primary anti
  // crash mechanism; the slot decoders degrade sub-trie misses to empty NOP
  // slots (always return Success) rather than asserting.
  // ======================================================================

  // Bundle128-first ordering. The standalone 16-bit NOP check
  // (Bytes[0:2] == 0x0000 -> Size=2 NOP) was placed BEFORE the Bundle128
  // probe, which intercepted ANY parcel whose first 2 bytes were 0x00. Real
  // Bundle128 parcels whose s0 window (bits[47:0]) is a §4 NOP slot start
  // with 6 zero bytes; the check gobbled them 2 bytes at a time
  // misaligning the cursor and rendering every subsequent slot <unknown>.
  // This was the Bundle128 decoder regression (d304 max64, flex-bytes-mac
  // move-instructions 0x40+ all failed because their parcels started with
  // 00 00).
  //
  // The fix is to probe Bundle128 FIRST whenever 16+ bytes are available.
  // tryDecodeBundle128Composite already decodes the all-zero Bundle128
  // (spec §10 NOP) correctly: all 3 slot windows are zero -> exempt from the
  // content gate -> composite trie matches -> each sub-trie misses on
  // the zero window -> 3 empty NOP slots -> "{ nop; nop; nop }". The encoder
  // confirms this: encodeBundle emits a 16-byte all-zero Bundle128 for an
  // all-NOP bundle (HaydnMCCodeEmitter.cpp:372-377), so a standalone `nop`
  // directive NEVER produces a 2-byte 0x0000 parcel in practice.
  //
  // The 2-byte standalone-NOP fallback is retained ONLY for the trailing
  // sub-16-byte case (hand-crafted hostile.text with a bare 0x0000 at EOF)
  // where Bundle128 cannot run. This preserves the anti-crash intent
  // without intercepting real Bundle128 parcels.
  if (Bytes.size() < 16) {
    // trailing standalone 16-bit NOP parcel (0x0000). With < 16 bytes
    // remaining, Bundle128 cannot run; decode a bare 0x0000 as Haydn::NOP
    // (Size = 2) for forward progress. Any other trailing bytes render
    // <unknown> with Size = Bytes.size.
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
