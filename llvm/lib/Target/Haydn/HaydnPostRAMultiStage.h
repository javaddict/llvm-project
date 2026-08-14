//===-- HaydnMultiStageSMS.h - Post-RA multi-stage SMS host -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Physical multi-stage SMS host inside shared post-RA MachineScheduler.
// AIE PostPipeliner scheduling core (NodeInfo windows, two-copy DAG,
// fitInInterval) plus Haydn transaction wrapper: PF-*/JM-* seats,
// snapshot/rollback, distinct prologue/kernel/epilogue MBBs, exact-E96
// commit tail, golden HR overlay. Product default OFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRAMULTISTAGE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRAMULTISTAGE_H

#include "HaydnAlternateDescriptors.h"
#include "HaydnHazardRecognizer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/Support/CommandLine.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvm {

class HaydnHazardRecognizer;
class MachineFunction;
class MachineInstr;
class SUnit;

extern cl::opt<bool> EnableHaydnMultiStageSMS;
extern cl::opt<bool> HaydnMultiStageSMSAnalysisOnly;
extern cl::opt<bool> HaydnMultiStageSMSForceFail;
extern cl::opt<std::string> HaydnMultiStageSMSForceFailSeat;

enum class HaydnMultiStagePreflightSeat {
  PF_CFG = 0, PF_PHI, PF_TRIP, PF_STAGE, PF_LIVE, PF_ALT, PF_BUNDLE, PF_LATE,
  PF_COUNT
};
enum class HaydnMultiStageJournalSeat {
  JM_ALLOC = 0, JM_SPLICE, JM_COMMIT, JM_TRIP, JM_LIVE, JM_ALT, JM_META,
  JM_COUNT
};

StringRef haydnMultiStagePreflightSeatName(HaydnMultiStagePreflightSeat S);
StringRef haydnMultiStageJournalSeatName(HaydnMultiStageJournalSeat S);
bool parseHaydnMultiStageForceFailSeat(StringRef Name, bool &IsPreflight,
                                       unsigned &SeatIndex);

class HaydnMultiStageStrategy;

/// AIE `NodeInfo` (`AIEPostPipeliner.h:40-110`). SlotCounts is omitted:
/// Haydn execution units and `HaydnHazardRecognizer` already own slot/unit
/// occupancy; AIE SlotCounts is AIE slot-set math.
struct HaydnMultiStageNodeInfo {
  bool Scheduled = false;
  int Cycle = 0;
  int ModuloCycle = 0;
  int Stage = 0;
  int Earliest = 0;
  int Latest = -1;
  int StaticEarliest = 0;
  int StaticLatest = -1;
  std::optional<int> TweakedEarliest;
  std::optional<int> TweakedLatest;
  std::optional<int> LastEarliestPusher;
  std::optional<int> LastLatestPusher;
  int NumPushedEarliest = 0;
  int NumPushedLatest = 0;
  int LCDLatest = -1;
  int EffectiveHeight = 0;
  std::unordered_set<int> Ancestors;
  std::unordered_set<int> Offspring;

  void update(int InitiationInterval);
  void reset(bool FullReset);
};

/// AIE `ScheduleInfo` (`AIEPostPipeliner.h:112-155`) without rotation.
/// Haydn peels into distinct Prolog/Kernel/Epilog MBBs instead of AIE's
/// in-place rotation of the existing loop block.
struct HaydnMultiStageScheduleInfo {
  std::vector<HaydnMultiStageNodeInfo> Nodes;
  int NInstr = 0;
  int Length = 0;

  void init(int NOrig) {
    NInstr = NOrig;
    Nodes.clear();
    Nodes.resize(static_cast<size_t>(2 * NOrig));
    Length = 0;
  }
  HaydnMultiStageNodeInfo &operator[](int N) { return Nodes[N]; }
  const HaydnMultiStageNodeInfo &operator[](int N) const { return Nodes[N]; }
  void commitCycle(int Index) {
    assert(Index < NInstr);
    HaydnMultiStageNodeInfo &Node = Nodes[Index];
    Node.Scheduled = true;
    Length = std::max(Length, Node.Cycle + 1);
  }
};

class HaydnMultiStageRegionSnapshot {
public:
  HaydnMultiStageRegionSnapshot() = default;
  ~HaydnMultiStageRegionSnapshot() { clear(); }
  HaydnMultiStageRegionSnapshot(const HaydnMultiStageRegionSnapshot &) = delete;
  HaydnMultiStageRegionSnapshot &
  operator=(const HaydnMultiStageRegionSnapshot &) = delete;

  void capture(MachineBasicBlock *Preheader, MachineBasicBlock *Kernel,
               MachineBasicBlock *Exit);
  void restore();
  void clear();
  bool empty() const { return Blocks.empty() && Created.empty(); }
  void registerCreated(MachineBasicBlock *MBB);

private:
  /// Identity-preserving MI snapshot. AIE PostPipeliner has no journaled
  /// rollback (`AIEPostPipeliner.cpp:1771` materializePipeline is
  /// fire-and-forget); register restore follows
  /// `AIEWawRegRewriter.cpp:384` revertAllocation — full operand state,
  /// including registers, so reg-trip remat cannot leak a scavenged dest.
  struct MIState {
    MachineInstr *MI = nullptr;
    MachineInstr *Clone = nullptr; // unparented full operand oracle
    bool BundledWithPred = false;
    bool BundledWithSucc = false;
  };
  struct BlockSnap {
    MachineBasicBlock *MBB = nullptr;
    SmallVector<MIState, 32> Original;
    SmallVector<MachineBasicBlock::RegisterMaskPair, 8> LiveIns;
    SmallVector<MachineBasicBlock *, 4> Successors;
  };
  SmallVector<BlockSnap, 3> Blocks;
  SmallVector<MachineBasicBlock *, 4> Created;
  MachineFunction *SnapMF = nullptr;
  HaydnAlternateDescriptors SavedAlts;
  MachineBasicBlock *SMSKernelBB = nullptr;
  void captureOne(MachineBasicBlock *MBB);
  void restoreOne(BlockSnap &S);
  void restoreSuccessors(BlockSnap &S);
  void restoreHostScratch();
};

struct HaydnMultiStageLCDEdge {
  int DefIdx = 0;
  int UseIdx = 0;
  int Latency = 1;
  int Distance = 1;
};

class HaydnMultiStageSMS {
public:
  HaydnMultiStageSMS() = default;
  ~HaydnMultiStageSMS();

  bool isCandidate(MachineBasicBlock &LoopBlock);
  int getResMII(MachineBasicBlock &LoopBlock) const;
  int computeRecMII();
  bool analyze(ScheduleDAGMI &DAG, unsigned IIHint = 0);
  bool runPreflight();
  bool computeLivePhysFixpoint();
  bool resourcesConverged(const HaydnHazardRecognizer &HR) const;
  bool materialize();
  bool tryAfterOrdinarySchedule(ScheduleDAGMI &DAG, unsigned IIHint = 0);

  int getStageCount() const { return NStages; }
  int getII() const { return II; }
  int getRecMII() const { return RecMII; }
  bool hasValidPlan() const { return HasValidPlan; }
  const char *getLastRejectReason() const { return LastRejectReason; }

  static constexpr bool productDefaultEnabled() { return false; }
  static ArrayRef<const char *> preflightSeatNames();
  static ArrayRef<const char *> journalSeatNames();

private:
  ScheduleDAGMI *DAG = nullptr;
  MachineBasicBlock *LoopBB = nullptr;
  MachineBasicBlock *Preheader = nullptr;
  MachineBasicBlock *ExitBB = nullptr;
  MachineBasicBlock *PrologMBB = nullptr;
  MachineBasicBlock *EpilogMBB = nullptr;
  MachineInstr *TripCountDef = nullptr;
  bool IsSoftCounted = false;
  Register SoftTripReg;
  std::vector<SUnit *> Body;
  HaydnMultiStageScheduleInfo Sched;
  SmallVector<HaydnMultiStageLCDEdge, 16> LCDEdges;
  std::unique_ptr<ScheduleDAGInstrs> TwoCopyDAG;
  MachineBasicBlock *TwoCopyMBB = nullptr;
  ResourceScoreboard<HaydnFuncUnitWrapper> PipeScoreboard;
  const HaydnHazardRecognizer *PipeHR = nullptr;
  int NInstr = 0;
  int MinLength = 0;
  int FirstUnscheduled = 0;
  int LastUnscheduled = -1;
  int ScoreboardSize = 0;
  int II = 1;
  int NStages = 0;
  int LinearLength = 0;
  int RecMII = 0;
  int LastResMII = 0;
  /// tryII certificate-reject count during II search (distinct-stage / LCD /
  /// lifetime / resourcesConverged). Not a separate II-raise engine.
  int ResourceRetryCount = 0;
  bool HasValidPlan = false;
  const char *LastRejectReason = nullptr;
  HaydnMultiStageRegionSnapshot OrdinarySnapshot;
  SmallVector<bool, 8> ExactCommitPlan;

  void emitRemark(MachineBasicBlock &MBB, const char *RemarkName,
                  const Twine &Msg) const;
  bool buildTwoCopyGraph(ScheduleDAGMI &Host);
  void destroyTwoCopyGraph();
  void computeForward();
  bool computeBackward();
  void computeRecMIIFromDAG();
  void computeEffectiveHeight();
  bool computeLoopCarriedParameters();
  int computeMinScheduleLength() const;
  void resetPipeSchedule(bool FullReset);
  int mostUrgent(class HaydnMultiStageStrategy &Strategy);
  void schedulePipeNode(SUnit &SU, int Cycle,
                        class HaydnMultiStageStrategy &Strategy);
  bool scheduleFirstIteration(HaydnMultiStageStrategy &Strategy);
  bool scheduleOtherIterations(HaydnMultiStageStrategy &Strategy);
  bool scheduleWithStrategy(HaydnMultiStageStrategy &Strategy);
  bool tryPipeApproaches(const HaydnHazardRecognizer &HR);
  bool computeASAPEarliest();
  bool tryII(int TryII, const HaydnHazardRecognizer &HR);
  bool forceFailPreflight(HaydnMultiStagePreflightSeat S) const;
  bool forceFailJournal(HaydnMultiStageJournalSeat S) const;
  bool preflightCFG();
  bool preflightPHI();
  bool preflightTrip();
  bool preflightStage();
  bool preflightLive();
  bool preflightAlt();
  bool preflightBundle();
  bool preflightLate();
  bool certificateKernelPlan() const;
  bool certificateExactCommitPlan();
  bool certificatePrologLiveness(MachineBasicBlock::iterator PrologInsertPt) const;
  bool certificateEpilogUses() const;
  bool certificateLCDTwoIteration() const;
  bool certificateLifetimesNoSpill() const;
  bool proveLivePhysNoSpillSubreg() const;
  bool certificateDistinctStageOccupancy() const;
  bool hasSufficientTripCount() const;
  bool certificateTripAdjust() const;
  bool adjustTripCount(int Delta) const;
  bool adjustSoftTripCount(int Delta) const;
  bool runSMSPeriodicCertificate(bool PostRewriteStillValid) const;
  void recordSWPSAnnotation() const;
  void clearPlan();
};

} // namespace llvm
#endif
