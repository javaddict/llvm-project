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
// Format E decode: 12-byte / 96-bit parcels. Size=12 on every Success/Fail
// from the composite path.
//
// 1. tryDecodeFormatEComposite — primary path when >= 12 bytes remain.
//    The entry count comes from the header bit Inst{3} and selects one of two
//    composite tries, DecoderTableFormatE296 (entries P20,P21) or
//    DecoderTableFormatE396 (P30,P31,P32). Each trie re-checks the full
//    6-bit header and the unused high bits, so a word that is not a format E
//    bundle of that entry count fails there rather than being mis-decoded.
//    The trie dispatches per-entry sub-tries via
//    decodeP20Slot/P21Slot/P30Slot/P31Slot/P32Slot.
// 2. < 12 bytes remaining — trailing bytes → <unknown> with
//    Size = Bytes.size (forward progress).
//
// NO hand-written content gate. Bundle128 needed one (isValidFlexSlotWindow,
// a hardcoded FU + opcode-range table) because its generated sub-tries were
// catch-all defaults that accepted any window. Format E's sub-tries are real
// tries — they switch on the entry's low bits (mapping + type) and
// OPC_CheckField the reserved bits — so the generated table IS the content
// authority and a second hand-maintained copy could only drift from it.
//
// That gate was also actively wrong here rather than merely redundant: it
// read the unit from the window's TOP three bits, which is where Bundle128
// put its FU and where format E puts reserved zeros. Format E's unit
// (`mapping`) is in the BOTTOM two bits and its value differs per entry
// position — ALU0 is 0b00 at P20 but 0b10 at P30. Applied to a valid
// ADD32_P20_ALU0 it read FU=0 and opcode=0 out of the reserved field and
// rejected it, so every non-NOP bundle would have failed to disassemble.
//
// CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort on hostile.text.
// The bounds-safe `<?>` printOperand defense (HaydnInstPrinter) remains the
// primary anti-crash mechanism; entry decoders degrade sub-trie misses to
// empty sub-MCInsts (always return Success) rather than asserting.
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

// Format E entry decode uses the generated per-position sub-tries plus
// decodeSImmOperandXStepWide / register class helpers only. The Bundle128
// bitfield helper that backed the hand-written content gate went with it.

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

// Format E composite per-entry decoders (forward declarations).
//
// The generated HaydnGenDisassemblerTables.inc defines a template
// `decodeToMCInst` whose two composite cases emit, for BUNDLE_E2:
// tmp = fieldFromInstruction(insn, 6, 45);  / entry0 window
// decodeP20Slot(MI, tmp, Address, Decoder);
// tmp = fieldFromInstruction(insn, 51, 41); / entry1 window
// decodeP21Slot(MI, tmp, Address, Decoder);
// and for BUNDLE_E3 the same shape over (6,31), (37,31), (68,27) calling
// decodeP30Slot / decodeP31Slot / decodeP32Slot. The names are fixed by the
// InstSlot defs in the generated encoding, not chosen here.
//
// Because `decodeToMCInst` is a template and `tmp` is a dependent type, the
// entry decoders must be visible by name BEFORE the.inc include for two-phase
// lookup to find them. This mirrors AIE's `SLOTDECODERDecl(...)` macro pattern
// (AIEDisassemblerPP.h:42) — forward-declare here, define after the include.
//
// Each entry decoder takes the already-extracted window (the composite trie
// did the fieldFromInstruction), runs that position's tablegen sub-trie on it
// and adds the result as an MCOperand::createInst sub-instruction operand to
// the composite MI (mirrors AIE's decodeAIE2PSSlot). On a sub-trie miss the
// sub-MCInst is cleared. Each decoder ALWAYS returns Success; the caller
// decides what an empty sub-MCInst means, because failing here would abort the
// whole composite and lose the entries that did decode.
//
// Note there is one sub-trie PER ENTRY POSITION, not per unit: the same
// logical has a different member (and different bit layout) at P20 than at
// P30, and the position is what selects the table.
//
// CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort on hostile.text.
// The bounds-safe `<?>` printOperand defense remains the primary anti-crash
// bar regardless.
namespace {
#define HAYDN_ENTRY_DECODER_DECL(NAME)                                         \
  template <typename InsnType>                                                 \
  static DecodeStatus NAME(MCInst &MI, InsnType &Insn, uint64_t Address,       \
                           const MCDisassembler *Decoder);
HAYDN_ENTRY_DECODER_DECL(decodeP20Slot)
HAYDN_ENTRY_DECODER_DECL(decodeP21Slot)
HAYDN_ENTRY_DECODER_DECL(decodeP30Slot)
HAYDN_ENTRY_DECODER_DECL(decodeP31Slot)
HAYDN_ENTRY_DECODER_DECL(decodeP32Slot)
#undef HAYDN_ENTRY_DECODER_DECL
} // namespace

