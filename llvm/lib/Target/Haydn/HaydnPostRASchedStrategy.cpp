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
// leaveMBB free-packs scheduled multi-MI via commitOneProductCycle
// (sole scheduled multi-MI producer; AIE applyBundles size()>1 peer at
// AIEHazardRecognizer.cpp:326-352), commits residual unstamped multi-member
// shells with the same ordinary multi-MI path when jointly legal (else
// sequentializes as recovery only), and replays multi-member parcel seam
// latency. Sequentialize is not a packing legality authority. No hard-root
// freeze identity and no force-coissue. Post-RA never invents SMS stages.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRASchedStrategy.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnSchedMutations.h"
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
STATISTIC(NumAuctionScoreCalls,
          "Number of ready-subset auction score requests (post-SU-sweep-cache)");
STATISTIC(NumAuctionOpcodeMemoHits,
          "Number of auction scores served by the opcode-multiset memo");
STATISTIC(NumAuctionSolves,
          "Number of auction scores that ran the subset/permutation solve");
STATISTIC(NumInterZonePadCaps,
          "Number of Top/Bot seam pads that hit the published occupancy "
          "horizon (T4 hang-containment; continue, do not loop)");
STATISTIC(NumBotScoreboardBundleReplays,
          "Number of scheduled successor cycle members replayed into the "
          "post-RA Bot scoreboard (inter-block; default-off)");

static cl::opt<bool> EnableHaydnPostRAReadySubsetAuction(
    "haydn-postra-ready-subset-auction", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: rank tryCandidate by bounded ready-subset cycle "
             "auction (maximize issued ops under exact product matching)"));

// Full 8-ready enumeration is 2^8·3! exact solves per tryCandidate compare.
// Pathological ILP regions can still dominate compile time even after the
// FormatE lookup memo; skip the auction entirely above this Available count.
static cl::opt<unsigned> HaydnPostRAAuctionSkipReady(
    "haydn-postra-auction-skip-ready", cl::init(24), cl::Hidden,
    cl::desc("Skip the subset auction when Available exceeds this count"));

// AIE handleRegionConflicts (AIEMachineScheduler.cpp:1192-1195) is
// unbounded. Dense MAC bodies can leave TopReadyCycle / CurrCycle far
// ahead of the reconstructed list and the while never returns. Bound
// pads to the published occupancy horizon (SIN_COS uimm4+2 = 17) plus
// scoreboard slack. Residual seam conflict after the cap continues —
// fatal would re-stick the product path. Peer loop stays the shape.
static constexpr unsigned HaydnPostRAMaxInterZonePads =
    HAYDN_SINCOS_OCCUPANCY_MAX + 16;

// legality + multi-MI MIR commit live on HaydnBundleMaterialize
// (instrsFormOneLegalCycle / commitOneProductCycle). PostRA owns
// region grouping, free multi-MI exact commit, residual unstamped multi-member
// ordinary commit/sequentialize, and multi-member seam latency replay.

/// Count multi-member TargetOpcode::BUNDLE roots in \p MBB (hard cycle
/// groups from SMS handoff / architectural rematch / late multipass).
static unsigned countMultiMemberHardRoots(MachineBasicBlock &MBB) {
  unsigned Count = 0;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle())
      continue;
    if (haydn::bundle::members(MI).size() >= 2)
      ++Count;
  }
  return Count;
}

HaydnPostRASchedStrategy::~HaydnPostRASchedStrategy() = default;

