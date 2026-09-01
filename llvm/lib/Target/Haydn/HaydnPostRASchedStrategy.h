//===-- HaydnPostRASchedStrategy.h - Haydn post-RA scheduler strategy -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA MachineScheduler strategy for the Haydn VLIW datapath.
//
// Phase B1 (Stream B, ): a thin subclass of PostGenericScheduler
// that delegates all scheduling decisions to the base. Its sole purpose in
// Phase B1 was to be a concrete strategy type the framework could instantiate
// via createSchedPostRA<HaydnPostRASchedStrategy> — the resource awareness
// comes from the HaydnHazardRecognizer, which the base PostGenericScheduler
// picks up automatically via TargetInstrInfo::CreateTargetMIHazardRecognizer
// (MachineScheduler.cpp:4295-4300).
//
// Bundle formation lives in this strategy. enterMBB stashes CurrentMBB and
// counts multi-member BUNDLE roots at entry (product expects zero — pre-RA
// SMS never freezes multi-member BUNDLE). leaveRegion reconstructs Top/Bot
// zones into an in-memory cycle list, runs handleRegionConflicts, then
// merges. leaveMBB free-packs scheduled multi-MI via
// commitOneProductCycle (sole scheduled multi-MI producer; AIE
// applyBundles size()>1 peer at AIEHazardRecognizer.cpp:325-352), commits
// residual unstamped multi-member shells with the same ordinary multi-MI
// path when the product coissue probe says they are jointly legal. Illegal
// shells sequentialize in schedule order as recovery only — sequentialize
// is not a packing legality authority. Replays multi-member parcel seam
// latency. No hard-root dissolve identity and no force-coissue.
// Dual-load packing is HR exactTryAddProduct → identity setDesc members.
// Closed singletons receive the ProductDefaultRowID member here so
// Finalize is construction-only (no late tryAdd chooser).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H

#include "HaydnInterBlockScheduling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ScheduleDAG.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace llvm {

class HaydnInstrInfo;
class MachineInstr;

namespace haydn {
namespace bundle {
struct AuctionAnyOrderLegalMemo;
} // namespace bundle
} // namespace haydn

// Post-RA scheduler strategy that forms VLIW bundles in leaveRegion/leaveMBB.
// Bundling is driven by the scheduled SUs' zone-local ready cycles
// (TopReadyCycle for Top-scheduled, BotReadyCycle for Bot-scheduled) and the
// HaydnHazardRecognizer resource model. Honours the normal LLVM
// -misched-postra-direction modes (topdown / bottomup / bidirectional).
class HaydnPostRASchedStrategy : public PostGenericScheduler {
public:
  const MachineSchedContext *Ctx = nullptr;

  HaydnPostRASchedStrategy(const MachineSchedContext *C);

  ~HaydnPostRASchedStrategy() override;

  // After the base HR construction, replay scheduled successor bundles into
  // the Bot scoreboard (AIE initializeBotScoreBoard,
  // AIEMachineScheduler.cpp:260-405). Sole replay seat: first successor
  // seeds BotHR, later successors ScratchHR+maxMergeSB (scoreboard and dest
  // remaining). Unscheduled/unknown successors stay full-latency.
  void initialize(ScheduleDAGMI *Dag) override;

  // Stash CurrentMBB for leaveMBB materialize (DAG BB is not publicly
  // accessible). Count multi-member BUNDLE roots at entry for metrics only
  // (product expects 0). Dual-load packing is HR alts tryAdd → identity setDesc.
  void enterMBB(MachineBasicBlock *MBB) override;

