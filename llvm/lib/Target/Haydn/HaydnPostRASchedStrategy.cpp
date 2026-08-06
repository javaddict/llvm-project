//===-- HaydnPostRASchedStrategy.cpp - Haydn post-RA bundle-forming sched -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the post-RA scheduler strategy that forms VLIW bundles in
// leaveRegion/leaveMBB. Port of AIE computeAndFinalizeBundles
// (AIEMachineScheduler.cpp:152-228) for both Top and Bot SchedBoundary zones,
// including final CurrCycle flush, Top+Bot merge (AIE leaveRegion
// :1092-1110), and full handleRegionConflicts (ExitReadyCycle pad + inter-zone
// scoreboard / TopReadyCycle hazard pads, AIEMachineScheduler.cpp:1149-1201),
// plus commitBlockSchedule / applyBundles materialize
// (AIEMachineScheduler.cpp:806-863, AIEHazardRecognizer.cpp:317-351).
// leaveMBB also exact-commits multi-member hard roots and replays the full
// MBB cycle stream for cross-boundary operand latency + Required/Reserved
// stalls (plan §5.5; no separate pass or inter-block fixpoint).
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
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>

#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

using namespace llvm;

#define DEBUG_TYPE "haydn-post-ra-sched"

STATISTIC(NumIdleCyclesMaterialized,
          "Number of post-RA idle (stall) cycles materialized as NOP");
STATISTIC(NumMultiMIBundlesFinalized,
          "Number of multi-MI cycles finalized as BUNDLE");
// qualification: must remain 0. Counts fail-closed cases where a
// scheduled multi-MI cycle could not exact-commit as one product cycle
// (solver/scheduler bug). Production does NOT greedily split those cycles.
STATISTIC(NumScheduledCyclesSplit,
          "Number of scheduled multi-MI cycles that failed exact no-split "
          "commit (must be 0 in product qualification)");
STATISTIC(NumTrueRAWCycleRejects,
          "Number of scheduled multi-MI cycles rejected for same-cycle true "
          "RAW (no-forwarding)");
// Multi-member hard BUNDLE roots at post-RA entry (approved producer or
// accidental). Product path before SMS-HANDOFF / architectural rematch
// expects 0 (silent). Non-zero is exact-committed in leaveMBB.
STATISTIC(NumPreExistingHardRootsAtPostRA,
          "Number of multi-member hard BUNDLE roots at post-RA MBB entry "
          "(product path expects 0 before an approved producer)");
STATISTIC(NumHardRootsExactCommitted,
          "Number of multi-member hard BUNDLE roots transactionally "
          "exact-committed at post-RA leaveMBB");
STATISTIC(NumCrossBoundaryReplayStalls,
          "Number of idle NOP cycles inserted by post-RA cross-boundary "
          "latency/Required/Reserved replay");

static cl::opt<bool> EnableHaydnPostRAReadySubsetAuction(
    "haydn-postra-ready-subset-auction", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: rank tryCandidate by bounded ready-subset cycle "
             "auction (maximize issued ops under exact product matching)"));

static cl::opt<bool> EnableHaydnPostRAHardRootReplay(
    "haydn-postra-hard-root-replay", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: exact-commit multi-member hard BUNDLE roots and "
             "replay cross-boundary operand latency + Required/Reserved"));

// legality + multi-MI MIR commit live on HaydnBundleMaterialize
// (instrsFormOneLegalCycle / commitExactMultiMIProductCycle /
// commitExactHardRootProductCycle). PostRA owns region grouping, skippable
// splice, hard-root recommit, cross-boundary replay, fail-closed stats.

/// Count multi-member TargetOpcode::BUNDLE roots in \p MBB (hard cycle
/// groups from SMS handoff / architectural rematch / late multipass).
static unsigned countMultiMemberHardRoots(MachineBasicBlock &MBB) {
  unsigned Count = 0;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle())
      continue;
    unsigned Kids = 0;
    for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I)
      ++Kids;
    if (Kids >= 2)
      ++Count;
  }
  return Count;
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

// True if MI is not a cycle member during post-RA bundle reconstruction.
// Forward-declared here for tryCandidate ready filtering; definition below.
static bool isBundleSkippable(const MachineInstr &MI);

/// Build BaseOpcodes from the live HR current-cycle preferred matching and
/// ReadyOpcodes with Focus first, then other Available (non-skippable) ops.
/// Returns auction fill score for Focus (IssuedCount of densest legal subset
/// that includes Focus).
static unsigned scoreReadySubsetAuction(SchedBoundary &Zone, SUnit *Focus) {
  if (!Focus || !Focus->getInstr() || !Zone.HazardRec ||
      !Zone.HazardRec->isEnabled())
    return 0;

  auto *HR = static_cast<HaydnHazardRecognizer *>(Zone.HazardRec);
  HaydnMCFormats Fmts;

  SmallVector<unsigned, 3> Base;
  const haydn::bundle::CycleState &Pref =
      haydn::bundle::selectPreferredCandidate(
          HR->getCurrentCycleCandidates());
  for (const haydn::bundle::CycleMember &M : Pref.Members)
    Base.push_back(M.LogicalOpcode);
  if (Base.size() >= Haydn::ISSUE_SLOT_COUNT)
    return Base.size();

  SmallVector<unsigned, 8> Ready;
  Ready.push_back(Focus->getInstr()->getOpcode());
  for (SUnit *SU : Zone.Available) {
    if (SU == Focus || !SU->getInstr())
      continue;
    if (isBundleSkippable(*SU->getInstr()))
      continue;
    Ready.push_back(SU->getInstr()->getOpcode());
    if (Ready.size() >= haydn::bundle::MaxReadySubsetAuctionReady)
      break;
  }
  return haydn::bundle::auctionFocusFillScore(Base, Ready, Fmts);
}

bool HaydnPostRASchedStrategy::tryCandidate(SchedCandidate &Cand,
                                            SchedCandidate &TryCand) {
  // Initialize the candidate if needed (match base / AIE).
  if (!Cand.isValid()) {
    TryCand.Reason = NodeOrder;
    return true;
  }

  // Bounded ready-subset cycle auction (issue width 3): prefer the SU that
  // participates in a denser exact-legal fill of the open cycle together with
  // other Available ops and the HR's already-placed base. Pure ranking only —
  // EmitInstruction / leaveRegion still exact-commit via BundleMaterialize.
  if (EnableHaydnPostRAReadySubsetAuction) {
    SchedBoundary &Zone = TryCand.AtTop ? Top : Bot;
    // When comparing cross-zone candidates AtTop may differ; score each in its
    // own zone. Same-zone picks (the common pickNodeFromQueue path) share Zone.
    SchedBoundary &CandZone = Cand.AtTop ? Top : Bot;
    const unsigned TryScore = scoreReadySubsetAuction(Zone, TryCand.SU);
    const unsigned CandScore = scoreReadySubsetAuction(CandZone, Cand.SU);
    if (TryScore != CandScore) {
      if (TryScore > CandScore) {
        TryCand.Reason = ResourceDemand;
        return true;
      }
      return false;
    }
  }

  // Prefer a load over a pure ALU/MAC when choosing the cycle's FIRST op so
  // multi-cycle load latency overlaps later compute. Once the current best
  // candidate is already a load, do NOT force-reject a non-load Try — fall
  // through to the base critical-path/resource heuristics so a ready MAC can
  // co-issue with the load (S2) instead of always hoisting another load that
  // only dual-issues at best and often serializes (bkfir CB-load streams).
  {
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

// True if MI is not a cycle member during post-RA bundle reconstruction.
// Never blanket-skip isPseudo: MultiSlot_Pseudo (alts-bearing) books slots in
// HR and must appear in cycle reconstruction/splice. True meta and non-issue
// markers stay skippable; no-alt expand residuals stay skippable until
// residual expansion is guaranteed before pack.
// INLINEASM is skippable here (not a product cycle member) but is *not*
// splice-movable — see spliceSkippablesForCycle. It remains a standalone
// opaque layout boundary (isSchedulingBoundary + FinalizeBundle skip).
static bool isBundleSkippable(const MachineInstr &MI) {
  // Non-issue markers independent of isPseudo / placement alts.
  if (MI.isDebugInstr() || MI.isPosition() || MI.isBundle() ||
      MI.isInlineAsm() || MI.isCopy())
    return true;
  // Remaining LLVM meta (CFI, LIFETIME, PHI, …). MultiSlot_Pseudo is not meta.
  if (MI.isMetaInstruction())
    return true;
  HaydnMCFormats Fmts;
  return Haydn::MachineBundle::isBundlePackSkippableOpcode(
      MI.getOpcode(), MI.isPseudo(), Fmts);
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
  //
  // Multi-member hard BUNDLE roots (SMS handoff / architectural rematch)
  // are counted here and transactionally exact-committed in leaveMBB.
  // Product path before an approved producer expects the counter to stay 0
  // (postmisched-exact-nosplit-pre-handoff.ll). Illegal membership fails
  // closed at exact-commit, not by silent entry.
  if (MBB) {
    unsigned HardRoots = countMultiMemberHardRoots(*MBB);
    if (HardRoots) {
      NumPreExistingHardRootsAtPostRA += HardRoots;
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: " << HardRoots
                        << " multi-member hard BUNDLE root(s) at post-RA "
                           "entry in bb."
                        << MBB->getNumber()
                        << " (exact-commit + replay in leaveMBB)\n");
    }
  }
  PostGenericScheduler::enterMBB(MBB);
}

void HaydnPostRASchedStrategy::leaveMBB() {
  // Materialize the bundles accumulated across all regions of this MBB into
  // actual BUNDLE MIs (standalone NOPs for idle cycles), in MBB order.
  // Mirrors AIE's commitBlockSchedule (AIEMachineScheduler.cpp:825-863), but
  // without the inter-block fixpoint gate (we always commit).
  //
  // Setup gap: ZOLSetupExitLatency raises ExitSU latency from every final SET
  // form by SetupIssueDistance (AIE LoopSetupDistance peer = 4). leaveRegion
  // flushes empty cycles through zone CurrCycle and ExitSU ready cycle so
  // residual following cycles materialize as NOPs here. SET is a real region
  // SU (not a TII boundary). Fixup still residual-pads if useful-window fill
  // is short.
  //
  // After region packs: (1) exact-commit multi-member hard roots that were
  // region boundaries (not in MBBBundles); (2) replay the full ordered cycle
  // stream for cross-boundary RAW latency + Req/Res stalls.
  //
  // Snapshot hard-root *children* before materializeBundles. Scheduled multi-MI
  // packs created by materialize are ordinary product cycles — not hard roots —
  // and must not re-enter exact-commit/replay (would double-count stats and
  // re-apply raw itinerary stalls across DAG-refined edges).
  if (CurrentMBB) {
    SmallPtrSet<MachineInstr *, 8> HardMembers;
    if (EnableHaydnPostRAHardRootReplay) {
      for (MachineInstr &MI : *CurrentMBB) {
        if (!MI.isBundle() || MI.isBundledWithPred())
          continue;
        SmallVector<MachineInstr *, 3> Kids;
        for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
             I != CurrentMBB->instr_end() && I->isBundledWithPred(); ++I)
          Kids.push_back(&*I);
        if (Kids.size() >= 2)
          HardMembers.insert(Kids.begin(), Kids.end());
      }
    }
    if (!MBBBundles.empty()) {
      materializeBundles(*CurrentMBB, MBBBundles);
      MBBBundles.clear();
    }
    if (EnableHaydnPostRAHardRootReplay && !HardMembers.empty()) {
      exactCommitHardRoots(*CurrentMBB, HardMembers);
      replayCrossBoundaryHazards(*CurrentMBB, HardMembers);
    }
  }
  // Pack ownership = leaveRegion/leaveMBB only.
  PostGenericScheduler::leaveMBB();
}

SmallVector<HaydnPostRASchedStrategy::CycleBundle>
HaydnPostRASchedStrategy::computeAndFinalizeBundles(SchedBoundary &Zone) {
  // Port of AIE::computeAndFinalizeBundles (AIEMachineScheduler.cpp:152-228).
  // Only called for regions that were actually scheduled (RegionWasScheduled
  // gate in leaveRegion), so the DAG region iterators are valid.
  LLVM_DEBUG(dbgs() << "Computing Bundles for Zone "
                    << (Zone.isTop() ? "Top\n" : "Bot\n"));
  SmallVector<CycleBundle> Bundles;
  CycleBundle CurrBundle;

  auto AddInBundles = [&](auto Range) {
    for (MachineInstr &MI : Range) {
      if (isBundleSkippable(MI))
        continue;

      SUnit *SU = DAG->getSUnit(&MI);
      if (!SU || !SU->isScheduled)
        continue;

      // Zone-local ready cycle only — never read TopReadyCycle for a
      // bottom-scheduled SU (or BotReadyCycle for a top-scheduled SU).
      unsigned EmitCycle =
          Zone.isTop() ? SU->TopReadyCycle : SU->BotReadyCycle;

      // Defensive clamp: pre-RA-style physreg reschedule can leave ReadyCycle
      // behind the emission order (AIE computeAndFinalizeBundles pre-RA arm).
      // Post-RA should not hit this; keep progress monotonic.
      if (EmitCycle < Bundles.size())
        EmitCycle = Bundles.size();

      if (EmitCycle != Bundles.size())
        bumpCycleForBundles(EmitCycle, Bundles, CurrBundle);

      CurrBundle.Instrs.push_back(&MI);
    }
  };

  if (Zone.isTop())
    AddInBundles(make_range(DAG->begin(), DAG->top()));
  else
    AddInBundles(reverse(make_range(DAG->bottom(), DAG->end())));

  // Flush any non-empty CurrBundle as its own cycle.
  if (!CurrBundle.empty()) {
    bumpCycleForBundles(Bundles.size() + 1, Bundles, CurrBundle);
    LLVM_DEBUG(dbgs() << "  Finalized Bundle. NumBundles=" << Bundles.size()
                      << "\n");
  }

  // Bot can emit into cycles beyond CurrCycle; pull the zone forward so the
  // subsequent pad has a well-defined upper bound.
  if (Bundles.size() > Zone.getCurrCycle()) {
    Zone.bumpCycle(Bundles.size());
    LLVM_DEBUG(dbgs() << "  Updated zone CurrCycle=" << Zone.getCurrCycle()
                      << "\n");
  }

  // Final CurrCycle flush: Top (and Bot after the sync above) may have been
  // advanced past the last emission cycle by hazards / issue stalls. Pad empty
  // cycles so the reconstructed list covers the full scheduled window.
  if (Zone.getCurrCycle() != Bundles.size())
    bumpCycleForBundles(Zone.getCurrCycle(), Bundles, CurrBundle);

  // Bot reconstruction walks reverse emission order; canonicalize to MBB order
  // for applyBundles / rolling NOP insert.
  if (!Zone.isTop()) {
    std::reverse(Bundles.begin(), Bundles.end());
    for (CycleBundle &Bundle : Bundles)
      std::reverse(Bundle.Instrs.begin(), Bundle.Instrs.end());
  }
  return Bundles;
}

// Splice skippable MIs out of [First,Last] so real members are contiguous.
// Returns false if a skippable has a reg conflict with a real member (unsafe),
// or if an opaque INLINEASM boundary lies strictly inside the multi-MI span
// (compiler BUNDLE must never cross INLINEASM — fail closed, leave sequential).
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
    // INLINEASM is a layout/scheduling boundary, not movable glue. Presence
    // between same-cycle members would mean a BUNDLE crossing the opaque
    // boundary — refuse the multi-MI pack rather than splice it aside.
    if (MI.isInlineAsm()) {
      BundleUnsafe = true;
      continue;
    }
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

// exact no-split materialize for a scheduled multi-MI cycle.
// Shared HaydnBundleMaterialize commit when one legal Format E product cycle
// (setDesc to real members inside commitExactMultiMIProductCycle); else fail
// closed (no greedy multi-MI sub-cycle repair). NumScheduledCyclesSplit
// diagnoses the bug; product qualification requires it to stay 0.
static void materializeExactNoSplitCycle(MachineBasicBlock &MBB,
                                         ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return;

  if (!spliceSkippablesForCycle(MBB, Instrs)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: unsafe skippable splice — "
                         "exact no-split fail-closed (sequential)\n");
    ++NumScheduledCyclesSplit;
    return;
  }
  if (!membersContiguous(Instrs)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: same-cycle MIs not contiguous — "
                         "exact no-split fail-closed (sequential)\n");
    ++NumScheduledCyclesSplit;
    return;
  }

  HaydnMCFormats Fmts;
  // leaveRegion materializeMultiOpcodeInstrs already baked AltDescs into
  // setDesc; commitExactMultiMIProductCycle re-solves as fail-closed second
  // line so residual logicals cannot enter a multi-MI Format E BUNDLE.
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  if (haydn::bundle::cycleMembersHaveTrueRAW(Instrs, TRI)) {
    // Intentional no-forwarding fail-closed: leave sequential parcels.
    // Not a solver/format bug — do not count as NumScheduledCyclesSplit.
    ++NumTrueRAWCycleRejects;
    LLVM_DEBUG({
      dbgs() << "HaydnPostRASched: multi-MI true RAW reject (no-forwarding):\n";
      for (MachineInstr *MI : Instrs)
        if (MI)
          dbgs() << "  " << *MI;
    });
    return;
  }
  if (haydn::bundle::instrsFormOneLegalCycle(Instrs, Fmts) &&
      haydn::bundle::commitExactMultiMIProductCycle(Instrs)) {
    ++NumMultiMIBundlesFinalized;
    return;
  }

  // Scheduled multi-MI cannot form one legal Format E product cycle —
  // solver/scheduler bug. Do NOT greedily split into multi-MI sub-cycles
  // (greedySplit is diagnostic-only; HaydnBundleMaterialize.h). Leave
  // sequential parcels; each singleton is wrapped by HaydnFinalizeBundle
  // (late setDesc + Format E row stamp).
  LLVM_DEBUG(dbgs() << "HaydnPostRASched: scheduled multi-MI not one legal "
                       "Format E product cycle — exact no-split fail-closed "
                       "(no greedy production split)\n");
  ++NumScheduledCyclesSplit;
}

void HaydnPostRASchedStrategy::materializeBundles(
    MachineBasicBlock &MBB, SmallVector<CycleBundle> &Bundles) {
  // Port of AIE materializeEmptyBundles + applyBundles, Top zone only.
 // Cycle ownership (exact no-split Format E encode):
  // * empty cycle → NOP at rolling position (before next real cycle / term)
  // * single MI → leave standalone here; HaydnFinalizeBundle wraps + stamps
  //   FormatID (AIEFinalizeBundle.cpp:40-59 peer)
  // * 2-3 MIs legal → shared commitExactMultiMIProductCycle
  // * 2-3 MIs illegal → fail closed (no production greedy split)
  //
  // Product plan: every encode cycle is Format E (registry EncodedBytes=12)
  // with BUNDLE-root BundleFormatRowID + CompletionStateID. Multi-MI roots
  // setDesc real members inside commitExactMultiMIProductCycle. Singleton
  // cycles become BUNDLE + row stamp in HaydnFinalizeBundle after this
  // scheduler (AIE2 addPreSched2 order; late setDesc for bare logicals).
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
      continue; // One cycle; HaydnFinalizeBundle wraps + late setDesc.

    materializeExactNoSplitCycle(MBB, CB.Instrs);
  }
}

void HaydnPostRASchedStrategy::materializeMultiOpcodeInstrs() {
  // AIE port of AIEPostRASchedStrategy::materializeMultiOpcodeInstrs
  // (AIEMachineScheduler.cpp:1121-1139): when HR selected a format-member
  // opcode (commitPlacementForEmit → setAlternateDescriptor), bake it into
  // the MachineInstr via setDesc. Product is Format E only; members are the
  // residual `_S*` placement peers used until live Format E entry Insts land.
  //
  // End-state (AIEMachineScheduler.cpp:1081-1082 +
  // AIEAlternateDescriptors.h:74): SelectedAltDescs.clear() after setDesc.
  // Post-commit placement is opcode identity via getSlotKind
  // (AIEBaseMCFormats.cpp:66-75). No slot side-map. Multi-MI commit also
  // re-solves setDesc as fail-closed second line (no residual logical pack).
  HaydnAlternateDescriptors &AltDescs =
      DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();

  auto MaterializePseudo = [&](MachineInstr &MI) {
    // AIE parity (AIEMachineScheduler.cpp:1126-1132): unconditional
    // MI.setDesc when getSelectedOpcode is present. AIE alts share operand
    // structure by construction (AIEAlternateDescriptors.h:64-68); Haydn
    // members now match logical NumOperands/NumDefs (S_SW_BREV_*_S* / BREV
    // load *_LD_S* tied shapes). No shape-gate, no MCFlags write.
    // INLINEASM is never a format-member logical — leave it alone.
    if (MI.isInlineAsm())
      return;
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

// Resolve the SUnit for a cycle-list MI (AIE getBundledSUnit peer,
// AIEMachineScheduler.cpp:1141-1147). leaveRegion reconstructs before
// leaveMBB exact-commit, so members are normally unbundled; fall back to
// BUNDLE root when multipass re-entry left a stamped hard group.
static const SUnit *getBundledSUnit(const ScheduleDAGMI *DAG,
                                    MachineInstr *MI) {
  if (const SUnit *SU = DAG->getSUnit(MI))
    return SU;
  MachineInstr &Root = *getBundleStart(MI->getIterator());
  return DAG->getSUnit(&Root);
}

bool HaydnPostRASchedStrategy::checkInterZoneConflicts(
    ArrayRef<CycleBundle> BotBundles) const {
  // AIE checkInterZoneConflicts (AIEMachineScheduler.cpp:1149-1174).
  auto *TopHR = static_cast<HaydnHazardRecognizer *>(Top.HazardRec);
  auto *BotHR = static_cast<HaydnHazardRecognizer *>(Bot.HazardRec);

  // Both zones finish their last scheduled bundles by advance/recede to an
  // empty cycle: Bot scoreboard[0] is cycle -1 relative to Top's write head.
  // Align Bot at DeltaCycles=-1 against Top and reject residual FU overlap.
  if (TopHR && BotHR && TopHR->isEnabled() && BotHR->isEnabled() &&
      TopHR->conflict(*BotHR, /*DeltaCycles=*/-1))
    return true;

  // Bot MIs inherit TopReadyCycle from Top-zone predecessors; after merge
  // they land at CurTopCycle, CurTopCycle+1, ... — pad Top when any MI is
  // still not Top-ready at its landing cycle.
  unsigned CurTopCycle = Top.getCurrCycle();
  for (const CycleBundle &Bundle : BotBundles) {
    for (MachineInstr *MI : Bundle.Instrs) {
      const SUnit *SU = getBundledSUnit(DAG, MI);
      if (SU && SU->TopReadyCycle > CurTopCycle)
        return true;
    }
    ++CurTopCycle;
  }
  return false;
}

void HaydnPostRASchedStrategy::handleRegionConflicts(
    const SUnit &ExitSU, SmallVectorImpl<CycleBundle> &TopBundles,
    ArrayRef<CycleBundle> BotBundles) {
  // AIE handleRegionConflicts (AIEMachineScheduler.cpp:1176-1201).

  // Meeting-point / Exit ready pad: residual setup distance from SET → ExitSU
  // (ZOLSetupExitLatency) becomes empty trailing cycles on Top before merge.
  const unsigned ExitReadyCycle = ExitSU.TopReadyCycle;
  const unsigned TopFinalCycle = Top.getCurrCycle() + Bot.getCurrCycle();
  LLVM_DEBUG(dbgs() << "  handleRegionConflicts: ExitReadyCycle="
                    << ExitReadyCycle << " TopFinalCycle=" << TopFinalCycle
                    << " TopCurr=" << Top.getCurrCycle()
                    << " BotCurr=" << Bot.getCurrCycle() << "\n");
  if (ExitReadyCycle > TopFinalCycle)
    Top.bumpCycle(ExitReadyCycle - Bot.getCurrCycle());

  // Pad NOPs between Top and Bot until scoreboards do not overlap and all
  // Bot TopReadyCycle deps are met. Each Top.bumpCycle advances Top's HR
  // (SchedBoundary::bumpCycle → AdvanceCycle) so multi-cycle FU tails slide
  // past the Bot window.
  while (checkInterZoneConflicts(BotBundles)) {
    LLVM_DEBUG(dbgs() << "  handleRegionConflicts: Bump Top cycle\n");
    Top.bumpCycle(Top.getCurrCycle() + 1);
  }

  // Reflect any Top CurrCycle growth into the Top cycle list as empty pads.
  if (Top.getCurrCycle() != TopBundles.size()) {
    CycleBundle Dummy;
    bumpCycleForBundles(Top.getCurrCycle(), TopBundles, Dummy);
  }
}

void HaydnPostRASchedStrategy::leaveRegion(const SUnit &ExitSU) {
  // Reconstruct this region's cycle list from both SchedBoundary zones and
  // append it to the per-MBB accumulator. The MBB is mutated later in leaveMBB.
  // Mirrors AIEPostRASchedStrategy::leaveRegion (AIEMachineScheduler.cpp:1073-
  // 1119) without the inter-block fixpoint gate / delay-slot fixup.
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

  SmallVector<CycleBundle> TopBundles = computeAndFinalizeBundles(Top);
  SmallVector<CycleBundle> BotBundles = computeAndFinalizeBundles(Bot);
  handleRegionConflicts(ExitSU, TopBundles, BotBundles);

  const unsigned NumTop = TopBundles.size();
  const unsigned NumBot = BotBundles.size();
  for (CycleBundle &CB : TopBundles)
    MBBBundles.push_back(std::move(CB));
  for (CycleBundle &CB : BotBundles)
    MBBBundles.push_back(std::move(CB));

  LLVM_DEBUG(dbgs() << "  << leaveRegion: " << NumTop << " top + " << NumBot
                    << " bot cycle(s) bundled\n");
}

void HaydnPostRASchedStrategy::exactCommitHardRoots(
    MachineBasicBlock &MBB,
    const SmallPtrSetImpl<MachineInstr *> &HardMembers) {
  // Collect multi-member roots whose children were hard at pre-materialize
  // snapshot — commitExactHardRootProductCycle mutates the instr list
  // (dissolve + refinalize). Skip ordinary multi-MI packs created by
  // materializeBundles this leaveMBB.
  SmallVector<MachineInstr *, 4> Roots;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle() || MI.isBundledWithPred())
      continue;
    unsigned Kids = 0;
    bool IsHard = false;
    for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I) {
      ++Kids;
      if (HardMembers.contains(&*I))
        IsHard = true;
    }
    if (Kids >= 2 && IsHard)
      Roots.push_back(&MI);
  }

  for (MachineInstr *Root : Roots) {
    if (!Root->getParent())
      continue; // already dissolved as part of a prior commit
    // Idempotent when already Format E row/completion-stamped and field-ordered:
    // still re-run so member setDesc, canonical order, and rebuilt
    // consolidated root operands/kills/InternalRead stay authoritative
    // (stale handoff root shell is erased inside the helper).
    if (!haydn::bundle::commitExactHardRootProductCycle(*Root, *HII)) {
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: hard-root exact-commit failed:\n"
                        << *Root);
      report_fatal_error(
          "Haydn: multi-member hard BUNDLE root is not one legal product "
          "cycle (exact-commit fail-closed)",
          /*GenCrashDiag=*/false);
    }
    ++NumHardRootsExactCommitted;
    ++NumMultiMIBundlesFinalized;
  }
}

// Collect real issue members of one top-level cycle head (BUNDLE root or
// bare real MI). Empty when \p Head is non-issue meta.
static void collectCycleMembers(MachineInstr &Head,
                                SmallVectorImpl<MachineInstr *> &Members) {
  Members.clear();
  MachineBasicBlock *MBB = Head.getParent();
  if (!MBB)
    return;
  if (Head.isBundle()) {
    for (MachineBasicBlock::instr_iterator I = std::next(Head.getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (!isBundleSkippable(*I))
        Members.push_back(&*I);
    }
    return;
  }
  if (!isBundleSkippable(Head))
    Members.push_back(&Head);
}

// True if any member of \p Members conflicts with \p SB at the issue cycle
// (stage-relative Required/Reserved + ports/issue).
static bool cycleConflictsScoreboard(
    const HaydnHazardRecognizer &HR,
    const ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
    ArrayRef<MachineInstr *> Members) {
  for (MachineInstr *MI : Members) {
    if (HR.checkConflict(SB, *MI, /*Cycle=*/0))
      return true;
  }
  // Same-cycle WAW on destination regs across members (spec: no dual def).
  SmallSet<Register, 8> SeenDefs;
  const TargetRegisterInfo *TRI = nullptr;
  for (MachineInstr *MI : Members) {
    if (!TRI)
      TRI = MI->getMF()->getSubtarget().getRegisterInfo();
    for (const MachineOperand &MO : MI->all_defs()) {
      if (!MO.isReg() || !MO.getReg() || MO.isDead())
        continue;
      Register R = MO.getReg();
      if (R == Haydn::SFR)
        continue;
      for (Register Prev : SeenDefs) {
        if (TRI->regsOverlap(Prev, R))
          return true;
      }
      SeenDefs.insert(R);
    }
  }
  return false;
}

// Minimum issue-cycle gap required between a prior def and a later use of the
// same physreg (operand latency). Returns 0 when no edge / latency ≤ 1.
static unsigned requiredLatencyGap(const TargetInstrInfo &TII,
                                   const InstrItineraryData *Itin,
                                   const MachineInstr &DefMI, unsigned DefIdx,
                                   const MachineInstr &UseMI, unsigned UseIdx) {
  if (std::optional<unsigned> L =
          TII.getOperandLatency(Itin, DefMI, DefIdx, UseMI, UseIdx)) {
    if (*L <= 1)
      return 0;
    return *L - 1;
  }
  // Itinerary miss: loads default to LoadLatency-driven gap of 1 (latency 2).
  if (DefMI.mayLoad())
    return 1;
  return 0;
}

void HaydnPostRASchedStrategy::replayCrossBoundaryHazards(
    MachineBasicBlock &MBB,
    const SmallPtrSetImpl<MachineInstr *> &HardMembers) {
  // Cross-boundary replay (plan §5.5): close hazards on both sides of a fixed
  // multi-member hard root. Region-local scheduling + handleRegionConflicts
  // already own intra-region latency/Req/Res; full-stream re-validation would
  // re-apply raw itinerary latencies that adjustSchedDependency refined in
  // the DAG. Activation: only when ≥1 pre-materialize multi-member hard root
  // is present (\p HardMembers non-empty). Ordinary leaveMBB multi-MI packs
  // are excluded — their children are not in HardMembers.
  //
  // Inserts only exact NOP cycles at hard-root seams; never
  // moves/merges/splits a hard cycle. Local scratch scoreboard only.
  if (MBB.empty() || HardMembers.empty())
    return;

  // Identify hard BUNDLE roots by surviving child identity (exact-commit may
  // have dissolved the pre-materialize shell and re-stamped a new root).
  SmallPtrSet<MachineInstr *, 4> HardRootSet;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle() || MI.isBundledWithPred())
      continue;
    unsigned Kids = 0;
    bool IsHard = false;
    for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I) {
      ++Kids;
      if (HardMembers.contains(&*I))
        IsHard = true;
    }
    if (Kids >= 2 && IsHard)
      HardRootSet.insert(&MI);
  }
  if (HardRootSet.empty())
    return;

  const MachineFunction &MF = *MBB.getParent();
  const InstrItineraryData *Itin =
      MF.getSubtarget().getInstrItineraryData();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  HaydnHazardRecognizer ScratchHR(HII, Itin, /*IsPreRA=*/false,
                                  /*AltDescs=*/nullptr);
  ScratchHR.Reset();

  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  const int Depth = std::max(ScratchHR.getPipelineDepth(), 1);
  SB.reset(Depth);

  // Def site: cycle index + MI/op. SideClass: which side of the nearest hard
  // root produced this def (PreRoot / InRoot / PostRoot).
  enum class Side : uint8_t { PreRoot, InRoot, PostRoot };
  struct DefInfo {
    unsigned Cycle = 0;
    MachineInstr *MI = nullptr;
    unsigned OpIdx = 0;
    Side Origin = Side::PreRoot;
  };
  DenseMap<Register, DefInfo> LastDef;
  unsigned CurrCycle = 0;
  bool SeenHardRoot = false;
  bool PrevWasHardRoot = false;

  auto insertStallBefore = [&](MachineBasicBlock::iterator InsertPt) {
    HII->insertNoop(MBB, InsertPt);
    SB.advance();
    ++CurrCycle;
    ++NumCrossBoundaryReplayStalls;
    ++NumIdleCyclesMaterialized;
  };

  for (MachineBasicBlock::iterator It = MBB.begin(); It != MBB.end();) {
    MachineInstr &Head = *It;
    MachineBasicBlock::iterator Next = std::next(It);
    if (Head.isBundledWithPred()) {
      It = Next;
      continue;
    }

    SmallVector<MachineInstr *, 3> Members;
    collectCycleMembers(Head, Members);
    if (Members.empty()) {
      It = Next;
      continue;
    }

    const bool IsHard = HardRootSet.contains(&Head);
    // Seam cycles: the hard root itself (producer→root) and the first cycle
    // after a hard root (root→consumer).
    const bool AtSeam = IsHard || PrevWasHardRoot;

    if (AtSeam) {
      unsigned LatencyStalls = 0;
      const Side UseSide =
          IsHard ? Side::InRoot
                 : (SeenHardRoot ? Side::PostRoot : Side::PreRoot);
      for (MachineInstr *UseMI : Members) {
        for (unsigned U = 0, UE = UseMI->getNumOperands(); U != UE; ++U) {
          const MachineOperand &MO = UseMI->getOperand(U);
          if (!MO.isReg() || !MO.getReg() || !MO.readsReg())
            continue;
          Register UseR = MO.getReg();
          for (const auto &KV : LastDef) {
            if (!TRI->regsOverlap(KV.first, UseR))
              continue;
            const DefInfo &DI = KV.second;
            // Cross-boundary only: def and use on different sides of a root.
            // PreRoot → InRoot, PreRoot → PostRoot, InRoot → PostRoot.
            bool Crosses = false;
            if (DI.Origin == Side::PreRoot &&
                (UseSide == Side::InRoot || UseSide == Side::PostRoot))
              Crosses = true;
            else if (DI.Origin == Side::InRoot && UseSide == Side::PostRoot)
              Crosses = true;
            if (!Crosses)
              continue;
            unsigned Gap = requiredLatencyGap(*HII, Itin, *DI.MI, DI.OpIdx,
                                              *UseMI, U);
            if (Gap == 0)
              continue;
            unsigned Ready = DI.Cycle + Gap + 1;
            if (CurrCycle + LatencyStalls < Ready)
              LatencyStalls = Ready - CurrCycle;
          }
        }
      }
      for (unsigned S = 0; S < LatencyStalls; ++S)
        insertStallBefore(Head.getIterator());

      unsigned Guard = 0;
      while (cycleConflictsScoreboard(ScratchHR, SB, Members) && Guard < 64) {
        insertStallBefore(Head.getIterator());
        ++Guard;
      }
      if (Guard >= 64) {
        report_fatal_error(
            "Haydn: cross-boundary replay failed to clear Req/Res conflict",
            /*GenCrashDiag=*/false);
      }
    }

    for (MachineInstr *MI : Members)
      ScratchHR.enterResources(SB, *MI, /*DeltaCycles=*/0);

    const Side DefSide =
        IsHard ? Side::InRoot
               : (SeenHardRoot ? Side::PostRoot : Side::PreRoot);
    for (MachineInstr *MI : Members) {
      for (unsigned D = 0, DE = MI->getNumOperands(); D != DE; ++D) {
        const MachineOperand &MO = MI->getOperand(D);
        if (!MO.isReg() || !MO.isDef() || !MO.getReg())
          continue;
        Register R = MO.getReg();
        if (R == Haydn::SFR)
          continue;
        LastDef[R] = DefInfo{CurrCycle, MI, D, DefSide};
      }
    }

    if (IsHard) {
      SeenHardRoot = true;
      PrevWasHardRoot = true;
    } else {
      PrevWasHardRoot = false;
    }

    SB.advance();
    ++CurrCycle;
    It = Next;
  }
}
