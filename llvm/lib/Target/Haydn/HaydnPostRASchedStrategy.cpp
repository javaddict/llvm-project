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
// leaveMBB free-packs scheduled multi-MI via commitExactMultiMIProductCycle
// (sole scheduled multi-MI producer), commits residual unstamped multi-member
// shells with the same ordinary multi-MI path when jointly legal (else
// sequentializes), and replays multi-member parcel seam latency. No hard-root
// freeze identity and no force-coissue. Post-RA never invents SMS stages.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRASchedStrategy.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnMemberSetDesc.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "HaydnPostRAScratch.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <optional>

// Register enums (Haydn::SFR, ...) arrive via HaydnPortModel ->
// MCTargetDesc/HaydnMCTargetDesc.h (GET_REGINFO_ENUM once). Do not re-include
// HaydnGenRegisterInfo.inc here — double enum definition is illegal.

using namespace llvm;

#define DEBUG_TYPE "haydn-post-ra-sched"

STATISTIC(NumIdleCyclesMaterialized,
          "Number of post-RA idle (stall) cycles materialized as NOP");
STATISTIC(NumMultiMIBundlesFinalized,
          "Number of multi-MI cycles finalized as BUNDLE");
// RESIDUAL(21): can be nonzero today. Field-order RAW fail-closed
// sequential fallback (postra-field-order-raw-narrow-store.mir) is
// correct product behavior — keep as STATISTIC; do not abort llc.
STATISTIC(NumScheduledCyclesSplit,
          "Number of scheduled multi-MI cycles that failed exact no-split "
          "commit (nonzero is sequential fallback, not a product abort)");
STATISTIC(NumTrueRAWCycleRejects,
          "Number of scheduled multi-MI cycles rejected for same-cycle true "
          "RAW (no-forwarding)");
// Multi-member BUNDLE roots at post-RA entry. Product path expects 0.
STATISTIC(NumPreExistingHardRootsAtPostRA,
          "Number of multi-member hard BUNDLE roots at post-RA MBB entry "
          "(product path expects 0)");
// Description strings keep historical FileCheck pins from residual MIR
// fixtures (hard-root era). Implementation is ordinary multi-MI commit /
// multi-member seam replay — not freeze identity.
STATISTIC(NumUnstampedMultiMemberCommitted,
          "Number of multi-member hard BUNDLE roots transactionally "
          "exact-committed at post-RA leaveMBB");
STATISTIC(NumUnstampedMultiMemberSequentialized,
          "Number of multi-member shells recovered by schedule-order "
          "sequentialize after the product coissue probe rejected packing "
          "(recovery only; not an independent legality authority)");
STATISTIC(NumProductCoissueProbeRejects,
          "Number of residual multi-member shells rejected by the product "
          "coissue probe (shared legality authority; sequentialize follows)");
STATISTIC(NumBundledFreePackRefusals,
          "Number of free scheduled multi-MI packs refused because they "
          "touched a hard-root member (commit-inside-group only)");
STATISTIC(NumMultiMemberSeamReplayStalls,
          "Number of idle NOP cycles inserted by post-RA cross-boundary "
          "latency/Required/Reserved replay");
STATISTIC(NumPostRAScheduledCyclesAudited,
          "Number of architectural cycles reconstructed by post-RA leaveMBB "
          "(emitted-cycle audit; one cycle per scheduled pack/idle/single)");
STATISTIC(NumPostRAAltDescsCleared,
          "Number of post-RA regions that cleared transient alternate "
          "descriptors after setDesc materialize (product expects every "
          "scheduled region)");
STATISTIC(NumPostRAAltDescLeakFatals,
          "Number of post-RA leaveRegion/leaveMBB residual alternate "
          "descriptor leaks (must be 0)");
STATISTIC(NumPostRAResourceAdmissionPinsHeld,
          "Number of post-RA enterMBB checks that held fail-closed per-op "
          "resource admission (product closed until golden import)");

static cl::opt<bool> EnableHaydnPostRAReadySubsetAuction(
    "haydn-postra-ready-subset-auction", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: rank tryCandidate by bounded ready-subset cycle "
             "auction (maximize issued ops under exact product matching)"));

// legality + multi-MI MIR commit live on HaydnBundleMaterialize
// (instrsFormOneLegalCycle / commitExactMultiMIProductCycle). PostRA owns
// region grouping, free multi-MI exact commit, residual unstamped multi-member
// ordinary commit/sequentialize, and multi-member seam latency replay.

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
  // Multi-member BUNDLE roots at entry are metrics-only (product expects 0).
  if (MBB) {
    // Fail-closed per-op resource admission pin (release-visible). Shares the
    // single PortModel availability-aware record with pre-RA / HR /
    // ResourceCycle: aggregate ceilings only; no admitted per-op table and no
    // competitive per-op claims until golden publishes the complete generated
    // import. LeaveMBB sequential fallout for illegal multi-member shells is
    // recovery only after the product coissue probe rejects packing — not an
    // independent legality authority.
    static bool ResourceAdmissionPinned = false;
    if (!ResourceAdmissionPinned) {
      ResourceAdmissionPinned = true;
      if (!haydnAvailabilityAwareConsumePinsHold())
        report_fatal_error(
            "Haydn post-RA product resource admission pins failed",
            /*GenCrashDiag=*/false);
    }
    ++NumPostRAResourceAdmissionPinsHeld;
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: resource-admission "
                         "per_op_records=0 competitive_claims=0 "
                         "aggregate_surface_bound=1\n");
    unsigned HardRoots = countMultiMemberHardRoots(*MBB);
    if (HardRoots) {
      NumPreExistingHardRootsAtPostRA += HardRoots;
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: " << HardRoots
                        << " multi-member BUNDLE root(s) at post-RA entry "
                           "in bb."
                        << MBB->getNumber()
                        << " (metrics only)\n");
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
  // Product multi-MI: free scheduled packs only, then residual unstamped
  // multi-member shells via ordinary commitExactMultiMIProductCycle (or
  // sequentialize if illegal). Multi-member seam latency replay is not a
  // hard-root freeze path.
  if (CurrentMBB) {
    // Snapshot multi-member children present before free pack. Seam latency
    // replay applies only to residual shells (and their ordinary multi-MI
    // recommits), not free scheduled multi-MI packs created this leaveMBB.
    SmallPtrSet<MachineInstr *, 8> PreExistingMultiMembers;
    for (MachineInstr &MI : *CurrentMBB) {
      if (!MI.isBundle() || MI.isBundledWithPred())
        continue;
      SmallVector<MachineInstr *, 3> Kids;
      for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
           I != CurrentMBB->instr_end() && I->isBundledWithPred(); ++I)
        Kids.push_back(&*I);
      if (Kids.size() >= 2)
        PreExistingMultiMembers.insert(Kids.begin(), Kids.end());
    }
    if (!MBBBundles.empty()) {
      NumPostRAScheduledCyclesAudited += MBBBundles.size();
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: emitted-cycle audit bb."
                        << CurrentMBB->getNumber()
                        << " cycles=" << MBBBundles.size() << "\n");
      materializeBundles(*CurrentMBB, MBBBundles);
      MBBBundles.clear();
    }
    commitOrSequentializeUnstampedMultiMemberBundles(*CurrentMBB);
    // Own only the current MBB. Predecessor re-probe after leave was a
    // wrong-layer repair for post-pipeliner mutating already-left MBBs;
    // post-pipeliner is default OFF and must preflight/commit whole-loop
    // itself (no cross-MBB callback repair).
    if (!PreExistingMultiMembers.empty())
      replayMultiMemberSeamHazards(*CurrentMBB, PreExistingMultiMembers);
    HaydnAlternateDescriptors &AltDescs =
        CurrentMBB->getParent()
            ->getInfo<HaydnMachineFunctionInfo>()
            ->getAltDescs();
    if (!AltDescs.empty()) {
      ++NumPostRAAltDescLeakFatals;
      report_fatal_error(
          "Haydn post-RA leaveMBB found residual alternate descriptors",
          /*GenCrashDiag=*/false);
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
    // Do not splice meta/COPY across a member def/use boundary. A COPY
    // that only *reads* a member-def'd reg used to pass the def-only
    // check and was hoisted above its producer (stale value).
    bool HasRegConflict = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      for (const MachineInstr *RealMI : Instrs) {
        if (RealMI == &MI)
          continue;
        if (MO.isDef() && (RealMI->readsRegister(Reg, TRI) ||
                           RealMI->definesRegister(Reg, TRI))) {
          HasRegConflict = true;
          break;
        }
        if (MO.isUse() && RealMI->definesRegister(Reg, TRI)) {
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

/// Layer 1–2 free-pack gate (available-cycle detect):
///   * same zone ReadyCycle for every member
///   * no Data edge Lat≥1 between members (ReadyCycle should already split)
///   * no schedule-order true RAW (\p cycleMembersHaveTrueRAW) — MI restatement
///     of the same avail-cycle contract after physreg paint
/// Anti may share a cycle only with use-before-redef; emission is layer 3.
static bool freePackReadyCycleAndDataDepsOK(
    const ScheduleDAGMI *DAG, ArrayRef<MachineInstr *> Instrs, bool IsTop) {
  if (Instrs.size() < 2)
    return true;

  MachineFunction *MF = Instrs.front()->getMF();
  const TargetRegisterInfo *TRI =
      MF ? MF->getSubtarget().getRegisterInfo() : nullptr;
  const TargetInstrInfo *TII =
      MF ? MF->getSubtarget().getInstrInfo() : nullptr;
  // Available-cycle detect at MI level (def-before-use cannot share a cycle).
  if (haydn::bundle::cycleMembersHaveTrueRAW(Instrs, TRI)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: free multi-MI fails avail-cycle "
                         "detect (schedule-order true RAW) — refuse\n");
    return false;
  }
  // SET_HWLOOP trip/Off sample under snapshot no-forwarding: refuse free pack
  // with any same-cycle producer of those regs (remat ADDI+SET peel).
  if (TII && haydn::bundle::cycleMembersHaveHwloopTripConflict(Instrs, *TII,
                                                               TRI)) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: free multi-MI SET trip/Off "
                         "conflict — refuse\n");
    return false;
  }

  if (!DAG)
    return true;

  std::optional<unsigned> AvailCycle;
  SmallVector<SUnit *, 3> SUs;
  SUs.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      return false;
    SUnit *SU = DAG->getSUnit(MI);
    if (!SU || !SU->isScheduled)
      return false;
    unsigned C = IsTop ? SU->TopReadyCycle : SU->BotReadyCycle;
    if (!AvailCycle)
      AvailCycle = C;
    else if (*AvailCycle != C) {
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: free multi-MI not same "
                           "available/ready cycle ("
                        << *AvailCycle << " vs " << C << ") — refuse\n");
      return false;
    }
    SUs.push_back(SU);
  }

  // Data Lat≥1 between coissue candidates ⇒ available-cycle invariant broken.
  for (SUnit *A : SUs) {
    for (const SDep &Succ : A->Succs) {
      if (Succ.getKind() != SDep::Data || Succ.getLatency() < 1)
        continue;
      SUnit *B = Succ.getSUnit();
      if (!B || !llvm::is_contained(SUs, B))
        continue;
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: free multi-MI Data dep "
                           "latency="
                        << Succ.getLatency()
                        << " between coissue candidates — refuse "
                           "(avail-cycle/dep-graph law)\n");
      return false;
    }
  }
  return true;
}

