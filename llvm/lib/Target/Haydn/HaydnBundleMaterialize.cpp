//===- HaydnBundleMaterialize.cpp - exact product-cycle commit ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Out-of-line emission probe + exact bake (AIE applyBundles size()>1 peer).
// Declarations live in HaydnBundleMaterialize.h.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnPackLegality.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include <algorithm>

namespace llvm {

HaydnCommitTxn snapshotForCommitTxn(const MachineInstr &MI) {
  HaydnCommitTxn T;
  T.Desc = &MI.getDesc();
  T.Operands.reserve(MI.getNumOperands());
  T.Flags = MI.getFlags();
  for (const MachineOperand &MO : MI.operands())
    T.Operands.push_back(MO);
  return T;
}

void restoreFromCommitTxn(MachineInstr &MI, const HaydnCommitTxn &T,
                          MachineFunction &MF) {
  assert(T.Desc && "restoreFromCommitTxn on an empty transaction");
  MI.setDesc(*T.Desc);
  for (unsigned I = MI.getNumOperands(); I > 0; --I)
    MI.removeOperand(I - 1);
  for (const MachineOperand &MO : T.Operands)
    MI.addOperand(MF, MO);
  MI.setFlags(T.Flags);
}

namespace haydn {
namespace bundle {

/// E2 and E3 Format E members cannot share a parcel. Stamping E3 for a
/// mixed group is what produced "E2 member under E96ThreeEntry" on
/// bundlesim_reg_intrin_x2x4_sfr_cmp (SFR-class x2 store + E2 member).
static bool cycleHasMixedFormatEModes(ArrayRef<MachineInstr *> Instrs) {
  bool AnyE2 = false;
  bool AnyE3 = false;
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  for (MachineInstr *MI : Instrs) {
    const unsigned Opc = MI->getOpcode();
    if (const format_e::FormatEMemberRec *Priv =
            lookupPrivateFormatEMember(Opc)) {
      if (Priv->Mode == 0)
        AnyE2 = true;
      else
        AnyE3 = true;
      continue;
    }
    const MCSlotKind Kind = Fmts.getSlotKind(Opc);
    if (formatECompositeSlotIsE3(Kind))
      AnyE3 = true;
    else if (formatECompositeSlotIsE2(Kind))
      AnyE2 = true;
  }
  return AnyE2 && AnyE3;
}

static bool cycleViolatesNamedSameCycleLaws(ArrayRef<MachineInstr *> Instrs) {
  SmallVector<const MachineInstr *, 4> Occupied;
  Occupied.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    if (pack::cycleViolatesNamedSameCycleLaws(*MI, Occupied))
      return true;
    Occupied.push_back(MI);
  }
  return false;
}

static bool allGeneratedProductMembers(ArrayRef<MachineInstr *> Instrs) {
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      return false;
    const unsigned Opc = MI->getOpcode();
    if (format_e::logicalOpcodeOrSelf(Opc) == Opc &&
        lateProductMemberOpcode(Opc) != Opc)
      return false;
  }
  return true;
}

/// AIE applyBundles (AIEHazardRecognizer.cpp:326-352) packs already-setDesc
/// members by getSlotKind. Probe only — no MIR mutation.
static bool asIsGeneratedMembersFormLegalCycle(
    ArrayRef<MachineInstr *> Instrs, const TargetRegisterInfo *TRI) {
  if (!allGeneratedProductMembers(Instrs))
    return false;
  if (cycleMembersHaveTrueRAW(Instrs, TRI) || cycleMembersHaveWAW(Instrs, TRI))
    return false;
  if (cycleMembersExceedPortBudget(Instrs))
    return false;
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI))
      return false;
    Bundle.add(MI);
  }
  if (Bundle.size() <= 1 || Bundle.isStandalone())
    return false;
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  if (!Fmt)
    return false;
  SmallVector<MachineInstr *, 3> FieldOrdered =
      getFieldOrderedMembers(Bundle, *Fmt);
  return !cycleMembersHaveTrueRAW(FieldOrdered, TRI) &&
         !cycleMembersHaveWAW(FieldOrdered, TRI);
}