  // Override tryCandidate: (1) bounded ready-subset cycle auction ranks denser
  // co-issue fills first (issue-width-3 exact product subsets of Available +
  // current cycle base); (2) prefer memory as the cycle's first issue to hide
  // load latency while still allowing a ready MAC/ALU to beat a non-load
  // once a load is already best — dual-load + MAC co-issue for hot-loop fill.
  // Sequentialize after a failed product commit stays recovery-only.
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand) override;

  // Same-base dual-load hold plus D1.9 store/nop stall: keep a pure load
  // pending while a nearby independent same-base partner is still waiting
  // on already-ready barrier stores, and keep a proven-disjoint same-base
  // store from occupying the idle cycle after another same-base store
  // while a nearby same-base load is still unscheduled. Holds are
  // cycle-relative so pickOnlyChoice bumpCycle releases (AIE
  // AIEMachineScheduler.cpp:719-771 TopReadyCycle = CurrCycle+1). Not
  // Anti-ALU. Dual-load is not the store/load overlap law (Hexagon
  // HexagonVLIWPacketizer.cpp:1559).
  bool isAvailableNode(SUnit &SU, SchedBoundary &Zone,
                       bool VerifyReadyCycle) override;

  // Clear the per-pick SU score cache, then delegate. Within one pickNode the
  // HR cycle state and each zone's Available set are constant across the
  // candidate sweep(s) — cycle advances happen before the sweep (pickOnlyChoice
  // / release) and emission happens after pickNode returns — so an SU's
  // auction score is a fixed value there. tryCandidate re-scores the incumbent
  // on every comparison, which stays quadratic even with the opcode-key memo;
  // this cache makes each (SU, zone) score at most one real computation per
  // pick (CB-153a, second layer).
  SUnit *pickNode(bool &IsTopNode) override;

  // Mark that the current region was actually scheduled (the base drive loop
  // calls exitRegion even for skipped regions — MachineScheduler.cpp:862-866).
  void schedNode(SUnit *SU, bool IsTopNode) override {
    RegionWasScheduled = true;
    ReadyAuctionScoreCache.clear();
    noteIssuedStore(SU, IsTopNode);
    PostGenericScheduler::schedNode(SU, IsTopNode);
  }

  // Materialize the accumulated per-region bundle lists into the MBB: insert
  // a standalone NOP for each empty cycle (AIE-style cycle-level padding) and
  // a real BUNDLE MI for each non-empty cycle. Stock bundleWithPred +
  // finalizeBundle (MachineInstrBundle.h). Port of AIE commitBlockSchedule.
  void leaveMBB() override;

  // AIE materializeMultiOpcodeInstrs
  // (AIEMachineScheduler.cpp:1121-1139) — for each MI in the DAG top/bottom
  // region ranges with a selected format-member opcode in AltDescs, call
  // MI.setDesc(TII->get(*AltOpcode)) unconditionally. Identity census is
  // empty: no keep/permutation/tie rewrite. HR wrote the selection via
  // setAlternateDescriptor in commitPlacementForEmit. Ends with full
  // AltDescs.clear() (AIEMachineScheduler.cpp:1081-1082;
  // AIEAlternateDescriptors.h:74).
  void materializeMultiOpcodeInstrs();

  // Reconstruct Top+Bot cycle lists for the region just scheduled, merge in
  // MBB order, and pad through zone CurrCycle / ExitSU ready cycles. The MBB
  // is NOT mutated here; leaveMBB materializes. Invoked by
  // HaydnScheduleDAGMI::exitRegion.
  void leaveRegion(const SUnit &ExitSU);

private:
  // A single cycle's worth of instructions, in MBB order. Empty Instrs means
  // an idle cycle (materialized as a rolling-position NOP). Product format is
  // always Format E (E96TwoEntry / E96ThreeEntry from HaydnBundlePlan).
  // Multi-MI materialize stamps BundleFormatRowID + CompletionStateID imms
  // on the BUNDLE root via stampBundleCommit. Singletons receive the closed-
  // cycle identity-compatible member here; HaydnFinalizeBundle wraps and
  // stamps only (AIEFinalizeBundle peer). Post-commit placement is
  // getSlotKind on member Desc (AIEBaseMCFormats.cpp:66-75).
  struct CycleBundle {
    SmallVector<MachineInstr *, 3> Instrs;
    bool empty() const { return Instrs.empty(); }
  };

  // Bundles accumulated across all regions of the current MBB, in MBB order.
  // leaveRegion appends each region's bundles here; leaveMBB materializes
  // them and clears it.
  SmallVector<CycleBundle> MBBBundles;

