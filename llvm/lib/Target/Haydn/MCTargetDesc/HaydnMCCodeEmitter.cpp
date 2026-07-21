//===-- HaydnMCCodeEmitter.cpp - Haydn Code Emitter interface ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HaydnMCCodeEmitter class.
//
// the encoder is Bundle128-ONLY. Every emit path routes
// through `encodeBundle128` (the 128-bit AIE-model composite). The legacy
// multi-width emitters (`emitWideLSParcel`, `emitMode0S0Bundle`
// `emitMode0S1FunctBundle`, `emitMode0S1RR3Bundle`, `emitMode0S1MACBundle`
// `emitMode0S1LDBundle`, `encodeBundleSlotOR`, `encodeSingleInstruction`, the
// `emitWord16`/`emitWord32`/`emitWord48`/`emitBundleWord` helpers, and the
// `HaydnM0FunctVariant.inc` include) are RETIRED.
//
// Forcing function: an opcode that lacks a `_S<k>` variant (and is not
// FU-routable by `encodeBundle128`) hits `report_fatal_error` with a clear
// "add a _S<k> def" message. This is the forcing function — once the
// remaining FLEX families land, the errors vanish.
//
// `encodeInstruction` routing:
// Haydn::BUNDLE -> encodeBundle (collects children, calls
// encodeBundle128 on the child list)
// PseudoLongB* -> expandLongBranch (pseudo expansion; the emitted
// real instructions recurse through
// encodeInstruction -> encodeBundle128)
// standalone single-op -> encodeBundle128 (single-op composite)
// everything else -> report_fatal_error (uncovered opcode)
//
//===----------------------------------------------------------------------===//

#include "HaydnMCCodeEmitter.h"
#include "HaydnBundle.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "haydn-mccodeemitter"

using namespace llvm;

namespace {

// (Flex encoder) — gate predicate for the Bundle128 path. Delegates
// to the shared `isHaydnBundle128TargetOpcode` in HaydnMCFormats so the size
// model and the encoder use the IDENTICAL predicate.
static bool isBundle128TargetOpcode(unsigned Opc, const MCInstrInfo &MII) {
  return isHaydnBundle128TargetOpcode(Opc, MII);
}

// Hexagon parity: PC-rel is a property of the *fixup kind*, set at
// MCFixup::create time — not via getFixupKindInfo Flags (FKF_IsPCRel is gone
// in this LLVM; Hexagon/RISCV leave Flags=0 and switch on kind here).
// Mirrors HexagonMCCodeEmitter.cpp `addFixup`.
static bool isHaydnPCRelFixupKind(unsigned Kind) {
  switch (Kind) {
  case Haydn::FIXUP_HAYDN_CallSImm20:
  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
  case Haydn::FIXUP_HAYDN_BranchSImm16:
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12:
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
  case Haydn::FIXUP_HAYDN_LongBranchSImm20:
  case Haydn::FIXUP_HAYDN_C_BranchSImm4:
  case Haydn::FIXUP_HAYDN_C_BranchSImm10:
  case Haydn::FIXUP_HAYDN_32_PCREL:
  case Haydn::FIXUP_HAYDN_PC_LO20:
  case Haydn::FIXUP_HAYDN_HWLoopOffset:
  case Haydn::FIXUP_HAYDN_HWLoopOff1:
  case Haydn::FIXUP_HAYDN_HWLoopOff2:
    return true;
  default:
    return false;
  }
}

static void addHaydnFixup(SmallVectorImpl<MCFixup> &Fixups, uint32_t Offset,
                          const MCExpr *Value, unsigned Kind) {
  Fixups.push_back(MCFixup::create(Offset, Value,
                                   static_cast<MCFixupKind>(Kind),
                                   isHaydnPCRelFixupKind(Kind)));
}

//===----------------------------------------------------------------------===//
// HaydnMCCodeEmitter
//===----------------------------------------------------------------------===//

class HaydnMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MII;

public:
  HaydnMCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MII)
      : Ctx(Ctx), MII(MII) {}
  ~HaydnMCCodeEmitter() override = default;

  // P2b — APInt 5-param overload (AIE-model). The 128-bit Bundle128
  // composite forces -gen-emitter to emit this signature.
  void getBinaryCodeForInstr(const MCInst &MI, SmallVectorImpl<MCFixup> &Fixups,
                             APInt &Inst, APInt &Scratch,
                             const MCSubtargetInfo &STI) const;

  void getMachineOpValue(const MCInst &MI, const MCOperand &MO, APInt &Op,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  void getBranchTargetOpValue(const MCInst &MI, unsigned OpNo, APInt &Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  void getCallTargetOpValue(const MCInst &MI, unsigned OpNo, APInt &Op,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  // (Stage 1): AIE-style scaled-immediate encoder for the 48-bit WIDE
  // formats. Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes
  // via tablegen `EncoderMethod`.
  template <unsigned N, unsigned Shift, bool IsSigned, bool IsPCRel,
            Haydn::Fixups FixupKind>
  void getSImmOpValueXStepWide(const MCInst &MI, unsigned OpNo, APInt &Op,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  // expand a long branch pseudo to: inverted-conditional-branch + JAL.
  // Each emitted real instruction recurses through `encodeInstruction`, which
  // routes to `encodeBundle128` (the single Bundle128 emit path).
  void expandLongBranch(const MCInst &MI, SmallVectorImpl<char> &CB,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const;

  // P2b — AIE-model slot sub-instruction encoding. Re-enters
  // getBinaryCodeForInstr on the sub-inst to recover its standalone slot-window
  // encoding, then slices the slot window and writes the slot bits into Op.
  void encodeSlotSubInst(const MCInst &Composite, const MCInst &SubInst,
                         APInt &Op, SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  // the ONLY bundle emit path. Collects real children from a
  // Haydn::BUNDLE MCInst and routes them through `encodeBundle128`.
  void encodeBundle(const MCInst &MBI, SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const;

  // emit a Bundle128 composite parcel for the children (the
  // permanent AIE-style CodeGenFormat path). Returns true if every child is a
  // Bundle128-target opcode (Flex-encodable or FU-routable); false otherwise.
  bool encodeBundle128(ArrayRef<const MCInst *> Children,
                       SmallVectorImpl<char> &CB,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const;

  // emit a Bundle128 128-bit (16-byte) composite parcel
  // little-endian.
  void emitBundle128Word(const APInt &Word, SmallVectorImpl<char> &CB) const;

  unsigned getBranchFixupKind(const MCInst &MI) const;
  unsigned getCallFixupKind(const MCInst &MI) const;
};

} // end anonymous namespace

// Determine the appropriate fixup kind for an expression operand based on
// the parent instruction opcode. Geometry per / HaydnRelocLayout.
// slot-variant cutover: `encodeSlotSubInst` maps legacy opcodes → `_S<k>`
// variants BEFORE `getBinaryCodeForInstr`. The slot encoding DAG routes
// EVERY symbolic operand through `getMachineOpValue`, so this function is the
// single fixup-kind decision point for slot-variant opcodes.
static unsigned getExprFixupKind(const MCInst &MI) {
  switch (MI.getOpcode()) {
  default:
    break;
  case Haydn::LUI:
  case Haydn::LUI_S0:
    // Bundle128 LUI_S0 carries a 12-bit high field (HaydnFU_ALU32_S0_I12).
    // HI12 pairs with LO20 on ADDI32 (not the retired 32-bit-parcel HI20/LO16).
    return Haydn::FIXUP_HAYDN_HI12;
  // ADDI32 Bundle128 RI20: imm20 at s0 bits[37:18] → LO20 (not legacy LO16).
  // ADDI32_W / ADDI32_W_S0 handled below with ORI32_W (block).
  case Haydn::ADDI32:
  case Haydn::ADDI32_S0:
  case Haydn::ADDI32_S1:
  case Haydn::ADDI32_S2:
    return Haydn::FIXUP_HAYDN_LO20;
  // ADDI32S/SUBI* still use signed imm fields; ANDI/ORI/XORI are RI20 ZEXT
  // (ISA: uimm20) — same LO20 window as ADDI32 Bundle128 peers (not LO16).
  case Haydn::ADDI32S:
  case Haydn::SUBI32:
  case Haydn::SUBI32S:
  case Haydn::ADDI32S_S0:
  case Haydn::ADDI32S_S1:
  case Haydn::ADDI32S_S2:
  case Haydn::SUBI32_S0:
  case Haydn::SUBI32_S1:
  case Haydn::SUBI32_S2:
  case Haydn::SUBI32S_S0:
  case Haydn::SUBI32S_S1:
  case Haydn::SUBI32S_S2:
    return Haydn::FIXUP_HAYDN_LO16;
  case Haydn::ANDI32:
  case Haydn::ORI32:
  case Haydn::XORI32:
  case Haydn::ANDI32_S0:
  case Haydn::ANDI32_S1:
  case Haydn::ANDI32_S2:
  case Haydn::ORI32_S0:
  case Haydn::ORI32_S1:
  case Haydn::ORI32_S2:
  case Haydn::XORI32_S0:
  case Haydn::XORI32_S1:
  case Haydn::XORI32_S2:
    return Haydn::FIXUP_HAYDN_LO20;
  // JAL/JALR `_S0` route symbolic call targets through
  // `getMachineOpValue` (not `getCallTargetOpValue`), so this switch decides
  // their kind. Match `getCallFixupKind`'s legacy mapping.
  case Haydn::JAL_S0:
    return Haydn::FIXUP_HAYDN_CallSImm20;
  case Haydn::JALR_S0:
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  // Bundle128 branch `_S0` forms use the same s0 windows as the `_W_S0`
  // peers (cutover). FIXUP_HAYDN_BranchSImm16 still has legacy-parcel
  // geometry (FieldLsb=0, FieldSize=16) and does not patch Bundle128
  // imm12 — linked BEQ/BNE kept offset 0. Map to the WIDE fixup kinds that
  // already carry correct FieldLsb (RI12 → bits[19:8]/8; I12 → bits[15:4]/4).
  // Bare logical opcodes (asm) + private _S0 encode peers.
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
  case Haydn::BEQ_S0:
  case Haydn::BNE_S0:
  case Haydn::BGE_S0:
  case Haydn::BGEU_S0:
  case Haydn::BLT_S0:
  case Haydn::BLTU_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
  case Haydn::BEQZ_S0:
  case Haydn::BNEZ_S0:
  case Haydn::BLTZ_S0:
  case Haydn::BGEZ_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // legacy BEQZ_W/BNEZ_W/BGEZ_W/BLTZ_W — CodeGen emits these opcodes
  // (HaydnConditionOptimizer, HaydnAsmPrinter B/RET expansion, ISel
  // G_BRINDIRECT). The encoder routes them through encodeBundle128 which
  // pairs them to the _S0 variant (HaydnFU_ALU32_S0_I12_ONE, imm12 at
  // LoWord bits[15:4]). The symbolic branch target MUST map to
  // FIXUP_HAYDN_WIDE_BranchSImm12 (geometry FieldLsb=4, matching the
  // Bundle128 imm12 position). Without this, the default FIXUP_HAYDN_32
  // writes a 32-bit value into the LoWord, clobbering the opcode/FU bits and
  // producing <unknown> on disassembly.
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ_W_S0:
  case Haydn::BNEZ_W_S0:
  case Haydn::BGEZ_W_S0:
  case Haydn::BLTZ_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // Two-register WIDE cond (RI12): imm12 at s0 bits[19:8] → FieldLsb=8.
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ_W_S0:
  case Haydn::BNE_W_S0:
  case Haydn::BGE_W_S0:
  case Haydn::BGEU_W_S0:
  case Haydn::BLT_W_S0:
  case Haydn::BLTU_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  // ADDI32_W/ORI32_W `_S0` carry the wide-reloc operand
  // (simm20_wide_abs / uimm20_wide_abs) — the symbolic operand MUST map to
  // FIXUP_HAYDN_LO20 (the 20-bit absolute LO20 reloc, paired with LUI's HI12).
  // Without this, the default FIXUP_HAYDN_32 clobbers the opcode bytes.
  // The legacy ADDI32_W/ORI32_W are also handled here (the AsmParser matches
  // the bare mnemonic to the legacy opcode, which the FlexMap auto-pairs to
  // the _S0 variant at emit time — but getExprFixupKind sees the original
  // opcode before the pairing).
  case Haydn::ADDI32_W:
  case Haydn::ORI32_W:
  case Haydn::ADDI32_W_S0:
  case Haydn::ORI32_W_S0:
    return Haydn::FIXUP_HAYDN_LO20;
  // (DEFERRED): LD32/ST32/LD64/ST64 still map to FIXUP_HAYDN_LO20.
  // The RISK-5 encoder-side change (all LS -> FIXUP_HAYDN_LS_IMM) was OVER-BROAD
  // it broke the WIDE LSOff20 path (LD32 with a 20-bit offset is correctly
  // LO20, as fixup-selection-regression.s verifies) AND HaydnELFObjectWriter
  // aborts (LS_IMM has no ELF reloc; marked it MC-only). 's real scope
  // is only the NARROW imm6 LS ops (RI6 form, 6-bit) conflated with LO20; the
  // WIDE LSOff20 (20-bit) is correctly LO20. Reverted until the narrow-vs-wide
  // distinction + R_HAYDN_LS_IMM ELF reloc land. LS_IMM kind/geometry
  // stay defined for that follow-up.
  case Haydn::LD32:
  case Haydn::ST32:
  case Haydn::LD64:
  case Haydn::ST64:
  case Haydn::LD32_S0:
  case Haydn::ST32_S0:
  case Haydn::LD64_S0:
  case Haydn::ST64_S0:
    return Haydn::FIXUP_HAYDN_LO20;
  }
  return Haydn::FIXUP_HAYDN_32;
}

//===----------------------------------------------------------------------===//
// Bundle128 word emit (little-endian 16-byte parcel)
//===----------------------------------------------------------------------===//

void HaydnMCCodeEmitter::emitBundle128Word(const APInt &Word,
                                           SmallVectorImpl<char> &CB) const {
  assert(Word.getBitWidth() == 128 &&
         "Bundle128 word must be exactly 128 bits");
  // Little-endian byte emit. APInt::getRawData exposes the limbs; for a
  // 128-bit value that is two 64-bit limbs with limb 0 holding the low bits.
  const uint64_t *Data = Word.getRawData();
  uint64_t Lo = Data[0];
  uint64_t Hi = (Word.getBitWidth() > 64) ? Data[1] : 0;
  support::endian::write<uint64_t>(CB, Lo, llvm::endianness::little);
  support::endian::write<uint64_t>(CB, Hi, llvm::endianness::little);
}

//===----------------------------------------------------------------------===//
// Top-level encode dispatch — Bundle128-only
//===----------------------------------------------------------------------===//

void HaydnMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  // 1. Haydn::BUNDLE — the legacy bundle pseudo (TargetOpcode::BUNDLE). Route
  // to encodeBundle, which collects the children and calls encodeBundle128.
  if (MI.getOpcode() == Haydn::BUNDLE) {
    encodeBundle(MI, CB, Fixups, STI);
    return;
  }
  // 2. Long-branch pseudos — expand to inverted-conditional-branch + JAL. The
  // emitted real instructions recurse through encodeInstruction.
  switch (MI.getOpcode()) {
  default:
    break;
  case Haydn::PseudoLongBEQ:
  case Haydn::PseudoLongBNE:
  case Haydn::PseudoLongBGE:
  case Haydn::PseudoLongBGEU:
  case Haydn::PseudoLongBLT:
  case Haydn::PseudoLongBLTU:
  case Haydn::PseudoLongBEQZ:
  case Haydn::PseudoLongBNEZ:
  case Haydn::PseudoLongBGEZ:
  case Haydn::PseudoLongBLTZ:
  case Haydn::PseudoLongB:
    expandLongBranch(MI, CB, Fixups, STI);
    return;
  }

  // 3. Standalone single-op → Bundle128 composite. This is the single emit
  // path for every non-bundle, non-pseudo instruction.
  const MCInst *Child = &MI;
  if (encodeBundle128({Child}, CB, Fixups, STI))
    return;

  // 5. FORCING FUNCTION : the opcode has no Bundle128 form. This is the
  // forcing function for the remaining FLEX families — once they land, the
  // error vanishes. Report a clear, actionable message.
  StringRef Name = MII.getName(MI.getOpcode());
  report_fatal_error("Haydn MC: opcode '" + Name + "' (op" +
                         Twine(static_cast<unsigned>(MI.getOpcode())) +
                         ") has no Bundle128 form — add a *private* encode-peer "
                         "_S<k> def + FlexMap pair (: public identity stays"
                         "logical; do not emit _S* from ISel/MIR)",
                     /*GenCrashDiag=*/false);
}

//===----------------------------------------------------------------------===//
// Bundle encoding — collect children, route through encodeBundle128
//===----------------------------------------------------------------------===//

void HaydnMCCodeEmitter::encodeBundle(const MCInst &MBI,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Collect real (non-null, non-NOP) child instructions from the bundle.
  SmallVector<const MCInst *, Haydn::ISSUE_SLOT_COUNT> Children;
  for (unsigned I = 0, E = MBI.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = MBI.getOperand(I);
    if (Op.isInst() && Op.getInst()) {
      const MCInst *Child = Op.getInst();
      if (Child->getOpcode() != Haydn::NOP)
        Children.push_back(Child);
    }
  }

  // An all-NOP bundle emits a 16-byte Bundle128 NOP (all-zero composite). The
  // Bundle128 composite of three NOP slot windows is the spec §10 NOP.
  if (Children.empty()) {
    emitBundle128Word(APInt(128, 0), CB);
    return;
  }

  // The single Bundle128 emit path. If every child is a Bundle128-target opcode
  // (Flex-encodable or FU-routable), emit the 128-bit composite.
  if (encodeBundle128(Children, CB, Fixups, STI))
    return;

  // FORCING FUNCTION : a child lacks a Bundle128 form. Report the first
  // offending child so the missing FLEX family is actionable.
  for (const MCInst *Child : Children) {
    if (!isBundle128TargetOpcode(Child->getOpcode(), MII)) {
      StringRef Name = MII.getName(Child->getOpcode());
      report_fatal_error("Haydn MC: bundle child opcode '" + Name + "' (op" +
                             Twine(static_cast<unsigned>(Child->getOpcode())) +
                             ") has no Bundle128 form — add a *private* "
                             "encode-peer _S<k> def + FlexMap pair (:"
                             "logical public identity only)",
                         /*GenCrashDiag=*/false);
    }
  }
  // If every child IS a Bundle128-target opcode but encodeBundle128 still
  // declined (e.g. two children routed to the same slot — a Stage-1 coverage
  // gap), report it.
  report_fatal_error("Haydn MC: encodeBundle128 declined a bundle of " +
                         Twine(static_cast<unsigned>(Children.size())) +
                         " Bundle128-target child(ren) — slot-assignment gap",
                     /*GenCrashDiag=*/false);
}

//===----------------------------------------------------------------------===//
// Bundle128 symmetric no-rewrite emit path (AIE two-step compose)
//===----------------------------------------------------------------------===//
//
// The Bundle128 path is the permanent AIE-style CodeGenFormat framework: ONE
// def per op, the EMITTER applies the slot offset (no generic->_M0 variant
// rewrite). It is wired-but-not-rewriting — the framework drives placement +
// decode via MCFormatDesc.
//
// Phase 2 — single public slot authority is placement (HaydnMCFlags
// Bundle position). Suffix `_S<k>` is legacy-only for already-flex
// opcodes (hand asm / PEI interim). Encode-local setOpcode to Flex is NOT a
// second public authority — it is private materialize for the tblgen DAG only
// (MCInst is not MIR).
bool HaydnMCCodeEmitter::encodeBundle128(
    ArrayRef<const MCInst *> Children, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // P2b — AIE two-step composite encoding. Build a BUNDLE128_FULL MCInst
  // whose 3 operands are the slot sub-instructions (s0_slot, s1_slot, s2_slot)
  // then call getBinaryCodeForInstr on it. The encoding DAG walks the slot
  // operands; the MO.isInst branch in getMachineOpValue re-enters
  // getBinaryCodeForInstr on each sub-inst, slices the slot window via the
  // Bundle128 format-desc offsets, and ORs the slot bits into the composite
  // Inst. The 128-bit Inst is then emitted as 16 bytes little-endian.
  //
  // Gate: every real child must be a Bundle128-target opcode (Flex-encodable).
  // If any child is not, return false and let the caller apply the forcing
  // function.
  for (const MCInst *Child : Children)
    if (!isBundle128TargetOpcode(Child->getOpcode(), MII))
      return false;

  // `Haydn::Bundle<MCInst>` as the AIE-faithful shuffler.
  // Slot HINT priority (Phase 2 / single authority):
  // 1. HaydnMCFlags::getHaydnSlot — placement (CodeGen AltDescs / AsmParser
  // source order). Public authority.
  // 2. `_S<k>` name suffix — legacy for already-flex opcodes only.
  // 3. Bare (no hint) → Bundle::add auto-picks the first free legal slot.
  // A hint that is occupied/illegal auto-spreads to a free legal slot (the
  // `add(Instr, HintSlot)` overload falls back to pickSlot).
  //
  // Solitary S0|S1 load: force S0. Matches llvm-mc demotion of a lone
  // ld32_s1/ld64 into the S0 window and keeps -c ≡ -S|mc. Dual-load
  // bundles keep AltDescs slot=1 on the second load.
  HaydnMCFormatsWithMII Formats(MII);
  if (Children.size() == 1) {
    MCInst *Only = const_cast<MCInst *>(Children[0]);
    SlotBits Legal = Formats.getLegalSlots(Only->getOpcode());
    if (Legal & Haydn::SLOT0)
      HaydnMCFlags::setHaydnSlot(*Only, /*Slot=*/0);
  }

  // Resolve a legal placement hint (Flags or Flex suffix). Illegal source-order
  // Flags from AsmParser (e.g. ADD64 forced to S0) are dropped.
  auto resolveHint = [&](const MCInst *Child) -> MCSlotKind {
    unsigned Opc = Child->getOpcode();
    SlotBits Legal = Formats.getLegalSlots(Opc);
    if (auto FlagSlot = HaydnMCFlags::getHaydnSlot(*Child)) {
      if (Legal & (SlotBits(1) << *FlagSlot))
        return MCSlotKind::Haydn_SLOT_S0 + *FlagSlot;
    } else if (int SuffixSlot = getHaydnFlexSlotFromName(Opc, MII);
               SuffixSlot >= 0) {
      if (Legal & (SlotBits(1) << static_cast<unsigned>(SuffixSlot)))
        return MCSlotKind::Haydn_SLOT_S0 + SuffixSlot;
    }
    return MCSlotKind(MCSlotKind::SLOT_UNKNOWN);
  };

  auto tryPack =
      [&](ArrayRef<const MCInst *> Order, bool UseHints) -> std::optional<Haydn::MCBundle> {
    Haydn::MCBundle B(&Formats);
    for (const MCInst *Child : Order) {
      unsigned Opc = Child->getOpcode();
      if (!B.canAdd(Opc))
        return std::nullopt;
      MCSlotKind Hint = UseHints ? resolveHint(Child)
                                 : MCSlotKind(MCSlotKind::SLOT_UNKNOWN);
      if (Hint == MCSlotKind(MCSlotKind::SLOT_UNKNOWN))
        B.add(const_cast<MCInst *>(Child));
      else
        B.add(const_cast<MCInst *>(Child), Hint);
    }
    return B;
  };

  // Pass 1: source order + legal Flags (CodeGen AltDescs / dual-load).
  std::optional<Haydn::MCBundle> Packed = tryPack(Children, /*UseHints=*/true);
  // Pass 2: most-constrained-first free placement. Fixes multi-op hand-asm
  // like `{ add64; add32; add64 }` where S2-first sequential order starves
  // the second S1|S2-only op when Flags stamp illegal source order.
  if (!Packed) {
    SmallVector<const MCInst *, 3> Ordered(Children.begin(), Children.end());
    llvm::stable_sort(Ordered, [&](const MCInst *A, const MCInst *B) {
      auto pop = [&](const MCInst *C) {
        return llvm::popcount(Formats.getLegalSlots(C->getOpcode()));
      };
      return pop(A) < pop(B); // fewer legal slots first
    });
    Packed = tryPack(Ordered, /*UseHints=*/false);
  }
  if (!Packed) {
    LLVM_DEBUG({
      dbgs() << "Haydn MC: encodeBundle128 pack failed for "
             << Children.size() << " child(ren)\n";
    });
    return false;
  }
  Haydn::MCBundle &Bundle = *Packed;

  // Encode-local materialize: rewrite each child MCInst to its committed
  // slot's Flex/private variant for the tblgen encoding DAG only. This is not
  // MIR multi-auth — MCInst is transient and not visible as MachineInstr.
  SmallVector<const MCInst *, 3> Slots(3, nullptr); // s0, s1, s2
  for (const auto &KV : Bundle.getSlotMap()) {
    MCSlotKind Slot = KV.first;
    MCInst *Child = KV.second;
    unsigned SlotIdx = static_cast<unsigned>(Slot) - MCSlotKind::Haydn_SLOT_S0;
    assert(SlotIdx < 3 && "Bundle committed an out-of-range slot");
    if (unsigned Var = getHaydnFlexVariantForSlot(Child->getOpcode(), SlotIdx, MII)) {
      // See encodeSlotSubInst: CSRW 3-op → CSRW_S0 2-op must drop dead $rd.
      if (Child->getOpcode() == Haydn::CSRW && Child->getNumOperands() == 3) {
        MCOperand Csr = Child->getOperand(1);
        MCOperand Rs = Child->getOperand(2);
        Child->clear();
        Child->setOpcode(Var);
        Child->addOperand(Csr);
        Child->addOperand(Rs);
      } else {
        Child->setOpcode(Var);
      }
    }
    Slots[SlotIdx] = Child;
  }

  // safety net: Bundle::add can leave SlotMap empty while still holding
  // real children (empty-bundle standalone escape when getLegalSlots==0). That
  // used to fall through to "3×NOP composite" and silently write 16×0x00 for
  // ld32_reg/st32_reg. Decline so the caller diagnoses instead of zero-encode.
  bool AnySlot = false;
  for (const MCInst *S : Slots)
    if (S) {
      AnySlot = true;
      break;
    }
  if (!AnySlot) {
    LLVM_DEBUG(dbgs() << "Haydn MC: encodeBundle128 empty SlotMap for "
                      << Children.size() << " child(ren) — declining\n");
    return false;
  }

  // Build the composite MCInst. BUNDLE128_FULL's operand dag is
  // (ins s0_slot:$s0, s1_slot:$s1, s2_slot:$s2). Push the slot children in
  // s0/s1/s2 order; missing slots get a NOP placeholder so the operand count
  // matches the dag.
  MCInst Composite;
  Composite.setOpcode(Haydn::BUNDLE128_FULL);
  for (unsigned I = 0; I < 3; ++I) {
    const MCInst *Child = Slots[I];
    if (!Child) {
      MCInst *Nop = Ctx.createMCInst(); // owned by MCContext, lifetime-safe
      Nop->setOpcode(Haydn::NOP);
      Composite.addOperand(MCOperand::createInst(Nop));
    } else {
      Composite.addOperand(MCOperand::createInst(Child));
    }
  }

  // AIE two-step: one getBinaryCodeForInstr call composes the 128-bit Inst via
  // the MO.isInst branches. Fixups are translated slot->composite scope by
  // encodeSlotSubInst (GAP-MC3: byte-offset translation, no longer a
  // verbatim copy).
  APInt Binary, Scratch;
  SmallVector<MCFixup, 4> CompositeFixups;
  getBinaryCodeForInstr(Composite, CompositeFixups, Binary, Scratch, STI);
  for (MCFixup &F : CompositeFixups)
    Fixups.push_back(std::move(F));

  emitBundle128Word(Binary, CB);
  return true;
}

//===----------------------------------------------------------------------===//
// prototype — pack-from-bundle-metadata encoder model.
//
// PROOF: a LEGACY semantic opcode (e.g. ADD32) + an EXTERNAL slot source (the
// `-haydn-pack-from-metadata` flag, simulating AltDescs metadata the full
// redesign would propagate) produces byte-identical Bundle128 bytes to the
// `_S{k}` path. See.
void HaydnMCCodeEmitter::encodeSlotSubInst(
    const MCInst &Composite, const MCInst &SubInst, APInt &Op,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // AIE two-step: re-enter getBinaryCodeForInstr on the sub-instruction to
  // recover its standalone slot-window encoding, then slice the slot window out
  // of it via the Bundle128 format-desc offsets. Fixups produced for the
  // standalone sub-inst are translated slot-relative -> composite-relative.
  unsigned SlotIdx = 0;
  for (unsigned I = 0, E = Composite.getNumOperands(); I != E; ++I) {
    const MCOperand &MO = Composite.getOperand(I);
    if (MO.isInst() && MO.getInst() == &SubInst) {
      SlotIdx = I;
      break;
    }
  }
  MCSlotKind Kind;
  switch (SlotIdx) {
  default:
    llvm_unreachable("Bundle128 operand index must be 0, 1, or 2");
  case 0: Kind = MCSlotKind::Haydn_SLOT_S0; break;
  case 1: Kind = MCSlotKind::Haydn_SLOT_S1; break;
  case 2: Kind = MCSlotKind::Haydn_SLOT_S2; break;
  }

  APInt SubBinary, SubScratch;
  SmallVector<MCFixup, 4> BaseFixups;
  HaydnMCFormats Formats;

  // map legacy opcode → _S<k> variant before encoding, so the slot
  // window carries slot-form bytes that the decoder's per-slot trie recognizes.
  unsigned FlexOpc = Formats.getFlexVariant(SubInst.getOpcode(), SlotIdx);
  auto encodeOne = [&](const MCInst &Inst) {
    // CSRW (FmtCSR) is a 3-op shape: (dead $rd, $csr_addr, $rs). CSRW_S0 is
    // 2-op: ($csr, $r). A blind opcode rewrite (here or in Bundle::slot map)
    // left Imm:0 in op0 so OpFields wrote csr=0 and truncated the real
    // address into the 4-bit rs field (BSP putchar/exit → `csrw 0, …`).
    // Drop the leading dead def whenever we see the 3-op shape on CSRW_S0.
    if ((Inst.getOpcode() == Haydn::CSRW_S0 ||
         Inst.getOpcode() == Haydn::CSRW) &&
        Inst.getNumOperands() == 3) {
      MCInst Fixed;
      Fixed.setOpcode(Haydn::CSRW_S0);
      Fixed.addOperand(Inst.getOperand(1));
      Fixed.addOperand(Inst.getOperand(2));
      getBinaryCodeForInstr(Fixed, BaseFixups, SubBinary, SubScratch, STI);
      return;
    }
    getBinaryCodeForInstr(Inst, BaseFixups, SubBinary, SubScratch, STI);
  };
  if (FlexOpc != 0) {
    MCInst FlexInst(SubInst);
    FlexInst.setOpcode(FlexOpc);
    encodeOne(FlexInst);
  } else {
    encodeOne(SubInst);
  }

  // Look up the slot window in the Bundle128 format-desc.
  const MCFormatDesc &B128 = Formats.getBundle128FormatDesc();
  auto Offsets = B128.getSlotOffsetsHiBit(Kind);
  unsigned WindowWidth = Offsets.RightOffset - Offsets.LeftOffset + 1;
  assert(SubBinary.getBitWidth() >= WindowWidth &&
         "sub-inst encoding narrower than slot window");
  Op = APInt(WindowWidth, SubBinary.extractBitsAsZExtValue(WindowWidth, 0));

  // GAP-MC3 — translate fixups slot-relative → composite-relative.
  //
  // The Bundle128 composite is 128 bits, MSB-indexed (offset 0 = MSB = bit 127
  // offset 127 = LSB = bit 0). A slot window spans MSB-offsets
  // [LeftOffset, RightOffset]; its LSB position in the composite is
  // (127 - RightOffset). The sub-inst is encoded with bit 0 = its own LSB, so
  // when sliced into the window, sub-inst bit 0 lands at composite bit
  // (127 - RightOffset). A fixup at sub-inst byte X therefore lands at composite
  // byte ((127 - RightOffset) / 8) + X. For Bundle128:
  // S0: RightOffset=127 → base 0; S1: RightOffset=79 → base 6;
  // S2: RightOffset=39 → base 11.
  // The prior code copied BaseFixups VERBATIM (byte offset 0), which placed
  // every non-S0 fixup at the wrong composite byte → wrong reloc patches.
  // Mirrors AIE's `translateFixupsInComposite` (AIEBaseMCCodeEmitter.cpp:189).
  // AIE translateFixupsInComposite: remap standalone → composite. We only
  // adjust byte offset (same kind); PCRel must survive (Hexagon addFixup).
  unsigned SlotWindowLSBByteBase = (127 - Offsets.RightOffset) / 8;
  for (const MCFixup &F : BaseFixups) {
    addHaydnFixup(Fixups, F.getOffset() + SlotWindowLSBByteBase, F.getValue(),
                  F.getKind());
  }
}

//===----------------------------------------------------------------------===//
// Fixup helpers
//===----------------------------------------------------------------------===//

unsigned HaydnMCCodeEmitter::getBranchFixupKind(const MCInst &MI) const {
  // Bundle128: match getExprFixupKind for the same opcode classes so both
  // getBranchTargetOpValue and getMachineOpValue attach the correct field
  // geometry (imm12 at FieldLsb 4 or 8, not legacy BranchSImm16).
  switch (MI.getOpcode()) {
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
  case Haydn::BEQ_S0:
  case Haydn::BNE_S0:
  case Haydn::BGE_S0:
  case Haydn::BGEU_S0:
  case Haydn::BLT_S0:
  case Haydn::BLTU_S0:
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ_W_S0:
  case Haydn::BNE_W_S0:
  case Haydn::BGE_W_S0:
  case Haydn::BGEU_W_S0:
  case Haydn::BLT_W_S0:
  case Haydn::BLTU_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
  case Haydn::BEQZ_S0:
  case Haydn::BNEZ_S0:
  case Haydn::BLTZ_S0:
  case Haydn::BGEZ_S0:
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ_W_S0:
  case Haydn::BNEZ_W_S0:
  case Haydn::BGEZ_W_S0:
  case Haydn::BLTZ_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  default:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  }
}

unsigned HaydnMCCodeEmitter::getCallFixupKind(const MCInst &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (Opcode == Haydn::JAL)
    return Haydn::FIXUP_HAYDN_CallSImm20;
  if (Opcode == Haydn::JALR)
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  return Haydn::FIXUP_HAYDN_CallSImm20;
}

//===----------------------------------------------------------------------===//
// Operand value extraction
//===----------------------------------------------------------------------===//

void
HaydnMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      APInt &Op, SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
    return;
  }
  if (MO.isImm()) {
    Op = static_cast<uint64_t>(MO.getImm());
    return;
  }
  if (MO.isExpr()) {
    // Hexagon-style kind→PCRel (addHaydnFixup). JAL_S0's generated
    // encoder routes here (getMachineOpValue), not getCallTargetOpValue.
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), getExprFixupKind(MI));
    Op = 0;
    return;
  }
  if (MO.isInst()) {
    // P2b — AIE-model slot composition. A composite MCInst (Haydn::BUNDLE
    // or Haydn::BUNDLE128_FULL) holds its slot sub-instructions as MCOperand::
    // isInst operands. Re-enter getBinaryCodeForInstr on the sub-inst.
    encodeSlotSubInst(MI, *MO.getInst(), Op, Fixups, STI);
    return;
  }
  llvm_unreachable("Unhandled operand type in getMachineOpValue");
}

void
HaydnMCCodeEmitter::getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                                           APInt &Op,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  unsigned Opcode = MI.getOpcode();

  // D1/§5.14: ALL branches use 2-byte units (<<1). Unified at ÷2.
  (void)Opcode;
  unsigned Alignment = 2;
  unsigned Shift = 1;

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (Imm & (Alignment - 1)) {
      Ctx.reportError(SMLoc(), "branch offset must be 2-byte aligned");
    }
    Op = static_cast<uint64_t>(Imm >> Shift);
    return;
  }

  if (MO.isExpr()) {
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), getBranchFixupKind(MI));
    Op = 0;
    return;
  }

  llvm_unreachable("Unhandled operand type in getBranchTargetOpValue");
}