// exact no-split materialize for a free scheduled multi-MI cycle.
// Product coissue law (HaydnBundleMaterialize.h):
//   (1) same available/ready cycle + no Data Lat≥1 (dep graph)
//   (2) emission pack + field-order no true RAW (canCoissueProductCycle)
// Already-bundled members are never free-packed.
static void materializeExactNoSplitCycle(
    MachineBasicBlock &MBB, ArrayRef<MachineInstr *> Instrs,
    const ScheduleDAGMI *DAG, bool IsTop) {
  if (Instrs.size() < 2)
    return;

  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    if (MI->isBundledWithPred() || MI->isBundledWithSucc() || MI->isBundle()) {
      ++NumBundledFreePackRefusals;
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: free multi-MI pack touches "
                           "bundled member — refuse free pack\n");
      return;
    }
  }

  // On avail-cycle refuse (true RAW / split ReadyCycle / Data Lat≥1): leave
  // members sequential in schedule order. True RAW is already def-before-use;
  // Anti use-before-def reorder would invent undefined physreg reads.
  if (!freePackReadyCycleAndDataDepsOK(DAG, Instrs, IsTop)) {
    ++NumTrueRAWCycleRejects;
    return;
  }

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

  // Layer 3: emission pack + field-order (canCoissueProductCycle).
  if (haydn::bundle::canCoissueProductCycle(Instrs) &&
      haydn::bundle::commitExactMultiMIProductCycle(Instrs)) {
    ++NumMultiMIBundlesFinalized;
    return;
  }

  // Same ReadyCycle but emission/field cannot pack — leave sequential in
  // schedule order (no Anti reorder; true RAW stays def-before-use).
  LLVM_DEBUG(dbgs() << "HaydnPostRASched: same-ready-cycle multi-MI cannot "
                       "coissue — sequential in schedule order\n");
  ++NumScheduledCyclesSplit;
}

