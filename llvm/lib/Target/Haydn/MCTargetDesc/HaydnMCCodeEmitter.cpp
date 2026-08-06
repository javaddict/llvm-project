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
// Bundle128-only emitter (AIE serialize-only). Encodes member Desc as-is;
// post-RA setDesc is the sole materialize authority.
//
// AIE peers:
//   AIEBaseMCCodeEmitter.cpp:45-68  encodeInstruction = getBinaryCode + emit
//   AIEBaseMCCodeEmitter.cpp:122-184 encode nested sub-inst from member Desc
//   AIEBaseMCFormats.cpp:66-75       getSlotKind on member opcode
//   AIEBaseAsmPrinter.cpp:161-164    Format->Opcode composite
//
// `encodeInstruction` routing:
//   BUNDLE128_FULL (preformed) -> getBinaryCodeForInstr + emitBundle128Word
//                                 (no re-slot / no re-auction)
//   Haydn::BUNDLE (asm residual) -> encodeBundle -> encodeBundle128
//   PseudoLongB* -> expandLongBranch (recurse encodeInstruction)
//   standalone single-op -> encodeBundle128 (transient composite; residual
//                           logical/hand-asm only — not a CodeGen writer)
//   else -> report_fatal_error (uncovered opcode)
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
#include "llvm/Support/Debug.h"
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
  // Expand a long branch pseudo to: inverted-conditional-branch + JAL.
  // Each emitted real instruction recurses through `encodeInstruction`.
  void expandLongBranch(const MCInst &MI, SmallVectorImpl<char> &CB,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const;

  // AIE-model slot sub-instruction encoding (AIEBaseMCCodeEmitter.cpp:122-184).
  // Always encodes SubInst member Desc as-is. Post-RA setDesc
  // (AIEMachineScheduler materializeMultiOpcodeInstrs) is the sole
  // materialize authority.
  void encodeSlotSubInst(const MCInst &Composite, const MCInst &SubInst,
                         APInt &Op, SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  // Assembler residual: collect children from Haydn::BUNDLE → encodeBundle128.
  // CodeGen emits preformed BUNDLE128_FULL (Format->Opcode) and skips this.
  void encodeBundle(const MCInst &MBI, SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const;

  // Build a transient BUNDLE128_FULL for residual children (standalone /
  // hand-asm). Placement is Bundle.add by member getSlotKind / alts — no
  // Flags, no constrained-first re-auction, no encode-local setOpcode Flex.
  bool encodeBundle128(ArrayRef<const MCInst *> Children,
                       SmallVectorImpl<char> &CB,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const;

  // emit a Bundle128 128-bit (16-byte) composite parcel
  // little-endian.
  void emitBundle128Word(const APInt &Word, SmallVectorImpl<char> &CB) const;

  unsigned getBranchFixupKind(const MCInst &MI) const;
  unsigned getCallFixupKind(const MCInst &MI) const;
  unsigned getExprFixupKind(const MCInst &MI) const;
};

} // end anonymous namespace

// Determine the appropriate fixup kind for an expression operand based on
// the parent instruction opcode. Geometry per HaydnRelocLayout.
// Slot-variant opcodes (post-setDesc members / residual flex) route symbolic
// operands through getMachineOpValue — this is the single fixup-kind point.
//
// The switch names logicals only. A member reaches here because its operand
// class carries no EncoderMethod, so it falls back to getMachineOpValue while
// the logical routes through getCallTargetOpValue / getBranchTargetOpValue;
// the fixup kind is a property of the instruction, not of where it was placed.
// Folding through the logical base keeps this table independent of how members
// are spelled — Bundle128's `_S<k>` today, format E's `_P<f><p>_<unit>` next.
unsigned HaydnMCCodeEmitter::getExprFixupKind(const MCInst &MI) const {
  switch (getHaydnLogicalBaseOpcode(MI.getOpcode(), MII)) {
  default:
    break;
  case Haydn::LUI:
    // Bundle128 LUI_S0 carries a 12-bit high field (HaydnFU_ALU32_S0_I12).
    // HI12 pairs with LO20 on ADDI32 (not the retired 32-bit-parcel HI20/LO16).
    return Haydn::FIXUP_HAYDN_HI12;
  // ADDI32 Bundle128 RI20: imm20 at s0 bits[37:18] → LO20 (not legacy LO16).
  // ADDI32_W / ADDI32_W_S0 handled below with ORI32_W (block).
  case Haydn::ADDI32:
    return Haydn::FIXUP_HAYDN_LO20;
  // ADDI32S/SUBI* still use signed imm fields; ANDI/ORI/XORI are RI20 ZEXT
  // (ISA: uimm20) — same LO20 window as ADDI32 Bundle128 peers (not LO16).
  case Haydn::ADDI32S:
  case Haydn::SUBI32:
  case Haydn::SUBI32S:
    return Haydn::FIXUP_HAYDN_LO16;
  case Haydn::ANDI32:
  case Haydn::ORI32:
  case Haydn::XORI32:
    return Haydn::FIXUP_HAYDN_LO20;
  // Reachable only as a member: logical JAL/JALR carry `calltarget`, whose
  // EncoderMethod sends the target to getCallTargetOpValue instead. Members
  // drop that operand class, land here, and must match getCallFixupKind.
  case Haydn::JAL:
    return Haydn::FIXUP_HAYDN_CallSImm20;
  case Haydn::JALR:
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  // Bundle128 branch `_S0` forms use the same s0 windows as the `_W_S0`
  // peers (cutover). FIXUP_HAYDN_BranchSImm16 still has legacy-parcel
  // geometry (FieldLsb=0, FieldSize=16) and does not patch Bundle128
  // imm12 — linked BEQ/BNE kept offset 0. Map to the WIDE fixup kinds that
  // already carry correct FieldLsb (RI12 → bits[19:8]/8; I12 → bits[15:4]/4).
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
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
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // Two-register WIDE cond (RI12): imm12 at s0 bits[19:8] → FieldLsb=8.
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  // ADDI32_W/ORI32_W `_S0` carry the wide-reloc operand
  // (simm20_wide_abs / uimm20_wide_abs) — the symbolic operand MUST map to
  // FIXUP_HAYDN_LO20 (the 20-bit absolute LO20 reloc, paired with LUI's HI12).
  // Without this, the default FIXUP_HAYDN_32 clobbers the opcode bytes.
  // The legacy ADDI32_W/ORI32_W are also handled here (the AsmParser matches
  // the bare mnemonic to the logical opcode; residual encode materializes the
  // _S0 member via alts — but getExprFixupKind sees the original opcode).
  case Haydn::ADDI32_W:
  case Haydn::ORI32_W:
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
  // 1. Preformed BUNDLE128_FULL composite (CodeGen AsmPrinter Format->Opcode;
  // AIEBaseAsmPrinter.cpp:161-164 + AIEBaseMCCodeEmitter.cpp:45-68). Serialize
  // only — no re-slot, no re-auction. Product sole live packet row is
  // BUNDLE128_FULL.
  if (MI.getOpcode() == Haydn::BUNDLE128_FULL) {
    APInt Binary, Scratch;
    getBinaryCodeForInstr(MI, Fixups, Binary, Scratch, STI);
    emitBundle128Word(Binary, CB);
    return;
  }

  // 2. Residual TargetOpcode::BUNDLE (legacy / non-asm producers). AsmParser
  // emits Format->Opcode BUNDLE128_FULL (fast path above); this collects +
  // packs any remaining generic BUNDLE MCInsts.
  if (MI.getOpcode() == Haydn::BUNDLE) {
    encodeBundle(MI, CB, Fixups, STI);
    return;
  }

  // 3. Long-branch pseudos — expand to inverted-conditional-branch + JAL.
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

  // 4. Standalone single-op residual → transient Bundle128 composite.
  // CodeGen bundles already arrived as BUNDLE128_FULL above; this is for
  // emitWrappedInst / hand-asm singles, not a third CodeGen placement writer.
  const MCInst *Child = &MI;
  if (encodeBundle128({Child}, CB, Fixups, STI))
    return;

  // 5. Forcing function: opcode has no Bundle128 form.
  StringRef Name = MII.getName(MI.getOpcode());
  report_fatal_error("Haydn MC: opcode '" + Name + "' (op" +
                         Twine(static_cast<unsigned>(MI.getOpcode())) +
                         ") has no Bundle128 form — add a format member / "
                         "PlacementAlternative (logical public identity; "
                         "post-RA setDesc materializes _S* for CodeGen)",
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

  // The single Bundle128 emit path. If every child is a Bundle128-target
  // opcode, emit the 128-bit composite.
  if (encodeBundle128(Children, CB, Fixups, STI))
    return;

  // FORCING FUNCTION: a child lacks a Bundle128 form. Report the first
  // offending child so the missing member family is actionable.
  for (const MCInst *Child : Children) {
    if (!isBundle128TargetOpcode(Child->getOpcode(), MII)) {
      StringRef Name = MII.getName(Child->getOpcode());
      report_fatal_error("Haydn MC: bundle child opcode '" + Name + "' (op" +
                             Twine(static_cast<unsigned>(Child->getOpcode())) +
                             ") has no Bundle128 form — add a *private* "
                             "encode-peer _S<k> def + PlacementAlternative "
                             "(logical public identity only)",
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
// Bundle128 residual pack → composite (AIE two-step compose)
//===----------------------------------------------------------------------===//
//
// Serialize-only: CodeGen already emits preformed BUNDLE128_FULL
// (Format->Opcode). This path builds a transient composite for residual
// standalone / hand-asm children only.
//
// Placement authority is Bundle.add by member getSlotKind (fixed Desc) or
// PlacementAlternative tryAdd (logical) — AIEBundle.h:92-145 peer.
// encodeSlotSubInst serializes member Desc as-is (AIEBaseMCCodeEmitter.cpp:
// 134-162); residual logicals must already be setDesc'd or asm format members.
bool HaydnMCCodeEmitter::encodeBundle128(
    ArrayRef<const MCInst *> Children, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // Gate: every real child must be a Bundle128-target opcode.
  for (const MCInst *Child : Children)
    if (!isBundle128TargetOpcode(Child->getOpcode(), MII))
      return false;

  // Single-pass Bundle.add (no Flags, no constrained-first re-auction).
  // Fixed getSlotKind members land in their Desc slot; multi-slot logicals
  // use Bundle pickSlot tryAdd (same canAdd authority).
  //
  // Residual solitary (non-brace / non-BUNDLE128_FULL) hand-asm: prefer S0
  // when legal (fixup window / -c≡mc stability). Bundle.add Hint — not Flags.
  // Multi-child residual BUNDLE packs source-order Bundle.add (no re-auction);
  // brace asm never reaches here (preformed BUNDLE128_FULL fast path).
  HaydnMCFormatsWithMII Formats(MII);
  Haydn::MCBundle Bundle(&Formats);
  if (Children.size() == 1) {
    MCInst *Only = const_cast<MCInst *>(Children[0]);
    unsigned Opc = Only->getOpcode();
    if (!Bundle.canAdd(Opc))
      return false;
    SlotBits Legal = Formats.getLegalSlots(Opc);
    if (Legal & Haydn::SLOT0)
      Bundle.add(Only, MCSlotKind(MCSlotKind::Haydn_SLOT_S0));
    else
      Bundle.add(Only);
  } else {
    for (const MCInst *Child : Children) {
      unsigned Opc = Child->getOpcode();
      if (!Bundle.canAdd(Opc)) {
        LLVM_DEBUG(dbgs() << "Haydn MC: encodeBundle128 canAdd failed for "
                          << MII.getName(Opc) << " (source-order pack; no "
                             "re-auction)\n");
        return false;
      }
      Bundle.add(const_cast<MCInst *>(Child));
    }
  }

  // SlotMap → s0/s1/s2; encodeSlotSubInst slices member Desc as-is
  // (AIE getBinaryCodeForInstr).
  SmallVector<const MCInst *, 3> Slots(3, nullptr);
  for (const auto &KV : Bundle.getSlotMap()) {
    MCSlotKind Slot = KV.first;
    MCInst *Child = KV.second;
    unsigned SlotIdx = static_cast<unsigned>(Slot) - MCSlotKind::Haydn_SLOT_S0;
    assert(SlotIdx < 3 && "Bundle committed an out-of-range slot");
    Slots[SlotIdx] = Child;
  }

  // Safety net: Bundle::add can leave SlotMap empty while still holding real
  // children (standalone escape when no legal slot). Decline vs silent 16x0.
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

  // Build composite. BUNDLE128_FULL dag: (ins s0_slot, s1_slot, s2_slot).
  // Missing slots get NOP placeholders (AIE SlotInfo NOP peer).
  MCInst Composite;
  Composite.setOpcode(Haydn::BUNDLE128_FULL);
  for (unsigned I = 0; I < 3; ++I) {
    const MCInst *Child = Slots[I];
    if (!Child) {
      MCInst *Nop = Ctx.createMCInst();
      Nop->setOpcode(Haydn::NOP);
      Composite.addOperand(MCOperand::createInst(Nop));
    } else {
      Composite.addOperand(MCOperand::createInst(Child));
    }
  }

  // AIE two-step: getBinaryCodeForInstr composes 128-bit Inst via MO.isInst
  // (encodeSlotSubInst). Same path as preformed BUNDLE128_FULL fast-path.
  APInt Binary, Scratch;
  SmallVector<MCFixup, 4> CompositeFixups;
  getBinaryCodeForInstr(Composite, CompositeFixups, Binary, Scratch, STI);
  for (MCFixup &F : CompositeFixups)
    Fixups.push_back(std::move(F));

  emitBundle128Word(Binary, CB);
  return true;
}

//===----------------------------------------------------------------------===//
// encodeSlotSubInst — AIE slice path (AIEBaseMCCodeEmitter.cpp:122-184)
//===----------------------------------------------------------------------===//
//
// AIEBaseMCCodeEmitter.cpp:134-162 encodes SubInst Desc as-is;
// SubInstFormat/slot geometry come from SubInst.getOpcode().
//
// CodeGen path: post-RA setDesc already committed format members
// (AIEMachineScheduler.cpp:1126-1132 materializeMultiOpcodeInstrs) → encode
// Desc as-is (AIE getSlotKind post-commit, AIEBaseMCFormats.cpp:66-75).
//
// Residual hand-asm: matcher may still match the logical public mnemonic
// (ADD32 before ADD32_S*). Materialize that residual via sparse
// getAlternateInstsOpcode[SlotIdx] — the same PlacementAlternative / setDesc
// member table. Local copy only; no MCFlags.
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

  auto encodeOne = [&](const MCInst &Inst) {
    // CSRW (FmtCSR) is a 3-op shape: (dead $rd, $csr_addr, $rs). CSRW_S0 is
    // 2-op: ($csr, $r). Drop the leading dead def — operand layout normalize
    // only.
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

  // AIE path: member Desc has fixed getSlotKind → encode as-is
  // (AIEBaseMCFormats.cpp:66-75 + AIEBaseMCCodeEmitter.cpp:136/161-162).
  // Residual logical (no fixed slot): AlternateInsts member for this composite
  // operand index — same table post-RA setDesc uses.
  MCSlotKind SubKind = Formats.getSlotKind(SubInst.getOpcode());
  if (SubKind != MCSlotKind()) {
    encodeOne(SubInst);
  } else if (const std::vector<unsigned> *Alts =
                 Formats.getAlternateInstsOpcode(SubInst.getOpcode());
             Alts && SlotIdx < Alts->size() && (*Alts)[SlotIdx] != 0) {
    MCInst Member(SubInst);
    Member.setOpcode((*Alts)[SlotIdx]);
    encodeOne(Member);
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

  // Translate fixups slot-relative → composite-relative.
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
  // Logical-only, for the reason given on getExprFixupKind.
  switch (getHaydnLogicalBaseOpcode(MI.getOpcode(), MII)) {
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  default:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  }
}

unsigned HaydnMCCodeEmitter::getCallFixupKind(const MCInst &MI) const {
  if (getHaydnLogicalBaseOpcode(MI.getOpcode(), MII) == Haydn::JALR)
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
    // AIE-model slot composition (AIEBaseMCCodeEmitter.cpp:122-184). Composite
    // BUNDLE128_FULL holds slot sub-instructions as MCOperand::isInst.
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
// getMachineOpValue via Bundle128 formats only.

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