HaydnPostRASchedStrategy::HaydnPostRASchedStrategy(const MachineSchedContext *C)
    : PostGenericScheduler(C), Ctx(C),
      LegalMemo(std::make_unique<haydn::bundle::AuctionAnyOrderLegalMemo>()) {
  // Cache the Haydn TII from the MachineFunction's subtarget. enterMBB runs
  // BEFORE the base scheduler calls initialize(DAG), so the DAG member is
  // still null when enterMBB first fires — get TII from the context instead.
  // (ScheduleDAGMI::startBlock -> SchedImpl->enterMBB happens before any
  // region's initialize; see MachineScheduler.cpp:824 vs :858/.)
  HII = static_cast<const HaydnInstrInfo *>(C->MF->getSubtarget().getInstrInfo());

  // W68.2R S2 reopen (STATUS limit #1, contracts/pipeline.md S1/S2 repair
  // law): the FIRST S2 invocation on this function (per-function MFI
  // invocation counter == 2: S1 at addPreSched2 was 1) rebuilds from
  // current bare MIs — never treats S1's committed BUNDLEs as immutable
  // final choices. Every provisional BUNDLE whose real children all
  // carry generated member->logical identity is dissolved and its
  // children canonicalized back to logical opcodes; unrecoverable roots
  // stay committed (S2 schedules around them).
  //
  // ONLY the first S2 invocation reopens. Later convergence-loop
  // iterations (invocation 3+) schedule the BUNDLEs their OWN previous
  // S2 committed; reopening those would break the driver's fixed-point
  // argument (a repack of a repack can oscillate — G003's bound relies
  // on pre-existing multi-member roots pinning their cycles after the
  // first repair pass). The strategy is constructed once per scheduler
  // invocation (PostMachineSchedulerImpl::run ->
  // createPostMachineScheduler), before any enterMBB/region.
  if (C && C->MF) {
    auto &MFI = *C->MF->getInfo<HaydnMachineFunctionInfo>();
    if (MFI.bumpPostRASchedInvocation() == 2) {
      // TargetInstrInfo IS-A MCInstrInfo (public inheritance) — the
      // subtarget's instr info serves directly (AsmPrinter idiom).
      const TargetInstrInfo &TII = *C->MF->getSubtarget().getInstrInfo();
      unsigned Reopened = haydn::bundle::reopenProvisionalBundles(*C->MF, TII);
      (void)Reopened;
      LLVM_DEBUG(dbgs() << "HaydnPostRASched S2: reopened " << Reopened
                        << " provisional BUNDLE root(s) in "
                        << C->MF->getName() << "\n");
    }
  }
}

static void gatherHaydnInterBlockEdges(
    const MachineSchedContext *C,
    DenseMap<const MachineBasicBlock *,
             SmallVector<std::unique_ptr<HaydnInterBlockEdges>, 2>> &ByPred) {
  MachineFunction &MF = *C->MF;
  const auto &ST = MF.getSubtarget();
  const auto *TII = ST.getInstrInfo();
  const auto *TRI = ST.getRegisterInfo();
  for (MachineBasicBlock &Pred : MF) {
    for (MachineBasicBlock *Succ : Pred.successors()) {
      if (Succ == &Pred)
        continue; // self-edges: loop recurrence, not a cross-block edge
      auto Edges = std::make_unique<HaydnInterBlockEdges>(*C, &Pred, Succ);
      Edges->reserveForBlocks(Pred, *Succ);
      auto PredEnd = Pred.getFirstTerminator();
      auto SuccEnd = Succ->getFirstTerminator();
      // Pre-boundary: Pred's real instructions below its terminators;
      // post-boundary: Succ's real instructions above its terminators.
      // Terminators carry no cross-boundary data dependence.
      for (auto It = Pred.begin(); It != PredEnd; ++It)
        if (!It->isTerminator() && !It->isPosition())
          Edges->addNode(&*It);
      Edges->markBoundary();
      for (auto It = Succ->begin(); It != SuccEnd; ++It)
        if (!It->isTerminator() && !It->isPosition())
          Edges->addNode(&*It);
      Edges->buildCrossBoundaryEdges(C->AA, TII, TRI,
                                     &Edges->getSchedModelRef());
      // S2 seeding: if the successor already carries S1's committed
      // BUNDLEs, seed each post-boundary MI's depth from its bundle index
      // (cycle 0 = first bundle). A fresh S1 pass finds no bundles and
      // keeps static depths.
      if (llvm::any_of(*Succ, [](const MachineInstr &MI) {
            return MI.isBundle();
          })) {
        int Cycle = -1;
        for (MachineInstr &MI : *Succ) {
          if (MI.isBundle()) {
            // Bundle roots start a new cycle; members share it.
            if (!MI.isBundledWithPred())
              ++Cycle;
            Edges->recordPostDepth(&MI, Cycle);
          }
        }
      }
      ByPred[&Pred].push_back(std::move(Edges));
    }
  }
}

