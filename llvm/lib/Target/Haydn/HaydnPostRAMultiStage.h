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
#include "HaydnBundleFormatSolver.h"
#include "HaydnHazardRecognizer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
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

/// AIE `AIE::SlotCounts` (`AIESlotCounts.h:23-74`, `AIESlotCounts.cpp:16-133`).
/// Overlay: Haydn FieldSlots / MCSlotInfo conflict-set bits (3 issue slots).
class HaydnMultiStageSlotCounts {
  static constexpr int MaxSlots = 16;
  int Counts[MaxSlots] = {};
  int Size = 0;

public:
  HaydnMultiStageSlotCounts() = default;
  explicit HaydnMultiStageSlotCounts(SlotBits Bits) {
    // AIE SlotCounts(SlotBits) (AIESlotCounts.cpp:16-23). Bound Size so a
    // high FieldSlots bit cannot assert-crash the II search.
    while (Bits && Size < MaxSlots) {
      Counts[Size] = static_cast<int>(Bits & 1);
      ++Size;
      Bits >>= 1;
    }
  }
  HaydnMultiStageSlotCounts(const HaydnMultiStageSlotCounts &) = default;
  HaydnMultiStageSlotCounts &
  operator=(const HaydnMultiStageSlotCounts &) = default;

  int max() const {
    int Max = 0;
    for (int I = 0; I < Size; ++I)
      Max = std::max(Max, Counts[I]);
    return Max;
  }
  int maxIndex() const {
    int MaxIdx = 0;
    for (int I = 1; I < Size; ++I)
      if (Counts[I] > Counts[MaxIdx])
        MaxIdx = I;
    return MaxIdx;
  }
  int totals() const {
    int Totals = 0;
    for (int I = 0; I < Size; ++I)
      Totals += Counts[I];
    return Totals;
  }
  int distance(const HaydnMultiStageSlotCounts &Other) const {
    int Sum = 0;
    const int N = std::max(Size, Other.Size);
    for (int I = 0; I < N; ++I) {
      const int D = at(I) - Other.at(I);
      Sum += D < 0 ? -D : D;
    }
    return Sum;
  }
  int at(int I) const { return I >= Size ? 0 : Counts[I]; }
  int size() const { return Size; }

  int &operator[](int I) {
    while (I >= Size) {
      assert(Size < MaxSlots);
      Counts[Size++] = 0;
    }
    return Counts[I];
  }
  const int &operator[](int I) const {
    assert(I < Size);
    return Counts[I];
  }

  HaydnMultiStageSlotCounts &
  operator+=(const HaydnMultiStageSlotCounts &Other) {
    for (int I = 0; I < Size && I < Other.Size; ++I)
      Counts[I] += Other.Counts[I];
    while (Size < Other.Size) {
      assert(Size < MaxSlots);
      Counts[Size] = Other.Counts[Size];
      ++Size;
    }
    assert(Size <= MaxSlots);
    return *this;
  }
  HaydnMultiStageSlotCounts
  operator+(const HaydnMultiStageSlotCounts &Other) const {
    HaydnMultiStageSlotCounts Result(*this);
    return Result += Other;
  }
  HaydnMultiStageSlotCounts &
  operator-=(const HaydnMultiStageSlotCounts &Other) {
    for (int I = 0; I < Size && I < Other.Size; ++I)
      Counts[I] -= Other.Counts[I];
    while (Size < Other.Size) {
      assert(Size < MaxSlots);
      Counts[Size] = -Other.Counts[Size];
      ++Size;
    }
    assert(Size <= MaxSlots);
    return *this;
  }
  HaydnMultiStageSlotCounts
  operator-(const HaydnMultiStageSlotCounts &Other) const {
    HaydnMultiStageSlotCounts Result(*this);
    return Result -= Other;
  }
  HaydnMultiStageSlotCounts &operator*=(int Scalar) {
    for (int I = 0; I < Size; ++I)
      Counts[I] *= Scalar;
    return *this;
  }
  HaydnMultiStageSlotCounts operator*(int Scalar) const {
    HaydnMultiStageSlotCounts Result(*this);
    return Result *= Scalar;
  }
};