void
HaydnMCCodeEmitter::getCallTargetOpValue(const MCInst &MI, unsigned OpNo,
                                         APInt &Op,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  unsigned Opcode = MI.getOpcode();
  bool IsJAL = (Opcode == Haydn::JAL);

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (IsJAL) {
      // Linear 20-bit encoding: Inst[19:0] = (offset >> 1) & 0xFFFFF.
      Op = static_cast<uint64_t>((Imm >> 1) & 0xFFFFF);
      return;
    }
    Op = static_cast<uint64_t>(Imm);
    return;
  }

  if (MO.isExpr()) {
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), getCallFixupKind(MI));
    Op = 0;
    return;
  }

  if (MO.isReg()) {
    Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
    return;
  }

  llvm_unreachable("Unhandled operand type in getCallTargetOpValue");
}

//===----------------------------------------------------------------------===//
// (Stage 1): AIE-style scaled-immediate encoder for 48-bit WIDE formats.
//
// Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes via tablegen
// `EncoderMethod`. Reproduces the bit layout per operand:
// immediate form: range-check alignment, then (Imm >> Shift) masked to N.
// symbolic form: push one MCFixup of `FixupKind` at parcel Offset=0.
// FixupKind == Haydn::FIXUP_HAYDN_NONE: pure-immediate field, skip the
// fixup push.
//===----------------------------------------------------------------------===//
template <unsigned N, unsigned Shift, bool IsSigned, bool IsPCRel,
          Haydn::Fixups FixupKind>