// True if MI is not a cycle member during post-RA bundle reconstruction.
// Forward-declared here for tryCandidate ready filtering; definition below.
static bool isBundleSkippable(const MachineInstr &MI);
static void collectCycleMembers(MachineInstr &Head,
                                SmallVectorImpl<MachineInstr *> &Members);

/// Build BaseOpcodes from the live HR current-cycle preferred matching and
/// ReadyOpcodes with Focus first, then other Available (non-skippable) ops.
/// Returns auction fill score for Focus (IssuedCount of densest legal subset
/// that includes Focus). High-ILP Available skips the auction.
///
/// Memoized (CB-153a). The auction result used here is ONLY IssuedCount of
/// the best subset that contains the focus. Base stays in sequence order in
/// the key (it is not permuted by the auction).
static unsigned scoreReadySubsetAuction(
    SchedBoundary &Zone, SUnit *Focus,
    std::unordered_map<std::vector<unsigned>, unsigned,
                       HaydnPostRASchedStrategy::AuctionScoreKeyHash> &Memo,
    haydn::bundle::AuctionAnyOrderLegalMemo &LegalMemo) {
  if (!Focus || !Focus->getInstr() || !Zone.HazardRec ||
      !Zone.HazardRec->isEnabled())
    return 0;

  auto *HR = static_cast<HaydnHazardRecognizer *>(Zone.HazardRec);

  SmallVector<unsigned, 3> Base;
  const haydn::bundle::CycleState &Pref =
      haydn::bundle::selectPreferredCandidate(
          HR->getCurrentCycleCandidates());
  for (const haydn::bundle::CycleMember &M : Pref.Members)
    Base.push_back(M.LogicalOpcode);
  if (Base.size() >= Haydn::ISSUE_SLOT_COUNT)
    return Base.size();

  if (Zone.Available.size() > HaydnPostRAAuctionSkipReady) {
    return HR->getHazardType(Focus, /*DeltaCycles=*/0) ==
                   ScheduleHazardRecognizer::NoHazard
               ? 1u
               : 0u;
  }

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

  // Canonical key: Base in order | ~0u separator | focus | sorted rest.
  std::vector<unsigned> Key;
  Key.reserve(Base.size() + 1 + Ready.size());
  Key.assign(Base.begin(), Base.end());
  Key.push_back(~0u);
  Key.push_back(Ready[0]);
  {
    const size_t RestAt = Key.size();
    Key.insert(Key.end(), Ready.begin() + 1, Ready.end());
    std::sort(Key.begin() + RestAt, Key.end());
  }

  ++NumAuctionScoreCalls;
  auto It = Memo.find(Key);
  if (It != Memo.end()) {
    ++NumAuctionOpcodeMemoHits;
    return It->second;
  }

  ++NumAuctionSolves;
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  const unsigned Score =
      haydn::bundle::auctionFocusFillScoreOnly(Base, Ready, Fmts, &LegalMemo);
  Memo.emplace(std::move(Key), Score);
  return Score;
}