public:
  struct AuctionScoreKeyHash {
    size_t operator()(const std::vector<unsigned> &V) const {
      return static_cast<size_t>(hash_combine_range(V.begin(), V.end()));
    }
  };
  // Memo for the tryCandidate ready-subset auction score. The score is the
  // IssuedCount of the densest legal subset containing the focus op, and the
  // auction tries every acceptance order of every subset, so the result is a
  // pure function of (Base sequence, focus opcode, MULTISET of the other
  // ready opcodes) — see scoreReadySubsetAuction for the key layout and the
  // order-invariance argument. tryCandidate re-ranks the whole Available
  // queue on every pick and re-scores the incumbent on every comparison,
  // which made the auction O(picks x queue x 2^Ready x perms x legality
  // solve) and 99% of a 30-second compile on straight-line thousands-of-MI
  // regions (CB-153a). Keyed on canonicalized opcode vectors, hit rate is
  // near-total there. Cleared per MBB (purity makes that hygiene, not a
  // correctness requirement).
  std::unordered_map<std::vector<unsigned>, unsigned, AuctionScoreKeyHash>
      AuctionScoreCache;

  // Any-order legality memo shared across auctions of one MBB (cleared with
  // AuctionScoreCache in enterMBB). Owned by pointer: the memo type lives in
  // HaydnBundleMaterialize.h, which this header does not pull in.
  std::unique_ptr<haydn::bundle::AuctionAnyOrderLegalMemo> LegalMemo;

  // Per-pick (SU, zone) score cache; [0] = Bot, [1] = Top. Cleared at pickNode
  // entry (an emission between picks changes the HR base) and additionally
  // tagged with the zone's CurrCycle at fill time: the base pickNode do-while
  // can re-enter pickOnlyChoice after popping an already-scheduled SU and bump
  // the cycle mid-pick, which resets the HR — a stale score must not survive
  // that. See pickNode / tryCandidate.
  SmallDenseMap<const SUnit *, unsigned, 32> SweepSUScore[2];
  unsigned SweepSUScoreCycle[2] = {~0u, ~0u};

private:

  const HaydnInstrInfo *HII = nullptr;

  /// Same availability-aware pin pre-RA / HR consume. Instance member,
  /// not a function-local static (one check per pipeline instance).
  bool ResourceAdmissionPinned = false;

  // Current MBB (stashed in enterMBB; the DAG's BB is protected and has no
  // public accessor, so the strategy tracks the block itself).
  MachineBasicBlock *CurrentMBB = nullptr;

  // True iff schedule was called for the current region (initialize sets it
  // false; schedNode sets it true). The base drive loop calls exitRegion even
  // for empty/single-MI regions that were SKIPPED (MachineScheduler.cpp:862
  // 866) — leaveRegion must not compute bundles for those, because the DAG's
  // SUnits/region iterators are stale from the previous region.
  bool RegionWasScheduled = false;

  // Per-pick memo of ready-subset auction scores. Cleared after every emit
  // so a later pick cannot reuse a stale Available/base snapshot.
  DenseMap<const SUnit *, unsigned> ReadyAuctionScoreCache;

  // Last Top-issued pure store per base (cycle + NodeNum). The empty cycle
  // immediately after that store stays idle while a nearby same-base load
  // is still unscheduled. Cleared in enterMBB.
  struct LastSameBaseStore {
    unsigned Cycle = 0;
    unsigned NodeNum = 0;
  };
  SmallDenseMap<unsigned, LastSameBaseStore, 8> LastSameBaseStoreCycle;

  // D1.30 (CB-153a): per-region index of pure-load SUnits grouped by base
  // register id, each list ascending by NodeNum. Built ONCE PER REGION in
  // initialize() — NOT enterMBB: startBlock->enterMBB runs before the
  // region's schedule()->buildSchedGraph, so DAG->SUnits do not exist yet
  // at enterMBB (cpp :202-205 comment; MachineScheduler.cpp:824/988 vs
  // :1060/:1073). enterMBB hosts the clear (LastSameBaseStoreCycle hygiene
  // pattern; skipped single-MI/empty regions never call initialize, so the
  // clear is what keeps a stale list from outliving its region's SUnits
  // vector). The index memoizes ONLY the region-static census (isPureLoad
  // + haydnMemBaseReg + NodeNum); isScheduled, suReaches, readiness, and
  // HR current-cycle contents stay per-query, so the three D1.10 seats
  // (hasNearbyUnscheduledSameBaseLoad, the isAvailableNode partner loop,
  // tryCandidate's hasTightPartner) return verdicts identical to the
  // unmemoized full-region scans. Query-time NodeNum windows are applied
  // over this window-free index — no ranking constant is baked in.
  // Members of the AuctionScoreCache/LegalMemo/LastSameBaseStoreCycle
  // memo family (:179-189, :235). Stored non-const: the partner loop
  // feeds baseAvail -> PostGenericScheduler::isAvailableNode(SUnit&,...).
  SmallDenseMap<unsigned, SmallVector<SUnit *, 4>, 8> SameBaseLoadIndex;

  void buildSameBaseLoadIndex();
  ArrayRef<SUnit *> sameBaseLoads(Register Base) const;

  void noteIssuedStore(const SUnit *SU, bool IsTopNode);
  bool hasNearbyUnscheduledSameBaseLoad(Register Base,
                                        unsigned NodeNum) const;
  bool isSameBaseStoreWithPendingLoad(const SUnit &SU) const;
  bool shouldHoldForSameBaseStoreStall(const SUnit &SU,
                                       SchedBoundary &Zone) const;

  // Push the in-progress bundle as the current cycle, then pad with empty
  // bundles until reaching \p ToCycle. Invariant: Bundles.size == current
  // cycle index. Mirrors AIE's bumpCycleForBundles. D1.26 contract: a
  // forward delta beyond HaydnPostRAMaxInterZonePads is a named fatal
  // (NumReconstructPadCapFatals) — never a silent clamp that would commit an
  // under-stalled cycle. Allocation stays bounded by the cap (T4
  // hang-containment); callers pass unclamped ready/CurrCycle values.
  static void bumpCycleForBundles(unsigned ToCycle,
                                  SmallVectorImpl<CycleBundle> &Bundles,
                                  CycleBundle &CurrBundle);

  // Port of AIE::computeAndFinalizeBundles (AIEMachineScheduler.cpp:152-228):
  // walk one SchedBoundary zone, group by zone-local ready cycle, flush the
  // last non-empty cycle, sync the zone CurrCycle, and pad empty cycles to
  // that final CurrCycle. Bot zone is reversed to MBB order on return.
  SmallVector<CycleBundle> computeAndFinalizeBundles(SchedBoundary &Zone);

  // AIE checkInterZoneConflicts peer (AIEMachineScheduler.cpp:1149-1174):
  // true if Top/Bot scoreboards still overlap at the seam (DeltaCycles=-1)
  // or a Bot-zone MI's TopReadyCycle is later than the cycle it would land on
  // after Top's final CurrCycle.
  bool checkInterZoneConflicts(ArrayRef<CycleBundle> BotBundles) const;

  // AIE handleRegionConflicts peer (AIEMachineScheduler.cpp:1176-1201):
  // ExitReadyCycle pad, then bump Top (and TopBundles) until inter-zone
  // scoreboard + TopReadyCycle deps are clear. Pads are capped at the
  // published occupancy horizon so dense MAC bodies cannot hang; residual
  // checkInterZoneConflicts after the cap is a named Top/Bot seam fatal.
  void handleRegionConflicts(const SUnit &ExitSU,
                             SmallVectorImpl<CycleBundle> &TopBundles,
                             ArrayRef<CycleBundle> BotBundles);

  // Insert one NOP (via TII->insertNoop) per empty cycle in \p Bundles,
  // apply identity-compatible closed-cycle members on singletons, and
  // exact-commit each legal multi-MI cycle via the one production site
  // haydn::bundle::commitOneProductCycle. Already-bundled members
  // are refused. Illegal scheduled multi-MI fails closed (no production
  // greedy split; NumScheduledCyclesSplit diagnostic).
  void materializeBundles(MachineBasicBlock &MBB,
                          SmallVector<CycleBundle> &Bundles);

  // Residual multi-member shells: ordinary multi-MI commit when the product
  // coissue probe accepts; sequentialize in schedule order as recovery when
  // it rejects. Sequentialize is not a packing legality authority. Format-E
  // stamped multi-member is kept only when the probe still passes (SET
  // trip/Off + true RAW + field order). Not a hard-root freeze path.
  void commitOrSequentializeUnstampedMultiMemberBundles(MachineBasicBlock &MBB);

  // Residual multi-member seam replay (pre-free-pack shells only).
  void replayMultiMemberSeamHazards(
      MachineBasicBlock &MBB,
      const SmallPtrSetImpl<MachineInstr *> &PreExistingMultiMembers);

  // True when this region is the last scheduling region of CurrentMBB
  // (AIE IsBottomRegion / MaxLatencyFinder.cpp:67-76).
  bool isBottomRegion() const;

  // Populate Bot HR from scheduled successors' committed cycles. Uses HR
  // emitInstruction(SU, Delta) + recedeScoreboard(Depth+1) (AIE
  // emitInScoreboard + recedeScoreboard). RecedeCycle would expire dest
  // remaining Depth+1 times before Bot starts. Exclusive successors
  // max-merge dest remaining per register (never |= / sum). Flag-gated
  // with -haydn-postra-interblock; do not flip region-end / WAW here.
  void initializeBotScoreBoard();
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCHEDSTRATEGY_H
