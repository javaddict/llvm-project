//===- HaydnFinalizeBundle.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEFinalizeBundle (AIEFinalizeBundle.cpp:22-54 isBundleCandidate +
// wrap loop). Haydn overlay vs AIE:
//
//   * Stamp Format E BundleFormatRowID + CompletionStateID on newly wrapped
//     singleton roots (ProductDefaultRowID + full-slot completion from the
//     shared pad/member census). Already-bundled roots that already carry
//     both imms are identity (AIEFinalizeBundle.cpp:49-56).
//   * Copy the earliest member DebugLoc onto a BUNDLE root that has none
//     (MachineInstrBundle.cpp:90-136; Hexagon packetize-debug-loc.mir).
//   * Mixed-stream code-bearing inline asm is fail-closed. Mixed generated
//     MemberId + leftover FieldSlot/logical is fail-closed. Reloc CSR I8
//     that is still a catalog FieldSlot is fail-closed (untyped NONE).
//
// This pass does not choose: no singleton row resettle, no name peel, no
// member/entry DFS or E2/E3 retry, no keep-map rewrite, no late setDesc.
// Direct-compatible member bake stays on the scheduler/materialize path
// (HaydnMemberSetDesc.h).
//
// Pipeline: addPreSched2 after PostMachineScheduler
// (AIE2TargetMachine.cpp:242-244); addPreEmit after BR/Fixup/BR; closure
// at addPostBBSections after the common executable tail.
//
//===----------------------------------------------------------------------===//

#include "HaydnFinalizeBundle.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnMemberSetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <iterator>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "haydn-finalize-mi-bundles"

namespace {

// Port of AIEFinalizeBundle.cpp:22-36 isBundleCandidate.
// AIE also skips isHardwareLoopEnd; Haydn PseudoLoopEnd is already
// isMetaInstruction (skipped below). LoopDec/LoopJNZ are real and wrap.
bool isBundleCandidate(MachineBasicBlock::instr_iterator MII) {
  MachineInstr *MI = &*MII;
  if (MI->isMetaInstruction() || MI->isBundle() || MII->isBundled())
    return false;
  // Inline asm is a TargetOpcode pseudo lowered by AsmPrinter::emitInlineAsm
  // on the top-level MI. Wrapping it as a BUNDLE child drops the APP block.
  if (MI->isInlineAsm())
    return false;
  // W67 non-tail fnptr call clone (JALR_MSP): golden JALR members are
  // isTerminator=1, so the mid-block call never takes a member Desc in MIR.
  // Bundle children must be member-encodable; this clone stays standalone.
  if (MI->isCall() && !MI->isTerminator() && !MI->isPseudo()) {
    MachineFunction *PMF = MI->getMF();
    if (PMF && PMF->getSubtarget().getInstrInfo()->getName(MI->getOpcode())
                   .ends_with("_MSP"))
      return false;
  }
  return true;
}

/// Keep-list from FieldSlot explicit operands onto generated member Desc.
/// Identity census is empty: accepted maps are identity when counts/kinds/defs
/// match, plus the isAsmParserOnly D_LDW_CB_IMM operand-order swap. Ties must
/// correspond exactly through the map. Used by post-RA/materialize bake only.
std::optional<SmallVector<unsigned, 4>>
fieldSlotKeepOperands(const MachineInstr &MI, const MCInstrDesc &NewDesc) {
  const MCInstrDesc &OldDesc = MI.getDesc();
  const unsigned OldN = OldDesc.getNumOperands();
  const unsigned NewN = NewDesc.getNumOperands();
  if (OldN == 0 && NewN == 0)
    return SmallVector<unsigned, 4>{};
  if (OldN == 0 || NewN == 0 || MI.getNumExplicitOperands() < OldN)
    return std::nullopt;

  auto kindOk = [&](unsigned OldI, unsigned NewI) -> bool {
    const MachineOperand &MO = MI.getOperand(OldI);
    const MCOperandInfo &Info = NewDesc.operands()[NewI];
    const bool WantReg =
        Info.OperandType == MCOI::OPERAND_REGISTER || Info.RegClass >= 0;
    if (WantReg)
      return MO.isReg();
    return MO.isImm() || MO.isMBB() || MO.isGlobal() || MO.isSymbol() ||
           MO.isCPI() || MO.isJTI() || MO.isBlockAddress() || MO.isMCSymbol() ||
           MO.isTargetIndex();
  };

  auto tiesOk = [&](ArrayRef<unsigned> Keep) -> bool {
    if (Keep.size() != NewN)
      return false;
    for (unsigned NewI = 0; NewI != NewN; ++NewI) {
      const int NewTie = NewDesc.getOperandConstraint(NewI, MCOI::TIED_TO);
      if (NewTie == -1)
        continue;
      if (static_cast<unsigned>(NewTie) >= NewN)
        return false;
      const int OldTie =
          OldDesc.getOperandConstraint(Keep[NewI], MCOI::TIED_TO);
      if (OldTie == static_cast<int>(Keep[NewTie]))
        continue;
      return false;
    }
    return true;
  };

  auto Keep = haydnFormatEKeepOperands(OldDesc, NewDesc, kindOk);
  if (!Keep)
    return std::nullopt;
  if (!tiesOk(*Keep))
    return std::nullopt;
  return Keep;
}

bool hasRelocatableOperand(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (MO.isMBB() || MO.isGlobal() || MO.isSymbol() || MO.isMCSymbol() ||
        MO.isBlockAddress() || MO.isJTI() || MO.isCPI() || MO.isTargetIndex())
      return true;
  }
  return false;
}