void HaydnMCCodeEmitter::getSImmOpValueXStepWide(
    const MCInst &MI, unsigned OpNo, APInt &Op,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  (void)IsSigned;  // bit-identical for sign/zero at the field level
  (void)STI;

  if (OpNo >= MI.getNumOperands()) {
    Op = 0;
    return;
  }

  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (Shift > 0) {
      unsigned Alignment = 1u << Shift;
      if (Imm & (Alignment - 1)) {
        Ctx.reportError(SMLoc(),
                        Twine("offset must be ") + Twine(Alignment) +
                            "-byte aligned");
      }
    }
    uint64_t Mask = (N >= 64) ? ~0ULL : ((uint64_t{1} << N) - 1);
    Op = static_cast<uint64_t>(Imm >> Shift) & Mask;
    return;
  }

  if (MO.isExpr()) {
    if (FixupKind != Haydn::FIXUP_HAYDN_NONE) {
      Fixups.push_back(MCFixup::create(
          /*Offset=*/0, MO.getExpr(), static_cast<MCFixupKind>(FixupKind),
          IsPCRel));
    }
    Op = 0;
    return;
  }

  Op = 0;
}

// Explicit instantiation is unnecessary: the tablegen-generated
// HaydnGenMCCodeEmitter.inc instantiates this template for every operand
// class that references it via EncoderMethod.