bool canCoissueProductCycle(ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2 || Instrs.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;
  for (MachineInstr *MI : Instrs) {
    if (!MI || !MI->getParent() || !MI->getMF() || MI->isInlineAsm())
      return false;
  }

  MachineFunction &MF = *Instrs.front()->getMF();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // One legality with HR getHazardType / ResourceCycle: SIN_COS/ARCTAN
  // alone, CSRW 0x20-0x25 vs SET_HWLOOP, LUI/ADDI32_W e0-alone. Sequentialize
  // after this reject is recovery, not a second pack authority.
  if (cycleViolatesNamedSameCycleLaws(Instrs))
    return false;
  if (cycleHasMixedFormatEModes(Instrs))
    return false;

  // SET_HWLOOP must not share a cycle with a producer of its trip/Off regs
  // (snapshot no-forwarding: WAR samples stale trip; RAW needs forwarding).
  if (cycleMembersHaveHwloopTripConflict(Instrs, TII, TRI))
    return false;

  // Format E E2-only logicals cannot form a 3-wide E3 parcel.
  if (Instrs.size() >= 3) {
    for (MachineInstr *MI : Instrs) {
      if (isFormatEE2OnlyOpcodeName(TII.getName(MI->getOpcode())))
        return false;
    }
  }

  // Shared RF-port law (GPR 4R/2W, DR 8R/3W, AR 2R/2W, SFR 2R/1W) before
  // the bake probe. Same predicate as commitExact / verify / HR.
  if (cycleMembersExceedPortBudget(Instrs))
    return false;

  // Already-baked members: keep HR's slot assignment when it is one
  // legal cycle (AIE applyBundles). Rematch can flip a legal WAR.
  if (asIsGeneratedMembersFormLegalCycle(Instrs, TRI))
    return true;

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  if (!instrsFormOneLegalCycle(Instrs, Fmts))
    return false;

  SmallVector<unsigned, 3> SavedOps;
  SavedOps.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs)
    SavedOps.push_back(MI->getOpcode());

  // Residual FieldSlots can accept dual single-unit logicals; MC unit
  // injectivity cannot. Fail closed before temporary setDesc.
  if (!opcodesHaveFormatEUnitCover(SavedOps, TII))
    return false;

  auto restoreDescs = [&]() {
    for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
      if (Instrs[I]->getOpcode() != SavedOps[I])
        Instrs[I]->setDesc(TII.get(SavedOps[I]));
    }
  };

  // Bake members the same way commitExactMultiMIProductCycle does.
  // Mixed first: already-baked members are peeled and rematched so a
  // preferred S2→S1→S0 assignment cannot flip a legal schedule-order WAR
  // into field-order RAW (AIE applyBundles getSlotKind after setDesc;
  // Haydn overlay is field-order no-forwarding).
  {
    SmallVector<unsigned, 3> Ops = SavedOps;
    if (auto Mixed = resolveMixedMemberCycleOnce(Instrs, TII, TRI, Fmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
        const unsigned Member = (*Mixed)[I];
        if (Member != Instrs[I]->getOpcode())
          Instrs[I]->setDesc(TII.get(Member));
      }
    } else if (auto Exact = exactSolveProductOpcodes(Ops, Fmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
        const unsigned Member = Exact->MemberOpcodes[I];
        if (Member != Instrs[I]->getOpcode())
          Instrs[I]->setDesc(TII.get(Member));
      }
    } else if (!opcodesFormOneLegalCycle(Ops, Fmts)) {
      restoreDescs();
      return false;
    }
  }

  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI)) {
      restoreDescs();
      return false;
    }
    Bundle.add(MI);
  }
  if (Bundle.size() <= 1 || Bundle.isStandalone()) {
    restoreDescs();
    return false;
  }
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  if (!Fmt) {
    restoreDescs();
    return false;
  }

  SmallVector<MachineInstr *, 3> FieldOrdered =
      getFieldOrderedMembers(Bundle, *Fmt);
  // Field order = emission order. True RAW here means an Anti/WAR that the
  // preferred placement cannot preserve (use-before-redef flipped).
  const bool FieldRAW = cycleMembersHaveTrueRAW(FieldOrdered, TRI);
  // Same-cycle live WAW (incl. multi-stage physreg redefs) cannot multi-MI.
  const bool FieldWAW = cycleMembersHaveWAW(FieldOrdered, TRI) ||
                        cycleMembersHaveWAW(Instrs, TRI);
  restoreDescs();
  return !FieldRAW && !FieldWAW;
}