// Include the auto-generated decoder tables (the two format E composite tries
// + the five per-entry-position sub-tries; the legacy Haydn16/32/48/64 tables
// remain generated from the .td but are not consulted by this disassembler).
#define LLVM_DISASSEMBLER_HAYDN_DECODER_TABLES
#include "HaydnGenDisassemblerTables.inc"

// Format E composite per-entry decoders (definitions).
//
// Mirrors AIE's decodeAIE2PSSlot (AIE2PSDisassembler.cpp:75-87): allocate a
// heap MCInst via MCContext (persists beyond this call), run that entry
// position's tablegen trie on the window bits, and add the sub-instruction as
// an MCOperand::createInst operand to the composite MI. The composite trie
// extracted the window already via fieldFromInstruction.
//
// Entry DecoderTable names come from the InstSlot namespaces in the generated
// HaydnFormatEEncoding.td. Their numeric suffix is the window PADDED to whole
// bytes (§ 6.5 — `Size` is a byte count and cannot express 45), not the window
// width:
//   P20: DecoderTableP2048 — 45-bit window, padded to 48
//   P21: DecoderTableP2148 — 41-bit window, padded to 48
//   P30: DecoderTableP3032 — 31-bit window, padded to 32
//   P31: DecoderTableP3132 — 31-bit window, padded to 32
//   P32: DecoderTableP3232 — 27-bit window, padded to 32
//
// On a sub-trie miss the sub-MCInst is cleared (per AIE) and the decoder still
// returns Success; tryDecodeFormatEComposite turns an empty sub-MCInst into a
// whole-bundle Fail. Doing it there rather than here keeps one policy in one
// place and lets the other entries decode first.
namespace {
#define HAYDN_ENTRY_DECODER_DEF(NAME, TABLE)                                   \
  template <typename InsnType>                                                 \
  static DecodeStatus NAME(MCInst &MI, InsnType &Insn, uint64_t Address,       \
                           const MCDisassembler *Decoder) {                    \
    MCInst *EntryInst = Decoder->getContext().createMCInst();                  \
    DecodeStatus Result =                                                      \
        decodeInstruction(TABLE, *EntryInst, Insn, Address, Decoder,           \
                          Decoder->getSubtargetInfo());                        \
    if (Result != MCDisassembler::Success)                                     \
      EntryInst->clear();                                                      \
    MI.addOperand(MCOperand::createInst(EntryInst));                           \
    return MCDisassembler::Success;                                            \
  }
HAYDN_ENTRY_DECODER_DEF(decodeP20Slot, DecoderTableP2048)
HAYDN_ENTRY_DECODER_DEF(decodeP21Slot, DecoderTableP2148)
HAYDN_ENTRY_DECODER_DEF(decodeP30Slot, DecoderTableP3032)
HAYDN_ENTRY_DECODER_DEF(decodeP31Slot, DecoderTableP3132)
HAYDN_ENTRY_DECODER_DEF(decodeP32Slot, DecoderTableP3232)
#undef HAYDN_ENTRY_DECODER_DEF
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
// Format E composite decode (AIE two-step model).
//===----------------------------------------------------------------------===//
//
// tryDecodeFormatEComposite is the decoder's symmetric inverse of the encoder's
// encodeBundleE (HaydnMCCodeEmitter.cpp): it reads a 12-byte / 96-bit parcel
// and runs one of the two generated composite tries on it.
//
// WHICH trie is the entry-count decision, and it is read from the header rather
// than guessed: Inst{3} is 0 for a 2-entry bundle and 1 for a 3-entry one
// (§ 3). Bundle128 had a single composite whose trie had no fixed bits and
// matched anything; format E's two tries each OPC_CheckField the whole 6-bit
// header AND the unused high bits, so selecting on Inst{3} is a fast path and
// the trie itself is still the one that validates. A word that is not a format
// E bundle at all — wrong format indicator, nonzero reserved or unused bits —
// fails inside the trie.
//
// The trie calls the per-position entry decoders on each already-extracted
// window, and the result is a BUNDLE_E2 / BUNDLE_E3 MCInst whose 2 or 3
// operands are MCOperand::createInst sub-instructions. Those are
// MCContext-allocated and persist beyond this call.
//
// There is NO hand-written content gate — see the file header for why the
// Bundle128 one was not merely redundant here but actively wrong.
//
// Post-trie sub-MCInst validation: an entry whose sub-MCInst came back empty
// (sub-trie miss → cleared by the entry decoder) means the window was not a
// legal entry, so the bundle is not decodable — return Fail.
//
// Note there is deliberately no "all-zero window is an exempt NOP" case, which
// Bundle128 needed. Format E has real NOP members at every entry position and
// an all-zero window decodes to whichever one has mapping == 0b00 there
// (NOP_P20_ALU0 at P20, NOP_P30_MAC0 at P30, …), so a NOP is a successful
// decode and not an empty sub-MCInst. An all-zero 12-byte WORD is still not a
// valid bundle, because its header is not 0b111 — that is the same fact that
// makes an all-zero pad wrong in HaydnAsmBackend::writeNopData.
//
// Size contract: a parcel is one whole composite — 12 bytes. On Success or
// Fail from this path, Size is that width (forward progress; never leave Size
// unset). A wrong stride does not fail cleanly, it desyncs the parcel stream.
static DecodeStatus tryDecodeFormatEComposite(MCInst &Instr, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              const MCDisassembler *DisAsm) {
  // Entry count from the header. Bit 3 of byte 0 — the payload starts at bit 6,
  // so the whole header is inside the first byte and is readable before any
  // width decision. Bytes.size() >= 1 is guaranteed by the caller's length
  // check, but read defensively: this is the hostile-input path.
  if (Bytes.empty())
    return MCDisassembler::Fail;
  const bool IsThreeEntry = (Bytes[0] >> 3) & 1;
  const unsigned CompositeOpc =
      IsThreeEntry ? Haydn::BUNDLE_E3 : Haydn::BUNDLE_E2;

  // Single-authority geometry: the bundle width and every entry window come
  // from the composite's format-desc — the SAME authority the encoder consults
  // — so a regenerated layout moves both sides together and there is no second
  // source of truth to drift. No hand-coded bit offsets: each entry's
  // MSB-indexed {LeftOffset, RightOffset} comes from getSlotOffsetsHiBit,
  // converted to LSB-indexed once.
  HaydnMCFormats Formats;
  const MCFormatDesc &Composite = Formats.getCompositeFormatDesc(CompositeOpc);
  const unsigned BundleBits = Composite.getFormatSize();
  const unsigned BundleBytes = BundleBits / 8;
  assert(BundleBits % 8 == 0 && BundleBits <= 128 &&
         "composite must be a whole number of bytes and fit one APInt read");

  if (Bytes.size() < BundleBytes)
    return MCDisassembler::Fail;

  // Read the parcel little-endian, low 64-bit lane first. APInt::insertBits
  // places each lane at its LSB offset. 96 bits is NOT two whole limbs — the
  // second lane is 4 bytes — which is the same arithmetic the encoder had to
  // get right when emitBundleWord stopped writing two uint64s.
  APInt Word(BundleBits, 0);
  for (unsigned Off = 0; Off < BundleBytes; Off += 8) {
    unsigned Lane = std::min(8u, BundleBytes - Off);
    uint64_t Bits = 0;
    for (unsigned I = 0; I < Lane; ++I)
      Bits |= static_cast<uint64_t>(Bytes[Off + I]) << (8 * I);
    Word.insertBits(Bits, 8 * Off, 8 * Lane);
  }

  // The entry positions of the chosen composite, in operand order (low entry
  // first). Two or three of them — this is the Slots[3] that could not stay a
  // fixed three under format E.
  static constexpr MCSlotKind TwoEntry[] = {MCSlotKind::Haydn_SLOT_P20,
                                            MCSlotKind::Haydn_SLOT_P21};
  static constexpr MCSlotKind ThreeEntry[] = {MCSlotKind::Haydn_SLOT_P30,
                                              MCSlotKind::Haydn_SLOT_P31,
                                              MCSlotKind::Haydn_SLOT_P32};
  ArrayRef<MCSlotKind> Entries =
      IsThreeEntry ? ArrayRef<MCSlotKind>(ThreeEntry)
                   : ArrayRef<MCSlotKind>(TwoEntry);

  // Run the generated composite trie. It re-checks the header, so a word whose
  // Inst{3} we read but whose remaining header bits are wrong fails here.
  DecodeStatus S = decodeInstruction(
      IsThreeEntry ? DecoderTableFormatE396 : DecoderTableFormatE296, Instr,
      Word, Address, DisAsm, DisAsm->getSubtargetInfo());
  if (S == MCDisassembler::Fail) {
    Size = BundleBytes;
    return MCDisassembler::Fail;
  }

  // Post-trie sub-MCInst validation. An empty sub-MCInst has getOpcode()==0 and
  // no operands (the entry decoder's `clear` resets it); a real decode always
  // has a non-zero opcode, NOPs included.
  if (Instr.getNumOperands() != Entries.size()) {
    Instr = MCInst();
    Size = BundleBytes;
    return MCDisassembler::Fail;
  }
  for (unsigned Idx = 0, E = Entries.size(); Idx != E; ++Idx) {
    const MCOperand &Op = Instr.getOperand(Idx);
    if (!Op.isInst() || Op.getInst()->getOpcode() == 0) {
      Instr = MCInst(); // clear the partial composite
      Size = BundleBytes;
      return MCDisassembler::Fail;
    }
  }

  Size = BundleBytes;
  return MCDisassembler::Success;
}

MCDisassembler::DecodeStatus HaydnDisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &CStream) const {

  // Format E decoder. Primary path is tryDecodeFormatEComposite (composite
  // trie; Size=12 on Success/Fail).
  //
  // Size / forward-progress contract:
  // * >= 12 bytes + composite Success → Size = 12, Success.
  // * >= 12 bytes + composite Fail (trie reject / post-trie miss)
  //   → Size = 12, Fail (caller renders <unknown>).
  // * < 12 bytes → Size = Bytes.size, Fail (EOF / trailing junk).
  //
  // Bundle128 had a fourth case here: a trailing 2-byte 0x0000 decoded as a
  // standalone Haydn::NOP. That is deliberately gone. It rested on Bundle128's
  // "all-zero word is the NOP" property, which format E does not have — the
  // header must be 0b111 — and there is no 2-byte format E encoding at all, so
  // synthesising a NOP from two zero bytes would be inventing an instruction
  // that cannot exist. Short tails now render <unknown>, which is what they
  // are. Revisit when § 5.4's lit expectations are regenerated.
  //
  // CLAUDE.md hard bar: `llvm-objdump -d` MUST NEVER abort.
  if (Bytes.size() < Haydn::BUNDLE_E_BYTES) {
    Size = Bytes.size();
    return MCDisassembler::Fail;
  }

  return tryDecodeFormatEComposite(Instr, Size, Bytes, Address, this);
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