// Mode-0 / page-1 EncoderMethods retired with the Mode-0 TableGen
// islands (legacy-retired). Live emission uses getSImmOpValueXStepWide +
// getMachineOpValue via Bundle128 FLEX formats only.
// Deleted: encodeS1LDPostImm6, encodeLSPage1Imm5, encodeLSPage1Reg.

MCCodeEmitter *llvm::createHaydnMCCodeEmitter(const MCInstrInfo &MCII,
                                               MCContext &Ctx) {
  return new HaydnMCCodeEmitter(Ctx, MCII);
}

//===----------------------------------------------------------------------===//
// Long branch pseudo expansion
//
// Expands a long-branch pseudo to: inverted-conditional-branch + JAL. Each
// emitted real instruction recurses through `encodeInstruction`, which routes
// to `encodeBundle128` (the single Bundle128 emit path). The inverted branch's
// literal skip-offset (8 bytes) emits no fixup; the JAL's symbolic target
// carries the long-branch fixup at byte offset 16 (the JAL's position within
// the 32-byte composite sequence — each real instruction is now a 16-byte
// Bundle128 parcel).
//===----------------------------------------------------------------------===//

// Map a long branch pseudo to the inverted conditional branch opcode.
static unsigned getInvertedBranchOpcode(unsigned LongBrOpc) {
  switch (LongBrOpc) {
  default:
    llvm_unreachable("Unexpected long branch opcode!");
  case Haydn::PseudoLongBEQ:  return Haydn::BNE;
  case Haydn::PseudoLongBNE:  return Haydn::BEQ;
  case Haydn::PseudoLongBGE:  return Haydn::BLT;
  case Haydn::PseudoLongBGEU: return Haydn::BLTU;
  case Haydn::PseudoLongBLT:  return Haydn::BGE;
  case Haydn::PseudoLongBLTU: return Haydn::BGEU;
  case Haydn::PseudoLongBEQZ: return Haydn::BNEZ;
  case Haydn::PseudoLongBNEZ: return Haydn::BEQZ;
  case Haydn::PseudoLongBGEZ: return Haydn::BLTZ;
  case Haydn::PseudoLongBLTZ: return Haydn::BGEZ;
  // Unconditional: no inverted condition needed; just JAL.
  case Haydn::PseudoLongB:    return 0;
  }
}