/// AIE `NodeInfo` (`AIEPostPipeliner.h:40-110`). Slots is the AIE SlotCounts
/// port used by resource-bias windows; HR still owns emit occupancy.
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
  HaydnMultiStageSlotCounts Slots;
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
  HaydnHazardRecognizer *PipeHR = nullptr;
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
  /// SF1 (Band 2S): Format E placement uses the HR modulo oracle
  /// (HaydnHazardRecognizer::canPlaceModulo / placeModulo) and the HR
  /// checkConflict Slots overlay (isFormatAvailable / getFormatOrNull /
  /// productCovers). Same exactTryAddProduct pair as current-cycle
  /// commitPlacementForEmit. This host does not keep a second oracle.
  /// SF2: per-MI preferred generated member. Transient; placement probes
  /// consult this so the format oracle sees real unit/entry geometry
  /// (AIE MultiSlotInstrMaterializer / AIEInterBlockScheduling.cpp:1560).
  /// Instance-aware (least-loaded slot) — not one member per logical.
  DenseMap<const MachineInstr *, unsigned> MemberPin;
  /// SF2 search-only AltDescs for two-copy clones. Never written into the
  /// function-lifetime map (AIE setDesc stays inside the candidate MBB;
  /// Haydn overlay: side-map on scratch clones so HR booking sees the
  /// pinned member without durable identity on the original body).
  HaydnAlternateDescriptors SearchAlts;
  /// Planned (analyze) or realized (materialize) kernel parcels. Must
  /// equal searched II; a mismatch fails verification. Analyze counts
  /// ExactCommitPlan via countPlannedParcels; materialize recounts
  /// ParcelsCommitted. Never copied from II.
  int MeasuredII = 0;
  /// Winning tryPipeApproaches lattice name (Config / IterCountSlack).
  const char *LastStrategyName = nullptr;
  /// AIE SWPSolver is Z3. Haydn does not ship LLVM_WITH_Z3 or
  /// pragma-II; the seat is fail-closed ("unavailable"), never a second
  /// solver.
  const char *LastSWPSolverStatus = "unavailable";
  /// Resource-bias windows applied in computeLoopCarriedParameters.
  bool LastResourceBias = false;
  /// Epilogue scoreboard pre-seeded from kernel steady state.
  bool LastEpiloguePreseed = false;

  void emitRemark(MachineBasicBlock &MBB, const char *RemarkName,
                  const Twine &Msg) const;
  unsigned placementOpcode(const MachineInstr &MI) const;
  bool pinTransientMembers();
  bool peelSideEffectFree();
  bool buildTwoCopyGraph(ScheduleDAGMI &Host);
  void destroyTwoCopyGraph();
  void computeForward();
  bool computeBackward();
  void computeRecMIIFromDAG();
  void computeEffectiveHeight();
  bool computeLoopCarriedParameters();
  /// AIE `biasForLocalResourceContention` (`AIEPostPipeliner.cpp:286-318`).
  void biasForLocalResourceContention(HaydnMultiStageNodeInfo &NI,
                                      const SUnit &SU);
  HaydnMultiStageSlotCounts conflictSlotsForMI(const MachineInstr &MI) const;
  /// AIE `initializeTopScoreBoard` (`AIEMachineScheduler.cpp:407-447`)
  /// overlay: replay kernel parcels until steady, then emit epilogue
  /// peels cycle-accurately with architectural idle for leftover
  /// occupancy and internal empty peel cycles.
  bool emitEpilogueWithKernelPreseed(
      SmallVectorImpl<MachineInstr *> &EpilogMIs,
      ArrayRef<SmallVector<int, 4>> KernelByMod);
  int computeMinScheduleLength() const;
  void resetPipeSchedule(bool FullReset);
  int mostUrgent(class HaydnMultiStageStrategy &Strategy);
  void schedulePipeNode(SUnit &SU, int Cycle,
                        class HaydnMultiStageStrategy &Strategy);
  bool scheduleFirstIteration(HaydnMultiStageStrategy &Strategy);
  bool scheduleOtherIterations(HaydnMultiStageStrategy &Strategy);
  bool scheduleWithStrategy(HaydnMultiStageStrategy &Strategy);
  bool tryPipeApproaches(HaydnHazardRecognizer &HR);
  bool computeASAPEarliest();
  bool tryII(int TryII, HaydnHazardRecognizer &HR);
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
  /// Planned parcels-per-iteration: one Format E parcel per modulo cycle
  /// after ExactCommitPlan closes. Peer: AIEPostPipeliner.cpp:1706-1715
  /// visitPipelineSection (M=0..II-1, one bundle each). 0 if any cycle
  /// is not a single parcel (II lie).
  int countPlannedParcels() const;
  /// F44: no may-alias store→load pair inside one modulo-cycle pack group
  /// (golden Constraints:67; hardware raises on overlap). Fail closed.
  bool certificatePackAlias() const;
  bool certificatePrologLiveness(MachineBasicBlock::iterator PrologInsertPt) const;
  bool certificateEpilogUses() const;
  bool certificateLCDTwoIteration() const;
  bool certificateLifetimesNoSpill() const;
  bool proveLivePhysNoSpillSubreg() const;
  bool certificateDistinctStageOccupancy() const;
  bool hasSufficientTripCount() const;
  /// F39: static min-trip proof shared by all trip arms — exact preheader
  /// constant on the trip source, else the llvm.loop.itercount.range floor.
  /// std::nullopt = unproven (must fail closed).
  std::optional<int64_t> provenMinTripCount() const;
  bool certificateTripAdjust() const;
  bool adjustTripCount(int Delta) const;
  bool adjustSoftTripCount(int Delta) const;
  bool runSMSPeriodicCertificate(bool PostRewriteStillValid) const;
  void recordSWPSAnnotation() const;
  void clearPlan();
};

} // namespace llvm
#endif
