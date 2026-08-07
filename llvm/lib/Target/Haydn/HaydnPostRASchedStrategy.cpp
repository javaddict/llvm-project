//===-- HaydnPostRASchedStrategy.cpp - Haydn post-RA bundle-forming sched -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the post-RA scheduler strategy that forms VLIW bundles in
// leaveRegion/leaveMBB (Stream B Phase B2, ). Port of AIE's
// computeAndFinalizeBundles (AIEMachineScheduler.cpp:158-234) + commitBlock
// Schedule (AIEMachineScheduler.cpp:806-863) + applyBundles
// (AIEHazardRecognizer.cpp:317-351), stripped to the Top-zone, per-region
// core (no inter-block fixpoint, no Bot zone). Stage-0 PostPipeliner is gated
// by -haydn-enable-post-pipeliner (default OFF): multi-stage materialize on
// single-BB ZOL; leaveRegion skips HR-auction bundling for those regions.
// Stage-0 inter-block (-haydn-enable-interblock, default OFF) runs after
// bundle materialize in leaveMBB — see HaydnInterBlockScheduling.h.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRASchedStrategy.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"

#include "HaydnMachineFunctionInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-post-ra-sched"

STATISTIC(NumIdleCyclesMaterialized,
          "Number of post-RA idle (stall) cycles materialized as NOP");
STATISTIC(NumMultiMIBundlesFinalized,
          "Number of multi-MI cycles finalized as BUNDLE");
STATISTIC(NumScheduledCyclesSplit,
          "Number of scheduled cycles explicitly split into >1 encode cycles");

// Safety net only: post-setDesc members form a legal Bundle128 via
// Bundle.canAdd + fixed getSlotKind (AIEBundle.h:62-105 / :92-104;
// AIEBaseMCFormats.cpp:66-75). AIE has no AltDescs slot side-map
// (AIEAlternateDescriptors.h:27-75 opcode-alt only).
// Shape-mismatch residual logicals still pack via Bundle alts tryAdd.
static bool cycleCanFormLegalBundle(ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.empty() || Instrs.size() > 3)
    return false;
  HaydnMCFormats Fmts;
  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI))
      return false;
    Bundle.add(MI);
  }
  // Multi-MI: require a covering packet format (AIE getFormatOrNull).
  if (Bundle.isStandalone())
    return false;
  return Bundle.getFormatOrNull() != nullptr;
}

HaydnPostRASchedStrategy::HaydnPostRASchedStrategy(const MachineSchedContext *C)
    : PostGenericScheduler(C) {
  // Cache the Haydn TII from the MachineFunction's subtarget. enterMBB runs
  // BEFORE the base scheduler calls initialize(DAG), so the DAG member is
  // still null when enterMBB first fires — get TII from the context instead.
  // (ScheduleDAGMI::startBlock -> SchedImpl->enterMBB happens before any
  // region's initialize; see MachineScheduler.cpp:824 vs :858/.)
  HII = static_cast<const HaydnInstrInfo *>(C->MF->getSubtarget().getInstrInfo());
}

bool HaydnPostRASchedStrategy::tryCandidate(SchedCandidate &Cand,
                                            SchedCandidate &TryCand) {
  // ISA-34 Gap D + dual-load+MAC pack:
  // Prefer a load over a pure ALU/MAC when choosing the cycle's FIRST op so
  // multi-cycle load latency overlaps later compute. Once the current best
  // candidate is already a load, do NOT force-reject a non-load Try — fall
  // through to the base critical-path/resource heuristics so a ready MAC can
  // co-issue with the load (S2) instead of always hoisting another load that
  // only dual-issues at best and often serializes (bkfir CB-load streams).
  if (Cand.isValid()) {
    bool CandIsLoad = Cand.SU->getInstr()->mayLoad();
    bool TryIsLoad = TryCand.SU->getInstr()->mayLoad();
    if (TryIsLoad && !CandIsLoad) {
      TryCand.Reason = ResourceDemand;
      return true;
    }
    // CandIsLoad && !TryIsLoad: fall through — do not return false.
  }
  return PostGenericScheduler::tryCandidate(Cand, TryCand);
}