void HaydnMCCodeEmitter::expandLongBranch(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  unsigned LongOpc = MI.getOpcode();
  unsigned InvOpc = getInvertedBranchOpcode(LongOpc);

  // Record the fixup count before the inverted branch so we can drop any
  // spurious fixup it produces (its offset is a literal 8, not a symbol).
  const size_t FixupBeforeInv = Fixups.size();

  if (InvOpc != 0) {
    // Build the inverted conditional branch with a fixed offset of 8 bytes
    // (skip over this instruction + the JAL that follows).
    MCInst InvBr;
    InvBr.setOpcode(InvOpc);

    if (LongOpc == Haydn::PseudoLongBEQ || LongOpc == Haydn::PseudoLongBNE ||
        LongOpc == Haydn::PseudoLongBGE || LongOpc == Haydn::PseudoLongBGEU ||
        LongOpc == Haydn::PseudoLongBLT || LongOpc == Haydn::PseudoLongBLTU) {
      // Two-register form: rs1, rs2, skip_offset
      InvBr.addOperand(MI.getOperand(0)); // rs1
      InvBr.addOperand(MI.getOperand(1)); // rs2
      InvBr.addOperand(MCOperand::createImm(8)); // skip 8 bytes
    } else {
      // Single-register form: rs, skip_offset
      InvBr.addOperand(MI.getOperand(0)); // rs
      InvBr.addOperand(MCOperand::createImm(8)); // skip 8 bytes
    }

    // route the inverted branch through encodeInstruction, which emits
    // it as a 16-byte Bundle128 parcel via encodeBundle128. Any fixup it
    // produces is spurious (literal offset) — drop it.
    encodeInstruction(InvBr, CB, Fixups, STI);
    Fixups.resize(FixupBeforeInv);
  }
  // PseudoLongB (unconditional): no inverted branch; the JAL alone suffices.

  // Emit the JAL that jumps to the actual target.
  // JAL R0, target — R0 as destination discards the return address.
  MCInst Jal;
  Jal.setOpcode(Haydn::JAL);
  Jal.addOperand(
      MCOperand::createReg(Ctx.getRegisterInfo()->getEncodingValue(Haydn::R0)));

  // The target operand is the last operand of the long branch pseudo.
  unsigned TargetOpIdx;
  if (LongOpc == Haydn::PseudoLongBEQ || LongOpc == Haydn::PseudoLongBNE ||
      LongOpc == Haydn::PseudoLongBGE || LongOpc == Haydn::PseudoLongBGEU ||
      LongOpc == Haydn::PseudoLongBLT || LongOpc == Haydn::PseudoLongBLTU) {
    TargetOpIdx = 2;
  } else if (LongOpc == Haydn::PseudoLongB) {
    TargetOpIdx = 0;
  } else {
    TargetOpIdx = 1;
  }

  Jal.addOperand(MI.getOperand(TargetOpIdx));

  // route the JAL through encodeInstruction (Bundle128 emit). The JAL's
  // getCallTargetOpValue emits a FIXUP_HAYDN_CallSImm20 for the symbolic
  // target. Update the fixup offset to account for the preceding inverted
  // branch (now a 16-byte Bundle128 parcel).
  const size_t FixupBeforeJal = Fixups.size();
  encodeInstruction(Jal, CB, Fixups, STI);
  for (size_t I = FixupBeforeJal; I < Fixups.size(); ++I) {
    MCFixup &F = Fixups[I];
    Fixups[I] = MCFixup::create(F.getOffset() + 16, F.getValue(), F.getKind());
  }
}

#include "HaydnGenMCCodeEmitter.inc"