SUnit *HaydnPostRASchedStrategy::pickNode(bool &IsTopNode) {
  // One pickNode = cycle settle (pickOnlyChoice / pending release) followed by
  // the candidate sweep(s); HR state and each zone's Available set are
  // constant across the sweeps, so per-(SU, zone) scores are fixed here.
  SweepSUScore[0].clear();
  SweepSUScore[1].clear();
  SweepSUScoreCycle[0] = SweepSUScoreCycle[1] = ~0u;
  ReadyAuctionScoreCache.clear();
  return PostGenericScheduler::pickNode(IsTopNode);
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
    SchedBoundary &CandZone = Cand.AtTop ? Top : Bot;
    auto scoreOnce = [&](SchedBoundary &Z, const SchedCandidate &C) {
      const unsigned ZIdx = C.AtTop ? 1 : 0;
      auto &M = SweepSUScore[ZIdx];
      if (SweepSUScoreCycle[ZIdx] != Z.getCurrCycle()) {
        M.clear();
        SweepSUScoreCycle[ZIdx] = Z.getCurrCycle();
      }
      auto It = M.find(C.SU);
      if (It != M.end())
        return It->second;
      const unsigned S =
          scoreReadySubsetAuction(Z, C.SU, AuctionScoreCache, *LegalMemo);
      M.try_emplace(C.SU, S);
      return S;
    };
    const unsigned TryScore = scoreOnce(Zone, TryCand);
    const unsigned CandScore = scoreOnce(CandZone, Cand);
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
  }

  // Auction-tie among equal-cost ready ops: keep region NodeOrder so the
  // first issued SU is the first in the MBB. Preferred materialize then
  // binds that SU to the highest free field. Do not override a height/
  // depth difference — generic tryLatency still owns the critical path
  // (post-call ADD vs R0 re-zero).
  if (TryCand.SU->getHeight() == Cand.SU->getHeight() &&
      TryCand.SU->getDepth() == Cand.SU->getDepth() &&
      TryCand.SU->NodeNum != Cand.SU->NodeNum) {
    if (TryCand.SU->NodeNum < Cand.SU->NodeNum) {
      TryCand.Reason = NodeOrder;
      return true;
    }
    return false;
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

static unsigned clampForwardCycle(unsigned From, unsigned To) {
  if (To <= From)
    return From;
  if (To - From > HaydnPostRAMaxInterZonePads)
    return From + HaydnPostRAMaxInterZonePads;
  return To;
}

void HaydnPostRASchedStrategy::bumpCycleForBundles(
    unsigned ToCycle, SmallVectorImpl<CycleBundle> &Bundles,
    CycleBundle &CurrBundle) {
  // Push the in-progress bundle as the current cycle, then pad with empty
  // bundles until reaching ToCycle. Mirrors AIE's bumpCycleForBundles
  // (AIEMachineScheduler.cpp:133-154). Invariant: Bundles.size == current
  // cycle index. Cap the forward delta so a UINT_MAX ReadyCycle cannot
  // allocate an unbounded empty-cycle list (T4 hang-root).
  unsigned CurrCycle = Bundles.size();
  ToCycle = clampForwardCycle(CurrCycle, ToCycle);
  if (ToCycle == CurrCycle)
    return;
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
  AuctionScoreCache.clear();
  LegalMemo->Map.clear();
  ReadyAuctionScoreCache.clear();
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
    if (!ResourceAdmissionPinned) {
      ResourceAdmissionPinned = true;
      if (!haydnAvailabilityAwareConsumePinsHold())
        report_fatal_error(
            "Haydn post-RA product resource admission pins failed",
            /*GenCrashDiag=*/false);
      // W68.2: build the inter-block DDGs once, before any block schedules.
      // Target-owned conservative construction (HC#0 declined): cross-
      // boundary register RAW/WAR/WAW + memory edges over-approximate, so
      // the effective-latency cut can only under-cut, never invent.
      if (haydnInterBlockEnabled()) {
        // S2 (second invocation) inherits S1's recorded depths: the
        // per-function owning registry (HaydnMachineFunctionInfo) merges
        // fresh graphs under the same keys and keeps depth state when a
        // key re-publishes without records.
        HaydnIBEdgesByPredMap Fresh;
        gatherHaydnInterBlockEdges(Ctx, Fresh);
        setHaydnInterBlockEdgesForFunction(*Ctx->MF, &Fresh);
      } else {
        setHaydnInterBlockEdgesForFunction(*Ctx->MF, nullptr);
      }
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

void HaydnPostRASchedStrategy::initialize(ScheduleDAGMI *Dag) {
  PostGenericScheduler::initialize(Dag);
  RegionWasScheduled = false;
  // Bot HR is (re)created in the base initialize; replay after that.
  initializeBotScoreBoard();
}

bool HaydnPostRASchedStrategy::isBottomRegion() const {
  // AIE MaxLatencyFinder.cpp:67-76. The last region of the MBB is the one
  // that meets successors; earlier regions must not consume Bot occupancy.
  if (!CurrentMBB || !DAG)
    return false;
  MachineInstr *ExitMI = DAG->ExitSU.getInstr();
  if (!ExitMI)
    return true;
  MachineBasicBlock::instr_iterator It(ExitMI);
  return std::next(It) == CurrentMBB->instr_end();
}

void HaydnPostRASchedStrategy::initializeBotScoreBoard() {
  // AIE AIEPostRASchedStrategy::initializeBotScoreBoard
  // (AIEMachineScheduler.cpp:260-405). Haydn overlay: unscheduled/unknown
  // successors keep full latency. Never static-depth-fill a successor that
  // has no recorded S1 schedule — that would invent a cut.
  if (!haydnInterBlockEnabled() || !CurrentMBB || !DAG)
    return;
  auto *BotHR = static_cast<HaydnHazardRecognizer *>(Bot.HazardRec);
  if (!BotHR || !BotHR->isEnabled())
    return;
  if (!isBottomRegion())
    return;

  if (CurrentMBB->succ_empty()) {
    LLVM_DEBUG(dbgs() << "HaydnPostRASched: Bot scoreboard skip bb."
                      << CurrentMBB->getNumber()
                      << " (no successors; full latency)\n");
    return;
  }
  for (const MachineInstr &T : CurrentMBB->terminators()) {
    if (T.isIndirectBranch()) {
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: Bot scoreboard skip bb."
                        << CurrentMBB->getNumber()
                        << " (unknown successors; full latency)\n");
      return;
    }
  }

  MachineFunction &MF = *CurrentMBB->getParent();
  SmallVector<MachineBasicBlock *, 4> ReplaySuccs;
  for (MachineBasicBlock *Succ : CurrentMBB->successors()) {
    if (Succ == CurrentMBB)
      continue; // self-edge: loop recurrence, not a scheduled successor
    if (!haydnSuccHasS1Depths(MF, Succ)) {
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: Bot scoreboard skip bb."
                        << CurrentMBB->getNumber() << " -> bb."
                        << Succ->getNumber()
                        << " (unscheduled successor; full latency)\n");
      return;
    }
    ReplaySuccs.push_back(Succ);
  }
  if (ReplaySuccs.empty())
    return;

  const int Depth =
      std::max(std::max(BotHR->getPipelineDepth(),
                        static_cast<int>(BotHR->getMaxLookAhead())),
               1);
  LLVM_DEBUG(dbgs() << "HaydnPostRASched: Bot scoreboard replay bb."
                    << CurrentMBB->getNumber() << " depth=" << Depth << "\n");

  // Insert successor cycle C at C-Depth so RecedeCycle(Depth+1) leaves
  // successor cycle 0 at scoreboard[+1] (AIE AlignScoreboardToCycleOne).
  unsigned Replayed = 0;
  for (MachineBasicBlock *Succ : ReplaySuccs) {
    int Cycle = 0;
    for (MachineInstr &MI : *Succ) {
      if (MI.isBundledWithPred())
        continue;
      if (MI.isDebugInstr() || MI.isPosition() || MI.isCFIInstruction() ||
          MI.isKill() || MI.isImplicitDef() || MI.isCopy() || MI.isPHI() ||
          MI.isLifetimeMarker())
        continue;
      if (Cycle >= Depth)
        break;
      SmallVector<MachineInstr *, 3> Members;
      collectCycleMembers(MI, Members);
      for (MachineInstr *M : Members) {
        SUnit Tmp(M, /*NodeNum=*/0);
        BotHR->emitInstruction(&Tmp, Cycle - Depth);
        ++Replayed;
        ++NumBotScoreboardBundleReplays;
      }
      ++Cycle;
    }
    LLVM_DEBUG(dbgs() << "  replayed bb." << Succ->getNumber()
                      << " through cycle " << Cycle << "\n");
  }

  if (!Replayed)
    return;
  for (int I = 0; I < Depth + 1; ++I)
    BotHR->RecedeCycle();
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
  // Product multi-MI: free scheduled packs only via commitOneProductCycle,
  // then residual unstamped multi-member shells through the same site.
  // Sequentialize is recovery after the product coissue probe rejects —
  // not a second packing authority. Multi-member seam latency replay is
  // not a hard-root freeze path.
  if (CurrentMBB) {
    // Snapshot multi-member children present before free pack. Seam latency
    // replay applies only to residual shells (and their ordinary multi-MI
    // recommits), not free scheduled multi-MI packs created this leaveMBB.
    SmallPtrSet<MachineInstr *, 8> PreExistingMultiMembers;
    for (MachineInstr &MI : *CurrentMBB) {
      if (!MI.isBundle() || MI.isBundledWithPred())
        continue;
      SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(MI);
      if (Kids.size() >= 2)
        PreExistingMultiMembers.insert(Kids.begin(), Kids.end());
    }
    if (!MBBBundles.empty()) {
      NumPostRAScheduledCyclesAudited += MBBBundles.size();
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: emitted-cycle audit bb."
                        << CurrentMBB->getNumber()
                        << " cycles=" << MBBBundles.size() << "\n");
      materializeBundles(*CurrentMBB, MBBBundles);
        // W68.2 S1 depth feed: record each materialized instruction's issue
  // cycle into every inter-block DDG where it is post-boundary, so the S2
  // pass's effective-latency cut uses scheduled (not static) depths. AIE
  // records these during its fixpoint replay (recordPostDepth family).
  if (haydnInterBlockEnabled())
    if (HaydnIBEdgesByPredMap *Reg =
            haydnGetInterBlockEdgesRegistry(*CurrentMBB->getParent()))
      for (const auto &[PredBB, Edges] : *Reg)
        for (auto &E : Edges)
          if (E->getSucc() == CurrentMBB)
            for (unsigned C = 0; C < MBBBundles.size(); ++C)
              for (MachineInstr *MI : MBBBundles[C].Instrs)
                E->recordPostDepth(MI, (int)C);
MBBBundles.clear();
    }
    commitOrSequentializeUnstampedMultiMemberBundles(*CurrentMBB);
    // Own only the current MBB. Predecessor re-probe after leave was a
    // wrong-layer repair for post-pipeliner mutating already-left MBBs;
    // post-pipeliner must preflight/commit whole-loop itself (no
    // cross-MBB callback repair).
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
      // Post-RA should not hit this; keep progress monotonic. Also cap a
      // huge ReadyCycle so reconstruction cannot hang (T4 hang-root).
      if (EmitCycle < Bundles.size())
        EmitCycle = Bundles.size();
      else
        EmitCycle = clampForwardCycle(Bundles.size(), EmitCycle);

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
  // cycles so the reconstructed list covers the full scheduled window. Cap
  // a runaway CurrCycle (T4 hang-root).
  if (Zone.getCurrCycle() != Bundles.size())
    bumpCycleForBundles(
        clampForwardCycle(Bundles.size(), Zone.getCurrCycle()), Bundles,
        CurrBundle);

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
// Returns false if a skippable has a reg conflict with an instruction of the
// remaining (not-yet-spliced) window set (unsafe), or if an opaque INLINEASM
// boundary lies strictly inside the multi-MI span (compiler BUNDLE must never
// cross INLINEASM — fail closed, leave sequential).
//
// W22 / CR-S2 (scheduling F4 family): the conflict oracle is the REMAINING
// (not-yet-spliced) set of the cycle window — every real member plus every
// window instruction that stays in place (non-member reals, refused
// skippables, INLINEASM boundaries). A splice hoists MI above First and
// therefore across every stayer between them, so the splice is refused when
// MI conflicts with ANY stayer in any direction:
//   * MI reads    a reg a stayer defines (RAW — hoisted reader would copy
//     the stale pre-producer value),
//   * MI defines  a reg a stayer reads   (WAR — stayer would observe the
//     spliced writer's new value),
//   * MI defines  a reg a stayer defines (WAW — window-final value swaps).
// The pre-W22 member-only oracle let a COPY reading a def that STAYS in the
// window (a WAR-refused skippable, an INLINEASM, a bundled kid) pass the
// member check and be hoisted above its producer — silent wrong code. One
// closed condition over the remaining set; no per-opcode cases.
static bool spliceSkippablesForCycle(MachineBasicBlock &MBB,
                                     ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return true;
  MachineInstr *First = Instrs.front();
  MachineInstr *Last = Instrs.back();
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  bool BundleUnsafe = false;
  // Remaining (not-yet-spliced) set. Seeded with every real member: members
  // never splice, and the F4 law (no splice across a member def/use
  // boundary) is the member subset of the same closed condition.
  SmallVector<MachineInstr *, 8> Staying(Instrs.begin(), Instrs.end());
  for (MachineBasicBlock::instr_iterator It = First->getIterator(),
                                         E = Last->getIterator();
       It != E;) {
    MachineInstr &MI = *It;
    ++It;
    if (&MI == First)
      continue;
    // INLINEASM is a layout/scheduling boundary, not movable glue. Presence
    // between same-cycle members would mean a BUNDLE crossing the opaque
    // boundary — refuse the multi-MI pack rather than splice it aside. It
    // also stays in the window, so later skippables may not cross it.
    if (MI.isInlineAsm()) {
      BundleUnsafe = true;
      Staying.push_back(&MI);
      continue;
    }
    // Real (non-skippable) window instructions stay in place — cycle members
    // (already seeded) and non-member reals alike.
    if (!isBundleSkippable(MI)) {
      if (!is_contained(Instrs, &MI))
        Staying.push_back(&MI);
      continue;
    }
    // Do not splice meta/COPY across a remaining def/use boundary. A COPY
    // that only *reads* a reg defined by a stayer (member or not) must not
    // be hoisted above its producer (stale value).
    bool HasRegConflict = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      for (const MachineInstr *StayMI : Staying) {
        if (StayMI == &MI)
          continue;
        if (MO.isDef() && (StayMI->readsRegister(Reg, TRI) ||
                           StayMI->definesRegister(Reg, TRI))) {
          HasRegConflict = true;
          break;
        }
        if (MO.isUse() && StayMI->definesRegister(Reg, TRI)) {
          HasRegConflict = true;
          break;
        }
      }
      if (HasRegConflict)
        break;
    }
    if (HasRegConflict) {
      BundleUnsafe = true;
      Staying.push_back(&MI);
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

  // Layer 3: one production commit site (as-is generated members or
  // rematch/bake). AIE applyBundles (AIEHazardRecognizer.cpp:326-352)
  // packs already-setDesc members by getSlotKind inside that site —
  // PostRA must not open a second bake path.
  if (haydn::bundle::commitOneProductCycle(Instrs)) {
    ++NumMultiMIBundlesFinalized;
    return;
  }

  // Same ReadyCycle but emission/field cannot pack — leave sequential in
  // schedule order (no Anti reorder; true RAW stays def-before-use).
  LLVM_DEBUG({
    dbgs() << "HaydnPostRASched: same-ready-cycle multi-MI cannot "
              "coissue — sequential in schedule order:\n";
    for (const MachineInstr *MI : Instrs)
      dbgs() << "    " << *MI;
  });
  ++NumScheduledCyclesSplit;
}

void HaydnPostRASchedStrategy::materializeBundles(
    MachineBasicBlock &MBB, SmallVector<CycleBundle> &Bundles) {
  // Port of AIE materializeEmptyBundles + applyBundles, Top zone only.
  // Cycle ownership (exact no-split Format E encode):
  // * empty cycle → NOP at rolling position (before next real cycle / term)
  // * single MI → leave standalone here; HaydnFinalizeBundle wraps + stamps
  // * 2-3 free MIs legal → commitOneProductCycle (one bake site)
  // * 2-3 free MIs illegal → leave sequential (recovery, not a pack law)
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
      if (haydn::bundle::formatECompositeSlotIsE2(Kind) ||
          haydn::bundle::formatECompositeSlotIsE3(Kind)) {
        // Keep-map rewrite only. Raw setDesc on a 5-op CB / 3-op WBARWUA
        // logical leaves an imm in a register slot (cbr_sel vs dest2).
        if (memberDescCompatible(MI, *AltOpcode, *HII))
          rewriteFieldSlotToMember(MI, *AltOpcode, *HII);
      } else
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
  if (ExitReadyCycle > TopFinalCycle) {
    const unsigned Want = ExitReadyCycle - Bot.getCurrCycle();
    Top.bumpCycle(clampForwardCycle(Top.getCurrCycle(), Want));
  }

  // Pad NOPs between Top and Bot until scoreboards do not overlap and all
  // Bot TopReadyCycle deps are met. Each Top.bumpCycle advances Top's HR
  // (SchedBoundary::bumpCycle → AdvanceCycle) so multi-cycle FU tails slide
  // past the Bot window. AIE's peer loop is unbounded
  // (AIEMachineScheduler.cpp:1192-1195); Haydn caps it so dense MAC
  // bodies cannot hang post-RA (T4 hang-root). Continue after the cap —
  // fatal would re-stick the product path.
  unsigned Guard = 0;
  while (checkInterZoneConflicts(BotBundles) &&
         Guard < HaydnPostRAMaxInterZonePads) {
    LLVM_DEBUG(dbgs() << "  handleRegionConflicts: Bump Top cycle\n");
    Top.bumpCycle(Top.getCurrCycle() + 1);
    ++Guard;
  }
  if (Guard >= HaydnPostRAMaxInterZonePads &&
      checkInterZoneConflicts(BotBundles)) {
    ++NumInterZonePadCaps;
    LLVM_DEBUG(dbgs() << "  handleRegionConflicts: pad cap "
                      << HaydnPostRAMaxInterZonePads << " — continue\n");
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
  // AIE InterBlockScheduling (AIEInterBlockScheduling.cpp, ~59K+17K) is a
  // separate unbounded port (post-QUALIFY). This leaveRegion has no
  // cross-block gate. successorsAreScheduled (AIEMachineScheduler.cpp:251-258)
  // is the first brick and stays conservative (unknown / empty succs =
  // not scheduled). SWPSolver is not a leaveRegion concern (Z3 unavailable).
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
  // The product coissue probe is the only legality authority (HR getHazardType
  // consults the same predicate so scheduled cycles are not packed then
  // sequentialized). Sequentialize is residual-shell recovery after that
  // probe rejects — it must not invent a pack. Stamped Format-E multi-member
  // is re-probed: remat glue / free pack can stamp a cycle product law
  // refuses (snapshot no-forwarding SET trip). Do not trust the stamp
  // alone. Not a hard-root freeze path.
  SmallVector<MachineInstr *, 4> Roots;
  for (MachineInstr &MI : MBB) {
    if (!MI.isBundle() || MI.isBundledWithPred())
      continue;
    if (haydn::bundle::members(MI).size() >= 2)
      Roots.push_back(&MI);
  }

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;

    SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(*Root);
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

    // A row stamp is not a committed product cycle while any child is
    // still a logical with a generated member form (AIE applyBundles
    // always setDesc's; AIEHazardRecognizer.cpp:326-352). Rebake through
    // the one commit site. Keep only when every child is already a
    // generated member.
    bool KidsAreGeneratedMembers = true;
    for (MachineInstr *K : Kids) {
      const unsigned Opc = K->getOpcode();
      if (haydn::format_e::logicalOpcodeOrSelf(Opc) == Opc &&
          haydn::bundle::lateProductMemberOpcode(Opc) != Opc) {
        KidsAreGeneratedMembers = false;
        break;
      }
    }
    if (haydn::bundle::getBundleRowID(*Root).has_value() &&
        KidsAreGeneratedMembers)
      continue;

    // Unstamped residual: dissolve, then the one bake site.
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
    if (haydn::bundle::commitOneProductCycle(Kids)) {
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
    for (MachineInstr *K : haydn::bundle::members(Head)) {
      if (!isBundleSkippable(*K))
        Members.push_back(K);
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
    SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(MI);
    bool IsPre = llvm::any_of(
        Kids, [&PreExistingMultiMembers](const MachineInstr *K) {
          return PreExistingMultiMembers.contains(K);
        });
    if (Kids.size() >= 2 && IsPre)
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