/// Copy the earliest member DebugLoc onto a BUNDLE root that has none.
/// Peer: MachineInstrBundle.cpp:90-101; Hexagon packetize-debug-loc.mir.
bool propagateEarliestMemberDebugLoc(MachineInstr &Root) {
  if (!Root.isBundle() || Root.getDebugLoc())
    return false;

  DebugLoc DL;
  MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
  MachineBasicBlock::instr_iterator E = getBundleEnd(Root.getIterator());
  for (; I != E; ++I) {
    if (DebugLoc MemberDL = I->getDebugLoc()) {
      if (MemberDL.getLine() != 0) {
        Root.setDebugLoc(MemberDL);
        return true;
      }
      if (!DL)
        DL = MemberDL;
    }
  }
  if (!DL)
    return false;
  Root.setDebugLoc(DL);
  return true;
}

/// Rewrite Haydn::B (bare or bundled) to BEQZ_W_MSP R0 in place so a
/// bundled uncond stays inside its BUNDLE. Catalog BEQZ_W is not a
/// barrier (AIEPseudoBranchExpansion.cpp:70-75 uses a Barrier opcode;
/// Haydn overlay is the JALR_MSP-style flag clone). Do not bake `_MSP`
/// onto catalog BEQZ_W. Every seat expands B: the size-oracle Finalize
/// (addPreSched2, before BR) included, because the MSP clone is
/// isUnconditionalBranch and BranchRelaxation still sees a barrier uncond.
bool expandUncondBToBeqz(MachineFunction &MF, const TargetInstrInfo &TII) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB.instrs())) {
      if (MI.getOpcode() != Haydn::B)
        continue;
      MachineBasicBlock *Target = nullptr;
      SmallVector<MachineOperand, 4> ExtraImplicits;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isMBB() && !Target) {
          Target = MO.getMBB();
          continue;
        }
        if (MO.isReg() && MO.isImplicit())
          ExtraImplicits.push_back(MO);
      }
      if (!Target)
        report_fatal_error("Haydn FinalizeBundle: B has no MBB operand",
                           /*GenCrashDiag=*/false);
      while (MI.getNumOperands())
        MI.removeOperand(MI.getNumOperands() - 1);
      MI.setDesc(TII.get(Haydn::BEQZ_W_MSP));
      MI.addOperand(MF, MachineOperand::CreateReg(Haydn::R0, /*isDef=*/false));
      MI.addOperand(MF, MachineOperand::CreateMBB(Target));
      for (const MachineOperand &MO : ExtraImplicits)
        MI.addOperand(MF, MO);
      Changed = true;
    }
  }
  return Changed;
}