bool commitExactMultiMIProductCycle(ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return false;

  // Opaque INLINEASM must stay standalone — never a multi-MI BUNDLE child.
  for (MachineInstr *MI : Instrs) {
    if (MI->isInlineAsm())
      return false;
  }
  if (cycleHasMixedFormatEModes(Instrs))
    return false;

  MachineBasicBlock *MBB = Instrs.front()->getParent();
  if (!MBB || !MBB->getParent())
    return false;
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // Fail closed: E2-only Format E logicals cannot form a 3-wide E3 parcel.
  if (Instrs.size() >= 3) {
    for (MachineInstr *MI : Instrs) {
      if (isFormatEE2OnlyOpcodeName(TII.getName(MI->getOpcode())))
        return false;
    }
  }

  // Fail closed on same-cycle true RAW before setDesc / finalizeBundle can
  // invent InternalRead markers for an illegal no-forwarding pack.
  if (cycleMembersHaveTrueRAW(Instrs, TRI))
    return false;
  // Fail closed on same-cycle WAW (leaveMBB seam replay cannot clear it).
  // Shared no-dual-write law: DEAD defs count (W39) — a dead-def dual write
  // is exactly as illegal as a live one (HR/RC reject it; commit must too).
  if (cycleMembersHaveWAW(Instrs, TRI))
    return false;
  if (cycleMembersExceedPortBudget(Instrs))
    return false;
  if (cycleMembersViolateSinCosWindow(Instrs, TII))
    return false;
  if (cycleViolatesNamedSameCycleLaws(Instrs))
    return false;
  // SET_HWLOOP trip/Off sample cannot coissue with a producer of those regs
  // (WAR would sample stale trip under snapshot no-forwarding).
  if (cycleMembersHaveHwloopTripConflict(Instrs, TII, TRI))
    return false;

  // AIE applyBundles: already-setDesc members pack by slot, no rematch.
  if (asIsGeneratedMembersFormLegalCycle(Instrs, TRI)) {
    const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
    Haydn::MachineBundle Bundle(&Fmts);
    for (MachineInstr *MI : Instrs) {
      if (!Bundle.canAdd(MI))
        return false;
      Bundle.add(MI);
    }
    const VLIWFormat *Fmt = Bundle.getFormatOrNull();
    if (!Fmt)
      return false;
    MachineBasicBlock::iterator BundleEnd =
        getBundleEnd(Instrs.back()->getIterator());
    applyFormatOrdering(Bundle, *Fmt, BundleEnd);
    MachineInstr &Root =
        *getBundleStart(Bundle.getInstrs().front()->getIterator());
    if (!Root.isBundle())
      report_fatal_error(
          "commitExactMultiMIProductCycle: as-is finalize produced no root",
          /*GenCrashDiag=*/false);
    SmallVector<unsigned, 3> MemberOps;
    MemberOps.reserve(Instrs.size());
    for (MachineInstr *MI : Instrs)
      MemberOps.push_back(MI->getOpcode());
    BundlePlan Plan =
        makeProductPlanForOpcodes(Bundle.getOccupiedSlots(), MemberOps, TII);
    stampBundleCommit(Root, Plan);
    return true;
  }

  // W23 / CR-B1 transactional bake. Open the transaction before the first
  // descriptor mutation; every failure return from here through the field
  // order re-validation restores the pre-attempt identity of every member.
  // A late fail must never leave baked member opcodes in MIR with no BUNDLE
  // root — that is the hard-constraint #8 violation surface (private Format E
  // member identity surviving outside a committed bundle; sequentialize lanes
  // and singleton fallbacks would then re-stamp private descs as bare reals).
  // The transaction covers descriptor setDesc, the keep-map operand rewrite,
  // tie drops, and InternalRead clearing — everything mutated before the
  // irreversible applyFormatOrdering (which reorders MBB topology and creates
  // the BUNDLE root; past that point a false return is impossible by
  // construction and stays an invariant break, not a bail-out).
  SmallVector<HaydnCommitTxn, 3> Txn;
  Txn.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs)
    Txn.push_back(snapshotForCommitTxn(*MI));
  auto restoreTxn = [&]() {
    for (unsigned I = 0, E = Instrs.size(); I != E; ++I)
      restoreFromCommitTxn(*Instrs[I], Txn[I], MF);
  };

  // Bake format-member descriptors before SlotMap / encode. leaveRegion
  // materializeMultiOpcodeInstrs is the primary AltDescs path; this is the
  // fail-closed second line so multi-MI commit never emits residual logicals.
  {
    SmallVector<unsigned, 3> Ops;
    Ops.reserve(Instrs.size());
    for (MachineInstr *MI : Instrs)
      Ops.push_back(MI->getOpcode());

    // Format E unit injectivity (MC serialize authority). Residual S* packs
    // that over-count single-unit logicals must not freeze a BUNDLE.
    if (!opcodesHaveFormatEUnitCover(Ops, TII)) {
      restoreTxn();
      return false;
    }

    const HaydnMCFormats &SolveFmts = haydnDefaultMCFormats();
    if (auto Mixed = resolveMixedMemberCycleOnce(Instrs, TII, TRI, SolveFmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I)
        bakeFormatEMemberDesc(*Instrs[I], (*Mixed)[I], TII);
    } else if (auto Exact = exactSolveProductOpcodes(Ops, SolveFmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I)
        bakeFormatEMemberDesc(*Instrs[I], Exact->MemberOpcodes[I], TII);
    } else if (!opcodesFormOneLegalCycle(Ops, SolveFmts)) {
      restoreTxn();
      return false;
    }
  }

  // finalizeBundle only sets IsInternalRead; it never clears. Members that
  // were previously bundled (hard-root recommit, re-order) may carry stale
  // markers that would survive a field-order change. Drop them so the
  // subsequent finalize rebuild is authoritative.
  for (MachineInstr *MI : Instrs) {
    for (MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.isInternalRead())
        MO.setIsInternalRead(false);
    }
  }

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI)) {
      restoreTxn();
      return false;
    }
    Bundle.add(MI);
  }

  if (Bundle.size() <= 1 || Bundle.isStandalone()) {
    restoreTxn();
    return false;
  }
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  if (!Fmt) {
    restoreTxn();
    return false;
  }

  // The schedule-order RAW check above is necessary but NOT sufficient.
  // applyFormatOrdering permutes members into Format.getSlots() field order —
  // the order the encoder / AsmPrinter / hardware observe. A legal same-cycle
  // WAR in schedule order (reader before writer) can be flipped by the field
  // order into writer-before-reader; finalizeBundle would then mark the
  // reader's operand IsInternalRead, modeling a same-cycle read of a
  // same-cycle def — the no-forwarding hazard (the reader observes the new,
  // not the pre-cycle, value). Re-validate the no-forwarding RAW law on the
  // EXACT field-ordered sequence that will be emitted. If it has a true RAW,
  // this cycle cannot be one product parcel: fail closed to sequential parcels
  // (schedule order is reader-before-writer, a legal WAR when split across
  // cycles — the reader observes the pre-cycle value, the writer takes effect
  // next cycle).
  SmallVector<MachineInstr *, 3> FieldOrdered =
      getFieldOrderedMembers(Bundle, *Fmt);
  if (cycleMembersHaveTrueRAW(FieldOrdered, TRI)) {
    // Post-bake late fail (W23): undo the member bake so sequentializing
    // callers see the original logical opcodes, not orphaned member descs.
    restoreTxn();
    return false;
  }

  // Iterator AFTER the last schedule-order member — re-insert point
  // (AIEHazardRecognizer.cpp:338-339 getBundleEnd of last instr).
  MachineBasicBlock::iterator BundleEnd =
      getBundleEnd(Instrs.back()->getIterator());
  applyFormatOrdering(Bundle, *Fmt, BundleEnd);

  MachineInstr &Root =
      *getBundleStart(Bundle.getInstrs().front()->getIterator());
  if (!Root.isBundle())
    // Past applyFormatOrdering the MBB topology is already committed (root
    // created, members field-ordered); a bail-out here could not restore the
    // pre-attempt MIR and would hand the caller a half-committed cycle.
    // finalizeBundle unconditionally prepends a BUNDLE root, so reaching this
    // arm is an invariant break, not a pack failure — fail closed loudly
    // (W23; same fatality policy as the P14/W38 fail-open deletions).
    report_fatal_error(
        "commitExactMultiMIProductCycle: finalizeBundle produced no root",
        /*GenCrashDiag=*/false);

  // Durable Format E row + completion from real post-setDesc members.
  // Row is selected from generated Format E unit cover so dual single-unit
  // logicals (two ADD32, ADD+XOR) freeze E3 when E2 cannot inject units —
  // never rely on MC residual DFS/row upgrade after commit. Full-slot
  // completion (AllEntriesReal) for non-empty cycles.
  SmallVector<unsigned, 3> MemberOps;
  MemberOps.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs)
    MemberOps.push_back(MI->getOpcode());
  BundlePlan Plan =
      makeProductPlanForOpcodes(Bundle.getOccupiedSlots(), MemberOps, TII);
  stampBundleCommit(Root, Plan);
  return true;
}

