//===-- HaydnInterBlockScheduling.h - inter-block DDG substrate -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// W68.2 substrate: inter-block dependence edges between flow-adjacent blocks.
// Port of AIE AIEDataDependenceHelper.h InterBlockEdges (the DDG only — NOT
// the AIE InterBlockScheduling SWP/fixpoint state machine: Haydn's SMS owner
// is the generic pre-RA MachinePipeliner, and a second in-scheduler SWP
// engine is forbidden by contracts/pipeline.md).
//
// One HaydnInterBlockEdges is built per CFG-successor edge at the end of the
// scheduler's gathering traversal: it holds the cross-boundary dependence
// edges, the post-boundary depth map, and the successor top occupancy that feed
//   (a) MaxLatencyFinder's effective-latency cut on ExitSU edges
//       (Remaining = EdgeLatency - Depth(Succ); the successor-side cycle a
//       bottom-up scheduler still has to cover), and
//   (b) the successor scoreboard replay in initializeBotScoreBoard
//       (bundle replay of scheduled successors; conservative depth fill of
//       unscheduled ones; missing/stale occupancy keeps full latency).
//
// Depths are lazily recomputed after the successor is (re)scheduled; SUnits
// and their dependences are invariant after first construction. Occupancy is
// the scheduled-successor bundle view AIE keeps on Region::Bundles
// (AIEInterBlockScheduling.h:146, replayed by AIEMachineScheduler.cpp:370-387).
// Haydn stores it on this DDG because BlockState/Region is the declined HC#0
// next-block driver (contracts/pipeline.md).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include <map>
#include <optional>
#include <vector>

namespace llvm {

class AAResults;
class MachineSchedContext;
class SUnit;
class TargetInstrInfo;
class TargetRegisterInfo;
#include "llvm/CodeGen/TargetSchedule.h"

/// Inter-block dependence graph for one CFG edge Pred->Succ (AIE
/// InterBlockEdges peer). Nodes are added in flow order; markBoundary()
/// splits pre-boundary (Pred bottom) from post-boundary (Succ top) SUnits.
/// The same MI may appear on both sides (self-edge loops) — separate maps
/// keep the two SUnits distinct.
class HaydnInterBlockEdges : public ScheduleDAGInstrs {
  MachineBasicBlock *Pred = nullptr;
  MachineBasicBlock *Succ = nullptr;
  // Index of the first post-boundary node (== its NodeNum).
  std::optional<unsigned> Boundary;

  using IndexMap = std::map<MachineInstr *, unsigned>;
  IndexMap PredMap;
  IndexMap SuccMap;

  /// Depth (top-down cycle) of post-boundary SUnits, keyed by NodeNum.
  std::map<unsigned, int> PostDepths;
  int PostRegionMaxDepth = 0;
  bool DepthsAreScheduled = false;

  /// Successor top-region occupancy: MIs issued in successor cycle i.
  /// Peer: AIE Region::Bundles (AIEInterBlockScheduling.h:146), not a
  /// Haydn BlockState/Region clone (HC#0 declined).
  SmallVector<SmallVector<MachineInstr *, 4>, 8> SuccOccupancy;
  bool OccupancyIsScheduled = false;
  bool OccupancyIsStale = false;
  bool OccupancyRecordingActive = false;

public:
  HaydnInterBlockEdges(const MachineSchedContext &Context,
                       MachineBasicBlock *Pred, MachineBasicBlock *Succ);

  /// The DDG's own initialized scheduling model (base member is protected).
  const TargetSchedModel &getSchedModelRef() const { return SchedModel; }

  /// True once S1 recorded scheduled post-boundary cycles (vs static
  /// depths only).
  bool hasRecordedPostDepths() const { return DepthsAreScheduled; }

  /// Inherit S1's recorded depths and successor occupancy into a freshly
  /// gathered S2 graph for the same edge (matched by MI, not by node number —
  /// S2's SUnit numbering may differ). Occupancy inherit is independent of
  /// depth inherit. Per-MI depths copy even if S2 already has
  /// DepthsAreScheduled from BUNDLE-root seeding; MIs whose parent is not
  /// Succ are skipped.
  void inheritRecordedPostDepths(const HaydnInterBlockEdges &S1);

  bool isPreBoundaryNode(const SUnit *SU) const {
    return Boundary ? SU->NodeNum < *Boundary : true;
  }
  bool isPostBoundaryNode(const SUnit *SU) const {
    return Boundary ? SU->NodeNum >= *Boundary : false;
  }

  MachineBasicBlock *getPred() const { return Pred; }
  MachineBasicBlock *getSucc() const { return Succ; }