/// Leftover RET / BR_JT / PseudoCALLIndirect (bare or bundled). Always,
/// including the size-oracle Finalize and -run-pass wrap: these are not the
/// CFG uncond shell BranchRelaxation analyzes.
bool expandLeftoverRetJtCall(MachineFunction &MF) {
  bool Changed = false;
  const HaydnInstrInfo &TII =
      static_cast<const HaydnInstrInfo &>(*MF.getSubtarget().getInstrInfo());
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB.instrs())) {
      const unsigned Opc = MI.getOpcode();
      if (Opc != Haydn::RET && Opc != Haydn::BR_JT &&
          Opc != Haydn::PseudoCALLIndirect)
        continue;
      if (TII.expandRepresentationPseudo(MI))
        Changed = true;
    }
  }
  return Changed;
}

/// Wrap leftover bare real MIs as singleton BUNDLEs and stamp the row the
/// child's InstSlot already occupies (AIE getSlotKind after setDesc,
/// AIEBaseMCFormats.cpp:66-75). Logical leftovers have no e2/e3 slot and
/// keep ProductDefaultRowID. Does not restamp already-bundled roots
/// (AIEFinalizeBundle.cpp:40-59). Does not setDesc, resettle, or peel `_E3_`.
bool wrapBareAndStamp(MachineFunction &MF) {
  bool Changed = false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator MII = MBB.instr_begin();
    MachineBasicBlock::instr_iterator MIE = MBB.instr_end();
    if (MII == MIE)
      continue;
    assert(!MII->isInsideBundle() && "First instr cannot be inside bundle!");

    while (MII != MIE) {
      if (!MII->isInsideBundle() && isBundleCandidate(MII)) {
        haydn::bundle::applyFinalDirectCompatibleSingleton(*MII, Fmts, TII);
        finalizeBundle(MBB, MII, std::next(MII));
        MachineInstr &Root = *getBundleStart(MII);
        assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
        SmallVector<unsigned, 3> Members =
            haydn::bundle::collectBundleMemberOpcodes(Root);
        const bool HasPad = haydn::bundle::bundleHasPadNop(Root);
        const auto Row = haydn::bundle::selectProductRowForOpcodes(Members);
        const auto Comp = haydn::bundle::selectCompletionForMembersAndPads(
            Row, Members.size(), HasPad);
        assert(haydn::bundle::isProductLegalCompletion(Comp) &&
               "singleton wrap must stamp full-slot product completion");
        haydn::bundle::stampBundleCommit(Root, Row, Comp);
        Changed = true;
      }
      ++MII;
    }
  }
  return Changed;
}

/// Stamp row/completion on already-bundled roots that are missing either
/// imm. Already-stamped roots are identity. Occupancy capacity chooses E3
/// only when three reals cannot sit on E2 — not a Mode/DFS retry.
bool stampUnstampedBundledRoots(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBundle())
        continue;
      auto Row = haydn::bundle::getBundleRowID(MI);
      const bool HasCompletion =
          haydn::bundle::getBundleCompletionID(MI).has_value();
      if (Row && HasCompletion)
        continue;
      SmallVector<unsigned, 3> Members =
          haydn::bundle::collectBundleMemberOpcodes(MI);
      const bool HasPadNop = haydn::bundle::bundleHasPadNop(MI);
      unsigned DagN = 0;
      {
        MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
        MachineBasicBlock::instr_iterator E = getBundleEnd(MI.getIterator());
        for (; I != E; ++I) {
          if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
            continue;
          ++DagN;
        }
      }
      if (!Row && Members.empty() && !HasPadNop)
        continue;
      const auto Needed = haydn::bundle::selectProductRowForMemberCount(
          std::max<unsigned>(Members.size(), DagN));
      if (!Row)
        Row = Needed;
      else if (DagN > 2 &&
               *Row != haydn::bundle::BundleFormatRowID::E96ThreeEntry)
        Row = Needed;
      haydn::bundle::stampBundleCommit(
          MI, *Row,
          haydn::bundle::selectCompletionForMembersAndPads(
              *Row, Members.size(), HasPadNop));
      Changed = true;
    }
  }
  return Changed;
}