void HaydnPostRASchedStrategy::materializeBundles(
    MachineBasicBlock &MBB, SmallVector<CycleBundle> &Bundles) {
  // Port of AIE materializeEmptyBundles + applyBundles, Top zone only.
  // Cycle ownership (exact no-split Format E encode):
  // * empty cycle → NOP at rolling position (before next real cycle / term)
  // * single MI → leave standalone here; HaydnFinalizeBundle wraps + stamps
  // * 2-3 free MIs legal → shared commitExactMultiMIProductCycle
  // * 2-3 free MIs illegal → fail closed (no production greedy split)
  for (unsigned Idx = 0; Idx < Bundles.size(); ++Idx) {
    CycleBundle &CB = Bundles[Idx];
    if (CB.Instrs.empty()) {
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
      continue;

    materializeExactNoSplitCycle(MBB, CB.Instrs, DAG, /*IsTop=*/true);
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
    if (std::optional<unsigned> AltOpcode = AltDescs.getSelectedOpcode(&MI)) {
      // AIE setDesc is unconditional (members share logical operand
      // shape). Haydn Format E members drop tied acc / vestigial uses;
      // rewrite from the keep-map. Slot comes from the format desc, not
      // an `_S*` postfix.
      const MCSlotKind Kind = haydnDefaultMCFormats().getSlotKind(*AltOpcode);
      if ((haydn::bundle::formatECompositeSlotIsE2(Kind) ||
           haydn::bundle::formatECompositeSlotIsE3(Kind)) &&
          memberDescCompatible(MI, *AltOpcode, *HII))
        rewriteFieldSlotToMember(MI, *AltOpcode, *HII);
      else
        MI.setDesc(HII->get(*AltOpcode));
    }
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
  if (!AltDescs.empty()) {
    ++NumPostRAAltDescLeakFatals;
    report_fatal_error(
        "Haydn post-RA leaveRegion left residual alternate descriptors",
        /*GenCrashDiag=*/false);
  }
  ++NumPostRAAltDescsCleared;
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

/// Keep residual multi-member kids in schedule order after unbundle.
///
/// Product no-forwarding law: same-cycle true RAW cannot coissue, so leaveMBB
/// sequentializes those shells. Schedule order is already def-before-use for
/// true RAW; preserving it turns the illegal same-cycle forward into a legal
/// multi-cycle RAW. Do **not** Anti-reorder (use-before-def) on true RAW —
/// that places a use before its only same-shell def and creates undefined
/// physreg reads under the verifier.
///
/// WAR-shaped shells that fail field-order coissue already have use-before-def
/// in schedule order; keeping that order preserves multi-cycle WAR (reader
/// observes the pre-cycle value). Mutual cyclic Anti is not repaired here —
/// product law deletes pre-RA hard freezes that would produce it.
static void sequentializeKidsInScheduleOrder(MachineBasicBlock &MBB,
                                             ArrayRef<MachineInstr *> Kids) {
  SmallVector<MachineInstr *, 4> Order(Kids.begin(), Kids.end());
  for (unsigned I = 1; I < Order.size(); ++I) {
    MachineInstr *Prev = Order[I - 1];
    MachineInstr *Cur = Order[I];
    if (!Prev || !Cur)
      continue;
    if (std::next(Prev->getIterator()) == Cur->getIterator())
      continue;
    MBB.splice(std::next(Prev->getIterator()), &MBB, Cur->getIterator());
  }
}

/// Unbundle a multi-member root shell and sequentialize kids in schedule order.
static void sequentializeMultiMemberRoot(MachineInstr &Root,
                                         ArrayRef<MachineInstr *> Kids) {
  MachineBasicBlock &MBB = *Root.getParent();
  for (MachineInstr *K : Kids) {
    if (!K)
      continue;
    for (MachineOperand &MO : K->operands()) {
      if (MO.isReg() && MO.isInternalRead())
        MO.setIsInternalRead(false);
    }
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }
  Root.eraseFromParent();
  sequentializeKidsInScheduleOrder(MBB, Kids);
}

void HaydnPostRASchedStrategy::commitOrSequentializeUnstampedMultiMemberBundles(
    MachineBasicBlock &MBB) {
  // Residual multi-member shells at leaveMBB:
  //   * jointly legal → ordinary multi-MI commit (unstamped) or keep (stamped)
  //   * illegal (true RAW / SET trip-Off conflict / field-order fail) →
  //     sequentialize in schedule order and clear InternalRead
  //
  // Stamped Format-E multi-member is re-probed: remat glue / free pack can
  // stamp a cycle product law refuses (snapshot no-forwarding SET trip).
  // Do not trust the stamp alone. Not a hard-root freeze path.
  SmallVector<MachineInstr *, 4> Roots;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle() || MI.isBundledWithPred())
      continue;
    unsigned Kids = 0;
    for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I)
      ++Kids;
    if (Kids >= 2)
      Roots.push_back(&MI);
  }

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;

    SmallVector<MachineInstr *, 3> Kids;
    for (MachineBasicBlock::instr_iterator I = std::next(Root->getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I)
      Kids.push_back(&*I);
    if (Kids.size() < 2)
      continue;

    // Product coissue probe is the legality authority (schedule + field +
    // SET trip/Off). Illegal → sequentialize recovery preserves schedule
    // order; sequentialize itself does not invent packing legality.
    if (!haydn::bundle::canCoissueProductCycle(Kids)) {
      ++NumProductCoissueProbeRejects;
      sequentializeMultiMemberRoot(*Root, Kids);
      ++NumUnstampedMultiMemberSequentialized;
      continue;
    }

    if (haydn::bundle::getBundleRowID(*Root).has_value())
      continue; // stamped and still jointly legal — keep

    // Unstamped residual: dissolve and ordinary multi-MI commit.
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
    Root->eraseFromParent();
    if (haydn::bundle::commitExactMultiMIProductCycle(Kids)) {
      ++NumUnstampedMultiMemberCommitted;
      ++NumMultiMIBundlesFinalized;
      continue;
    }
    sequentializeKidsInScheduleOrder(MBB, Kids);
    ++NumUnstampedMultiMemberSequentialized;
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

static bool cycleConflictsScoreboard(
    const HaydnHazardRecognizer &HR,
    const ResourceScoreboard<HaydnFuncUnitWrapper> &SB,
    ArrayRef<MachineInstr *> Members) {
  for (MachineInstr *MI : Members) {
    if (HR.checkConflict(SB, *MI, /*Cycle=*/0))
      return true;
  }
  const TargetRegisterInfo *TRI = nullptr;
  if (!Members.empty() && Members.front()->getMF())
    TRI = Members.front()->getMF()->getSubtarget().getRegisterInfo();
  return haydn::bundle::cycleMembersHaveWAW(Members, TRI);
}

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
  if (DefMI.mayLoad())
    return 1;
  return 0;
}

void HaydnPostRASchedStrategy::replayMultiMemberSeamHazards(
    MachineBasicBlock &MBB,
    const SmallPtrSetImpl<MachineInstr *> &PreExistingMultiMembers) {
  // Residual multi-member seam replay only (pre-free-pack shells). Free
  // multi-MI packs are excluded; latency is DAG + LatencyStalls.
  if (MBB.empty() || PreExistingMultiMembers.empty())
    return;

  SmallPtrSet<MachineInstr *, 4> MultiMemberRoots;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle() || MI.isBundledWithPred())
      continue;
    unsigned Kids = 0;
    bool IsPre = false;
    for (MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
         I != MBB.instr_end() && I->isBundledWithPred(); ++I) {
      ++Kids;
      if (PreExistingMultiMembers.contains(&*I))
        IsPre = true;
    }
    if (Kids >= 2 && IsPre)
      MultiMemberRoots.insert(&MI);
  }
  if (MultiMemberRoots.empty())
    return;

  const MachineFunction &MF = *MBB.getParent();
  const InstrItineraryData *Itin = MF.getSubtarget().getInstrItineraryData();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  HaydnHazardRecognizer ScratchHR(HII, Itin, /*IsPreRA=*/false,
                                  /*AltDescs=*/nullptr);
  ScratchHR.Reset();

  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  const int Depth = std::max(ScratchHR.getPipelineDepth(), 1);
  SB.reset(Depth);

  enum class Side : uint8_t { PreRoot, InRoot, PostRoot };
  struct DefInfo {
    unsigned Cycle = 0;
    MachineInstr *MI = nullptr;
    unsigned OpIdx = 0;
    Side Origin = Side::PreRoot;
  };
  DenseMap<Register, DefInfo> LastDef;
  unsigned CurrCycle = 0;
  bool SeenMulti = false;
  bool PrevWasMulti = false;

  auto insertStallBefore = [&](MachineBasicBlock::iterator InsertPt) {
    HII->insertNoop(MBB, InsertPt);
    SB.advance();
    ++CurrCycle;
    ++NumMultiMemberSeamReplayStalls;
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

    const bool IsMulti = MultiMemberRoots.contains(&Head);
    const bool AtSeam = IsMulti || PrevWasMulti;

    if (AtSeam) {
      unsigned LatencyStalls = 0;
      const Side UseSide =
          IsMulti ? Side::InRoot
                  : (SeenMulti ? Side::PostRoot : Side::PreRoot);
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
            "Haydn: multi-member seam replay failed to clear Req/Res conflict",
            /*GenCrashDiag=*/false);
      }
    }

    for (MachineInstr *MI : Members)
      ScratchHR.enterResources(SB, *MI, /*DeltaCycles=*/0);

    const Side DefSide =
        IsMulti ? Side::InRoot
                : (SeenMulti ? Side::PostRoot : Side::PreRoot);
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

    if (IsMulti) {
      SeenMulti = true;
      PrevWasMulti = true;
    } else {
      PrevWasMulti = false;
    }

    SB.advance();
    ++CurrCycle;
    It = Next;
  }
}