// True if MI should be skipped during bundle grouping (not a bundle member):
// pseudos, meta instructions, debug, bundles themselves. Mirrors the retired
// packetizer's ignorePseudoInstruction skip list.
static bool isBundleSkippable(const MachineInstr &MI) {
  return MI.isPseudo() || MI.isImplicitDef() || MI.isKill() ||
         MI.isDebugInstr() || MI.isCopy() || MI.isInlineAsm() ||
         MI.isPosition() || MI.isBundle();
}

void HaydnPostRASchedStrategy::bumpCycleForBundles(
    unsigned ToCycle, SmallVectorImpl<CycleBundle> &Bundles,
    CycleBundle &CurrBundle) {
  // Push the in-progress bundle as the current cycle, then pad with empty
  // bundles until reaching ToCycle. Mirrors AIE's bumpCycleForBundles
  // (AIEMachineScheduler.cpp:133-154). Invariant: Bundles.size == current
  // cycle index.
  unsigned CurrCycle = Bundles.size();
  assert(ToCycle > CurrCycle && "bumpCycleForBundles must move forward");
  Bundles.push_back(CurrBundle);
  ++CurrCycle;
  CurrBundle.Instrs.clear();
  while (ToCycle != CurrCycle) {
    Bundles.push_back({});
    ++CurrCycle;
  }
}

void HaydnPostRASchedStrategy::enterMBB(MachineBasicBlock *MBB) {
  CurrentMBB = MBB;
  // Dual-load packing is HR tryAddProduct PlacementAlternatives → setDesc
  // members (AIEHazardRecognizer.cpp:389; AIEMachineScheduler.cpp:1121-1132).
  // No promoteLoadsToSlot1 / AlternateSlots residual (AIE
  // AIEAlternateDescriptors.h:27-75 opcode-alt only).
  PostGenericScheduler::enterMBB(MBB);
}

void HaydnPostRASchedStrategy::leaveMBB() {
  // Materialize the bundles accumulated across all regions of this MBB into
  // actual BUNDLE MIs (+ standalone NOPs for idle cycles), in MBB order.
  // Mirrors AIE's commitBlockSchedule (AIEMachineScheduler.cpp:825-863), but
  // without the inter-block fixpoint gate (we always commit).
  //
  // t−3: PostRA DAG mutation ZOLSetupExitLatency raises ExitSU latency from
  // SET_HWLOOP (AIE LoopSetupDistance peer). Fixup still deficit-pads if
  // residual layout is short after relax.
  if (CurrentMBB && !MBBBundles.empty()) {
    materializeBundles(*CurrentMBB, MBBBundles);
    MBBBundles.clear();
  }
  // Stage-0 InterBlock densify deleted (YOLO). Pack ownership = leaveRegion only.
  PostGenericScheduler::leaveMBB();
}

SmallVector<HaydnPostRASchedStrategy::CycleBundle>
HaydnPostRASchedStrategy::computeRegionBundles() {
  // Walk the scheduled region's MIs and group by SU->TopReadyCycle (the cycle
  // the base list scheduler assigned). The MIs are in the MBB in scheduled
  // order (the base scheduler reorders in place via moveInstruction), so
  // same-cycle MIs are already adjacent — this is what makes bundleWithPred
  // safe in materializeBundles. Port of AIE computeAndFinalizeBundles
  // (AIEMachineScheduler.cpp:158-234), Top zone only.
  //
  // Only called for regions that were actually scheduled (RegionWasScheduled
  // gate in leaveRegion), so the MBB iterators are valid.
  SmallVector<CycleBundle> Bundles;
  CycleBundle CurrBundle;

  MachineBasicBlock::iterator Begin = DAG->begin();
  MachineBasicBlock::iterator End = DAG->end();
  if (Begin == End)
    return Bundles;

  for (MachineBasicBlock::iterator I = Begin; I != End; ++I) {
    MachineInstr &MI = *I;
    if (isBundleSkippable(MI))
      continue;

    SUnit *SU = DAG->getSUnit(&MI);
    if (!SU || !SU->isScheduled)
      continue;
    unsigned EmitCycle = SU->TopReadyCycle;

    if (EmitCycle != Bundles.size()) {
      assert(EmitCycle >= Bundles.size() && "emit cycle went backward");
      bumpCycleForBundles(EmitCycle, Bundles, CurrBundle);
    }
    CurrBundle.Instrs.push_back(&MI);
  }
  if (!CurrBundle.Instrs.empty())
    bumpCycleForBundles(Bundles.size() + 1, Bundles, CurrBundle);

  return Bundles;
}

// Splice skippable MIs out of [First,Last] so real members are contiguous.
// Returns false if a skippable has a reg conflict with a real member (unsafe).
static bool spliceSkippablesForCycle(MachineBasicBlock &MBB,
                                     ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return true;
  MachineInstr *First = Instrs.front();
  MachineInstr *Last = Instrs.back();
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  bool BundleUnsafe = false;
  for (MachineBasicBlock::instr_iterator It = First->getIterator(),
                                         E = Last->getIterator();
       It != E;) {
    MachineInstr &MI = *It;
    ++It;
    if (!isBundleSkippable(MI) || &MI == First)
      continue;
    bool HasRegConflict = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isDef() || !MO.getReg())
        continue;
      Register DefReg = MO.getReg();
      for (const MachineInstr *RealMI : Instrs) {
        if (RealMI == &MI)
          continue;
        if (RealMI->readsRegister(DefReg, TRI) ||
            RealMI->definesRegister(DefReg, TRI)) {
          HasRegConflict = true;
          break;
        }
      }
      if (HasRegConflict)
        break;
    }
    if (HasRegConflict) {
      BundleUnsafe = true;
      continue;
    }
    MBB.splice(First->getIterator(), &MBB, MI.getIterator());
  }
  return !BundleUnsafe;
}

static bool membersContiguous(ArrayRef<MachineInstr *> Instrs) {
  for (unsigned I = 1; I < Instrs.size(); ++I) {
    MachineBasicBlock::instr_iterator Prev = Instrs[I - 1]->getIterator();
    if (std::next(Prev) != Instrs[I]->getIterator())
      return false;
  }
  return true;
}

// Finalize one multi-MI group that is already contiguous and slot-legal.
// True AIE applyFormatOrdering path
// (AIEHazardRecognizer.cpp:278-314, call site applyBundles 343-344 when
// B.size()>1):
//   1. Build Haydn::MachineBundle SlotMap from post-setDesc getSlotKind
//      only (AIEBundle.h:92-104; AIEBaseMCFormats.cpp:66-75).
//   2. applyFormatOrdering(Bundle, *getFormatOrNull(), BundleEnd) so
//      children land in Format.getSlots() field order (BUNDLE128_FULL:
//      S2→S1→S0 per HaydnGenFormats.inc FormatSlotData).
//   3. stampBundleFormatID(ProductFormatID) on the BUNDLE root (plan §6.3)
//      — durable MIR/clone truth, not AltDesc.
static void finalizeLegalMultiMI(MachineBasicBlock &MBB,
                                 ArrayRef<MachineInstr *> Instrs) {
  assert(Instrs.size() >= 2 && "multi-MI finalize only");

  HaydnMCFormats Fmts;
  Haydn::MachineBundle Bundle(&Fmts);

  // SlotMap authority: fixed getSlotKind after setDesc (AIE shape). Bundle
  // pickSlot handles residual logicals via alts tryAdd.
  for (MachineInstr *MI : Instrs) {
    assert(Bundle.canAdd(MI) && "legal multi-MI must pack into Bundle");
    Bundle.add(MI);
  }

  // Iterator AFTER the last schedule-order member — re-insert point
  // (AIEHazardRecognizer.cpp:338-339 getBundleEnd of last instr).
  MachineBasicBlock::iterator BundleEnd =
      getBundleEnd(Instrs.back()->getIterator());

  // AIE only reorders when size()>1 (standalone may lack formats on AIE1).
  assert(Bundle.size() > 1 && "multi-MI finalize requires size()>1");
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  assert(Fmt && "legal multi-MI cycle must cover a packet format");
  applyFormatOrdering(Bundle, *Fmt, BundleEnd);

  // First field-order member is now the bundle interior lead; finalizeBundle
  // (inside applyFormatOrdering) inserted the BUNDLE root before it.
  MachineInstr &Root =
      *getBundleStart(Bundle.getInstrs().front()->getIterator());
  assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
  // Stamp the format the packer actually chose, not a constant. Format E has
  // two composites and this is the multi-MI path, so a 3-entry cycle must be
  // stamped BundleE3 — Bundle128 had one row and the constant was correct.
  // Fmt is the row Bundle::getFormatOrNull picked by slot coverage, which IS
  // the entry-count decision.
  haydn::bundle::stampBundleFormatID(
      Root, haydn::bundle::formatIDForSlotSet(Fmt->getSlotSet())
                .value_or(haydn::bundle::ProductFormatID));
  ++NumMultiMIBundlesFinalized;
}

// When a scheduled cycle cannot form one legal BUNDLE, greedily split
// into ordered legal sub-cycles (multi-MI BUNDLE or singleton standalone).
// Never silently leave a multi-MI illegal cycle as an unordered fog — each
// sub-cycle is an explicit architectural cycle (a product FormatID row).
static void materializeMaybeSplitCycle(MachineBasicBlock &MBB,
                                       ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return;

  if (!spliceSkippablesForCycle(MBB, Instrs)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: unsafe skippable splice — "
                         "explicit split to singletons\n");
    ++NumScheduledCyclesSplit;
    // Explicit N singleton cycles (already sequential in MBB).
    return;
  }
  if (!membersContiguous(Instrs)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: same-cycle MIs not contiguous — "
                         "explicit split to singletons\n");
    ++NumScheduledCyclesSplit;
    return;
  }

  if (cycleCanFormLegalBundle(Instrs)) {
    finalizeLegalMultiMI(MBB, Instrs);
    return;
  }

  // Greedy left-to-right legal sub-cycles (Bundle.canAdd / getSlotKind oracle).
  LLVM_DEBUG(dbgs() << "HaydnPostRASched: scheduled cycle not one legal "
                       "Bundle128 — explicit greedy split\n");
  ++NumScheduledCyclesSplit;

  SmallVector<MachineInstr *, 3> Cur;
  auto flush = [&]() {
    if (Cur.size() >= 2 && cycleCanFormLegalBundle(Cur) &&
        membersContiguous(Cur))
      finalizeLegalMultiMI(MBB, Cur);
    // size==1 or still-illegal pair: leave as sequential standalone parcels
    // (each is one Bundle128 encode cycle; product FormatID still Full).
    Cur.clear();
  };

  for (MachineInstr *MI : Instrs) {
    Cur.push_back(MI);
    if (Cur.size() == 1)
      continue;
    if (cycleCanFormLegalBundle(Cur))
      continue;
    // Last op does not fit: close prior group, restart at MI.
    MachineInstr *Overflow = Cur.pop_back_val();
    flush();
    Cur.push_back(Overflow);
  }
  flush();
}

void HaydnPostRASchedStrategy::materializeBundles(
    MachineBasicBlock &MBB, SmallVector<CycleBundle> &Bundles) {
  // Port of AIE materializeEmptyBundles + applyBundles, Top zone only.
  // Cycle ownership (behavior-preserving Bundle128 encode):
  // * empty cycle → NOP at rolling position (before next real cycle / term)
  // * single MI → leave standalone here; HaydnFinalizeBundle wraps + stamps
  //   FormatID (AIEFinalizeBundle.cpp:40-59 peer)
  // * 2-3 MIs legal → finalizeBundle + stamp FormatID
  // * 2-3 MIs illegal → explicit greedy split (not silent fog)
  //
  // Product plan: every encode cycle is BundleE2 or BundleE3 / 12 B
  // (haydn::bundle::BundlePlan). Multi-MI BUNDLE roots carry FormatID imm 0
  // (stampBundleFormatID). Singleton cycles become BUNDLE + FormatID in
  // HaydnFinalizeBundle after this scheduler (AIE2 addPreSched2 order).
  for (unsigned Idx = 0; Idx < Bundles.size(); ++Idx) {
    CycleBundle &CB = Bundles[Idx];
    if (CB.Instrs.empty()) {
      // Rolling idle NOP — before next real cycle's first MI, else term.
      MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
      for (unsigned J = Idx + 1; J < Bundles.size(); ++J) {
        if (!Bundles[J].Instrs.empty()) {
          InsertPt = Bundles[J].Instrs.front()->getIterator();
          break;
        }
      }
      HII->insertNoop(MBB, InsertPt);
      ++NumIdleCyclesMaterialized;
      continue;
    }
    if (CB.Instrs.size() == 1)
      continue; // One logical cycle; HaydnFinalizeBundle wraps.

    materializeMaybeSplitCycle(MBB, CB.Instrs);
  }
}

void HaydnPostRASchedStrategy::materializeMultiOpcodeInstrs() {
  // AIE port of AIEPostRASchedStrategy::materializeMultiOpcodeInstrs
  // (AIEMachineScheduler.cpp:1121-1139): when HR selected a format-member
  // opcode (commitPlacementForEmit → setAlternateDescriptor), bake it into
  // the MachineInstr via setDesc. Product still Bundle128 Full only.
  //
  // End-state (AIEMachineScheduler.cpp:1081-1082 +
  // AIEAlternateDescriptors.h:74): SelectedAltDescs.clear() after setDesc.
  // Post-commit placement is opcode identity via getSlotKind
  // (AIEBaseMCFormats.cpp:66-75). No slot side-map.
  HaydnAlternateDescriptors &AltDescs =
      DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();

  auto MaterializePseudo = [&](MachineInstr &MI) {
    // AIE parity (AIEMachineScheduler.cpp:1126-1132): unconditional
    // MI.setDesc when getSelectedOpcode is present. AIE alts share operand
    // structure by construction (AIEAlternateDescriptors.h:64-68); Haydn
    // members now match logical NumOperands/NumDefs (S_SW_BREV_*_S* / BREV
    // load *_LD_S* tied shapes). No shape-gate, no MCFlags write.
    if (std::optional<unsigned> AltOpcode = AltDescs.getSelectedOpcode(&MI))
      MI.setDesc(HII->get(*AltOpcode));
  };

  // AIE asserts top==bottom for PostRA; Haydn PostGenericScheduler is
  // top-down only — walk both ranges like AIE for shape parity.
  for (MachineInstr &MI : make_range(DAG->begin(), DAG->top()))
    MaterializePseudo(MI);
  for (MachineInstr &MI : make_range(DAG->bottom(), DAG->end()))
    MaterializePseudo(MI);

  // AIE leaveRegion: materialize then SelectedAltDescs.clear()
  // (AIEMachineScheduler.cpp:1081-1082). Full clear — no slot side-map survives.
  AltDescs.clear();
}

void HaydnPostRASchedStrategy::leaveRegion(const SUnit &ExitSU) {
  // Compute this region's bundle list from the scheduled Top zone and append
  // it to the per-MBB accumulator. The MBB is mutated later in leaveMBB.
  // Mirrors AIEPostRASchedStrategy::leaveRegion (AIEMachineScheduler.cpp:1037
  // 1082) stripped of the inter-block fixpoint gate, Bot zone, and
  // handleRegionConflicts. materializeMultiOpcodeInstrs is PORTED (
  // enabled by the pipeline reorder) — see above.
  //
  // CRITICAL: the base drive loop calls exitRegion (and thus this leaveRegion)
  // even for empty/single-MI regions that were SKIPPED (MachineScheduler.cpp:
  // 862-866) — schedule was never called, so the DAG's SUnits and region
  // iterators are stale from the previous region. The RegionWasScheduled flag
  // (set by schedNode, cleared at the start of each region) gates this: only
  // regions that actually ran the list scheduler produce bundles.
  if (!RegionWasScheduled)
    return;
  RegionWasScheduled = false;

  // Bake selected format-member opcodes via setDesc BEFORE bundle formation,
  // then full AltDescs.clear() (AIEMachineScheduler.cpp:1081-1082
  // materializeMultiOpcodeInstrs + SelectedAltDescs.clear before
  // computeAndFinalizeBundles).
  materializeMultiOpcodeInstrs();

  SmallVector<CycleBundle> RegionBundles = computeRegionBundles();
  for (CycleBundle &CB : RegionBundles)
    MBBBundles.push_back(std::move(CB));

  LLVM_DEBUG({
    dbgs() << "  << leaveRegion: " << RegionBundles.size()
           << " cycle(s) bundled\n";
  });
}