bool commitOneProductCycle(ArrayRef<MachineInstr *> Instrs) {
  if (!canCoissueProductCycle(Instrs))
    return false;
  return commitExactMultiMIProductCycle(Instrs);
}

bool commitExactHardRootProductCycle(MachineInstr &BundleRoot,
                                     const MCInstrInfo &MII) {
  (void)MII;
  if (!BundleRoot.isBundle() || !BundleRoot.getParent())
    return false;

  MachineBasicBlock &MBB = *BundleRoot.getParent();
  if (!MBB.getParent())
    return false;

  SmallVector<MachineInstr *, 3> Kids;
  for (MachineBasicBlock::instr_iterator I =
           std::next(BundleRoot.getIterator());
       I != MBB.instr_end() && I->isBundledWithPred(); ++I)
    Kids.push_back(&*I);

  if (Kids.size() < 2 || Kids.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;

  // INLINEASM is never a legal hard-root member.
  for (MachineInstr *K : Kids) {
    if (K->isInlineAsm())
      return false;
  }

  // Probe first so dissolve never runs on a group the one bake site cannot
  // finish. AIE applyBundles (AIEHazardRecognizer.cpp:326-352) erases the
  // old root then re-finalizes; Haydn overlay is the same funnel as free
  // multi-MI: commitOneProductCycle (probe + exact bake).
  if (!canCoissueProductCycle(Kids))
    return false;

  for (MachineInstr *K : Kids) {
    for (MachineOperand &MO : K->operands()) {
      if (MO.isReg() && MO.isInternalRead())
        MO.setIsInternalRead(false);
    }
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }
  BundleRoot.eraseFromParent();

  if (!commitOneProductCycle(Kids))
    report_fatal_error(
        "commitExactHardRootProductCycle: probe accepted but bake failed",
        /*GenCrashDiag=*/false);

  MachineInstr &NewRoot = *getBundleStart(Kids.front()->getIterator());
  if (!NewRoot.isBundle() || !getBundleRowID(NewRoot).has_value())
    report_fatal_error(
        "commitExactHardRootProductCycle: recommit produced no stamped root",
        /*GenCrashDiag=*/false);
  return true;
}

std::optional<SmallVector<unsigned, 3>>
resolveMixedMemberCycleOnce(ArrayRef<MachineInstr *> Instrs,
                            const TargetInstrInfo &TII,
                            const TargetRegisterInfo *TRI,
                            const HaydnMCFormats &Fmts) {
  if (Instrs.size() < 2 || Instrs.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::nullopt;

  SmallVector<unsigned, 3> SavedOps;
  SmallVector<unsigned, 3> Peeled;
  SavedOps.reserve(Instrs.size());
  Peeled.reserve(Instrs.size());
  bool AnyPeeled = false;
  for (MachineInstr *MI : Instrs) {
    const unsigned Opc = MI->getOpcode();
    SavedOps.push_back(Opc);
    const unsigned Log = productSolveLogicalOpcode(Opc, Fmts);
    if (Log != Opc)
      AnyPeeled = true;
    Peeled.push_back(Log);
  }
  if (!AnyPeeled)
    return std::nullopt;

  if (!opcodesHaveFormatEUnitCover(Peeled, TII))
    return std::nullopt;

  auto Exact = exactSolveProductOpcodes(Peeled, Fmts);
  if (!Exact || Exact->MemberOpcodes.size() != Instrs.size())
    return std::nullopt;

  auto restoreDescs = [&]() {
    for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
      if (Instrs[I]->getOpcode() != SavedOps[I])
        Instrs[I]->setDesc(TII.get(SavedOps[I]));
    }
  };

  auto validate = [&](ArrayRef<unsigned> Members,
                      bool &FieldLawOnly) -> bool {
    FieldLawOnly = false;
    for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
      if (Members[I] != Instrs[I]->getOpcode())
        Instrs[I]->setDesc(TII.get(Members[I]));
    }
    Haydn::MachineBundle Bundle(&Fmts);
    for (MachineInstr *MI : Instrs) {
      if (!Bundle.canAdd(MI)) {
        restoreDescs();
        return false;
      }
      Bundle.add(MI);
    }
    if (Bundle.size() <= 1 || Bundle.isStandalone()) {
      restoreDescs();
      return false;
    }
    const VLIWFormat *Fmt = Bundle.getFormatOrNull();
    if (!Fmt) {
      restoreDescs();
      return false;
    }
    SmallVector<MachineInstr *, 3> FieldOrdered =
        getFieldOrderedMembers(Bundle, *Fmt);
    const bool FieldRAW = cycleMembersHaveTrueRAW(FieldOrdered, TRI);
    const bool FieldWAW = cycleMembersHaveWAW(FieldOrdered, TRI);
    restoreDescs();
    if (FieldRAW || FieldWAW) {
      FieldLawOnly = true;
      return false;
    }
    return true;
  };

  if (cycleMembersHaveWAW(Instrs, TRI))
    return std::nullopt;

  bool FieldLawOnly = false;
  if (validate(Exact->MemberOpcodes, FieldLawOnly))
    return SmallVector<unsigned, 3>(Exact->MemberOpcodes.begin(),
                                    Exact->MemberOpcodes.end());

  if (FieldLawOnly) {
    const uint8_t Mode =
        Exact->Plan.Row == BundleFormatRowID::E96ThreeEntry ? 1 : 0;
    SmallVector<unsigned, 3> Rev(Peeled.rbegin(), Peeled.rend());
    SmallVector<unsigned, 3> Bound = assignMemberOpcodesForSettledRow(Rev, Mode);
    if (Bound.size() == Instrs.size()) {
      std::reverse(Bound.begin(), Bound.end());
      bool Dummy = false;
      if (validate(Bound, Dummy))
        return Bound;
    }
  }
  return std::nullopt;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