/// Leftover public logicals inside an already-formed BUNDLE (hand MIR /
/// limited -run-pass / BR insert) take the one materialize bake site.
/// Singleton leftovers prefer ProductDefaultRowID E2. Multi-member leftovers
/// use commitExactHardRootProductCycle (exactSolveProductOpcodes). Not a
/// Finalize DFS / name-peel / keep-map chooser.
bool bakeLeftoverLogicalBundles(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  bool Changed = false;
  SmallVector<MachineInstr *, 8> Roots;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.isBundle() && !MI.isBundledWithPred())
        Roots.push_back(&MI);

  auto leftover = [&](const MachineInstr &MI) {
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isPosition() ||
        MI.isCFIInstruction() || MI.isKill() || MI.isImplicitDef())
      return false;
    if (haydn::bundle::isPadNopOpcode(MI.getOpcode()))
      return false;
    const StringRef Name = TII.getName(MI.getOpcode());
    if (Name.ends_with("_MSP"))
      return false;
    return !isGeneratedFormatEMemberName(Name);
  };

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;
    SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(*Root);
    unsigned LeftoverN = 0;
    for (MachineInstr *K : Kids)
      if (K && leftover(*K))
        ++LeftoverN;
    if (!LeftoverN)
      continue;
    // Leftover logical kids (hand MIR / limited pipelines that never
    // passed the scheduler/stalls bake seats) take their generated member
    // Desc here: the structural inverse and the serialize-only printer
    // both require generated members, and reloc CSR I8 needs the typed
    // member for FIXUP_HAYDN_CSR_UImm8 (untyped NONE is refused below).
    // One solve authority (exactSolve), identity-gated by the keep-map —
    // no DFS, no sequentialize, no name peel.
    bool Baked = false;
    {
      // Multi-member logical roots take the one exact product member list
      // (same authority as the scheduler commit): the structural inverse
      // requires generated members at membership entry and encode scatters
      // by committed EntryIdx. Pad-NOP siblings are completion fill, not
      // solve inputs. No DFS / sequentialize when the solve refuses — the
      // mixed refusal below is the wall.
      SmallVector<MachineInstr *, 3> Reals;
      bool AnyMemberKid = false;
      for (MachineInstr *K : Kids) {
        if (!K)
          continue;
        if (leftover(*K))
          Reals.push_back(K);
        else if (isGeneratedFormatEMemberName(TII.getName(K->getOpcode())))
          AnyMemberKid = true;
      }
      // Mixed MemberId + leftover FieldSlot is the fail-closed wall: a
      // partial keep-map of a subset of children is exactly the silent
      // repair W68.4 deletes (mixed_memberid_leftover_st8).
      if (AnyMemberKid && !Reals.empty()) {
        report_fatal_error(
            "Haydn FinalizeBundle: mixed MemberId and leftover FieldSlot "
            "after cutover — refuse untyped encode",
            /*GenCrashDiag=*/false);
      }
      if (Reals.size() >= 2) {
        SmallVector<unsigned, 3> Ops;
        Ops.reserve(Reals.size());
        for (MachineInstr *K : Reals)
          Ops.push_back(
              haydn::bundle::productSolveLogicalOpcode(K->getOpcode(), Fmts));
        if (auto Exact = haydn::bundle::exactSolveProductOpcodes(Ops, Fmts)) {
          if (Exact->MemberOpcodes.size() == Reals.size()) {
            for (unsigned I = 0, E = Reals.size(); I != E; ++I)
              bakeFormatEMemberDesc(*Reals[I], Exact->MemberOpcodes[I], TII);
            Baked = true;
          }
        }
      } else if (Reals.size() == 1) {
        const unsigned Logical =
            haydn::format_e::logicalOpcodeOrSelf(Reals.front()->getOpcode());
        unsigned Member = 0;
        if (auto Exact = haydn::bundle::exactSolveLateSingleton(Logical, Fmts))
          Member = Exact->MemberOpcodes.front();
        // Row-pinned single: a hand root stamped E3 keeps an E3 member so
        // the deliberate stamp is preserved (member/row stay coherent).
        const auto Stamp = haydn::bundle::getBundleRowID(*Root);
        if (Stamp &&
            *Stamp == haydn::bundle::BundleFormatRowID::E96ThreeEntry &&
            !haydn::bundle::formatECompositeSlotIsE3(Fmts.getSlotKind(Member))) {
          const std::string LogicalName = TII.getName(Logical).upper();
          const StringRef Stem =
              StringRef(LogicalName).take_front(LogicalName.find('_'));
          for (unsigned O = 0, E = TII.getNumOpcodes(); O != E; ++O) {
            const StringRef N = TII.getName(O);
            if (N.starts_with(Stem) && N.contains("_E3_")) {
              Member = O;
              break;
            }
          }
        }
        if (Member) {
          bakeFormatEMemberDesc(*Reals.front(), Member, TII);
          Baked = true;
        }
      }
    }

    SmallVector<unsigned, 3> Members =
        haydn::bundle::collectBundleMemberOpcodes(*Root);
    const bool HasPad = haydn::bundle::bundleHasPadNop(*Root);
    // Existing hand/limited-pipeline row stamps are identity for roots
    // we did NOT bake; a root whose kids just took member Descs (the
    // reloc-CSR cutover) re-derives its row from the members — the dual
    // ADD32+CSRW fixture rebinds e0/e1 onto E3 and restamps.
    const auto ExistingRow = haydn::bundle::getBundleRowID(*Root);
    const auto Row =
        (ExistingRow && !Baked)
            ? *ExistingRow
            : haydn::bundle::selectProductRowForOpcodes(Members);
    haydn::bundle::stampBundleCommit(
        *Root, Row,
        haydn::bundle::selectCompletionForMembersAndPads(Row, Members.size(),
                                                         HasPad));
    Changed = true;
  }
  return Changed;
}

void refuseResidualRelocCsrFieldSlot(MachineFunction &MF,
                                     const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      if (MI.isBundle() || MI.isMetaInstruction() || MI.isDebugInstr() ||
          MI.isPosition())
        continue;
      const StringRef Name = TII.getName(MI.getOpcode());
      if (isGeneratedFormatEMemberName(Name))
        continue;
      if (!hasRelocatableOperand(MI))
        continue;
      if (!Name.equals_insensitive("CSRW") &&
          !Name.equals_insensitive("CSRW_W") &&
          !Name.equals_insensitive("CSRR") &&
          !Name.equals_insensitive("CSRR_W"))
        continue;
      report_fatal_error(
          "Haydn FinalizeBundle: reloc CSR I8 remained FieldSlot after "
          "MemberId cutover — refuse untyped NONE fixup",
          /*GenCrashDiag=*/false);
    }
  }
}

void refuseMixedMemberIdAndFieldSlot(MachineFunction &MF,
                                     const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &Root : MBB) {
      if (!Root.isBundle())
        continue;
      bool AnyMember = false;
      bool AnyLeftover = false;
      MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
      MachineBasicBlock::instr_iterator E = getBundleEnd(Root.getIterator());
      for (; I != E; ++I) {
        if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
          continue;
        if (haydn::bundle::isPadNopOpcode(I->getOpcode()))
          continue;
        const StringRef Name = TII.getName(I->getOpcode());
        if (isGeneratedFormatEMemberName(Name))
          AnyMember = true;
        else
          AnyLeftover = true;
      }
      if (AnyMember && AnyLeftover)
        report_fatal_error(
            "Haydn FinalizeBundle: mixed MemberId and leftover FieldSlot "
            "after cutover — refuse untyped encode",
            /*GenCrashDiag=*/false);
    }
  }
}

} // namespace

bool llvm::memberDescCompatible(const MachineInstr &MI, unsigned MemberOpc,
                                const TargetInstrInfo &TII) {
  const MCInstrDesc &NewDesc = TII.get(MemberOpc);
  return fieldSlotKeepOperands(MI, NewDesc).has_value();
}

void llvm::rewriteFieldSlotToMember(MachineInstr &MI, unsigned MemberOpc,
                                    const TargetInstrInfo &TII) {
  const MCInstrDesc &OldDesc = MI.getDesc();
  const MCInstrDesc &NewDesc = TII.get(MemberOpc);
  auto Keep = fieldSlotKeepOperands(MI, NewDesc);
  assert(Keep && "rewriteFieldSlotToMember requires memberDescCompatible");
  MachineFunction *MF = MI.getMF();
  assert(MF && "rewriteFieldSlotToMember requires a parent function");

  const unsigned OldN = OldDesc.getNumOperands();
  const unsigned NewN = NewDesc.getNumOperands();
  assert(Keep->size() == NewN && "keep map must cover every member operand");

  SmallVector<MachineOperand, 4> Kept;
  Kept.reserve(NewN);
  for (unsigned NewI = 0; NewI != NewN; ++NewI)
    Kept.push_back(MI.getOperand((*Keep)[NewI]));

  SmallVector<MachineOperand, 4> ImplicitTail;
  for (unsigned I = MI.getNumOperands(); I > OldN; --I)
    ImplicitTail.push_back(MI.getOperand(I - 1));

  while (MI.getNumOperands())
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.setDesc(NewDesc);
  for (unsigned NewI = 0; NewI != NewN; ++NewI)
    MI.addOperand(*MF, Kept[NewI]);
  for (unsigned I = ImplicitTail.size(); I > 0; --I)
    MI.addOperand(*MF, ImplicitTail[I - 1]);
}

bool HaydnFinalizeBundle::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  // Mixed-stream inline-asm admission (fail closed): opaque inline asm
  // beside compiler packets is outside the exact Format E layout model.
  // Admitted: metadata-only asm (empty text) and asm-only naked bodies.
  {
    SmallVector<const MachineInstr *, 4> CodeBearingAsm;
    bool AnyBundleCandidate = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineBasicBlock::instr_iterator II = MBB.instr_begin(),
                                             IE = MBB.instr_end();
           II != IE; ++II) {
        MachineInstr &MI = *II;
        if (MI.isInlineAsm()) {
          const unsigned Len = TII.getInlineAsmLength(
              MI.getOperand(0).getSymbolName(),
              *MF.getTarget().getMCAsmInfo());
          if (Len != 0)
            CodeBearingAsm.push_back(&MI);
          continue;
        }
        if (isBundleCandidate(II))
          AnyBundleCandidate = true;
      }
    }
    if (AnyBundleCandidate) {
      for (const MachineInstr *MI : CodeBearingAsm) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "Haydn Finalize: code-bearing inline asm is outside the exact "
              "Format E layout model (typed admission or reject; metadata-"
              "only asm and asm-only naked bodies are legal). MI:\n";
        MI->print(OS);
        report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
      }
    }
  }

  bool Changed = false;
  if (expandLeftoverRetJtCall(MF))
    Changed = true;
  // Expand B to BEQZ_W_MSP (isBarrier uncond clone). Catalog BEQZ_W is
  // isConditionalBranch and made BranchRelaxation dereference the last-block
  // sentinel. The clone is isUnconditionalBranch, so expansion at every seat
  // (size-oracle included) is BR-safe; mid/closure still catch insertBranch
  // leftover B.
  if (expandUncondBToBeqz(MF, TII))
    Changed = true;
  if (wrapBareAndStamp(MF))
    Changed = true;
  if (bakeLeftoverLogicalBundles(MF))
    Changed = true;
  if (stampUnstampedBundledRoots(MF))
    Changed = true;

  refuseResidualRelocCsrFieldSlot(MF, TII);
  refuseMixedMemberIdAndFieldSlot(MF, TII);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (propagateEarliestMemberDebugLoc(MI))
        Changed = true;
    }
  }

  return Changed;
}

char HaydnFinalizeBundle::ID = 0;

INITIALIZE_PASS(HaydnFinalizeBundle, DEBUG_TYPE, "Haydn Bundle Finalization",
                false, false)

HaydnFinalizeBundle::HaydnFinalizeBundle() : MachineFunctionPass(ID) {
  initializeHaydnFinalizeBundlePass(*PassRegistry::getPassRegistry());
}

void HaydnFinalizeBundle::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createHaydnFinalizeBundlePass() {
  return new HaydnFinalizeBundle();
}