  /// Target-owned conservative cross-boundary edge construction (no HC#0):
  /// register RAW/WAR/WAW + memory store->load/store edges, pre->post only,
  /// over-approximating so the effective-latency cut can only under-cut,
  /// never invent. Replaces stock buildSchedGraph for this DDG.
  void buildCrossBoundaryEdges(AAResults *AA, const TargetInstrInfo *TII,
                               const TargetRegisterInfo *TRI,
                               const TargetSchedModel *TSM);

  /// Longest-upward earliest-cycle fill of post-boundary nodes: max over
  /// post-boundary Preds of latency+getPostDepthOr(Pred,0), else 0.
  void recomputePostDepthsFromEdges(const TargetSchedModel *TSM,
                                    const TargetInstrInfo *TII);

  /// Record the scheduled issue cycle of a post-boundary instruction (S1
  /// output feeding the S2 effective-latency cut; AIE recordPostDepth).
  /// Real (non-BUNDLE, non-meta) members also occupy that successor cycle
  /// for Bot scoreboard replay. BUNDLE roots seed depths only: they are not
  /// a bundle occupancy view (AIE replays Region::Bundles members).
  void recordPostDepth(MachineInstr *MI, int Cycle) {
    DepthsAreScheduled = true;
    if (const SUnit *SU = getPostBoundaryNode(MI)) {
      PostDepths[SU->NodeNum] = Cycle;
      PostRegionMaxDepth = std::max(PostRegionMaxDepth, Cycle);
    } else {
      // Not a DDG node (fixed/inserted code): still evidence of occupied
      // cycles in the successor (AIE recordPostDepth(int) behavior).
      PostRegionMaxDepth = std::max(PostRegionMaxDepth, Cycle);
    }
    noteSuccessorOccupancy(MI, Cycle);
  }

  /// Record only the successor-region max cycle (AIE recordPostDepth(int)).
  /// Does not publish occupancy; missing occupancy keeps full latency.
  void recordPostDepth(int Cycle) {
    DepthsAreScheduled = true;
    PostRegionMaxDepth = std::max(PostRegionMaxDepth, Cycle);
  }

  /// Reserve SUnit/map capacity for both blocks (newSUnit cannot grow).
  void reserveForBlocks(const MachineBasicBlock &Pred,
                        const MachineBasicBlock &Succ);

  /// Extra capacity beyond the block-size bound (tests that re-add one MI
  /// on both sides of the boundary; real MIR never needs this).
  void reserveMore(size_t N) {
    SUnits.reserve(SUnits.capacity() + N);
  }

  /// Add a MI as a node (pre-boundary until markBoundary()).
  void addNode(MachineInstr *MI);

  /// Mark the Pred/Succ boundary; nodes added after are post-boundary.
  void markBoundary();

  auto begin() { return SUnits.begin(); }
  auto end() { return SUnits.end(); }

  /// The SUnit representing MI's instance before/after the boundary.
  const SUnit *getPreBoundaryNode(MachineInstr *MI) const;
  const SUnit *getPostBoundaryNode(MachineInstr *MI) const;

  /// Depth of a post-boundary node (top-down earliest cycle), or -1.
  int getPostDepth(const SUnit &SU) const;
  /// AIE getPostDepthOr: recorded depth, or \p Default when unrecorded.
  int getPostDepthOr(const SUnit *SU, int Default) const;
  int getPostRegionMaxDepth() const { return PostRegionMaxDepth; }

  /// True once real successor members occupied scheduled cycles.
  bool hasScheduledSuccessorOccupancy() const { return OccupancyIsScheduled; }
  /// Every occupancy MI still lives in Succ (CFG/MI mutation otherwise
  /// invalidates the view).
  bool successorOccupancyIsLive() const;
  /// Bot scoreboard may replay this occupancy. False → keep full latency
  /// (missing, stale, or empty successor schedule).
  bool canReplaySuccessorOccupancy() const;
  ArrayRef<SmallVector<MachineInstr *, 4>> getSuccessorOccupancy() const {
    return SuccOccupancy;
  }

  /// Recompute post-boundary depths with the same Pred-based fill as
  /// recomputePostDepthsFromEdges (skipped-region / unscheduled successor).
  void recomputePostDepths();

  /// Cross-boundary edges: for a pre-boundary SU, its successor edges that
  /// reach post-boundary SUnits.
  SmallVector<const SDep *, 4> getCrossBoundaryEdges(const SUnit &SU) const;

private:
  void schedule() override {}
  void noteSuccessorOccupancy(MachineInstr *MI, int Cycle);
  void clearSuccessorOccupancy();
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINTERBLOCKSCHEDULING_H
