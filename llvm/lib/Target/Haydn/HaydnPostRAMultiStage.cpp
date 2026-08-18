//===-- HaydnMultiStageSMS.cpp - Post-RA multi-stage SWP engine -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fresh target-local post-RA physical multi-stage SMS host.
// Analysis-only under -haydn-multistage-sms-analysis-only: bounded II search
// and structured remarks with zero MIR mutation.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRAMultiStage.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFormatERecords.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPackLegality.h"
#include "HaydnPostRAScratch.h"
#include "HaydnResourceCycle.h"
#include "HaydnResourceRestrictionClasses.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

namespace {
class HaydnTwoCopySchedGraph final : public ScheduleDAGInstrs {
public:
  HaydnTwoCopySchedGraph(MachineFunction &MF, const MachineLoopInfo *MLI)
      : ScheduleDAGInstrs(MF, MLI, /*RemoveKillFlags=*/true) {}
  void schedule() override {}
};
} // namespace

void HaydnMultiStageNodeInfo::update(int InitiationInterval) {
  assert(InitiationInterval > 0);
  ModuloCycle = Cycle % InitiationInterval;
  Stage = Cycle / InitiationInterval;
}

void HaydnMultiStageNodeInfo::reset(bool FullReset) {
  Cycle = 0;
  Scheduled = false;
  Earliest = 0;
  Latest = -1;
  if (FullReset) {
    TweakedEarliest.reset();
    TweakedLatest.reset();
    NumPushedEarliest = 0;
    NumPushedLatest = 0;
    LastEarliestPusher.reset();
    LastLatestPusher.reset();
  }
}

HaydnMultiStageSMS::~HaydnMultiStageSMS() { clearPlan(); }

namespace llvm {
// Occupants already scheduled into one modulo cycle. Defined after the
// opcode identity helpers; used by fitInInterval so placement sees the
// same ARCTAN/SIN_COS-alone, CSRW↔SET_HWLOOP, and LUI/ADDI32_W e0-alone
// laws as HR getHazardType (those flags live on the emit path, not on
// checkConflict).
static bool cycleViolatesHRSameCycleLaws(const HaydnHazardRecognizer &HR,
                                         const MachineInstr &Cand,
                                         ArrayRef<const MachineInstr *> Occ);

/// AIE `PostPipelinerStrategy` (`AIEPostPipeliner.h:157-212`) with Haydn HR.
class HaydnMultiStageStrategy {
protected:
  ScheduleDAGInstrs &DAG;
  HaydnMultiStageScheduleInfo &SI;
  int LatestBias = 0;
  bool Changed = false;
public:
  HaydnMultiStageStrategy(ScheduleDAGInstrs &TheDAG,
                          HaydnMultiStageScheduleInfo &TheInfo, int LatestBiasIn)
      : DAG(TheDAG), SI(TheInfo), LatestBias(LatestBiasIn) {}
  virtual ~HaydnMultiStageStrategy() = default;

  void setEarliest(int Index, int Value) { SI[Index].Earliest = Value; }
  void setLatest(int Index, int Value) {
    SI[Index].Latest = Value - LatestBias;
  }
  void setChanged() { Changed = true; }
  bool checkAndResetChanged() {
    bool Old = Changed;
    Changed = false;
    return Old;
  }

  virtual std::string name() { return "HaydnMultiStageStrategy"; }
  virtual bool better(const SUnit &A, const SUnit &B) {
    return SI[A.NodeNum].Latest < SI[B.NodeNum].Latest;
  }
  virtual int earliest(const SUnit &N) { return SI[N.NodeNum].Earliest; }
  virtual int latest(const SUnit &N) {
    return SI[N.NodeNum].Latest + LatestBias;
  }
  virtual int mobility(const SUnit &N) { return latest(N) - earliest(N); }
  virtual bool fromTop() { return true; }
  virtual void selected(const SUnit &) {}

  /// AIE `PostPipelinerStrategy::fitInInterval` (`AIEPostPipeliner.cpp:60-79`).
  virtual std::optional<int>
  fitInInterval(const SUnit &SU, int First, int Last, int InitiationInterval,
                const HaydnHazardRecognizer &HR,
                ResourceScoreboard<HaydnFuncUnitWrapper> &Scoreboard) {
    MachineInstr &MI = *SU.getInstr();
    (void)DAG;
    assert(First <= Last);
    int Step = fromTop() ? 1 : -1;
    int Lo = First;
    int Hi = Last;
    if (Step < 0)
      std::swap(Lo, Hi);
    const int Limit = Hi + Step;
    for (int C = Lo; C != Limit; C += Step) {
      const int Mod = C % InitiationInterval;
      // SF1: canPlaceModulo (exactTryAddProduct) plus checkConflict,
      // which now books PacketFormats Slots and asks isFormatAvailable /
      // getFormatOrNull / productCovers (AIE FuncUnitWrapper::conflict).
      if (!HR.canPlaceModulo(MI, Mod))
        continue;
      SmallVector<const MachineInstr *, 4> Occ;
      for (int K = 0; K < SI.NInstr; ++K) {
        if (!SI[K].Scheduled)
          continue;
        if ((SI[K].Cycle % InitiationInterval) != Mod)
          continue;
        if (K < 0 || static_cast<unsigned>(K) >= DAG.SUnits.size())
          continue;
        if (MachineInstr *OMI = DAG.SUnits[K].getInstr())
          Occ.push_back(OMI);
      }
      if (cycleViolatesHRSameCycleLaws(HR, MI, Occ))
        continue;
      if (!HR.checkConflict(Scoreboard, MI, Mod))
        return C;
    }
    return std::nullopt;
  }

};

/// AIE `getMinOutputLat` (`AIEPostPipeliner.cpp:1091-1099`).
static int getMinOutputLat(const SUnit &SU) {
  int Min = std::numeric_limits<int>::max();
  bool Any = false;
  for (const SDep &Dep : SU.Succs) {
    if (Dep.getKind() != SDep::Output)
      continue;
    Any = true;
    Min = std::min(Min, static_cast<int>(Dep.getLatency()));
  }
  return Any ? Min : 0;
}

static bool isSideEffectFreeMI(const MachineInstr *MI) {
  if (!MI)
    return false;
  return !MI->mayStore() && !MI->hasUnmodeledSideEffects() && !MI->isCall() &&
         !MI->isInlineAsm() && !MI->isBranch() && !MI->isReturn();
}

/// AIE `IterCountSlackStrategy` (`AIEPostPipeliner.cpp:1156-1190`).
class HaydnIterCountSlackStrategy : public HaydnMultiStageStrategy {
  bool TopDown = true;

public:
  HaydnIterCountSlackStrategy(ScheduleDAGInstrs &TheDAG,
                              HaydnMultiStageScheduleInfo &TheInfo, int Bias)
      : HaydnMultiStageStrategy(TheDAG, TheInfo, Bias) {}
  std::string name() override { return "IterCountSlackStrategy"; }
  bool fromTop() override { return TopDown; }
  bool better(const SUnit &A, const SUnit &B) override {
    if (!TopDown)
      return SI[A.NodeNum].Earliest > SI[B.NodeNum].Earliest;
    const bool SEFA = isSideEffectFreeMI(A.getInstr());
    const bool SEFB = isSideEffectFreeMI(B.getInstr());
    if (SEFA != SEFB)
      return SEFA;
    return SI[A.NodeNum].Latest > SI[B.NodeNum].Latest;
  }
  void selected(const SUnit &N) override {
    if (TopDown && !isSideEffectFreeMI(N.getInstr()))
      TopDown = false;
  }
};

/// AIE `ConfigStrategy` (`AIEPostPipeliner.cpp:1192-1398`).
class HaydnConfigStrategy : public HaydnMultiStageStrategy {
public:
  enum PriorityComponent {
    NodeNum,
    Latest,
    Critical,
    Sibling,
    LCDLatest,
    DepLength,
    Liveness,
    EffHeight,
    Size
  };
  enum PlacementModifier { DeferNonCritical, PlacementSize };
  struct Configuration {
    int ExtraStages = 0;
    bool TopDown = true;
    bool Alternate = false;
    int Runs = 0;
    SmallVector<PriorityComponent, 4> Components;
    SmallVector<PlacementModifier, 2> Modifiers;
  };

private:
  bool TopDown = true;
  bool Alternate = false;
  std::string Name;
  SmallVector<PlacementModifier, 2> Modifiers;
  DenseSet<int> SuccSiblingScheduled;
  DenseSet<int> PredSiblingScheduled;
  SmallVector<PriorityComponent, 4> Priority;

  bool fromTop() override { return TopDown; }

  bool better(const SUnit &A, const SUnit &B) override {
    for (PriorityComponent P : Priority) {
      const HaydnMultiStageNodeInfo &IA = SI[A.NodeNum];
      const HaydnMultiStageNodeInfo &IB = SI[B.NodeNum];
      bool Pref = false;
      bool Decided = true;
      switch (P) {
      case NodeNum:
        Pref = TopDown ? A.NodeNum < B.NodeNum : A.NodeNum > B.NodeNum;
        break;
      case Latest:
        Pref = TopDown ? IA.Latest < IB.Latest : IA.Earliest > IB.Earliest;
        break;
      case Critical:
        Pref = TopDown ? IA.NumPushedEarliest > IB.NumPushedEarliest
                       : IA.NumPushedLatest > IB.NumPushedLatest;
        break;
      case Sibling: {
        const DenseSet<int> &Sib =
            TopDown ? SuccSiblingScheduled : PredSiblingScheduled;
        Pref = Sib.count(static_cast<int>(A.NodeNum)) >
               Sib.count(static_cast<int>(B.NodeNum));
        break;
      }
      case LCDLatest:
        Pref = IA.LCDLatest < IB.LCDLatest;
        break;
      case DepLength:
        Pref = A.getDepth() > B.getDepth();
        break;
      case Liveness:
        Pref = getMinOutputLat(A) < getMinOutputLat(B);
        break;
      case EffHeight:
        Pref = IA.EffectiveHeight > IB.EffectiveHeight;
        break;
      default:
        Decided = false;
        break;
      }
      if (Decided && Pref)
        return true;
      if (Decided) {
        bool Other = false;
        switch (P) {
        case NodeNum:
          Other = TopDown ? B.NodeNum < A.NodeNum : B.NodeNum > A.NodeNum;
          break;
        case Latest:
          Other = TopDown ? IB.Latest < IA.Latest : IB.Earliest > IA.Earliest;
          break;
        case Critical:
          Other = TopDown ? IB.NumPushedEarliest > IA.NumPushedEarliest
                          : IB.NumPushedLatest > IA.NumPushedLatest;
          break;
        case Sibling: {
          const DenseSet<int> &Sib =
              TopDown ? SuccSiblingScheduled : PredSiblingScheduled;
          Other = Sib.count(static_cast<int>(B.NodeNum)) >
                  Sib.count(static_cast<int>(A.NodeNum));
          break;
        }
        case LCDLatest:
          Other = IB.LCDLatest < IA.LCDLatest;
          break;
        case DepLength:
          Other = B.getDepth() > A.getDepth();
          break;
        case Liveness:
          Other = getMinOutputLat(B) < getMinOutputLat(A);
          break;
        case EffHeight:
          Other = IB.EffectiveHeight > IA.EffectiveHeight;
          break;
        default:
          break;
        }
        if (Other)
          return false;
      }
    }
    return false;
  }

  void selected(const SUnit &N) override {
    HaydnMultiStageNodeInfo *Pushed = &SI[static_cast<int>(N.NodeNum)];
    // Bound the AIE critical-path walk. A LastEarliestPusher cycle is
    // the T4 post-RA hang on dense MAC bodies (bkfir).
    int Guard = 0;
    const int Cap = static_cast<int>(SI.Nodes.size()) + 1;
    SmallSet<int, 8> Seen;
    while (Pushed->LastEarliestPusher && Guard++ < Cap) {
      const int P = *Pushed->LastEarliestPusher;
      if (P < 0 || P >= static_cast<int>(SI.Nodes.size()) ||
          !Seen.insert(P).second)
        break;
      Pushed = &SI[P];
      Pushed->NumPushedEarliest++;
      setChanged();
    }
    for (const SDep &SDepE : N.Succs) {
      if (SDepE.getKind() != SDep::Data)
        continue;
      for (const SDep &PDep : SDepE.getSUnit()->Preds) {
        if (PDep.getKind() != SDep::Data)
          continue;
        SuccSiblingScheduled.insert(static_cast<int>(PDep.getSUnit()->NodeNum));
      }
    }
    for (const SDep &PDep : N.Preds) {
      if (PDep.getKind() != SDep::Data)
        continue;
      for (const SDep &SDepE : PDep.getSUnit()->Succs) {
        if (SDepE.getKind() != SDep::Data)
          continue;
        PredSiblingScheduled.insert(static_cast<int>(PDep.getSUnit()->NodeNum));
      }
    }
    if (Alternate)
      TopDown = !TopDown;
  }

public:
  std::string name() override { return Name; }
  HaydnConfigStrategy(ScheduleDAGInstrs &TheDAG,
                      HaydnMultiStageScheduleInfo &TheInfo, int Length,
                      bool FromTop, bool Alt,
                      ArrayRef<PriorityComponent> Components,
                      ArrayRef<PlacementModifier> Mods = {})
      : HaydnMultiStageStrategy(TheDAG, TheInfo, Length), TopDown(FromTop),
        Alternate(Alt), Modifiers(Mods.begin(), Mods.end()) {
    Name = (Twine("Config_") + Twine(Length) + "_" + Twine(FromTop) + "_" +
            Twine(Alt))
               .str();
    for (PriorityComponent Comp : Components)
      Priority.push_back(Comp);
  }

  std::optional<int>
  fitInInterval(const SUnit &SU, int First, int Last, int InitiationInterval,
                const HaydnHazardRecognizer &HR,
                ResourceScoreboard<HaydnFuncUnitWrapper> &Scoreboard) override {
    const bool ShouldDefer =
        llvm::is_contained(Modifiers, DeferNonCritical) &&
        SI[SU.NodeNum].EffectiveHeight == 0;
    if (ShouldDefer && First + 1 <= Last) {
      auto Result = HaydnMultiStageStrategy::fitInInterval(
          SU, First + 1, Last, InitiationInterval, HR, Scoreboard);
      if (Result)
        return Result;
      return HaydnMultiStageStrategy::fitInInterval(
          SU, First, First, InitiationInterval, HR, Scoreboard);
    }
    return HaydnMultiStageStrategy::fitInInterval(
        SU, First, Last, InitiationInterval, HR, Scoreboard);
  }
};
} // namespace llvm


#undef DEBUG_TYPE
#define DEBUG_TYPE "haydn-multistage-sms"

STATISTIC(NumMultiStageSuccess,
          "Number of loops successfully post-RA software-pipelined");
STATISTIC(NumMultiStageFail,
          "Number of post-RA pipeliner attempts that failed closed");
STATISTIC(NumMultiStageAnalysisAccept,
          "Number of post-RA pipeliner analysis-only accepts");
STATISTIC(NumMultiStageCertReject,
          "Number of post-RA pipeliner certificate rejects");
STATISTIC(NumMultiStageKernelParcels,
          "Number of multi-MI kernel parcels committed by post-RA pipeliner");
STATISTIC(NumMultiStagePeels,
          "Number of prolog+epilog peels inserted by post-RA pipeliner");
STATISTIC(NumMultiStageQualifySeated,
          "Number of multi-stage accepts with parcels-per-iter equal to II");

// Defined in namespace llvm to match the extern in HaydnMultiStageSMS.h.
namespace llvm {
// Product default is HaydnMultiStageSMS::productDefaultEnabled() only.
cl::opt<bool> EnableHaydnMultiStageSMS(
    "haydn-enable-multistage-sms", cl::Hidden,
    cl::init(HaydnMultiStageSMS::productDefaultEnabled()),
    cl::desc(
        "Enable the post-RA multi-stage software pipeliner "
        "(HaydnMultiStageSMS). Product default follows "
        "HaydnMultiStageSMS::productDefaultEnabled(); remains OFF until "
        "independent then combined qualification and a policy-only flip."));

cl::opt<bool> HaydnMultiStageSMSAnalysisOnly(
    "haydn-multistage-sms-analysis-only", cl::Hidden, cl::init(false),
    cl::desc("Post-RA pipeliner: candidate/II search only; never mutate MIR."));

cl::opt<bool> HaydnMultiStageSMSForceFail(
    "haydn-multistage-sms-force-fail", cl::Hidden, cl::init(false),
    cl::desc("Post-RA pipeliner: force certificate failure after II search "
             "so materialize leaves MIR unchanged."));

cl::opt<std::string> HaydnMultiStageSMSForceFailSeat(
    "haydn-multistage-sms-force-fail-seat", cl::Hidden, cl::init(""),
    cl::desc("Force-fail one PF-* or JM-* seat by name."));
} // namespace llvm

// Maximum body instructions the engine will attempt (keeps search cheap).
// bkfir32x32 L1 MAC is ~26 real ops; leave headroom for similar FIR kernels.
static constexpr unsigned MaxBodyInstrs = 40;
// Dense MAC bodies (bkfir) used to livelock Latest / LastEarliestPusher
// walks. Those walks are capped; this bound also shrinks the Config
// lattice so a later post-RA enable cannot look like a hang.
static constexpr int LargeBodyInstrs = 20;
// Cap II search distance.
static constexpr int MaxIISearch = 24;
// Bound on the F39 static-trip materialization-chain walk (preheader only).
static constexpr unsigned MaxTripConstWalk = 8;

static const char *const PreflightNames[] = {
    "PF-CFG", "PF-PHI", "PF-TRIP", "PF-STAGE",
    "PF-LIVE", "PF-ALT", "PF-BUNDLE", "PF-LATE"};
static const char *const JournalNames[] = {
    "JM-ALLOC", "JM-SPLICE", "JM-COMMIT", "JM-TRIP",
    "JM-LIVE",  "JM-ALT",    "JM-META"};

ArrayRef<const char *> HaydnMultiStageSMS::preflightSeatNames() {
  return ArrayRef(PreflightNames);
}
ArrayRef<const char *> HaydnMultiStageSMS::journalSeatNames() {
  return ArrayRef(JournalNames);
}
StringRef llvm::haydnMultiStagePreflightSeatName(HaydnMultiStagePreflightSeat S) {
  return PreflightNames[static_cast<unsigned>(S)];
}
StringRef llvm::haydnMultiStageJournalSeatName(HaydnMultiStageJournalSeat S) {
  return JournalNames[static_cast<unsigned>(S)];
}
bool llvm::parseHaydnMultiStageForceFailSeat(StringRef Name, bool &IsPreflight,
                                             unsigned &SeatIndex) {
  if (Name.empty()) return false;
  for (unsigned I = 0; I < sizeof(PreflightNames)/sizeof(*PreflightNames); ++I)
    if (Name.equals_insensitive(PreflightNames[I])) {
      IsPreflight = true; SeatIndex = I; return true;
    }
  for (unsigned I = 0; I < sizeof(JournalNames)/sizeof(*JournalNames); ++I)
    if (Name.equals_insensitive(JournalNames[I])) {
      IsPreflight = false; SeatIndex = I; return true;
    }
  return false;
}

// Snapshot helpers — preserve original MachineInstr* identity so ScheduleDAG
// SUnits remain valid after transactional rollback to the ordinary baseline.
// F4: clones are the operand oracle (registers included). AIE PostPipeliner
// does not journal (`AIEPostPipeliner.cpp:1771`); register restore is the
// Haydn transaction overlay of `AIEWawRegRewriter.cpp:384` revertAllocation.

static void restoreRegisterOperand(MachineOperand &Dst,
                                   const MachineOperand &Src) {
  assert(Src.isReg() && "register oracle");
  if (!Dst.isReg()) {
    Dst.ChangeToRegister(Src.getReg(), Src.isDef(), Src.isImplicit(),
                         Src.isKill(), Src.isDead(), Src.isUndef(),
                         Src.isDebug());
  } else {
    Dst.setReg(Src.getReg());
    if (Dst.isDef() != Src.isDef())
      Dst.setIsDef(Src.isDef());
    Dst.setImplicit(Src.isImplicit());
  }
  Dst.setSubReg(Src.getSubReg());
  if (Dst.isDef()) {
    Dst.setIsDead(Src.isDead());
    Dst.setIsEarlyClobber(Src.isEarlyClobber());
  } else {
    Dst.setIsKill(Src.isKill());
    Dst.setIsDebug(Src.isDebug());
  }
  Dst.setIsUndef(Src.isUndef());
  Dst.setIsInternalRead(Src.isInternalRead());
  if (Src.getReg().isPhysical())
    Dst.setIsRenamable(Src.isRenamable());
}

static void restoreOperandFromClone(MachineOperand &Dst,
                                    const MachineOperand &Src) {
  if (Src.isReg()) {
    restoreRegisterOperand(Dst, Src);
    return;
  }
  if (Src.isImm()) {
    if (Dst.isImm())
      Dst.setImm(Src.getImm());
    else
      Dst.ChangeToImmediate(Src.getImm(), Src.getTargetFlags());
    if (Dst.isImm())
      Dst.setTargetFlags(Src.getTargetFlags());
    return;
  }
  if (Src.isMBB() && Dst.isMBB()) {
    Dst.setMBB(Src.getMBB());
    return;
  }
  if (Src.isFI() && Dst.isFI()) {
    Dst.setIndex(Src.getIndex());
    Dst.setTargetFlags(Src.getTargetFlags());
    return;
  }
  if (!Src.isReg() && !Dst.isReg() && Dst.isImm() == Src.isImm())
    Dst.setTargetFlags(Src.getTargetFlags());
}

static void restoreMIFromClone(MachineInstr &MI, const MachineInstr &Clone,
                               const TargetInstrInfo &TII) {
  if (MI.getOpcode() != Clone.getOpcode())
    MI.setDesc(TII.get(Clone.getOpcode()));
  MI.setFlags(Clone.getFlags());
  MI.setDebugLoc(Clone.getDebugLoc());
  while (MI.getNumOperands() > Clone.getNumOperands())
    MI.removeOperand(MI.getNumOperands() - 1);
  MachineFunction *MF = MI.getMF();
  for (unsigned I = 0, E = Clone.getNumOperands(); I != E; ++I) {
    const MachineOperand &Src = Clone.getOperand(I);
    if (I >= MI.getNumOperands()) {
      if (MF)
        MI.addOperand(*MF, Src);
      else
        MI.addOperand(Src);
      continue;
    }
    restoreOperandFromClone(MI.getOperand(I), Src);
  }
}

void HaydnMultiStageRegionSnapshot::captureOne(MachineBasicBlock *MBB) {
  if (!MBB)
    return;
  MachineFunction *MF = MBB->getParent();
  BlockSnap S;
  S.MBB = MBB;
  for (const auto &LI : MBB->liveins())
    S.LiveIns.push_back(LI);
  for (MachineBasicBlock *Succ : MBB->successors())
    S.Successors.push_back(Succ);
  // Include BUNDLE headers so already-committed ordinary parcels in a
  // previously scheduled preheader survive rollback (F5 byte-identity).
  for (MachineInstr &MI : MBB->instrs()) {
    MIState St;
    St.MI = &MI;
    St.Clone = MF ? MF->CloneMachineInstr(&MI) : nullptr;
    St.BundledWithPred = MI.isBundledWithPred();
    St.BundledWithSucc = MI.isBundledWithSucc();
    S.Original.push_back(St);
  }
  Blocks.push_back(std::move(S));
}
void HaydnMultiStageRegionSnapshot::capture(MachineBasicBlock *A,
                                            MachineBasicBlock *B,
                                            MachineBasicBlock *C) {
  clear();
  SnapMF = B ? B->getParent() : (A ? A->getParent() : nullptr);
  if (!SnapMF && C)
    SnapMF = C->getParent();
  if (SnapMF) {
    auto *MFI = SnapMF->getInfo<HaydnMachineFunctionInfo>();
    SavedAlts = MFI->getAltDescs();
    SMSKernelBB = B;
  }
  captureOne(A);
  captureOne(B);
  captureOne(C);
}
void HaydnMultiStageRegionSnapshot::restoreOne(BlockSnap &S) {
  if (!S.MBB)
    return;
  MachineBasicBlock &MBB = *S.MBB;
  MachineFunction *MF = MBB.getParent();
  if (!MF)
    return;
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  DenseSet<MachineInstr *> OrigSet;
  for (const MIState &St : S.Original)
    if (St.MI)
      OrigSet.insert(St.MI);

  // Fully unbundle first so removeFromParent is always legal.
  for (MachineInstr &MI : make_early_inc_range(MBB.instrs())) {
    while (MI.isBundledWithSucc())
      MI.unbundleFromSucc();
    while (MI.isBundledWithPred())
      MI.unbundleFromPred();
  }

  // Erase non-original inserts (remat ADDI, exact-commit BUNDLE headers that
  // were not in the ordinary baseline). Captured BUNDLE headers stay.
  for (MachineInstr &MI : make_early_inc_range(MBB.instrs())) {
    if (!OrigSet.count(&MI))
      MI.eraseFromParent();
  }

  // Revert full operand state (registers included — F4) then detach and
  // re-append in capture order (preserves MachineInstr* identity).
  SmallVector<std::pair<MachineInstr *, bool>, 32> Ordered;
  Ordered.reserve(S.Original.size());
  for (const MIState &St : S.Original) {
    MachineInstr *MI = St.MI;
    if (!MI)
      continue;
    if (MI->getParent() && MI->getParent() != &MBB)
      continue;
    if (St.Clone)
      restoreMIFromClone(*MI, *St.Clone, *TII);
    if (MI->getParent() == &MBB)
      MI->removeFromParent();
    Ordered.push_back({MI, St.BundledWithSucc});
  }
  // Drop any leftover non-originals that survived (should be none).
  while (!MBB.empty())
    MBB.begin()->eraseFromParent();
  for (auto &P : Ordered)
    MBB.insert(MBB.end(), P.first);
  for (unsigned I = 0, E = Ordered.size(); I + 1 < E; ++I) {
    if (!Ordered[I].second)
      continue;
    MachineInstr *MI = Ordered[I].first;
    if (MI && !MI->isBundledWithSucc())
      MI->bundleWithSucc();
  }

  MBB.clearLiveIns();
  for (const auto &LI : S.LiveIns)
    MBB.addLiveIn(LI);
}
void HaydnMultiStageRegionSnapshot::restoreSuccessors(BlockSnap &S) {
  if (!S.MBB)
    return;
  while (!S.MBB->succ_empty())
    S.MBB->removeSuccessor(*S.MBB->succ_begin());
  for (MachineBasicBlock *Succ : S.Successors)
    if (!S.MBB->isSuccessor(Succ))
      S.MBB->addSuccessor(Succ);
}

static void eraseCreatedStageMBB(MachineBasicBlock *MBB) {
  if (!MBB || !MBB->getParent())
    return;
  SmallVector<MachineBasicBlock *, 4> Preds(MBB->pred_begin(), MBB->pred_end());
  for (MachineBasicBlock *P : Preds)
    if (P->isSuccessor(MBB))
      P->removeSuccessor(MBB);
  SmallVector<MachineBasicBlock *, 4> Succs(MBB->succ_begin(), MBB->succ_end());
  for (MachineBasicBlock *S : Succs)
    MBB->removeSuccessor(S);
  for (MachineInstr &MI : make_early_inc_range(MBB->instrs())) {
    while (MI.isBundledWithSucc())
      MI.unbundleFromSucc();
    while (MI.isBundledWithPred())
      MI.unbundleFromPred();
    MI.eraseFromParent();
  }
  MBB->eraseFromParent();
}

void HaydnMultiStageRegionSnapshot::registerCreated(MachineBasicBlock *MBB) {
  if (MBB)
    Created.push_back(MBB);
}

void HaydnMultiStageRegionSnapshot::restoreHostScratch() {
  if (!SnapMF)
    return;
  auto *MFI = SnapMF->getInfo<HaydnMachineFunctionInfo>();
  MFI->getAltDescs() = SavedAlts;
  if (SMSKernelBB)
    MFI->eraseSMSLoop(SMSKernelBB);
}

void HaydnMultiStageRegionSnapshot::restore() {
  restoreHostScratch();
  for (BlockSnap &S : Blocks)
    restoreSuccessors(S);
  for (MachineBasicBlock *MBB : Created)
    eraseCreatedStageMBB(MBB);
  Created.clear();
  for (BlockSnap &S : Blocks)
    restoreOne(S);
}
void HaydnMultiStageRegionSnapshot::clear() {
  if (SnapMF) {
    for (BlockSnap &S : Blocks)
      for (MIState &St : S.Original)
        if (St.Clone) {
          SnapMF->deleteMachineInstr(St.Clone);
          St.Clone = nullptr;
        }
  }
  Blocks.clear();
  Created.clear();
  SavedAlts.clear();
  SMSKernelBB = nullptr;
  SnapMF = nullptr;
}


bool HaydnMultiStageSMS::forceFailPreflight(HaydnMultiStagePreflightSeat S) const {
  bool IsPF=false; unsigned Idx=0;
  if (!parseHaydnMultiStageForceFailSeat(HaydnMultiStageSMSForceFailSeat, IsPF, Idx))
    return false;
  return IsPF && Idx == static_cast<unsigned>(S);
}
bool HaydnMultiStageSMS::forceFailJournal(HaydnMultiStageJournalSeat S) const {
  bool IsPF=false; unsigned Idx=0;
  if (!parseHaydnMultiStageForceFailSeat(HaydnMultiStageSMSForceFailSeat, IsPF, Idx))
    return false;
  return !IsPF && Idx == static_cast<unsigned>(S);
}

static bool isZOLTerminator(const MachineInstr &MI) {
  return MI.getOpcode() == Haydn::PseudoLoopEnd;
}

static bool isSkippableBodyMI(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.isPosition() || MI.isKill() ||
         MI.isImplicitDef() || MI.isCFIInstruction();
}

// LoopStart (IR ZOL) or any product SET_HWLOOP form (logical/wide/member).
static bool isHwLoopSetup(const MachineInstr &MI) {
  const auto *TII = MI.getMF()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  return TII->isHardwareLoopSetupInstr(MI);
}

namespace llvm {
static bool cycleViolatesHRSameCycleLaws(const HaydnHazardRecognizer &HR,
                                         const MachineInstr &Cand,
                                         ArrayRef<const MachineInstr *> Occ) {
  // One law: HR emit path + pack + this placement conjunct
  // (HaydnPackLegality.h cycleViolatesNamedSameCycleLaws).
  (void)HR;
  return HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(Cand, Occ);
}
} // namespace llvm

// True when a product SET_HWLOOP form encodes \p Loop as its start MBB.
// Operand shape: sel, loop_start MBB, loop_end MBB, cnt/rs (logical/wide).
// LoopStart has no MBB operands — handled separately by findHwLoopSetup.
static bool setupTargetsLoop(const MachineInstr &MI,
                             const MachineBasicBlock &Loop) {
  if (MI.getOpcode() == Haydn::LoopStart)
    return false;
  const auto *TII = MI.getMF()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  if (!TII->isHardwareLoopSetupInstr(MI))
    return false;
  // Operands: sel, loop_start MBB, loop_end MBB, cnt/rs
  if (MI.getNumOperands() < 3 || !MI.getOperand(1).isMBB())
    return false;
  return MI.getOperand(1).getMBB() == &Loop;
}

// Dedicated fallthrough preheader: unique non-self predecessor that either
// layout-falls-through into Loop or has Loop as its only successor.
static MachineBasicBlock *
findFallThroughPreheader(MachineBasicBlock &Loop) {
  MachineBasicBlock *PH = nullptr;
  for (MachineBasicBlock *Pred : Loop.predecessors()) {
    if (Pred == &Loop)
      continue;
    if (PH)
      return nullptr;
    PH = Pred;
  }
  if (!PH)
    return nullptr;
  if (PH->isLayoutSuccessor(&Loop))
    return PH;
  if (PH->succ_size() == 1 && *PH->succ_begin() == &Loop)
    return PH;
  return nullptr;
}

static MachineBasicBlock *findUniqueExit(MachineBasicBlock &Loop) {
  MachineBasicBlock *Exit = nullptr;
  for (MachineBasicBlock *S : Loop.successors()) {
    if (S == &Loop)
      continue;
    if (Exit)
      return nullptr;
    Exit = S;
  }
  return Exit;
}

// Tripcount setup in preheader: LoopStart, or SET_HWLOOP{,_REG} targeting Loop.
static MachineInstr *findHwLoopSetup(MachineBasicBlock &Preheader,
                                     MachineBasicBlock &Loop) {
  for (MachineInstr &MI : reverse(Preheader)) {
    if (MI.getOpcode() == Haydn::LoopStart)
      return &MI;
    if (setupTargetsLoop(MI, Loop))
      return &MI;
  }
  return nullptr;
}

// Soft-counted countdown body: trip_reg = trip_reg ± 1 (ADDI -1 or SUBI +1).
// Independent of hardware-loop product enablement; used with hwloops OFF.
// Accept logical and residual slot-member / wide forms of the same arithmetic.
static bool isSoftCountdownBump(const MachineInstr &MI, Register &TripReg) {
  const unsigned Opc = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  const bool IsSub = Opc == Haydn::SUBI32;
  const bool IsAdd = Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W;
  if (!IsSub && !IsAdd)
    return false;
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  if (!Dst.isPhysical() || Dst == Haydn::R0 || Dst != Src)
    return false;
  const int64_t Imm = MI.getOperand(2).getImm();
  if (IsSub) {
    if (Imm != 1)
      return false;
  } else if (Imm != -1) {
    return false;
  }
  TripReg = Dst;
  return true;
}

static MachineInstr *findSoftCountdownBump(MachineBasicBlock &Loop) {
  MachineInstr *Fallback = nullptr;
  for (MachineInstr &MI : Loop) {
    Register Trip;
    if (!isSoftCountdownBump(MI, Trip))
      continue;
    if (!Fallback)
      Fallback = &MI;
    for (MachineBasicBlock::iterator It = std::next(MI.getIterator()),
                                     E = Loop.getFirstTerminator();
         It != E; ++It) {
      MachineInstr &Cmp = *It;
      if (Cmp.isDebugInstr() || Cmp.isPosition())
        continue;
      for (const MachineOperand &MO : Cmp.operands()) {
        if (MO.isReg() && MO.readsReg() && MO.getReg() == Trip)
          return &MI;
      }
    }
  }
  return Fallback;
}

static bool hasSelfBackedge(const MachineBasicBlock &Loop) {
  return llvm::is_contained(Loop.successors(), &Loop);
}

/// F39 static trip oracle: recover the constant feeding a physical trip
/// register by walking the dedicated preheader from the last def of \p Trip.
/// Post-RA materialization shapes only (LOADI32 imm; ADDI32/ADDI32_W/SUBI32
/// reg+imm chains bottoming at R0), bounded to the preheader and to
/// MaxTripConstWalk steps. Returns std::nullopt when the value is not
/// provably constant inside the preheader (variable trip) — the caller must
/// then fail closed, never guess.
static std::optional<int64_t>
staticPhysTripConstant(Register Trip, const MachineBasicBlock &Preheader) {
  if (!Trip || !Trip.isPhysical() || Trip == Haydn::R0)
    return std::nullopt;
  auto lastDefOf = [&Preheader](Register R) -> const MachineInstr * {
    const MachineInstr *Def = nullptr;
    for (const MachineInstr &MI : Preheader)
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.isDef() && MO.getReg() == R)
          Def = &MI;
    return Def;
  };
  Register Base = Trip;
  int64_t Offset = 0;
  for (unsigned Step = 0; Step < MaxTripConstWalk; ++Step) {
    const MachineInstr *BaseDef = lastDefOf(Base);
    if (!BaseDef)
      return std::nullopt; // defined outside the preheader; unprovable
    const unsigned Opc = BaseDef->getOpcode();
    // LOADI32 rd, imm — terminal constant.
    if (Opc == Haydn::LOADI32 && BaseDef->getNumOperands() > 1 &&
        BaseDef->getOperand(1).isImm())
      return BaseDef->getOperand(1).getImm() + Offset;
    // ADDI32/ADDI32_W rd, rs, imm / SUBI32 rd, rs, imm — accumulate and
    // follow rs; chain bottoms at R0 (soft zero).
    if ((Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W ||
         Opc == Haydn::SUBI32) &&
        BaseDef->getNumOperands() > 2 && BaseDef->getOperand(1).isReg() &&
        BaseDef->getOperand(2).isImm()) {
      const int64_t Imm = BaseDef->getOperand(2).getImm();
      Offset += (Opc == Haydn::SUBI32) ? -Imm : Imm;
      Register Src = BaseDef->getOperand(1).getReg();
      if (Src == Haydn::R0)
        return Offset;
      if (!Src.isPhysical())
        return std::nullopt;
      Base = Src;
      continue;
    }
    // Any other def shape (LUI pairs, computed values, incoming params) is
    // not a provable in-preheader constant for this purpose.
    return std::nullopt;
  }
  return std::nullopt;
}

/// F39 metadata oracle: `llvm.loop.itercount.range` on the loop header's
/// backedge terminator carries a proven minimum iteration count (same shape
/// as the AIE fork's LLVMLoopIterCount). Authoritative when present — the
/// front end guarantees it. Returns std::nullopt when absent/malformed.
static std::optional<int64_t>
loopMDMinTripCount(const MachineBasicBlock &LoopBB) {
  const BasicBlock *BB = LoopBB.getBasicBlock();
  if (!BB)
    return std::nullopt;
  const Instruction *Term = BB->getTerminator();
  if (!Term)
    return std::nullopt;
  const MDNode *LoopID = Term->getMetadata(LLVMContext::MD_loop);
  if (!LoopID)
    return std::nullopt;
  for (unsigned I = 1, E = LoopID->getNumOperands(); I < E; ++I) {
    const MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(I));
    if (!MD || MD->getNumOperands() < 2)
      continue;
    const MDString *S = dyn_cast<MDString>(MD->getOperand(0));
    if (!S || S->getString() != "llvm.loop.itercount.range")
      continue;
    const auto *C = mdconst::dyn_extract_or_null<ConstantInt>(MD->getOperand(1));
    if (C)
      return C->getSExtValue();
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Candidate / ResMII
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Candidate / ResMII
//===----------------------------------------------------------------------===//

static int depLat(const SDep &Dep) {
  // AIE uses SDep::getSignedLatency(); LLVM 22 SDep only has unsigned
  // getLatency(). Anti/Output typically publish 0, which still constrains
  // same-cycle order (successor earliest >= predecessor earliest).
  return static_cast<int>(Dep.getLatency());
}

/// Placement-relevant edges: Data, Anti, Output, and memory Order.
/// AIE computeForward/scheduleNode walk every signed-latency succ
/// (AIEPostPipeliner.cpp:239-256 / :320-360). Barrier/Artificial/Weak
/// Order stays excluded — those are two-copy seam artifacts.
static bool isPlacementDep(const SDep &Dep) {
  switch (Dep.getKind()) {
  case SDep::Data:
  case SDep::Anti:
  case SDep::Output:
    return true;
  case SDep::Order:
    return Dep.isNormalMemory();
  }
  return false;
}

/// Loop-carried facts from the two-copy graph: register Data/Anti/Output
/// and memory Order. Barrier/Artificial/Weak/Cluster Order across the
/// copy seam is a concatenation artifact (LLVM 22 `buildSchedGraph` walks
/// one MBB; AIE `initSUnit` twice + `buildEdges` over `SUnits` does not
/// insert a linear BB seam). Named Haydn overlay vs
/// `AIEMachineScheduler.cpp:1764-1793`.
static bool isTwoCopyLCDDep(const SDep &Dep) {
  switch (Dep.getKind()) {
  case SDep::Data:
  case SDep::Anti:
  case SDep::Output:
    return true;
  case SDep::Order:
    return Dep.isNormalMemory();
  }
  return false;
}

/// Drop Barrier/Artificial Order from copy 0 onto copy 1 so RecMII/windows
/// see AIE-shaped LCDs, not "every earlier MI precedes every later clone".
static void pruneTwoCopySeamArtifacts(ScheduleDAGInstrs &G, int NInstr) {
  for (int K = 0; K < NInstr; ++K) {
    SUnit &SU = G.SUnits[K];
    SmallVector<SDep, 8> Drop;
    for (const SDep &Dep : SU.Succs) {
      SUnit *Dst = Dep.getSUnit();
      if (!Dst || Dst->isBoundaryNode())
        continue;
      const int S = static_cast<int>(Dst->NodeNum);
      if (S < NInstr || S >= 2 * NInstr)
        continue;
      if (!isTwoCopyLCDDep(Dep))
        Drop.push_back(Dep);
    }
    for (SDep &SuccDep : Drop) {
      SUnit *Dst = SuccDep.getSUnit();
      SDep PredDep = SuccDep;
      PredDep.setSUnit(&SU);
      Dst->removePred(PredDep);
    }
  }
}

void HaydnMultiStageSMS::destroyTwoCopyGraph() {
  if (TwoCopyDAG) {
    for (SUnit &SU : TwoCopyDAG->SUnits)
      if (MachineInstr *MI = SU.getInstr())
        MemberPin.erase(MI);
  }
  TwoCopyDAG.reset();
  PipeHR = nullptr;
  if (!TwoCopyMBB)
    return;
  MachineFunction *MF = TwoCopyMBB->getParent();
  TwoCopyMBB->clear();
  if (MF)
    MF->deleteMachineBasicBlock(TwoCopyMBB);
  TwoCopyMBB = nullptr;
}

bool HaydnMultiStageSMS::buildTwoCopyGraph(ScheduleDAGMI &Host) {
  destroyTwoCopyGraph();
  NInstr = static_cast<int>(Body.size());
  if (NInstr < 2)
    return false;

  MachineFunction &MF = Host.MF;
  TwoCopyMBB = MF.CreateMachineBasicBlock();
  for (int Copy = 0; Copy < 2; ++Copy) {
    for (SUnit *SU : Body) {
      MachineInstr *MI = SU->getInstr();
      if (!MI) {
        destroyTwoCopyGraph();
        return false;
      }
      TwoCopyMBB->insert(TwoCopyMBB->end(), MF.CloneMachineInstr(MI));
    }
  }

  auto Graph = std::make_unique<HaydnTwoCopySchedGraph>(MF, /*MLI=*/nullptr);
  Graph->startBlock(TwoCopyMBB);
  const unsigned RegionN = static_cast<unsigned>(TwoCopyMBB->size());
  Graph->enterRegion(TwoCopyMBB, TwoCopyMBB->begin(), TwoCopyMBB->end(),
                     RegionN);
  AAResults *AA = static_cast<HaydnScheduleDAGMI &>(Host).getAliasAnalysis();
  Graph->buildSchedGraph(AA);
  if (static_cast<int>(Graph->SUnits.size()) != 2 * NInstr) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: two-copy SUnits="
                      << Graph->SUnits.size() << " want " << (2 * NInstr)
                      << "\n");
    TwoCopyDAG = std::move(Graph);
    destroyTwoCopyGraph();
    return false;
  }

  TwoCopyDAG = std::move(Graph);
  pruneTwoCopySeamArtifacts(*TwoCopyDAG, NInstr);
  Sched.init(NInstr);

  LCDEdges.clear();
  auto addLCD = [&](int Def, int Use, int Lat, int Dist) {
    if (Def < 0 || Use < 0 || Def >= NInstr || Use >= NInstr)
      return;
    Lat = std::max(0, Lat);
    Dist = std::max(1, Dist);
    for (HaydnMultiStageLCDEdge &E : LCDEdges)
      if (E.DefIdx == Def && E.UseIdx == Use && E.Distance == Dist) {
        E.Latency = std::max(E.Latency, Lat);
        return;
      }
    LCDEdges.push_back({Def, Use, Lat, Dist});
  };
  for (int K = 0; K < NInstr; ++K) {
    for (const SDep &Dep : TwoCopyDAG->SUnits[K].Succs) {
      if (Dep.getSUnit()->isBoundaryNode())
        continue;
      if (Dep.getKind() != SDep::Data && !Dep.isNormalMemory())
        continue;
      const int S = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (S < NInstr || S >= 2 * NInstr)
        continue;
      addLCD(K, S - NInstr, depLat(Dep), /*Distance=*/1);
    }
  }
  return true;
}

/// Count may-alias store→load pairs in the original body. Informational
/// for remarks; loop-carried memory edges come from the two-copy
/// `buildSchedGraph` (AIE `AIEPostRASchedStrategy::buildGraph` NCopies=2
/// at `AIEMachineScheduler.cpp:1764-1793`).
static unsigned countMayAliasStoreLoadPairs(ArrayRef<SUnit *> BodySUs,
                                            AAResults *AA) {
  unsigned Count = 0;
  for (SUnit *StoreSU : BodySUs) {
    MachineInstr *StoreMI = StoreSU->getInstr();
    if (!StoreMI || isSkippableBodyMI(*StoreMI) || isZOLTerminator(*StoreMI))
      continue;
    if (!StoreMI->mayStore())
      continue;
    for (SUnit *LoadSU : BodySUs) {
      MachineInstr *LoadMI = LoadSU->getInstr();
      if (!LoadMI || isSkippableBodyMI(*LoadMI) || isZOLTerminator(*LoadMI))
        continue;
      if (!LoadMI->mayLoad())
        continue;
      if (StoreMI->mayAlias(AA, *LoadMI, /*UseTBAA=*/true))
        ++Count;
    }
  }
  return Count;
}

namespace {
using HCache = std::vector<std::optional<std::optional<int>>>;

std::optional<int> computeHeight(HCache &Heights, SUnit *Start, SUnit *End) {
  if (Start == End)
    return 0;
  if (Start->NodeNum > End->NodeNum || Start->NodeNum >= Heights.size())
    return std::nullopt;
  auto Cached = Heights[Start->NodeNum];
  if (Cached)
    return *Cached;
  std::optional<int> Height;
  for (auto &Dep : Start->Succs) {
    SUnit *Dst = Dep.getSUnit();
    if (!Dst || Dst->isBoundaryNode())
      continue;
    auto SuccHeight = computeHeight(Heights, Dst, End);
    if (SuccHeight) {
      int NewHeight = *SuccHeight + depLat(Dep);
      Height = Height ? std::max(NewHeight, *Height) : NewHeight;
    }
  }
  Heights[Start->NodeNum] = Height;
  return Height;
}
} // namespace

void HaydnMultiStageSMS::computeRecMIIFromDAG() {
  // AIE `PostPipeliner::computeRecMII` (AIEPostPipeliner.cpp:452-479)
  // counts every copy-0→copy-1 succ as a backedge. Haydn RecMII circuits
  // are Data (RAW) only: Anti/Output/Order LCDs with all-succ height walk
  // the concatenated body and report RecMII≈NInstr. Those edges still
  // fold into Earliest/LCDLatest windows. Per-edge ceil(Lat/Dist) remains
  // the II floor before Ancestors exist (analyze()) and for Dist>1.
  RecMII = 0;
  if (!TwoCopyDAG || NInstr <= 0)
    return;
  for (int K = 0; K < NInstr; ++K) {
    SUnit &Src = TwoCopyDAG->SUnits[K];
    for (auto &Dep : Src.Succs) {
      SUnit *Dst = Dep.getSUnit();
      if (!Dst || Dst->isBoundaryNode() || Dep.getKind() != SDep::Data ||
          Dst->NodeNum < static_cast<unsigned>(NInstr))
        continue;
      const HaydnMultiStageNodeInfo &Me = Sched[K];
      int SNum = static_cast<int>(Dst->NodeNum) - NInstr;
      if (SNum < 0 || SNum >= NInstr)
        continue;
      if (!Me.Ancestors.count(SNum))
        continue;
      HCache Heights(static_cast<size_t>(NInstr));
      auto Height = computeHeight(Heights, &TwoCopyDAG->SUnits[SNum], &Src);
      if (!Height)
        continue;
      int Circuit = *Height + depLat(Dep);
      RecMII = std::max(Circuit, RecMII);
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: RecMII backedge " << K << " -> "
                        << SNum << " lat=" << depLat(Dep)
                        << " height=" << *Height << " circuit=" << Circuit
                        << "\n");
    }
  }
  for (const HaydnMultiStageLCDEdge &E : LCDEdges) {
    int Dist = std::max(1, E.Distance);
    int Lat = std::max(0, E.Latency);
    RecMII = std::max(RecMII, (Lat + Dist - 1) / Dist);
  }
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: RecMII=" << RecMII << "\n");
}

int HaydnMultiStageSMS::computeRecMII() {
  computeRecMIIFromDAG();
  return RecMII;
}

bool HaydnMultiStageSMS::proveLivePhysNoSpillSubreg() const {
  if (!HasValidPlan || II < 1 || Body.empty() || !DAG)
    return false;
  const TargetRegisterInfo *TRI = DAG->MF.getSubtarget().getRegisterInfo();
  if (!TRI)
    return false;
  const int N = static_cast<int>(Body.size());
  SmallVector<int, 16> Order;
  for (int I = 0; I < N; ++I) {
    if (!Sched[I].Scheduled)
      return false;
    Order.push_back(I);
  }
  llvm::stable_sort(Order, [&](int A, int B) {
    if (Sched[A].Cycle != Sched[B].Cycle)
      return Sched[A].Cycle < Sched[B].Cycle;
    return A < B;
  });

  SmallVector<std::pair<Register, int>, 16> OpenDefs;
  for (int Idx : Order) {
    MachineInstr *MI = Body[Idx]->getInstr();
    if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
      continue;
    const int C = Sched[Idx].Cycle;
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.readsReg() || !MO.getReg().isPhysical())
        continue;
      Register Reg = MO.getReg();
      if (Reg == Haydn::R0)
        continue;
      for (const auto &OD : OpenDefs) {
        if (!TRI->regsOverlap(Reg, OD.first))
          continue;
        if (C >= OD.second && (C - OD.second) >= II)
          return false;
      }
    }
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
        continue;
      Register Reg = MO.getReg();
      if (Reg == Haydn::R0)
        continue;
      llvm::erase_if(OpenDefs, [&](const std::pair<Register, int> &OD) {
        return TRI->regsOverlap(Reg, OD.first);
      });
      if (!MO.isDead())
        OpenDefs.emplace_back(Reg, C);
    }
  }
  return true;
}

bool HaydnMultiStageSMS::computeLivePhysFixpoint() {
  // SF8: AIE LiveRegs worklist (AIELiveRegs.cpp:82-107) over the SMS
  // region. Replaces the hash-theater loop whose digest could not change
  // between iterations. Search-time no-spill stays proveLivePhysNoSpillSubreg
  // (regsOverlap).
  if (!LoopBB || !Preheader || !ExitBB || !DAG)
    return false;
  const TargetRegisterInfo *TRI = DAG->MF.getSubtarget().getRegisterInfo();
  if (!TRI)
    return false;

  SmallVector<MachineBasicBlock *, 4> Region = {ExitBB, LoopBB, Preheader};
  if (PrologMBB)
    Region.push_back(PrologMBB);
  if (EpilogMBB)
    Region.push_back(EpilogMBB);

  DenseMap<const MachineBasicBlock *, SmallVector<MCPhysReg, 8>> LiveIns;
  SmallVector<const MachineBasicBlock *, 8> Work;
  DenseMap<const MachineBasicBlock *, bool> InWork;
  auto enqueue = [&](const MachineBasicBlock *MBB) {
    if (!MBB || InWork.lookup(MBB))
      return;
    Work.push_back(MBB);
    InWork[MBB] = true;
  };
  for (MachineBasicBlock *MBB : Region)
    enqueue(MBB);

  auto snapshot = [](const LivePhysRegs &L) {
    SmallVector<MCPhysReg, 8> V;
    for (MCPhysReg R : L)
      V.push_back(R);
    llvm::sort(V);
    return V;
  };

  unsigned Guard = 0;
  const unsigned GuardMax = 32 * std::max<unsigned>(1, Region.size());
  while (!Work.empty() && Guard++ < GuardMax) {
    const MachineBasicBlock *MBB = Work.pop_back_val();
    InWork[MBB] = false;
    LivePhysRegs Current(*TRI);
    for (const MachineBasicBlock *Succ : MBB->successors()) {
      auto It = LiveIns.find(Succ);
      if (It == LiveIns.end())
        continue;
      for (MCPhysReg R : It->second)
        Current.addReg(R);
    }
    for (const MachineInstr &MI : llvm::reverse(*MBB))
      Current.stepBackward(MI);
    SmallVector<MCPhysReg, 8> Next = snapshot(Current);
    auto &Old = LiveIns[MBB];
    if (Old == Next)
      continue;
    Old = std::move(Next);
    for (const MachineBasicBlock *Pred : MBB->predecessors()) {
      bool InRegion = false;
      for (MachineBasicBlock *R : Region)
        if (R == Pred) {
          InRegion = true;
          break;
        }
      if (InRegion)
        enqueue(Pred);
    }
  }
  if (Guard >= GuardMax)
    return false;
  if (!proveLivePhysNoSpillSubreg())
    return false;

  if (NStages < 2)
    return true;
  MachineBasicBlock::iterator PrologInsertPt = Preheader->getFirstTerminator();
  for (MachineInstr &MI : *Preheader)
    if (isHwLoopSetup(MI))
      PrologInsertPt = std::next(MI.getIterator());
  return certificatePrologLiveness(PrologInsertPt) && certificateEpilogUses();
}

bool HaydnMultiStageSMS::resourcesConverged(
    const HaydnHazardRecognizer &HR) const {
  if (!HasValidPlan || II < 1 || Body.empty())
    return false;
  // SF1 const verifier path: replay the T4 oracle type (same
  // ModuloCyclePlacementOracle as HR.ModuloOracle) on a private instance.
  // This method is const; the search-path HR oracle is not copied here.
  haydn::bundle::ModuloCyclePlacementOracle VerifyOracle;
  VerifyOracle.init(static_cast<unsigned>(II), haydnDefaultMCFormats());
  ResourceScoreboard<HaydnFuncUnitWrapper> Scoreboard;
  Scoreboard.config(0, II - 1);
  const int N = static_cast<int>(Body.size());
  for (int I = 0; I < N; ++I) {
    if (!Sched[I].Scheduled)
      return false;
    MachineInstr *MI = Body[I]->getInstr();
    if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
      continue;
    const int Mod = Sched[I].ModuloCycle;
    if (Mod < 0 || Mod >= II)
      return false;
    if (!VerifyOracle.place(placementOpcode(*MI), Mod)) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: resourcesConverged format "
                        << "reject SU=" << I << " mod=" << Mod << "\n");
      return false;
    }
    if (HR.checkConflict(Scoreboard, *MI, Mod))
      return false;
    HR.emitInScoreboard(Scoreboard, *MI, Mod);
  }
  return true;
}

bool HaydnMultiStageSMS::preflightCFG() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_CFG)) {
    LastRejectReason = "PF-CFG-force";
    return false;
  }
  if (!LoopBB || !Preheader || !ExitBB)
    return false;
  if (Preheader->succ_size() != 1 || *Preheader->succ_begin() != LoopBB)
    return false;
  if (!llvm::is_contained(ExitBB->predecessors(), LoopBB))
    return false;
  if (IsSoftCounted && !hasSelfBackedge(*LoopBB))
    return false;
  return true;
}
bool HaydnMultiStageSMS::preflightPHI() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_PHI)) { LastRejectReason="PF-PHI-force"; return false; }
  if (LoopBB) for (const MachineInstr &MI : *LoopBB) if (MI.isPHI()) return false;
  return true;
}
bool HaydnMultiStageSMS::preflightTrip() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_TRIP)) { LastRejectReason="PF-TRIP-force"; return false; }
  return TripCountDef && hasSufficientTripCount();
}
bool HaydnMultiStageSMS::preflightStage() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_STAGE)) { LastRejectReason="PF-STAGE-force"; return false; }
  if (!HasValidPlan || NStages < 1) return false;
  for (int I = 0; I < NInstr; ++I) {
    const HaydnMultiStageNodeInfo &N = Sched[I];
    if (!N.Scheduled || N.Stage < 0 || N.Stage >= NStages ||
        N.ModuloCycle < 0 || N.ModuloCycle >= II)
      return false;
  }
  if (!certificateDistinctStageOccupancy()) {
    LastRejectReason = "stage-occupancy";
    return false;
  }
  if (!certificateLCDTwoIteration()) {
    LastRejectReason = "lcd-two-iteration";
    return false;
  }
  if (LoopBB) {
    AAResults *AA = DAG ? static_cast<HaydnScheduleDAGMI *>(DAG)
                              ->getAliasAnalysis()
                        : nullptr;
    const unsigned MemLCD = countMayAliasStoreLoadPairs(Body, AA);
    emitRemark(*LoopBB, "MultiStageLCD",
               "lcd two-iteration RecMII=" + Twine(RecMII) +
                   " edges=" + Twine(static_cast<unsigned>(LCDEdges.size())) +
                   " mem=" + Twine(MemLCD) + " stages=" + Twine(NStages) +
                   " lcd-as-windows placement-deps=data+anti+output+mem");
  }
  return true;
}
bool HaydnMultiStageSMS::preflightLive() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_LIVE)) { LastRejectReason="PF-LIVE-force"; return false; }
  if (!certificateLifetimesNoSpill()) {
    LastRejectReason = "lifetime-spill";
    return false;
  }
  if (!computeLivePhysFixpoint())
    return false;
  if (LoopBB)
    emitRemark(*LoopBB, "MultiStageLivePhys",
               "livephys fixpoint no-spill subreg II=" + Twine(II));
  return true;
}
bool HaydnMultiStageSMS::preflightAlt() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_ALT)) { LastRejectReason="PF-ALT-force"; return false; }
  // SF2: every tracked MultiSlot logical in the body must have a transient
  // member pin so placement saw real unit/entry geometry. Untracked
  // opcodes (no PlacementAlternatives) never consult the solver.
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  for (SUnit *SU : Body) {
    MachineInstr *MI = SU->getInstr();
    if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
      continue;
    const unsigned Log = MI->getOpcode();
    if (!hasPlacementAlternatives(Fmts, Log))
      continue;
    if (!MemberPin.count(MI)) {
      LastRejectReason = "member-pin";
      return false;
    }
  }
  return true;
}
bool HaydnMultiStageSMS::preflightBundle() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_BUNDLE)) {
    LastRejectReason = "PF-BUNDLE-force";
    return false;
  }
  if (!certificateKernelPlan()) {
    LastRejectReason = "kernel-plan";
    return false;
  }
  if (!certificateExactCommitPlan()) {
    LastRejectReason = "exact-commit-plan";
    return false;
  }
  // F44 pack-alias is a tryII placement certificate (II search can separate
  // a may-alias pair into different pack windows); preflightBundle only
  // re-verifies it as a belt-and-braces no-mutation gate.
  if (!certificatePackAlias()) {
    LastRejectReason = "pack-alias";
    return false;
  }
  if (!DAG)
    return false;
  const TargetSubtargetInfo &ST = DAG->MF.getSubtarget();
  // Same SearchAlts pin the II-search HR used (AIE setDesc on the
  // candidate MBB). Without it, checkConflict books the logical opcode
  // and the format overlay diverges from placement.
  HaydnHazardRecognizer HR(ST.getInstrInfo(), ST.getInstrItineraryData(),
                           /*IsPreRA=*/false, &SearchAlts);
  if (!resourcesConverged(HR)) {
    LastRejectReason = "resource-converge";
    return false;
  }
  if (LoopBB)
    emitRemark(*LoopBB, "MultiStageResources",
               "resources converged II=" + Twine(II) +
                   " retry=" + Twine(ResourceRetryCount));
  return true;
}
bool HaydnMultiStageSMS::preflightLate() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_LATE)) { LastRejectReason="PF-LATE-force"; return false; }
  if (!LoopBB) return false;
  for (const MachineInstr &MI : *LoopBB)
    if (MI.isCall() || MI.isInlineAsm()) return false;
  return true;
}
bool HaydnMultiStageSMS::runPreflight() {
  if (!preflightCFG()) { if (!LastRejectReason) LastRejectReason="PF-CFG"; return false; }
  if (!preflightPHI()) { if (!LastRejectReason) LastRejectReason="PF-PHI"; return false; }
  if (!preflightTrip()) { if (!LastRejectReason) LastRejectReason="PF-TRIP"; return false; }
  if (!preflightStage()) { if (!LastRejectReason) LastRejectReason="PF-STAGE"; return false; }
  if (!preflightLive()) { if (!LastRejectReason) LastRejectReason="PF-LIVE"; return false; }
  if (!preflightAlt()) { if (!LastRejectReason) LastRejectReason="PF-ALT"; return false; }
  if (!preflightBundle()) { if (!LastRejectReason) LastRejectReason="PF-BUNDLE"; return false; }
  if (!preflightLate()) { if (!LastRejectReason) LastRejectReason="PF-LATE"; return false; }
  if (!runSMSPeriodicCertificate(true)) { LastRejectReason="SMSPeriodicCertificate"; return false; }
  return true;
}
bool HaydnMultiStageSMS::runSMSPeriodicCertificate(bool PostRewriteStillValid) const {
  if (!HasValidPlan || II < 1) return false;
  SmallVector<SMSCertPhaseOp, 16> Ops;
  for (int I = 0, N = (int)Body.size(); I < N; ++I) {
    MachineInstr *MI = Body[I]->getInstr();
    if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI)) continue;
    SMSCertPhaseOp Op;
    Op.NormalizedPhase = (unsigned)Sched[I].ModuloCycle;
    Op.Opcode = MI->getOpcode();
    Op.StageCycles = 1;
    Ops.push_back(Op);
  }
  SMSCertLifecycle S = HaydnResourceCycle::runPeriodicCertificate((unsigned)II, Ops, PostRewriteStillValid);
  if (SMSPeriodicCertificate::mustRollback(S)) return false;
  return SMSPeriodicCertificate::originalLoopMustRemain(S) ||
         SMSPeriodicCertificate::mayDiscardOriginalLoop(S);
}


//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

bool HaydnMultiStageSMS::isCandidate(MachineBasicBlock &LoopBlock) {
  IsSoftCounted = false;

  // 1. Dedicated fallthrough preheader (needed for trip setup / soft adjust).
  Preheader = findFallThroughPreheader(LoopBlock);
  if (!Preheader) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: no fallthrough preheader\n");
    return false;
  }
  // Dedicated preheader ownership: sole successor is the kernel (PF-CFG).
  if (Preheader->succ_size() != 1 || *Preheader->succ_begin() != &LoopBlock) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: preheader not dedicated\n");
    return false;
  }

  // 2. Trip facts: hardware-loop setup OR soft countdown (hwloops OFF path).
  TripCountDef = findHwLoopSetup(*Preheader, LoopBlock);
  if (!TripCountDef) {
    TripCountDef = findSoftCountdownBump(LoopBlock);
    if (!TripCountDef || !hasSelfBackedge(LoopBlock)) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: no hwloop setup and no soft "
                           "countdown self-loop\n");
      return false;
    }
    IsSoftCounted = true;
    if (!isSoftCountdownBump(*TripCountDef, SoftTripReg))
      SoftTripReg = Register();
  }

  // 3. Shape checks.
  // Form A — LoopStart + PseudoLoopEnd terminator (self-loop CFG).
  // Form B — SET_HWLOOP{,_REG} targeting this MBB; body is fallthrough.
  // Form C — soft-counted self-loop with ADDI/SUBI countdown (no hwloop).
  const HaydnInstrInfo *TII =
      LoopBlock.getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  bool IsSetForm = false;
  bool HasPLE = false;
  if (!IsSoftCounted) {
    const unsigned SetupOpc = TripCountDef->getOpcode();
    IsSetForm = SetupOpc != Haydn::LoopStart &&
                TII->isHardwareLoopSetupOpcode(SetupOpc);
    if (IsSetForm) {
      if (TripCountDef->getNumOperands() >= 3 &&
          TripCountDef->getOperand(1).isMBB() &&
          TripCountDef->getOperand(2).isMBB() &&
          TripCountDef->getOperand(1).getMBB() !=
              TripCountDef->getOperand(2).getMBB()) {
        LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: multi-BB SET — skip\n");
        return false;
      }
    }
    auto Term = LoopBlock.getFirstInstrTerminator();
    HasPLE = Term != LoopBlock.end() && isZOLTerminator(*Term);
    if (SetupOpc == Haydn::LoopStart && !HasPLE) {
      LLVM_DEBUG(
          dbgs() << "HaydnMultiStageSMS: LoopStart without PseudoLoopEnd\n");
      return false;
    }
    if (!HasPLE && !IsSetForm) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: no ZOL shape\n");
      return false;
    }
  }

  // 4. Unique exit for epilogue clones.
  ExitBB = findUniqueExit(LoopBlock);
  if (!ExitBB) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: no unique exit\n");
    return false;
  }

  // 5. Body size: at least 2 ops (else no overlap opportunity), capped.
  unsigned NBody = 0;
  for (const MachineInstr &MI : LoopBlock) {
    if (isZOLTerminator(MI) || isSkippableBodyMI(MI))
      continue;
    ++NBody;
  }
  if (NBody < 2 || NBody > MaxBodyInstrs) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: body size " << NBody
                      << " out of range\n");
    return false;
  }

  LoopBB = &LoopBlock;
  LLVM_DEBUG({
    dbgs() << "HaydnMultiStageSMS: candidate " << LoopBlock.getName()
           << " body=" << NBody;
    if (IsSoftCounted)
      dbgs() << " form=soft-countdown";
    else
      dbgs() << (IsSetForm ? " form=SET_HWLOOP" : " form=LoopStart")
             << (HasPLE ? "+PLE" : "");
    dbgs() << "\n";
  });
  return true;
}

int HaydnMultiStageSMS::getResMII(MachineBasicBlock &LoopBlock) const {
  // SF6 ResMII = max(row-capacity, logical primary-slot, pinned SlotCounts).
  // Row term: ceil((NBody + ceil(N_E2only/2)) / 3). Logical slot term: AIE
  // getSlotCounts / Counts.max() (AIEPostPipeliner.cpp:216-228) over
  // MultiSlot unused-slot assignment. Pin term: booked-member getSlotSet
  // occupancy (AIE SlotCounts on the materialized opcode). moduloPrimarySlotII
  // / countE2OnlyBodyOps enumerate PlacementAlternatives, which members do
  // not have — feeding placementOpcode skipped every slot increment and
  // left SlotMII=1. RecMII is computed separately before StartII.
  unsigned NBody = 0;
  SmallVector<unsigned, 16> LogicalOps;
  SmallVector<unsigned, 16> BookedOps;
  auto pushBodyOp = [&](const MachineInstr &MI) {
    if (isZOLTerminator(MI) || isSkippableBodyMI(MI))
      return;
    if (MI.isPseudo() && !MI.isCopy())
      return;
    ++NBody;
    LogicalOps.push_back(MI.getOpcode());
    BookedOps.push_back(placementOpcode(MI));
  };
  if (!Body.empty()) {
    for (SUnit *SU : Body) {
      if (MachineInstr *MI = SU->getInstr())
        pushBodyOp(*MI);
    }
  } else {
    for (const MachineInstr &MI : LoopBlock)
      pushBodyOp(MI);
  }
  const unsigned NE2Only = haydn::bundle::countE2OnlyBodyOps(LogicalOps);
  const int RowMII = static_cast<int>(
      haydn::bundle::moduloRowCapacityII(NBody, NE2Only));
  const int LogicalSlotMII = static_cast<int>(
      haydn::bundle::moduloPrimarySlotII(LogicalOps));
  // SF9 SlotCounts bias residual: booked-member primary-slot occupancy.
  // AIE PostPipeliner.cpp:216-228 counts the materialized slot, not the
  // logical unused-slot assignment.
  HaydnMultiStageSlotCounts PinSlots;
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  for (unsigned Opc : BookedOps) {
    const MCSlotKind Kind = Fmts.getSlotKind(Opc);
    if (Kind == MCSlotKind())
      continue;
    const MCSlotInfo *SI = Fmts.getSlotInfo(Kind);
    if (!SI)
      continue;
    SlotBits Bits = SI->getSlotSet();
    if (!Bits)
      Bits = SI->getConflictSet();
    PinSlots += HaydnMultiStageSlotCounts(Bits);
  }
  const int PinSlotMII = PinSlots.max();
  const int SlotMII = std::max(LogicalSlotMII, PinSlotMII);
  const int MII = std::max(std::max(RowMII, SlotMII), 1);
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: ResMII=" << MII << " (NBody="
                    << NBody << " E2only=" << NE2Only
                    << " LogicalSlotMII=" << LogicalSlotMII
                    << " PinSlotMII=" << PinSlotMII << ")\n");
  return MII;
}

//===----------------------------------------------------------------------===//
// Loop-carried windows + pipe schedule (AIE PostPipeliner core)
//===----------------------------------------------------------------------===//

HaydnMultiStageSlotCounts
HaydnMultiStageSMS::conflictSlotsForMI(const MachineInstr &MI) const {
  // AIE getConflictCounts (AIESlotUtils.cpp:23-26): MCSlotInfo conflict set.
  // Overlay: booked / pinned member FieldSlots when conflict bits are empty.
  const unsigned Booked = placementOpcode(MI);
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  const MCSlotKind Kind = Fmts.getSlotKind(Booked);
  if (Kind == MCSlotKind())
    return HaydnMultiStageSlotCounts();
  const MCSlotInfo *SI = Fmts.getSlotInfo(Kind);
  if (!SI)
    return HaydnMultiStageSlotCounts();
  SlotBits Bits = SI->getConflictSet();
  if (!Bits)
    Bits = SI->getSlotSet();
  return HaydnMultiStageSlotCounts(Bits);
}

void HaydnMultiStageSMS::biasForLocalResourceContention(
    HaydnMultiStageNodeInfo &NI, const SUnit &SU) {
  // AIE PostPipeliner::biasForLocalResourceContention
  // (AIEPostPipeliner.cpp:286-318).
  HaydnMultiStageSlotCounts Slots(NI.Slots);
  int PredEarliest = std::numeric_limits<int>::max();
  SmallSet<int, 8> UniqueAncestors;
  int Count = 0;
  for (const SDep &Dep : SU.Preds) {
    if (Dep.getKind() != SDep::Data)
      continue;
    const int P = static_cast<int>(Dep.getSUnit()->NodeNum);
    if (P < 0 || P >= NInstr)
      continue;
    const HaydnMultiStageNodeInfo &Pred = Sched[P];
    if (P >= static_cast<int>(SU.NodeNum))
      continue;
    if (UniqueAncestors.insert(P).second) {
      Slots += Pred.Slots;
      ++Count;
    }
    PredEarliest = std::min(PredEarliest, Pred.Earliest);
  }
  if (Count > 0 && Slots.max() > Count) {
    const int NewEarliest = PredEarliest + Slots.max() - 1;
    if (NewEarliest > NI.Earliest) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: bias SU=" << SU.NodeNum
                        << " MaxSlots=" << Slots.max() << " Earliest "
                        << NI.Earliest << " -> " << NewEarliest << "\n");
      NI.Earliest = NewEarliest;
      LastResourceBias = true;
    }
  }
}

void HaydnMultiStageSMS::computeForward() {
  // AIE PostPipeliner::computeForward (AIEPostPipeliner.cpp:320-360).
  // Earliest follows every placement-relevant edge (AIE computeForward
  // signed-latency succs). Anti/Output with unsigned latency 0 still
  // order same-cycle WAR/WAW; Barrier/Artificial seams stay excluded.
  for (int K = 0; K < NInstr; ++K) {
    HaydnMultiStageNodeInfo &Me = Sched[K];
    SUnit &SU = TwoCopyDAG->SUnits[K];
    biasForLocalResourceContention(Me, SU);
    for (auto &Dep : SU.Preds) {
      if (Dep.getKind() != SDep::Data)
        continue;
      int P = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (P < 0 || P >= NInstr || P >= K)
        continue;
      const HaydnMultiStageNodeInfo &Pred = Sched[P];
      Me.Ancestors.insert(P);
      for (int Anc : Pred.Ancestors)
        Me.Ancestors.insert(Anc);
    }
    for (auto &Dep : SU.Succs) {
      SUnit *Succ = Dep.getSUnit();
      if (Succ->isBoundaryNode())
        continue;
      const int SNum = static_cast<int>(Succ->NodeNum);
      // AIE computeForward walks every signed-latency succ
      // (AIEPostPipeliner.cpp:346-358). Two-copy Anti/Output stay off
      // the Earliest walk: LLVM 22 publishes unsigned latency 0, which
      // would serialize the concatenated body. Those LCDs still fold
      // into LCDLatest / addLCD.
      if (!isPlacementDep(Dep))
        continue;
      if ((Dep.getKind() == SDep::Anti || Dep.getKind() == SDep::Output) &&
          SNum >= NInstr)
        continue;
      HaydnMultiStageNodeInfo &SInfo = Sched[SNum];
      const int NewEarliest = Me.Earliest + depLat(Dep);
      if (NewEarliest > SInfo.Earliest)
        SInfo.Earliest = NewEarliest;
    }
  }
}

bool HaydnMultiStageSMS::computeBackward() {
  // AIE PostPipeliner::computeBackward (AIEPostPipeliner.cpp:362-394).
  bool Changed = false;
  auto addOffspring = [&](HaydnMultiStageNodeInfo &NI, int E) {
    if (NI.Offspring.insert(E).second)
      Changed = true;
  };
  for (int K = NInstr - 1; K >= 0; --K) {
    SUnit &SU = TwoCopyDAG->SUnits[K];
    HaydnMultiStageNodeInfo &Me = Sched[K];
    const int Latest = Me.Latest;
    for (auto &Dep : SU.Preds) {
      if (Dep.getKind() != SDep::Data)
        continue;
      int P = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (P < 0 || P >= NInstr)
        continue;
      HaydnMultiStageNodeInfo &Pred = Sched[P];
      addOffspring(Pred, K);
      for (int Offs : Me.Offspring)
        addOffspring(Pred, Offs);
      int NewLatest = Latest - depLat(Dep);
      if (NewLatest < Pred.Latest) {
        Pred.Latest = NewLatest;
        Changed = true;
      }
    }
  }
  return Changed;
}

void HaydnMultiStageSMS::computeEffectiveHeight() {
  // AIE PostPipeliner::computeEffectiveHeight (AIEPostPipeliner.cpp:482-515).
  for (int K = NInstr - 1; K >= 0; --K) {
    const SUnit &SU = TwoCopyDAG->SUnits[K];
    int MaxEH = 0;
    for (const SDep &Dep : SU.Succs) {
      if (Dep.getKind() != SDep::Data)
        continue;
      const int S = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (S >= NInstr)
        continue;
      const int Latency = depLat(Dep);
      if (Sched[K].Earliest + Latency < Sched[S].Earliest)
        continue;
      MaxEH = std::max(MaxEH, Latency + Sched[S].EffectiveHeight);
    }
    Sched[K].EffectiveHeight = MaxEH;
  }
}

bool HaydnMultiStageSMS::computeLoopCarriedParameters() {
  // AIE PostPipeliner::computeLoopCarriedParameters (AIEPostPipeliner.cpp:517-606).
  LastResourceBias = false;
  for (HaydnMultiStageNodeInfo &N : Sched.Nodes) {
    N.Earliest = 0;
    N.Latest = -1;
    N.Ancestors.clear();
    N.Offspring.clear();
    N.EffectiveHeight = 0;
    N.LCDLatest = -1;
    N.Scheduled = false;
    N.Cycle = 0;
    N.TweakedEarliest.reset();
    N.TweakedLatest.reset();
    N.Slots = HaydnMultiStageSlotCounts();
  }
  for (int K = 0; K < NInstr; ++K) {
    if (MachineInstr *MI = TwoCopyDAG->SUnits[K].getInstr())
      Sched[K].Slots = conflictSlotsForMI(*MI);
  }
  computeForward();
  // Bound the Latest fixpoint: Data-only Latest only decreases, but a
  // cyclic first-copy edge would otherwise walk Latest to -inf (T4 hang).
  // Hitting the cap means the first-copy graph is cyclic; a partial
  // Latest is wider than the true window, so fail closed and retry II.
  int BackwardGuard = 0;
  const int BackwardCap = std::max(NInstr * 2, 8);
  while (computeBackward() && ++BackwardGuard < BackwardCap)
    ;
  if (BackwardGuard >= BackwardCap) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: Latest fixpoint did not "
                         "converge (T4 cycle cap)\n");
    return false;
  }
  computeRecMIIFromDAG();
  for (int K = 0; K < NInstr; ++K) {
    HaydnMultiStageNodeInfo &Me = Sched[K];
    HaydnMultiStageSlotCounts ASlots(Me.Slots);
    for (int A : Me.Ancestors) {
      if (A >= 0 && A < NInstr)
        ASlots += Sched[A].Slots;
    }
    HaydnMultiStageSlotCounts OSlots(Me.Slots);
    for (int O : Me.Offspring) {
      if (O >= 0 && O < NInstr)
        OSlots += Sched[O].Slots;
    }
    const int NewEarliest = 0 + (ASlots.max() - 1);
    const int NewLatest = -1 - (OSlots.max() - 1);
    if (NewEarliest > Me.Earliest || NewLatest < Me.Latest)
      LastResourceBias = true;
    Me.Earliest = std::max(Me.Earliest, NewEarliest);
    Me.Latest = std::min(Me.Latest, NewLatest);
  }
  for (int K = 0; K < NInstr; ++K) {
    const int KNext = K + NInstr;
    const int Earliest = Sched[KNext].Earliest - II;
    Sched[K].Earliest = std::max(Sched[K].Earliest, Earliest);
  }
  for (int K = 0; K < NInstr; ++K) {
    HaydnMultiStageNodeInfo &Me = Sched[K];
    int LCDLatest = Me.Latest;
    SUnit &SU = TwoCopyDAG->SUnits[K];
    for (auto &Dep : SU.Succs) {
      const int S = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (S < NInstr)
        continue;
      const int Earliest = Sched[S - NInstr].Earliest;
      LCDLatest = std::min(LCDLatest, Earliest - depLat(Dep));
    }
    Me.LCDLatest = LCDLatest;
  }
  computeEffectiveHeight();
  for (HaydnMultiStageNodeInfo &N : Sched.Nodes) {
    N.StaticEarliest = N.Earliest;
    N.StaticLatest = N.Latest;
  }
  MinLength = computeMinScheduleLength();
  LinearLength = 0;
  for (int K = 0; K < NInstr; ++K)
    LinearLength = std::max(LinearLength, Sched[K].Earliest + 1);
  return true;
}

int HaydnMultiStageSMS::computeMinScheduleLength() const {
  int ML = II;
  const int Cap = II * std::max(NInstr + 4, 8);
  for (int K = 0; K < NInstr; ++K) {
    const HaydnMultiStageNodeInfo &Node = Sched[K];
    while (Node.Earliest > Node.Latest + ML) {
      ML += II;
      if (ML > Cap) {
        ML = Cap;
        break;
      }
    }
  }
  return ML;
}

void HaydnMultiStageSMS::schedulePipeNode(SUnit &SU, int Cycle,
                                         HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::scheduleNode (AIEPostPipeliner.cpp:232-282).
  Sched[static_cast<int>(SU.NodeNum)].Cycle = Cycle;
  for (auto &Dep : SU.Succs) {
    // AIE scheduleNode walks every signed-latency succ
    // (AIEPostPipeliner.cpp:239-256). Barrier/Artificial two-copy seams
    // stay excluded. Anti/Output only constrain the intra-iteration copy
    // (unsigned latency 0 would serialize copy-1).
    SUnit *Succ = Dep.getSUnit();
    if (Succ->isBoundaryNode())
      continue;
    const int SNum = static_cast<int>(Succ->NodeNum);
    if (!isPlacementDep(Dep))
      continue;
    if ((Dep.getKind() == SDep::Anti || Dep.getKind() == SDep::Output) &&
        SNum >= NInstr)
      continue;
    int Latency = depLat(Dep);
    const int NewEarliest = Cycle + Latency;
    if (NewEarliest > Strategy.earliest(*Succ)) {
      Sched[SNum].LastEarliestPusher = static_cast<int>(SU.NodeNum);
      Sched[static_cast<int>(SU.NodeNum)].NumPushedEarliest++;
      Strategy.setEarliest(SNum, NewEarliest);
      Strategy.setChanged();
    }
  }
  for (auto &Dep : SU.Preds) {
    // AIE scheduleNode walks every pred; Haydn keeps Data for Latest
    // (AIE computeBackward is Data-only) plus intra-iteration Anti/Output
    // so WAR/WAW order the same cycle without two-copy serialization.
    SUnit *Pred = Dep.getSUnit();
    if (Pred->isBoundaryNode())
      continue;
    const int PNum = static_cast<int>(Pred->NodeNum);
    if (Dep.getKind() == SDep::Anti || Dep.getKind() == SDep::Output) {
      if (PNum >= NInstr)
        continue;
    } else if (Dep.getKind() != SDep::Data) {
      continue;
    }
    int Latency = depLat(Dep);
    const int OldLatest = Strategy.latest(*Pred);
    const int NewLatest = Cycle - Latency;
    if (NewLatest < OldLatest) {
      Sched[PNum].LastLatestPusher = static_cast<int>(SU.NodeNum);
      Sched[static_cast<int>(SU.NodeNum)].NumPushedLatest++;
      Strategy.setLatest(PNum, NewLatest);
      Strategy.setChanged();
    }
  }
  int Next = static_cast<int>(SU.NodeNum) + NInstr;
  if (Next < static_cast<int>(Sched.Nodes.size()))
    Sched[Next].Earliest = std::max(Sched[Next].Earliest, Cycle + II);
}

int HaydnMultiStageSMS::mostUrgent(HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::mostUrgent (AIEPostPipeliner.cpp:788-825).
  if (FirstUnscheduled > LastUnscheduled)
    return -1;
  while (FirstUnscheduled <= LastUnscheduled && FirstUnscheduled < NInstr &&
         Sched[FirstUnscheduled].Scheduled)
    ++FirstUnscheduled;
  while (LastUnscheduled >= FirstUnscheduled && LastUnscheduled >= 0 &&
         Sched[LastUnscheduled].Scheduled)
    --LastUnscheduled;
  if (FirstUnscheduled > LastUnscheduled || FirstUnscheduled >= NInstr ||
      LastUnscheduled < 0)
    return -1;
  auto notScheduled = [this](const SDep &Dep) {
    SUnit *SU = Dep.getSUnit();
    if (SU->isBoundaryNode())
      return false;
    int N = static_cast<int>(SU->NodeNum);
    return N < NInstr && !Sched[N].Scheduled;
  };
  int Best = -1;
  for (int K = FirstUnscheduled; K <= LastUnscheduled; ++K) {
    const SUnit &SU = TwoCopyDAG->SUnits[K];
    auto &Edges = Strategy.fromTop() ? SU.Preds : SU.Succs;
    if (Sched[K].Scheduled || llvm::any_of(Edges, notScheduled))
      continue;
    if (Best == -1 || Strategy.better(SU, TwoCopyDAG->SUnits[Best]))
      Best = K;
  }
  return Best;
}

void HaydnMultiStageSMS::resetPipeSchedule(bool FullReset) {
  // AIE PostPipeliner::resetSchedule (AIEPostPipeliner.cpp:827-841).
  PipeScoreboard.clear();
  int K = 0;
  for (HaydnMultiStageNodeInfo &N : Sched.Nodes) {
    N.reset(FullReset);
    if (K < NInstr) {
      N.Earliest = N.TweakedEarliest ? *N.TweakedEarliest : N.StaticEarliest;
      N.Latest = N.TweakedLatest ? *N.TweakedLatest : N.StaticLatest;
    }
    ++K;
  }
  FirstUnscheduled = 0;
  LastUnscheduled = NInstr - 1;
}

bool HaydnMultiStageSMS::scheduleFirstIteration(
    HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::scheduleFirstIteration (AIEPostPipeliner.cpp:843-908).
  for (int K = 0; K < NInstr; ++K) {
    const int N = mostUrgent(Strategy);
    if (N < 0)
      return false;
    SUnit &SU = TwoCopyDAG->SUnits[N];
    MachineInstr *const MI = SU.getInstr();
    const int Earliest = Strategy.earliest(SU);
    const int Latest = Strategy.latest(SU);
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: place " << N << " in ["
                      << Earliest << "," << Latest << "]\n");
    if (Earliest > Latest) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: latency window empty\n");
      return false;
    }
    auto OptCycle = Strategy.fitInInterval(SU, Earliest, Latest, II, *PipeHR,
                                           PipeScoreboard);
    if (!OptCycle) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: fitInInterval miss SU=" << N
                        << "\n");
      Sched[N].TweakedEarliest.reset();
      Sched[N].TweakedLatest.reset();
      if (Sched[N].LastEarliestPusher) {
        const int P = *Sched[N].LastEarliestPusher;
        if (P >= 0 && P < NInstr && Sched[P].Scheduled) {
          const int Delayed = Sched[P].Cycle + 1;
          if (!Sched[P].TweakedEarliest || *Sched[P].TweakedEarliest < Delayed) {
            Sched[P].TweakedEarliest = Delayed;
            Strategy.setChanged();
          }
        }
      }
      return false;
    }
    const int Actual = *OptCycle;
    Strategy.selected(SU);
    const int ModCycle = Actual % II;
    // SF1 accept path: mutate the T4 HR modulo oracle. A reject here
    // means fitInInterval skipped the format gate (stale II); fail closed.
    if (!PipeHR->placeModulo(*MI, ModCycle)) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: format oracle reject SU=" << N
                        << " mod=" << ModCycle << "\n");
      return false;
    }
    // SF7: AIE first-iter emit horizon is min(II+PD, Size-PD)
    // (AIEPostPipeliner scoreboard wrap). ProductMaxInstrStageCycles==1
    // makes one wrap enough; the static pin below trips if a multi-cycle
    // itinerary lands.
    int Cycle = ModCycle;
    const int PD = std::max(PipeHR->getPipelineDepth(), 1);
    if (haydn::restriction::ProductMaxInstrStageCycles > 1) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: multi-cycle itinerary pin "
                           "tripped ProductMaxInstrStageCycles="
                        << haydn::restriction::ProductMaxInstrStageCycles
                        << "\n");
      return false;
    }
    const int Horizon = std::min(II + PD, ScoreboardSize - PD);
    while (Cycle < Horizon) {
      if (PipeHR->checkConflict(PipeScoreboard, *MI, Cycle)) {
        LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: scoreboard conflict SU=" << N
                          << " cycle=" << Cycle << "\n");
        return false;
      }
      PipeHR->emitInScoreboard(PipeScoreboard, *MI, Cycle);
      Cycle += II;
    }
    schedulePipeNode(SU, Actual, Strategy);
    Sched.commitCycle(N);
  }
  int MaxStage = 0;
  for (int K = 0; K < NInstr; ++K) {
    Sched[K].update(II);
    MaxStage = std::max(MaxStage, Sched[K].Stage);
  }
  NStages = MaxStage + 1;
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: first-iter NStages=" << NStages
                    << " II=" << II << "\n");
  if (NStages > 4)
    return false;
  if (NStages < 1)
    return false;
  // AIE checkStages accepts NS==1 (AIEPostPipeliner.cpp:1678-1695).
  // A legal single-stage schedule is kernel-only — no manufactured
  // overlap. Materialize skips peel MBBs when NStages==1.
  // Trip is an II-search constraint: insufficient static trip rejects
  // this II so a later II (fewer stages) can still win. Auto-applying
  // peelSideEffectFree here without AIE cycle rotation would drop an
  // epilogue completion (wrong last-iteration late stages). The SEF
  // helper stays available; rotation is the remaining SF10 optional.
  if (!hasSufficientTripCount()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: trip < NStages=" << NStages
                      << " — reject II (retry)\n");
    return false;
  }
  return true;
}

bool HaydnMultiStageSMS::scheduleOtherIterations(
    HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::scheduleOtherIterations (AIEPostPipeliner.cpp:949-1067).
  auto isOnEarliestChain = [&](int Start, int Target) {
    std::optional<int> Prev = Sched[Start].LastEarliestPusher;
    SmallSet<int, 8> Seen;
    int Guard = 0;
    const int Cap = static_cast<int>(Sched.Nodes.size()) + 1;
    while (Prev && Guard++ < Cap) {
      const int P = *Prev;
      if (P == Target)
        return true;
      if (P < 0 || P >= static_cast<int>(Sched.Nodes.size()) ||
          !Seen.insert(P).second)
        return false;
      Prev = Sched[P].LastEarliestPusher;
    }
    return false;
  };
  for (int K = 0; K < NInstr; ++K) {
    const int N = K + NInstr;
    SUnit &SU = TwoCopyDAG->SUnits[N];
    HaydnMultiStageNodeInfo &Node = Sched[N];
    const SUnit &ModuloSU = TwoCopyDAG->SUnits[K];
    HaydnMultiStageNodeInfo &ModuloNode = Sched[K];
    const int Earliest = Node.Earliest;
    const int Insert = ModuloNode.Cycle + II;
    if (Earliest > Insert) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: other-iter SU=" << N
                        << " Earliest=" << Earliest << " Insert=" << Insert
                        << "\n");
      const bool HasScheduleSlack = Strategy.mobility(ModuloSU) > 0;
      const int ScheduleLengthOffset =
          Strategy.latest(ModuloSU) -
          Sched[static_cast<int>(ModuloSU.NodeNum)].Latest;
      const int OriginalLatest = ModuloNode.StaticLatest + ScheduleLengthOffset;
      const bool CanPlaceLaterInOriginalInterval =
          ModuloNode.Earliest < OriginalLatest;
      const bool DelayReducesGap =
          !isOnEarliestChain(N, static_cast<int>(ModuloSU.NodeNum));
      const bool CanDelayModuloNode =
          HasScheduleSlack ||
          (CanPlaceLaterInOriginalInterval && DelayReducesGap);
      if (CanDelayModuloNode) {
        ModuloNode.TweakedEarliest = ModuloNode.Cycle + 1;
        Strategy.setChanged();
        return false;
      }
      if (Node.LastEarliestPusher && *Node.LastEarliestPusher < NInstr) {
        HaydnMultiStageNodeInfo &Pusher = Sched[*Node.LastEarliestPusher];
        if (Strategy.mobility(
                TwoCopyDAG->SUnits[*Node.LastEarliestPusher]) > 0) {
          ModuloNode.TweakedEarliest.reset();
          Pusher.TweakedLatest = Pusher.Latest - 1;
          Strategy.setChanged();
          return false;
        }
      }
      return false;
    }
    schedulePipeNode(SU, Insert, Strategy);
  }
  const int PipelineDepth = std::max(PipeHR->getPipelineDepth(), 1);
  // SF1 steady-state format check: each iteration issues the IDENTICAL
  // modulo cycle set, so format feasibility is iteration-invariant — verify
  // once against a fresh local oracle seeded with the accepted copy-0
  // placements (place() per op; a second place of the same op must fail,
  // which is exactly why this is NOT inside the Start loop). The member
  // oracle cannot be reused: first-iteration already placed every op.
  haydn::bundle::ModuloCyclePlacementOracle SteadyOracle;
  SteadyOracle.init(static_cast<unsigned>(II), haydnDefaultMCFormats());
  for (int I = 0; I < NInstr; ++I) {
    MachineInstr &MI = *TwoCopyDAG->SUnits[I].getInstr();
    if (!SteadyOracle.place(placementOpcode(MI), Sched[I].ModuloCycle)) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: steady-state format conflict"
                        << " SU=" << I << " mod=" << Sched[I].ModuloCycle
                        << "\n");
      return false;
    }
  }
  ResourceScoreboard<HaydnFuncUnitWrapper> Resources;
  Resources.config(0, 2 * II + PipelineDepth);
  for (int Start = 0; Start < II + PipelineDepth; Start += II) {
    (void)Start;
    for (int I = 0; I < NInstr; ++I) {
      MachineInstr &MI = *TwoCopyDAG->SUnits[I].getInstr();
      int ModCycle = Sched[I].ModuloCycle;
      if (PipeHR->checkConflict(Resources, MI, ModCycle)) {
        LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: other-iter resource conflict"
                          << " SU=" << I << " mod=" << ModCycle << "\n");
        return false;
      }
      PipeHR->emitInScoreboard(Resources, MI, ModCycle);
    }
    for (int I = 0; I < II; ++I)
      Resources.advance();
  }
  return true;
}

bool HaydnMultiStageSMS::scheduleWithStrategy(HaydnMultiStageStrategy &S) {
  if (!scheduleFirstIteration(S))
    return false;
  return scheduleOtherIterations(S);
}

bool HaydnMultiStageSMS::tryPipeApproaches(HaydnHazardRecognizer &HR) {
  // AIE tryApproaches ConfigStrategy lattice (AIEPostPipeliner.cpp:1441-1473).
  // AIE then calls SWPSolver (AIESWPSolver.cpp, Z3). Haydn does not
  // ship LLVM_WITH_Z3 and has no pragma-II; the seat is fail-closed.
  PipeHR = &HR;
  LastSWPSolverStatus = "unavailable";
  const int PipeDepth = std::max(HR.getPipelineDepth(), 1);
  ScoreboardSize = 2 * II + PipeDepth;
  PipeScoreboard.config(0, ScoreboardSize - 1);
  using Prio = HaydnConfigStrategy::PriorityComponent;
  using Plc = HaydnConfigStrategy::PlacementModifier;
  // AIE tryApproaches uses HeuristicRuns=8. Dense MAC bodies (bkfir)
  // times that lattice livelocked post-RA; shrink the run count so the
  // Latest / pusher caps stay the hang bound.
  const int HeuristicRuns = NInstr > LargeBodyInstrs ? 2 : 8;
  const HaydnConfigStrategy::Configuration Heuristics[] = {
      {1, true, false, 1, {Prio::NodeNum}, {}},
      {0, true, false, HeuristicRuns, {Prio::NodeNum}, {}},
      {1, true, false, HeuristicRuns, {Prio::Latest}, {}},
      {1, true, false, HeuristicRuns, {Prio::Critical}, {}},
      {1, true, false, HeuristicRuns, {Prio::Latest, Prio::Sibling}, {}},
      {1, true, false, HeuristicRuns, {Prio::DepLength, Prio::Latest}, {}},
      {1, true, false, HeuristicRuns, {Prio::Critical, Prio::LCDLatest}, {}},
      {1, true, false, HeuristicRuns, {Prio::Liveness, Prio::Latest}, {}},
      {1,
       true,
       false,
       HeuristicRuns,
       {Prio::EffHeight, Prio::Latest},
       {Plc::DeferNonCritical}},
      {0, false, false, 2, {Prio::Critical, Prio::LCDLatest}, {}},
      {1, false, false, 2, {Prio::Critical, Prio::LCDLatest}, {}},
      {1, false, false, 1, {Prio::NodeNum}, {}},
  };
  for (const auto &Config : Heuristics) {
    HaydnConfigStrategy Strategy(*TwoCopyDAG, Sched,
                                 MinLength + Config.ExtraStages * II,
                                 Config.TopDown, Config.Alternate,
                                 Config.Components, Config.Modifiers);
    resetPipeSchedule(/*FullReset=*/true);
    for (int Run = 0; Run < Config.Runs && Run < HeuristicRuns; ++Run) {
      if (scheduleWithStrategy(Strategy)) {
        LastStrategyName = "Config";
        return true;
      }
      if (!Strategy.checkAndResetChanged())
        break;
      resetPipeSchedule(/*FullReset=*/false);
    }
  }
  HaydnIterCountSlackStrategy Relaxed(*TwoCopyDAG, Sched, MinLength + II);
  resetPipeSchedule(/*FullReset=*/true);
  if (scheduleWithStrategy(Relaxed)) {
    LastStrategyName = "IterCountSlack";
    return true;
  }
  // AIE PostPipeliner::tryApproaches last arm is SWPSolver (Z3).
  // Without Z3 / pragma-II the arm does not invent a second solver.
  LastSWPSolverStatus = "unavailable";
  return false;
}

bool HaydnMultiStageSMS::computeASAPEarliest() {
  // Intra-iteration ASAP is computeForward; LCD fold is computeLoopCarriedParameters.
  // Seed conflict-set SlotCounts first so biasForLocalResourceContention
  // (AIEPostPipeliner.cpp:286-318) participates in LinearLength. Without
  // this, MaxII is the unbiased window and SF9 bias cannot widen search.
  if (!TwoCopyDAG || NInstr < 1)
    return false;
  LastResourceBias = false;
  for (int K = 0; K < NInstr; ++K) {
    Sched[K].Earliest = 0;
    Sched[K].Ancestors.clear();
    if (MachineInstr *MI = TwoCopyDAG->SUnits[K].getInstr())
      Sched[K].Slots = conflictSlotsForMI(*MI);
    else
      Sched[K].Slots = HaydnMultiStageSlotCounts();
  }
  computeForward();
  LinearLength = 0;
  for (int I = 0; I < NInstr; ++I)
    LinearLength = std::max(LinearLength, Sched[I].Earliest + 1);
  return LinearLength >= 1;
}

bool HaydnMultiStageSMS::tryII(int TryII, HaydnHazardRecognizer &HR) {
  assert(TryII >= 1);
  if (TryII < RecMII)
    return false;
  II = TryII;
  // SF2: HR occupancySlots / resolveBookingOpcode read the transient pin
  // so checkConflict books the same member SlotSet the oracle searched.
  // Restore the function-lifetime map on every exit of this attempt.
  struct AltDescScope {
    HaydnHazardRecognizer &HR;
    HaydnAlternateDescriptors *Saved;
    AltDescScope(HaydnHazardRecognizer &H, HaydnAlternateDescriptors *A)
        : HR(H), Saved(H.getAlternateDescriptors()) {
      HR.setAlternateDescriptors(A);
    }
    ~AltDescScope() { HR.setAlternateDescriptors(Saved); }
  } PinScope(HR, &SearchAlts);
  // SF1: seed the HR modulo oracle wholesale — a rejected II discards
  // the state (next tryII re-inits). Placement probes use canPlaceModulo;
  // the accept path mutates via placeModulo. checkConflict now also
  // asks isFormatAvailable / getFormatOrNull on booked Slots.
  HR.ModuloOracle.init(static_cast<unsigned>(TryII), haydnDefaultMCFormats());
  if (!computeLoopCarriedParameters())
    return false;
  if (TryII < RecMII) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: tryII=" << TryII
                      << " < RecMII=" << RecMII << "\n");
    return false;
  }
  if (!tryPipeApproaches(HR))
    return false;
  HasValidPlan = true;
  if (!certificateDistinctStageOccupancy()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject distinct-stage\n");
    HasValidPlan = false;
    ++ResourceRetryCount; // II-search certificate miss; next TryII continues
    return false;
  }
  if (!certificateLCDTwoIteration()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject LCD certificate\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  if (!certificateLifetimesNoSpill()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject lifetime certificate\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  if (!resourcesConverged(HR)) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject resourcesConverged\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  // F44 placement-legality certificate (golden Constraints:67): no
  // may-alias store→load pair inside one modulo-cycle pack group. This is a
  // property of the PLACEMENT itself, so it gates every tryII candidate
  // (raising II may separate the pair into different pack windows).
  if (!certificatePackAlias()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject pack-alias\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  // SF3: format infeasibility rejects this II — never sequentialize.
  if (!certificateExactCommitPlan()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject exact-commit (format)\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  if (!certificateKernelPlan()) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject kernel-plan\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  // Planned parcels are counted from ExactCommitPlan, not copied from II.
  const int Planned = countPlannedParcels();
  if (Planned != II) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject planned-parcels="
                      << Planned << " != II=" << II << "\n");
    HasValidPlan = false;
    ++ResourceRetryCount;
    return false;
  }
  MeasuredII = Planned;
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: tryII success II=" << II
                    << " NStages=" << NStages << "\n");
  return true;
}

//===----------------------------------------------------------------------===//
// Materialize
//===----------------------------------------------------------------------===//

bool HaydnMultiStageSMS::adjustSoftTripCount(int Delta) const {
  assert(IsSoftCounted && Preheader);
  if (Delta == 0)
    return true;
  Register Trip = SoftTripReg;
  if (!Trip && TripCountDef)
    (void)isSoftCountdownBump(*TripCountDef, Trip);
  if (!Trip || !Trip.isPhysical() || Trip == Haydn::R0)
    return false;
  if (Delta < -32 || Delta > 31)
    return false;
  MachineFunction &MF = *Preheader->getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator Ins = Preheader->getFirstTerminator();
  DebugLoc DL;
  if (!Preheader->empty())
    DL = Preheader->begin()->getDebugLoc();
  // Insert a pure preheader correction; leave the body countdown intact.
  BuildMI(*Preheader, Ins, DL, TII->get(Haydn::ADDI32), Trip)
      .addReg(Trip)
      .addImm(Delta);
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: soft-trip " << printReg(Trip)
                    << " += " << Delta << " in preheader\n");
  return true;
}

bool HaydnMultiStageSMS::adjustTripCount(int Delta) const {
  assert(TripCountDef && Preheader);
  if (Delta == 0)
    return true;
  if (IsSoftCounted)
    return adjustSoftTripCount(Delta);

  const unsigned Opc = TripCountDef->getOpcode();
  const HaydnInstrInfo *TII =
      Preheader->getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();

  // Form A — LoopStart: $src, simm6:$adj (folded into emitted count).
  if (Opc == Haydn::LoopStart) {
    if (TripCountDef->getNumOperands() < 2 ||
        !TripCountDef->getOperand(1).isImm())
      return false;
    int64_t Cur = TripCountDef->getOperand(1).getImm();
    int64_t New = Cur + Delta;
    // simm6 encoding range for the adj operand.
    if (New < -32 || New > 31)
      return false;
    TripCountDef->getOperand(1).setImm(New);
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: LoopStart adj " << Cur << " -> "
                      << New << "\n");
    return true;
  }

  // Immediate-trip product forms: sel, start, end, imm count.
  if (TII->isHardwareLoopImmTripOpcode(Opc)) {
    if (TripCountDef->getNumOperands() <= 3 ||
        !TripCountDef->getOperand(3).isImm())
      return false;
    int64_t Cur = TripCountDef->getOperand(3).getImm();
    int64_t New = Cur + Delta;
    if (New < 0)
      return false;
    TripCountDef->getOperand(3).setImm(New);
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: imm-trip cnt " << Cur << " -> "
                      << New << "\n");
    return true;
  }

  // Register-trip product forms (SET_HWLOOP_REG / F2_W / members): remat.
  if (TII->isHardwareLoopRegTripOpcode(Opc)) {
    if (TripCountDef->getNumOperands() <= 3 ||
        !TripCountDef->getOperand(3).isReg())
      return false;
    Register Cnt = TripCountDef->getOperand(3).getReg();
    if (!Cnt.isPhysical() || Cnt == Haydn::R0)
      return false;
    Register Dest =
        rematerializeAddImmForUse(*TripCountDef, /*UseOpIdx=*/3, Delta);
    if (!Dest || !Dest.isPhysical() || Dest == Haydn::R0)
      return false;
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reg-trip cnt "
                      << printReg(Cnt) << " + " << Delta << " -> "
                      << printReg(Dest) << "\n");
    return true;
  }

  return false;
}

// Clear kill flags on a cloned MI — kill marks from the original body are
// not valid in prolog/epilog contexts and trip the machine verifier.
static void clearKillFlags(MachineInstr *MI) {
  for (MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isKill())
      MO.setIsKill(false);
  }
}

// Ensure \p MBB lists every physreg used (read) by \p MI as a live-in.
static void addUsesAsLiveIns(MachineBasicBlock &MBB, const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical() || Reg == Haydn::R0)
      continue;
    if (!MBB.isLiveIn(Reg))
      MBB.addLiveIn(Reg);
  }
}

// Apply one MI's defs/kills to a physreg liveness bit.
static void stepPhysLiveness(const MachineInstr &MI, Register Reg,
                             bool &Live) {
  // Uses first (kills), then defs — required for self-redefs such as
  // `$r1 = ADD32 killed $r1, $r2` so the kill does not erase the new def.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.getReg() != Reg || MO.isDef())
      continue;
    if (MO.readsReg() && MO.isKill())
      Live = false;
  }
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.getReg() != Reg || !MO.isDef())
      continue;
    Live = !MO.isDead();
  }
}

// Stage-0 physreg safety: every use of a peeled prolog MI must be live at the
// prolog insert point (after existing preheader instrs up to \p InsertPt, and
// after earlier prolog clones). Checking only isLiveIn is insufficient
// LoopStart often kills the trip-count reg, and loop-carried pointer regs
// may share that physreg after RA.
static bool prologUsesAreAvailable(
    MachineBasicBlock &Preheader, MachineBasicBlock::iterator InsertPt,
    ArrayRef<MachineInstr *> ExistingProlog, const MachineInstr &Orig) {
  for (const MachineOperand &MO : Orig.operands()) {
    if (!MO.isReg() || !MO.readsReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || !Reg.isPhysical() || Reg == Haydn::R0)
      continue;

    bool Live = Preheader.isLiveIn(Reg);
    for (MachineInstr &MI :
         make_range(Preheader.begin(), MachineBasicBlock::iterator(InsertPt)))
      stepPhysLiveness(MI, Reg, Live);
    for (const MachineInstr *P : ExistingProlog)
      stepPhysLiveness(*P, Reg, Live);

    if (!Live) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: prolog use of " << Reg
                        << " not live at insert pt: " << Orig);
      return false;
    }
  }
  return true;
}

// Loop-carried self-bump (iv = iv + step). F40: a stage-s consumer of a
// bumped IV needs k+s bumps before the kernel starts; the kernel body runs
// only the remaining k, so every skipped bump is a stale-address deficit.
// The ONLY skippable self-bump is the soft TRIP bump — its deficit is
// exactly absorbed by adjustSoftTripCount's preheader ADDI32. All other
// self-bumps (pointer IVs) must be cloned into the prologue, or the plan
// rejected when the clone's uses are not live at the insert point.
static bool isLoopCarriedSelfBump(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  if (Opc != Haydn::ADD32 && Opc != Haydn::SUB32 && Opc != Haydn::ADDI32 &&
      Opc != Haydn::ADDI32_W)
    return false;
  if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg())
    return false;
  Register Dst = MI.getOperand(0).getReg();
  if (!Dst || !Dst.isPhysical())
    return false;
  for (unsigned I = 1, E = MI.getNumOperands(); I < E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (MO.isReg() && MO.getReg() == Dst)
      return true;
  }
  return false;
}

// The one self-bump whose prologue deficit has a closed-form correction:
// the soft countdown trip itself (adjustSoftTripCount re-bases the counter
// in the preheader, so cloning it into the prologue would double-count).
static bool isSoftTripBumpMI(const MachineInstr &MI) {
  Register Trip;
  return isSoftCountdownBump(MI, Trip);
}

void HaydnMultiStageSMS::clearPlan() {
  HasValidPlan = false;
  II = 1;
  NStages = 0;
  LinearLength = 0;
  RecMII = 0;
  LastResMII = 0;
  ResourceRetryCount = 0;
  IsSoftCounted = false;
  SoftTripReg = Register();
  PrologMBB = nullptr;
  EpilogMBB = nullptr;
  Body.clear();
  Sched.init(0);
  NInstr = 0;
  MinLength = 0;
  destroyTwoCopyGraph();
  LCDEdges.clear();
  ExactCommitPlan.clear();
  MemberPin.clear();
  SearchAlts.clear();
  MeasuredII = 0;
  LastStrategyName = nullptr;
  LastSWPSolverStatus = "unavailable";
  LastResourceBias = false;
  LastEpiloguePreseed = false;
  OrdinarySnapshot.clear();
}

unsigned HaydnMultiStageSMS::placementOpcode(const MachineInstr &MI) const {
  auto It = MemberPin.find(&MI);
  if (It != MemberPin.end())
    return It->second;
  return MI.getOpcode();
}

bool HaydnMultiStageSMS::pinTransientMembers() {
  // SF2: AIE staticallyMaterializeMultiSlotInstructions
  // (AIEInterBlockScheduling.cpp:1560-1571). Pin each MultiSlot logical to
  // one generated member before placement. Instance-aware least-loaded
  // FieldSlots bit (AIE unused-slot assignment). Analysis-only never
  // setDesc's the MI.
  MemberPin.clear();
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  unsigned SlotOcc[Haydn::ISSUE_SLOT_COUNT] = {};
  for (SUnit *SU : Body) {
    MachineInstr *MI = SU->getInstr();
    if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
      continue;
    const unsigned Log = MI->getOpcode();
    if (!hasPlacementAlternatives(Fmts, Log))
      continue;
    SmallVector<PlacementAlternative, 8> Alts;
    if (!enumeratePlacementAlternatives(Fmts, Log, Alts) || Alts.empty())
      return false;
    const PlacementAlternative *Best = nullptr;
    unsigned BestS = Haydn::ISSUE_SLOT_COUNT;
    unsigned BestLoad = ~0u;
    for (const PlacementAlternative &A : Alts) {
      if (!A.MemberOpcode || !A.FieldSlots)
        continue;
      for (unsigned S = 0; S < Haydn::ISSUE_SLOT_COUNT; ++S) {
        if (!(A.FieldSlots & (SlotBits(1) << S)))
          continue;
        if (SlotOcc[S] < BestLoad ||
            (SlotOcc[S] == BestLoad && S < BestS)) {
          BestLoad = SlotOcc[S];
          BestS = S;
          Best = &A;
        }
      }
    }
    if (!Best)
      Best = &Alts.front();
    if (!Best->MemberOpcode)
      return false;
    MemberPin[MI] = Best->MemberOpcode;
    if (BestS < Haydn::ISSUE_SLOT_COUNT)
      ++SlotOcc[BestS];
  }
  return true;
}

bool HaydnMultiStageSMS::peelSideEffectFree() {
  // AIE PostPipeliner::peelSideEffectFree (AIEPostPipeliner.cpp:1630-1676).
  // If stage 0 is entirely side-effect-free and trip covers NStages-1,
  // drop that stage instead of rejecting the schedule.
  if (NStages < 2 || !TwoCopyDAG)
    return false;
  const int OneFewer = NStages - 1;
  const int Saved = NStages;
  NStages = OneFewer;
  const bool TripOK = hasSufficientTripCount();
  NStages = Saved;
  if (!TripOK)
    return false;
  for (int K = 0; K < NInstr; ++K) {
    if (Sched[K].Stage != 0)
      continue;
    MachineInstr *MI = TwoCopyDAG->SUnits[K].getInstr();
    if (!isSideEffectFreeMI(MI))
      return false;
  }
  --NStages;
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: peeled SEF stage, NStages="
                    << NStages << "\n");
  return NStages >= 1;
}

bool HaydnMultiStageSMS::hasSufficientTripCount() const {
  if (!TripCountDef || NStages < 1 || NStages > 4)
    return false;
  // tryII already rejects NStages > 4. Peels consume NStages-1 iterations;
  // require the static trip (when known) to cover full stage depth.
  const int Need = NStages;
  const int PeelIters = NStages - 1;

  if (IsSoftCounted) {
    Register Trip;
    if (!isSoftCountdownBump(*TripCountDef, Trip))
      return false;
    if (!Trip.isPhysical() || Trip == Haydn::R0 || !Preheader)
      return false;
    // F39: the prologue unconditionally peels NStages-1 iterations before
    // the kernel runs; if the real trip is smaller, peeled stage ops execute
    // for iterations that never happen (OOB loads/stores) and the preheader
    // ADDI32 drives the countdown negative. There is no runtime guard, so a
    // static min-trip proof (exact preheader constant, or the loop-MD
    // itercount.range floor) is mandatory; unknown trip fails closed.
    return provenMinTripCount().has_value();
  }

  const unsigned Opc = TripCountDef->getOpcode();
  const HaydnInstrInfo *TII =
      LoopBB->getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  // Immediate-trip forms: count is a folded imm (safe to check statically).
  if (TII->isHardwareLoopImmTripOpcode(Opc) &&
      TripCountDef->getNumOperands() > 3 &&
      TripCountDef->getOperand(3).isImm()) {
    int64_t Cnt = TripCountDef->getOperand(3).getImm();
    // After -(NStages-1) the remaining kernel trip must stay positive.
    return Cnt >= Need && (Cnt - PeelIters) >= 1;
  }
  // LoopStart: $src holds the static trip, $adj (simm6) is the SMS delta.
  // F39: the peel still executes NStages-1 real iterations, so $src needs a
  // static min-trip proof AND the adjusted adj must stay encodable.
  if (Opc == Haydn::LoopStart) {
    if (TripCountDef->getNumOperands() < 2 ||
        !TripCountDef->getOperand(0).isReg() ||
        !TripCountDef->getOperand(1).isImm())
      return false;
    int64_t NewAdj = TripCountDef->getOperand(1).getImm() - PeelIters;
    if (NewAdj < -32 || NewAdj > 31)
      return false;
    return provenMinTripCount().has_value();
  }
  // Register-trip: remat absorbs -(NStages-1) into the count register, but
  // F39 still needs a static min-trip proof >= NStages — the prologue peels
  // real iterations before the ZOL kernel starts.
  if (TII->isHardwareLoopRegTripOpcode(Opc)) {
    if (TripCountDef->getNumOperands() <= 3 ||
        !TripCountDef->getOperand(3).isReg())
      return false;
    Register Cnt = TripCountDef->getOperand(3).getReg();
    if (!Cnt.isPhysical() || Cnt == Haydn::R0)
      return false;
    return provenMinTripCount().has_value();
  }
  return false;
}

/// F39 combined static-trip proof: exact preheader constant (authoritative),
/// else the loop-MD minimum, else the -haydn-loop-min-tripcount soak floor
/// (the same single option the pre-RA ZOL gate uses). Either source must
/// prove trip >= Need and trip - PeelIters >= 1; otherwise std::nullopt
/// (fail closed).
std::optional<int64_t> HaydnMultiStageSMS::provenMinTripCount() const {
  if (!LoopBB || !Preheader)
    return std::nullopt;
  const int Need = NStages;
  const int PeelIters = NStages - 1;
  auto sufficient = [&](int64_t C) {
    return C >= Need && (C - PeelIters) >= 1;
  };
  std::optional<int64_t> Exact;
  if (IsSoftCounted) {
    Register Trip;
    if (!isSoftCountdownBump(*TripCountDef, Trip) || !Trip.isPhysical() ||
        Trip == Haydn::R0)
      return std::nullopt;
    Exact = staticPhysTripConstant(Trip, *Preheader);
  }
  if (Exact)
    return sufficient(*Exact) ? Exact : std::nullopt;
  std::optional<int64_t> MD = loopMDMinTripCount(*LoopBB);
  if (MD && sufficient(*MD))
    return MD;
  if (HaydnLoopMinTripCount > 0 && sufficient(HaydnLoopMinTripCount))
    return static_cast<int64_t>(HaydnLoopMinTripCount.getValue());
  return std::nullopt;
}

bool HaydnMultiStageSMS::certificateTripAdjust() const {
  // Same static predicates as hasSufficientTripCount; kept separate so the
  // materialize gate names the trip-adjust certificate explicitly.
  return hasSufficientTripCount();
}

bool HaydnMultiStageSMS::certificateKernelPlan() const {
  assert(HasValidPlan && II >= 1);
  const int N = static_cast<int>(Body.size());
  if (N < 2 || NStages < 1 || NStages > 4 || II < 1)
    return false;

  int MaxStage = 0;
  for (int I = 0; I < N; ++I) {
    if (!Sched[I].Scheduled)
      return false;
    if (Sched[I].ModuloCycle < 0 || Sched[I].ModuloCycle >= II)
      return false;
    if (Sched[I].Stage < 0 || Sched[I].Stage >= NStages)
      return false;
    if (Sched[I].Cycle != Sched[I].Stage * II + Sched[I].ModuloCycle)
      return false;
    MaxStage = std::max(MaxStage, Sched[I].Stage);
  }
  if (MaxStage + 1 != NStages)
    return false;

  // SF4: all placement-relevant kinds re-check the kernel (certificate,
  // not window). Data/memory already constrained placement; Anti/Output
  // with unsigned latency 0 require Cycle[use] >= Cycle[def].
  for (int I = 0; I < N; ++I) {
    for (const SDep &Pred : Body[I]->Preds) {
      if (Pred.getSUnit()->isBoundaryNode())
        continue;
      if (!isPlacementDep(Pred))
        continue;
      for (int J = 0; J < N; ++J) {
        if (Body[J] != Pred.getSUnit())
          continue;
        int Need = Sched[J].Cycle + static_cast<int>(Pred.getLatency());
        if (Sched[I].Cycle < Need)
          return false;
      }
    }
  }

  // SF3: every modulo group is at most issue-width wide. Multi-MI groups
  // must be able to exact-commit as one Format E parcel — sequential
  // fallback is not a legal accepted kernel.
  for (int M = 0; M < II; ++M) {
    unsigned Count = 0;
    for (int I = 0; I < N; ++I) {
      if (Sched[I].ModuloCycle != M)
        continue;
      MachineInstr *MI = Body[I]->getInstr();
      if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
        continue;
      ++Count;
    }
    if (Count > Haydn::ISSUE_SLOT_COUNT)
      return false;
  }
  return true;
}

bool HaydnMultiStageSMS::certificateExactCommitPlan() {
  ExactCommitPlan.assign(II, false);
  if (!HasValidPlan || II < 1)
    return false;
  const int N = static_cast<int>(Body.size());
  for (int M = 0; M < II; ++M) {
    SmallVector<MachineInstr *, 4> Group;
    for (int I = 0; I < N; ++I) {
      if (Sched[I].ModuloCycle != M)
        continue;
      MachineInstr *MI = Body[I]->getInstr();
      if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
        continue;
      Group.push_back(MI);
    }
    if (Group.empty()) {
      ExactCommitPlan[M] = true; // idle parcel of the II
      continue;
    }
    if (Group.size() == 1) {
      ExactCommitPlan[M] = true; // singleton parcel
      continue;
    }
    // More than issue-width members would sequentialize leftovers.
    // Format infeasibility rejects the II — never a 3+tail split.
    if (Group.size() > Haydn::ISSUE_SLOT_COUNT)
      return false;
    ArrayRef<MachineInstr *> Slice(Group.data(), Group.size());
    if (!haydn::bundle::canCoissueProductCycle(Slice))
      return false;
    ExactCommitPlan[M] = true;
  }
  return true;
}

int HaydnMultiStageSMS::countPlannedParcels() const {
  // AIE visitPipelineSection (AIEPostPipeliner.cpp:1706-1715) walks
  // M=0..II-1 and emits one bundle per modulo cycle. Overlay: each cycle
  // is one Format E parcel once ExactCommitPlan[M] is true. A false slot
  // would sequentialize leftovers — fail closed (0) rather than report II.
  if (II < 1 || ExactCommitPlan.size() != static_cast<size_t>(II))
    return 0;
  int Planned = 0;
  for (int M = 0; M < II; ++M) {
    if (!ExactCommitPlan[M])
      return 0;
    ++Planned;
  }
  return Planned;
}

/// F44 pack-boundary alias law (golden Constraints:67): within one Format E
/// parcel, a store and a load must not target overlapping addresses; if
/// non-overlap cannot be PROVEN, the compiler must place them in separate
/// parcels. The multi-stage kernel packs each modulo cycle's group into one
/// issue cycle; checkConflict now covers Format E occupancy, but alias is
/// a memory law, not a slot-cover law. Fail closed per pair:
/// AA NoAlias (or trivially disjoint accesses) is the only accept.
bool HaydnMultiStageSMS::certificatePackAlias() const {
  if (!HasValidPlan || II < 1 || Body.empty())
    return false;
  AAResults *AA =
      DAG ? static_cast<HaydnScheduleDAGMI *>(DAG)->getAliasAnalysis()
          : nullptr;
  const int N = static_cast<int>(Body.size());
  for (int M = 0; M < II; ++M) {
    SmallVector<MachineInstr *, 4> Group;
    for (int I = 0; I < N; ++I) {
      if (Sched[I].ModuloCycle != M)
        continue;
      MachineInstr *MI = Body[I]->getInstr();
      if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
        continue;
      Group.push_back(MI);
    }
    // Pack-layer gate (HaydnPackLegality, Constraints:67). Same predicate
    // the ordinary ReadyCycle path does not own — Order latency 0 would
    // otherwise let a may-alias store/load share one Format E parcel.
    if (haydn::pack::cycleHasMayAliasStoreLoad(ArrayRef<MachineInstr *>(Group),
                                               AA)) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: may-alias store/load in pack "
                           "cycle "
                        << M << "\n");
      return false;
    }
  }
  return true;
}

bool HaydnMultiStageSMS::certificateLCDTwoIteration() const {
  if (!HasValidPlan || II < 1 || Body.empty() || !TwoCopyDAG)
    return false;
  if (II < RecMII)
    return false;
  const int N = NInstr;
  if (N <= 0)
    return false;
  for (int I = 0; I < N; ++I) {
    if (!Sched[I].Scheduled)
      return false;
  }
  for (const HaydnMultiStageLCDEdge &E : LCDEdges) {
    if (E.DefIdx < 0 || E.UseIdx < 0 || E.DefIdx >= N || E.UseIdx >= N)
      return false;
    const int Dist = std::max(1, E.Distance);
    const int Lat = std::max(0, E.Latency);
    if (Sched[E.UseIdx].Cycle + Dist * II < Sched[E.DefIdx].Cycle + Lat)
      return false;
  }
  if (RecMII < 1) {
    if (!LCDEdges.empty())
      return false;
    for (SUnit *SU : Body) {
      MachineInstr *MI = SU->getInstr();
      if (MI && isLoopCarriedSelfBump(*MI))
        return false;
    }
  }
  return true;
}

bool HaydnMultiStageSMS::certificateLifetimesNoSpill() const {
  // SF8: one regsOverlap lifetime proof at search time.
  return proveLivePhysNoSpillSubreg();
}

bool HaydnMultiStageSMS::certificateDistinctStageOccupancy() const {
  if (!HasValidPlan || NStages < 1 || Body.empty())
    return false;
  SmallVector<bool, 4> Occupied(NStages, false);
  for (int I = 0; I < NInstr; ++I) {
    const HaydnMultiStageNodeInfo &N = Sched[I];
    if (!N.Scheduled || N.Stage < 0 || N.Stage >= NStages)
      return false;
    Occupied[N.Stage] = true;
  }
  for (int S = 0; S < NStages; ++S)
    if (!Occupied[S])
      return false;
  return true;
}

void HaydnMultiStageSMS::recordSWPSAnnotation() const {
  if (!HasValidPlan || !LoopBB || !DAG || II < 1 || NStages < 1)
    return;
  // Never stamp an assumed II. Analysis-only does not reach here;
  // materialize only calls this after ParcelsCommitted == II.
  if (MeasuredII != II)
    return;
  auto *MFI = DAG->MF.getInfo<HaydnMachineFunctionInfo>();
  if (!MFI)
    return;
  const int ReportII = MeasuredII;
  HaydnMachineFunctionInfo::SMSSWPSInfo SW;
  SW.ResMII = static_cast<unsigned>(std::max(0, LastResMII));
  SW.RecMII = static_cast<unsigned>(std::max(0, RecMII));
  SW.MII = std::max(SW.ResMII, SW.RecMII);
  SW.StageCount = static_cast<unsigned>(NStages);
  SW.NumOps = static_cast<unsigned>(Body.size());
  SW.ScheduledII = static_cast<unsigned>(ReportII);
  MFI->recordSMSLoop(LoopBB, SW);
  emitRemark(*LoopBB, "MultiStageSWPS",
             "swps measured-II=" + Twine(ReportII) + " searched-II=" +
                 Twine(II) + " stages=" + Twine(NStages) +
                 " ResMII=" + Twine(LastResMII) + " RecMII=" + Twine(RecMII) +
                 " ops=" + Twine((unsigned)Body.size()));
}

// Epilog peels complete unfinished stages after the kernel. Every physreg use
// must either be defined by an earlier epilog peel in plan order, or be a
// plausible loop live-out (live-in on ExitBB or defined somewhere in LoopBB).
bool HaydnMultiStageSMS::certificateEpilogUses() const {
  assert(HasValidPlan && LoopBB && ExitBB);
  const int N = static_cast<int>(Body.size());
  const int EpiBase = 1;
  SmallVector<MachineInstr *, 8> Planned;
  for (int S = 0; S < NStages - 1; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle != M || Sched[I].Cycle < (EpiBase + S) * II)
          continue;
        MachineInstr *Orig = Body[I]->getInstr();
        if (!Orig || isSkippableBodyMI(*Orig) || isZOLTerminator(*Orig))
          continue;
        for (const MachineOperand &MO : Orig->operands()) {
          if (!MO.isReg() || !MO.readsReg())
            continue;
          Register Reg = MO.getReg();
          if (!Reg || !Reg.isPhysical() || Reg == Haydn::R0)
            continue;
          bool Live = ExitBB->isLiveIn(Reg) || LoopBB->isLiveIn(Reg);
          if (!Live) {
            for (const MachineInstr &BMI : *LoopBB) {
              for (const MachineOperand &BMO : BMI.operands()) {
                if (BMO.isReg() && BMO.isDef() && BMO.getReg() == Reg &&
                    !BMO.isDead()) {
                  Live = true;
                  break;
                }
              }
              if (Live)
                break;
            }
          }
          if (!Live) {
            for (const MachineInstr *P : Planned) {
              for (const MachineOperand &PMO : P->operands()) {
                if (PMO.isReg() && PMO.isDef() && PMO.getReg() == Reg &&
                    !PMO.isDead()) {
                  Live = true;
                  break;
                }
              }
              if (Live)
                break;
            }
          }
          if (!Live) {
            LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: epilog use of " << Reg
                              << " not certified live: " << *Orig);
            return false;
          }
        }
        Planned.push_back(Orig);
      }
    }
  }
  return true;
}

bool HaydnMultiStageSMS::certificatePrologLiveness(
    MachineBasicBlock::iterator PrologInsertPt) const {
  assert(HasValidPlan && Preheader);
  const int N = static_cast<int>(Body.size());
  const int NPrologStages = NStages - 1;
  SmallVector<MachineInstr *, 8> SimulatedProlog;
  for (int S = 0; S < NPrologStages; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle != M || Sched[I].Cycle >= (S + 1) * II)
          continue;
        MachineInstr *Orig = Body[I]->getInstr();
        // Soft trip bump is never cloned: adjustSoftTripCount's preheader
        // ADDI32 absorbs the peel deficit. Cloning it when uses happen to
        // be live would double-count (ADDI32 plus peeled decrements).
        // Every other self-bump (pointer IVs) must stay in the peel, or
        // the plan rejects when uses are not live at the insert point.
        if (IsSoftCounted && isSoftTripBumpMI(*Orig))
          continue;
        if (!prologUsesAreAvailable(*Preheader, PrologInsertPt, SimulatedProlog,
                                    *Orig))
          return false;
        SimulatedProlog.push_back(Orig);
      }
    }
  }
  return true;
}

bool HaydnMultiStageSMS::emitEpilogueWithKernelPreseed(
    SmallVectorImpl<MachineInstr *> &EpilogMIs,
    ArrayRef<SmallVector<int, 4>> KernelByMod) {
  // AIE initializeTopScoreBoard (AIEMachineScheduler.cpp:407-447) plus
  // visitPipelineSection empty-bundle emission (AIEPostPipeliner.cpp:1697-
  // 1717 / PipelineExtractor finish trim). Replay kernel parcels until
  // steady, then emit epilogue clones cycle-accurately. Leftover occupancy
  // and internal empty peel cycles become architectural NOP idle; trailing
  // empty cycles are dropped (AIE finish() NonEmpty trim).
  LastEpiloguePreseed = false;
  if (!EpilogMBB || !DAG || II < 1 || NInstr < 1 || NStages < 2)
    return false;
  if (static_cast<int>(KernelByMod.size()) < II)
    return false;

  const TargetSubtargetInfo &ST = DAG->MF.getSubtarget();
  const TargetInstrInfo *TII = ST.getInstrInfo();
  // SearchAlts keys two-copy clones. Epilogue replay/peel uses original
  // body MIs and off-side clones, so stamp a local map from MemberPin
  // (AIE MultiSlot materialize before scoreboard emit).
  HaydnAlternateDescriptors EpiAlts;
  for (int I = 0; I < NInstr; ++I) {
    MachineInstr *Orig = Body[I] ? Body[I]->getInstr() : nullptr;
    if (!Orig)
      continue;
    auto It = MemberPin.find(Orig);
    if (It != MemberPin.end())
      EpiAlts.setAlternateDescriptor(Orig, It->second, *TII);
  }
  HaydnHazardRecognizer EpiHR(TII, ST.getInstrItineraryData(),
                              /*IsPreRA=*/false, &EpiAlts);
  ResourceScoreboard<HaydnFuncUnitWrapper> EpiSB;
  // AIE initializeTopScoreBoard (AIEMachineScheduler.cpp:427-433):
  // replay = ceil(LoopSize / getConflictHorizon()). Overlay: Haydn peels
  // into a distinct EpilogMBB, so this local scoreboard is the live Top
  // HR. reset(D) == config(-D, D-1) keeps the booked cycle after
  // advance(); config(0, …) would clear it (pre-seed absent).
  const int ConflictHorizon = EpiHR.getConflictHorizon();
  const int Window = ConflictHorizon + std::max(II, 1);
  EpiSB.reset(Window);
  const int LoopReplayTimes = (II + ConflictHorizon - 1) / ConflictHorizon;
  for (int R = 0; R < LoopReplayTimes; ++R) {
    for (int M = 0; M < II; ++M) {
      for (int Idx : KernelByMod[M]) {
        if (Idx < 0 || Idx >= NInstr)
          continue;
        if (MachineInstr *MI = Body[Idx]->getInstr())
          EpiHR.emitInScoreboard(EpiSB, *MI, 0);
      }
      EpiSB.advance();
    }
  }

  const int N = static_cast<int>(Body.size());
  const int EpiBase = 1;
  const int Cap = ConflictHorizon + II;
  size_t EpiIdx = 0;
  int PendingEmpty = 0;
  auto emitIdle = [&]() {
    TII->insertNoop(*EpilogMBB, EpilogMBB->end());
    EpiSB.advance();
  };

  for (int S = 0; S < NStages - 1; ++S) {
    for (int M = 0; M < II; ++M) {
      SmallVector<MachineInstr *, 4> Cycle;
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle != M ||
            Sched[I].Cycle < (EpiBase + S) * II)
          continue;
        if (EpiIdx >= EpilogMIs.size())
          return false;
        MachineInstr *Clone = EpilogMIs[EpiIdx++];
        if (MachineInstr *Orig = Body[I]->getInstr()) {
          auto Pin = MemberPin.find(Orig);
          if (Pin != MemberPin.end())
            EpiAlts.setAlternateDescriptor(Clone, Pin->second, *TII);
        }
        Cycle.push_back(Clone);
      }
      if (Cycle.empty()) {
        ++PendingEmpty;
        continue;
      }
      while (PendingEmpty > 0) {
        emitIdle();
        --PendingEmpty;
      }
      auto cycleConflicts = [&]() {
        for (MachineInstr *C : Cycle)
          if (EpiHR.checkConflict(EpiSB, *C, 0))
            return true;
        return false;
      };
      int Guard = 0;
      while (cycleConflicts() && Guard++ < Cap)
        emitIdle();
      if (cycleConflicts())
        return false;
      for (MachineInstr *Clone : Cycle) {
        EpilogMBB->insert(EpilogMBB->end(), Clone);
        addUsesAsLiveIns(*EpilogMBB, *Clone);
        EpiHR.emitInScoreboard(EpiSB, *Clone, 0);
      }
      EpiSB.advance();
    }
  }
  if (EpiIdx != EpilogMIs.size())
    return false;
  LastEpiloguePreseed = true;
  return true;
}

bool HaydnMultiStageSMS::materialize() {
  if (!HasValidPlan || !DAG || !LoopBB || !Preheader || !ExitBB || !TripCountDef)
    return false;
  if (HaydnMultiStageSMSForceFail) {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    return false;
  }

  OrdinarySnapshot.capture(Preheader, LoopBB, ExitBB);
  auto rollback = [&](const char *Why) -> bool {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    LastRejectReason = Why;
    OrdinarySnapshot.restore();
    OrdinarySnapshot.clear();
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: rollback — " << Why << "\n");
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageRollback",
                 Twine("rollback to ordinary baseline (") + Why + ")");
    return false;
  };
  // Journal seats fire after the first MIR/CFG edit so rollback is exercised.

  // Full pre-mutation certificate suite. Fail closed with zero MIR change.
  auto rejectCert = [&](const char *Why) -> bool {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    OrdinarySnapshot.clear();
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: certificate reject — " << Why
                      << "\n");
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageReject",
                 Twine("rejected: materialize certificate failed (") + Why +
                     ")");
    return false;
  };
  if (NStages >= 2 && !certificateTripAdjust())
    return rejectCert("trip-adjust");
  if (!certificateKernelPlan())
    return rejectCert("kernel-plan");
  if (ExactCommitPlan.size() != static_cast<size_t>(II) &&
      !certificateExactCommitPlan())
    return rejectCert("exact-commit-plan");

  // Compute prolog insert point and run off-side prolog/epilog certificates.
  // SF5: NStages==1 is kernel-only — no peel blocks, no trip peel.
  MachineBasicBlock::iterator PrologInsertPt =
      Preheader->getFirstTerminator();
  for (MachineInstr &MI : *Preheader) {
    if (isHwLoopSetup(MI))
      PrologInsertPt = std::next(MI.getIterator());
  }
  if (NStages >= 2 && !certificatePrologLiveness(PrologInsertPt))
    return rejectCert("prolog-liveness");
  if (NStages >= 2 && !certificateEpilogUses())
    return rejectCert("epilog-uses");

  const int N = static_cast<int>(Body.size());
  MachineFunction &MF = *LoopBB->getParent();

  // Build per-modulo-cycle kernel order: for cycle M, emit body nodes with
  // ModuloCycle==M sorted by Stage descending (older iteration first — read
  // previous value before same-cycle WAR write of next).
  SmallVector<SmallVector<int, 4>, 8> KernelByMod(II);
  for (int I = 0; I < N; ++I)
    KernelByMod[Sched[I].ModuloCycle].push_back(I);
  for (int M = 0; M < II; ++M) {
    llvm::stable_sort(KernelByMod[M], [&](int A, int B) {
      if (Sched[A].Stage != Sched[B].Stage)
        return Sched[A].Stage > Sched[B].Stage;
      return A < B;
    });
  }

  // ---- Off-side clone plan (MF still unmodified) ----
  // SF10: AIE visitPipelineSection (AIEPostPipeliner.cpp:1697-1717) —
  // for each prologue stage S, for each modulo cycle M, emit nodes with
  // ModuloCycle==M && Cycle < (S+1)*II. NStages==1 is kernel-only.
  const int NPrologStages = std::max(0, NStages - 1);
  SmallVector<MachineInstr *, 8> PrologMIs;
  auto discardOrphanClones = [&](SmallVectorImpl<MachineInstr *> &Clones) {
    for (MachineInstr *C : Clones)
      if (C && !C->getParent())
        MF.deleteMachineInstr(C);
    Clones.clear();
  };
  for (int S = 0; S < NPrologStages; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle != M || Sched[I].Cycle >= (S + 1) * II)
          continue;
        MachineInstr *Orig = Body[I]->getInstr();
        if (IsSoftCounted && isSoftTripBumpMI(*Orig)) {
          LLVM_DEBUG(dbgs()
                     << "HaydnMultiStageSMS: skip prolog peel "
                        "(soft trip bump; preheader re-base): "
                     << *Orig);
          continue;
        }
        if (!prologUsesAreAvailable(*Preheader, PrologInsertPt, PrologMIs,
                                    *Orig)) {
          discardOrphanClones(PrologMIs);
          return rejectCert("unsafe-prolog-peel");
        }
        MachineInstr *Clone = MF.CloneMachineInstr(Orig);
        clearKillFlags(Clone);
        PrologMIs.push_back(Clone);
      }
    }
  }

  // Epilogue: nodes with Cycle >= (1+S)*II. Still off-side (not inserted).
  SmallVector<MachineInstr *, 8> EpilogMIs;
  const int EpiBase = 1;
  for (int S = 0; S < NStages - 1; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle == M &&
            Sched[I].Cycle >= (EpiBase + S) * II) {
          MachineInstr *Clone = MF.CloneMachineInstr(Body[I]->getInstr());
          clearKillFlags(Clone);
          EpilogMIs.push_back(Clone);
        }
      }
    }
  }

  const bool KernelOnly = NStages < 2;

  // ---- Mutation boundary: distinct stage MBBs + journaled rollback ----
  auto retargetSuccessor = [&](MachineBasicBlock *From, MachineBasicBlock *Old,
                               MachineBasicBlock *New) {
    if (!From || !Old || !New || Old == New || !From->isSuccessor(Old))
      return;
    From->replaceSuccessor(Old, New);
    for (MachineInstr &MI : From->terminators())
      for (MachineOperand &MO : MI.operands())
        if (MO.isMBB() && MO.getMBB() == Old)
          MO.setMBB(New);
  };
  auto retargetPHIIncoming = [&](MachineBasicBlock *MBB, MachineBasicBlock *Old,
                                 MachineBasicBlock *New) {
    if (!MBB)
      return;
    for (MachineInstr &MI : *MBB) {
      if (!MI.isPHI())
        break;
      for (MachineOperand &MO : MI.operands())
        if (MO.isMBB() && MO.getMBB() == Old)
          MO.setMBB(New);
    }
  };

  if (!KernelOnly) {
    PrologMBB = MF.CreateMachineBasicBlock();
    EpilogMBB = MF.CreateMachineBasicBlock();
    MF.insert(LoopBB->getIterator(), PrologMBB);
    MF.insert(std::next(LoopBB->getIterator()), EpilogMBB);
    OrdinarySnapshot.registerCreated(PrologMBB);
    OrdinarySnapshot.registerCreated(EpilogMBB);
    retargetSuccessor(Preheader, LoopBB, PrologMBB);
    if (!PrologMBB->isSuccessor(LoopBB))
      PrologMBB->addSuccessor(LoopBB);
    retargetSuccessor(LoopBB, ExitBB, EpilogMBB);
    if (!EpilogMBB->isSuccessor(ExitBB))
      EpilogMBB->addSuccessor(ExitBB);
    retargetPHIIncoming(ExitBB, LoopBB, EpilogMBB);
  }
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_ALLOC))
    return rollback("JM-ALLOC-force");

  LastEpiloguePreseed = false;
  if (!KernelOnly) {
    for (MachineInstr *Clone : PrologMIs)
      PrologMBB->insert(PrologMBB->end(), Clone);
    if (!emitEpilogueWithKernelPreseed(EpilogMIs, KernelByMod)) {
      discardOrphanClones(EpilogMIs);
      return rollback("epilogue-preseed");
    }
  }

  MachineBasicBlock::iterator Anchor = LoopBB->end();
  for (MachineInstr &MI : *LoopBB) {
    if (isZOLTerminator(MI)) {
      Anchor = MI.getIterator();
      break;
    }
  }
  if (Anchor == LoopBB->end())
    Anchor = LoopBB->getFirstTerminator();

  SmallVector<MachineInstr *, 16> KernelOrder;
  for (int M = 0; M < II; ++M)
    for (int Idx : KernelByMod[M])
      KernelOrder.push_back(Body[Idx]->getInstr());

  for (MachineInstr *MI : KernelOrder) {
    clearKillFlags(MI);
    LoopBB->splice(Anchor, LoopBB, MI->getIterator());
  }
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_SPLICE))
    return rollback("JM-SPLICE-force");

  // Trip adjust before exact commit (JM-TRIP) while soft-countdown
  // opcodes are still recognized by isSoftCountdownBump. Kernel-only
  // (NStages==1) peels zero iterations.
  if (NStages >= 2 && !adjustTripCount(-(NStages - 1)))
    return rollback("JM-TRIP-restore");
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_TRIP))
    return rollback("JM-TRIP-force");

  // SF2: stamp AltDescs from the transient member pin so commit sees
  // the same geometry the oracle searched. setDesc stays inside
  // commitExactMultiMIProductCycle.
  if (DAG) {
    const TargetInstrInfo *TII = DAG->MF.getSubtarget().getInstrInfo();
    auto &Alts = DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
    for (SUnit *SU : Body) {
      MachineInstr *MI = SU->getInstr();
      if (!MI)
        continue;
      auto It = MemberPin.find(MI);
      if (It != MemberPin.end())
        Alts.setAlternateDescriptor(MI, It->second, *TII);
    }
  }

  unsigned ParcelsCommitted = 0;
  for (int M = 0; M < II; ++M) {
    SmallVector<MachineInstr *, 4> Group;
    for (int Idx : KernelByMod[M]) {
      MachineInstr *MI = Body[Idx]->getInstr();
      if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
        continue;
      Group.push_back(MI);
    }
    // SF3: every modulo cycle is one parcel. Empty = idle (counts).
    // Singleton = one parcel. Multi-MI must exact-commit or rollback —
    // never sequentialize leftovers past issue width.
    if (Group.size() > Haydn::ISSUE_SLOT_COUNT)
      return rollback("JM-COMMIT-restore");
    if (Group.size() < 2) {
      ++ParcelsCommitted;
      continue;
    }
    ArrayRef<MachineInstr *> Slice(Group.data(), Group.size());
    if (!haydn::bundle::canCoissueProductCycle(Slice) ||
        !haydn::bundle::commitExactMultiMIProductCycle(Slice)) {
      return rollback("JM-COMMIT-restore");
    }
    ++ParcelsCommitted;
  }
  if (static_cast<int>(ParcelsCommitted) != II)
    return rollback("measured-ii-mismatch");
  MeasuredII = static_cast<int>(ParcelsCommitted);
  // F5: JM-COMMIT fires after exact commit, not at splice.
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_COMMIT))
    return rollback("JM-COMMIT-force");

  for (MachineInstr *Clone : PrologMIs) {
    for (const MachineOperand &MO : Clone->operands()) {
      if (!MO.isReg() || !MO.isDef() || MO.isDead())
        continue;
      Register Reg = MO.getReg();
      if (!Reg || !Reg.isPhysical() || Reg == Haydn::R0)
        continue;
      if (!LoopBB->isLiveIn(Reg))
        LoopBB->addLiveIn(Reg);
    }
  }
  if (DAG) {
    const TargetRegisterInfo *TRI = DAG->MF.getSubtarget().getRegisterInfo();
    if (TRI) {
      for (MachineBasicBlock *MBB : {PrologMBB, LoopBB, EpilogMBB, ExitBB}) {
        if (!MBB)
          continue;
        LivePhysRegs Live(*TRI);
        MBB->clearLiveIns();
        computeAndAddLiveIns(Live, *MBB);
      }
    }
  }
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_LIVE))
    return rollback("JM-LIVE-force");

  // JM-ALT: drop transaction AltDescs. Rollback restores the ordinary map
  // captured at snapshot (AIE leaveRegion clear is
  // AIEAlternateDescriptors.h:74 / AIEMachineScheduler.cpp:1081-1082).
  if (DAG)
    DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs().clear();
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_ALT))
    return rollback("JM-ALT-force");

  // JM-META: stamp kernel SMS metadata. Rollback erases it.
  recordSWPSAnnotation();
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_META))
    return rollback("JM-META-force");

  for (int I = 0; I < N; ++I) {
    SUnit *SU = Body[I];
    SU->isScheduled = true;
    SU->TopReadyCycle = static_cast<unsigned>(Sched[I].ModuloCycle);
    SU->BotReadyCycle = SU->TopReadyCycle;
  }


  NumMultiStagePeels += PrologMIs.size() + EpilogMIs.size();
  NumMultiStageKernelParcels += ParcelsCommitted;
  if (LoopBB && PrologMBB && EpilogMBB)
    emitRemark(*LoopBB, "MultiStageStageMBB",
               "stage-mbb prolog=" + Twine(PrologMBB->getNumber()) +
                   " epilog=" + Twine(EpilogMBB->getNumber()) +
                   " stages=" + Twine(NStages) +
                   " peel-order=modulo-cycle measured-II=" +
                   Twine(MeasuredII) + " epilogue-preseed=" +
                   Twine(LastEpiloguePreseed ? "kernel-steady" : "absent"));
  else if (LoopBB)
    emitRemark(*LoopBB, "MultiStageStageMBB",
               "stage-mbb kernel-only stages=" + Twine(NStages) +
                   " measured-II=" + Twine(MeasuredII) +
                   " peel-order=modulo-cycle epilogue-preseed=" +
                   Twine(LastEpiloguePreseed ? "kernel-steady" : "none"));
  OrdinarySnapshot.clear();
  ++NumMultiStageSuccess;
  LLVM_DEBUG({
    dbgs() << "HaydnMultiStageSMS: materialize done II=" << II
           << " NStages=" << NStages
           << (IsSoftCounted ? " soft" : " zol")
           << " prolog=" << PrologMIs.size()
           << " epilog=" << EpilogMIs.size()
           << " kernel_parcels=" << ParcelsCommitted << "\n";
    dbgs() << "  Preheader:\n";
    for (auto &MI : *Preheader)
      dbgs() << "    " << MI;
    dbgs() << "  Loop:\n";
    for (auto &MI : *LoopBB)
      dbgs() << "    " << MI;
    dbgs() << "  Exit:\n";
    for (auto &MI : *ExitBB)
      dbgs() << "    " << MI;
  });

  return true;
}


void HaydnMultiStageSMS::emitRemark(MachineBasicBlock &MBB,
                                    const char *RemarkName,
                                    const Twine &Msg) const {
  if (!DAG)
    return;
  MachineOptimizationRemarkEmitter ORE(DAG->MF, /*MBFI=*/nullptr);
  DebugLoc DL;
  if (!MBB.empty())
    DL = MBB.begin()->getDebugLoc();
  // DiagnosticInfoOptimizationBase::operator<< accepts StringRef only (not
  // Twine). Materialize before the emit lambda so the StringRef stays live.
  std::string MsgStorage = Msg.str();
  ORE.emit([&]() {
    return MachineOptimizationRemarkAnalysis(DEBUG_TYPE, RemarkName, DL, &MBB)
           << StringRef(MsgStorage);
  });
}

//===----------------------------------------------------------------------===//
// schedule
//===----------------------------------------------------------------------===//

bool HaydnMultiStageSMS::analyze(ScheduleDAGMI &TheDAG, unsigned IIHint) {
  clearPlan();
  DAG = &TheDAG;
  LastRejectReason = nullptr;
  if (!EnableHaydnMultiStageSMS)
    return false;
  // ScheduleDAGInstrs::BB is protected; recover the region block from an SU
  // or from the region's first instruction.
  MachineBasicBlock *MBBPtr = nullptr;
  if (!TheDAG.SUnits.empty() && TheDAG.SUnits[0].getInstr())
    MBBPtr = TheDAG.SUnits[0].getInstr()->getParent();
  if (!MBBPtr && TheDAG.begin() != TheDAG.end())
    MBBPtr = TheDAG.begin()->getParent();
  if (!MBBPtr) {
    ++NumMultiStageFail;
    LastRejectReason = "no-region-block";
    return false;
  }
  MachineBasicBlock &MBB = *MBBPtr;

  if (!isCandidate(MBB)) {
    ++NumMultiStageFail;
    LastRejectReason = "not-candidate";
    // Remark only on likely loop headers to avoid post-RA region noise.
    if (MBB.pred_size() >= 1 &&
        (llvm::is_contained(MBB.predecessors(), &MBB) || MBB.pred_size() >= 2)) {
      emitRemark(MBB, "MultiStageReject",
                 "rejected: not a single-BB ZOL/soft-countdown post-RA "
                 "candidate");
    }
    return false;
  }

  // Collect body SUnits from the built DAG.
  Body.clear();
  for (SUnit &SU : TheDAG.SUnits) {
    if (SU.isBoundaryNode())
      continue;
    MachineInstr *MI = SU.getInstr();
    if (!MI || isZOLTerminator(*MI) || isSkippableBodyMI(*MI))
      continue;
    Body.push_back(&SU);
  }
  if (Body.size() < 2) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: <2 body SUnits\n");
    ++NumMultiStageFail;
    LastRejectReason = "too-few-body";
    emitRemark(MBB, "MultiStageReject",
               "rejected: fewer than 2 body SUnits");
    return false;
  }
  if (Body.size() > MaxBodyInstrs) {
    ++NumMultiStageFail;
    LastRejectReason = "body-too-large";
    emitRemark(MBB, "MultiStageReject",
               "rejected: body SUnits=" + Twine((unsigned)Body.size()) +
                   " > cap=" + Twine(MaxBodyInstrs) +
                   " hang-containment no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }

  if (!pinTransientMembers()) {
    ++NumMultiStageFail;
    LastRejectReason = "member-pin";
    emitRemark(MBB, "MultiStageReject",
               "rejected: transient member pin failed no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }
  emitRemark(MBB, "MultiStagePin",
             "member-pin n=" + Twine((unsigned)MemberPin.size()) +
                 " placement-deps=data+anti+output+mem no-seq-fallback"
                 " hr-same-cycle=arctan-sincos+csrw-set+abs-e0"
                 " resource-bias=slot-windows"
                 " swpsolver=unavailable");

  if (!buildTwoCopyGraph(TheDAG)) {
    ++NumMultiStageFail;
    LastRejectReason = "two-copy-graph";
    emitRemark(MBB, "MultiStageReject",
               "rejected: two-copy buildSchedGraph failed no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }
  // Two-copy clones are distinct MI*; copy the instance pin so the
  // format oracle sees the same member on both copies. SearchAlts is
  // local to this attempt — AIEInterBlockScheduling.cpp:1568 setDesc on
  // the candidate MBB; Haydn keeps the original body logical.
  SearchAlts.clear();
  if (TwoCopyDAG && DAG) {
    const TargetInstrInfo *PinTII = DAG->MF.getSubtarget().getInstrInfo();
    for (int K = 0; K < NInstr; ++K) {
      MachineInstr *Orig = Body[K]->getInstr();
      auto It = MemberPin.find(Orig);
      if (It == MemberPin.end())
        continue;
      const unsigned Mem = It->second;
      MachineInstr *C0 = TwoCopyDAG->SUnits[K].getInstr();
      MachineInstr *C1 = TwoCopyDAG->SUnits[K + NInstr].getInstr();
      if (C0) {
        MemberPin[C0] = Mem;
        SearchAlts.setAlternateDescriptor(C0, Mem, *PinTII);
      }
      if (C1) {
        MemberPin[C1] = Mem;
        SearchAlts.setAlternateDescriptor(C1, Mem, *PinTII);
      }
    }
  }
  RecMII = computeRecMII();
  int ResMII = getResMII(MBB);
  LastResMII = ResMII;
  AAResults *AA =
      static_cast<HaydnScheduleDAGMI &>(TheDAG).getAliasAnalysis();
  const unsigned MemLCD = countMayAliasStoreLoadPairs(Body, AA);
  emitRemark(MBB, "MultiStageLCD",
             "lcd two-iteration RecMII=" + Twine(RecMII) +
                 " edges=" + Twine(static_cast<unsigned>(LCDEdges.size())) +
                 " mem=" + Twine(MemLCD) +
                 " ResMII=" + Twine(ResMII) +
                 " lcd-as-windows placement-deps=data+anti+output+mem");

  if (!computeASAPEarliest()) {
    ++NumMultiStageFail;
    LastRejectReason = "asap-fail";
    emitRemark(MBB, "MultiStageReject",
               "rejected: ASAP earliest placement did not converge ResMII=" +
                   Twine(ResMII) + " RecMII=" + Twine(RecMII) +
                   " no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }

  const TargetSubtargetInfo &ST = TheDAG.MF.getSubtarget();
  const TargetInstrInfo *TII = ST.getInstrInfo();
  const InstrItineraryData *Itin = ST.getInstrItineraryData();
  HaydnHazardRecognizer HR(TII, Itin, /*IsPreRA=*/false, &SearchAlts);

  int StartII = std::max(std::max(ResMII, RecMII), 1);
  if (IIHint > 0)
    StartII = static_cast<int>(IIHint);

  bool Found = false;
  // SF5: II==LinearLength is a legal kernel-only schedule (AIE checkStages
  // accepts NS==1). SF6: search starts at ResMII even when that equals
  // LinearLength — a strict II < LinearLength left ResMII untried.
  if (LinearLength < 1) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: LinearLength=" << LinearLength
                      << " — no schedule length\n");
    ++NumMultiStageFail;
    LastRejectReason = "no-headroom";
    emitRemark(MBB, "MultiStageReject",
               "rejected: no schedule length (LinearLength=" +
                   Twine(LinearLength) + ") ResMII=" + Twine(ResMII) +
                   " RecMII=" + Twine(RecMII) + " no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }
  const int ListBaseline = std::max(LinearLength, StartII);
  const int MaxII = std::min(ListBaseline, MaxIISearch);
  if (StartII > MaxII) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: StartII=" << StartII
                      << " > MaxII=" << MaxII << " (ListBaseline="
                      << ListBaseline << " LinearLength=" << LinearLength
                      << " ResMII=" << ResMII << ") — skip (fail closed)\n");
    ++NumMultiStageFail;
    LastRejectReason = "ii-window-empty";
    emitRemark(MBB, "MultiStageExhaustion",
               "exhausted: empty II window StartII=" + Twine(StartII) +
                   " MaxII=" + Twine(MaxII) + " ResMII=" + Twine(ResMII) +
                   " LinearLength=" + Twine(LinearLength) +
                   " pins=" + Twine((unsigned)MemberPin.size()) +
                   " no-seq-fallback qualify-or-cut=seated product-off");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: II search [" << StartII << ","
                    << MaxII << "] ResMII=" << ResMII
                    << " LinearLength=" << LinearLength
                    << " ListBaseline=" << ListBaseline << "\n");
  auto acceptII = [&]() {
    if (II < 1 || II > MaxII) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject II=" << II
                        << " outside [" << StartII << "," << MaxII << "]\n");
      return false;
    }
    return true;
  };
  for (int Try = StartII; Try <= MaxII; ++Try) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: trying II=" << Try << "\n");
    if (tryII(Try, HR) && acceptII()) {
      Found = true;
      break;
    }
  }
  if (!Found && IIHint > 0) {
    for (int Try = ResMII; Try <= MaxII; ++Try) {
      if (static_cast<unsigned>(Try) == IIHint)
        continue;
      if (tryII(Try, HR) && acceptII()) {
        Found = true;
        break;
      }
    }
  }

  if (!Found) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: no schedule found\n");
    ++NumMultiStageFail;
    LastRejectReason = "ii-exhaustion";
    emitRemark(MBB, "MultiStageExhaustion",
               "exhausted: no feasible II in [" + Twine(StartII) + "," +
                   Twine(MaxII) + "] ResMII=" + Twine(ResMII) +
                   " LinearLength=" + Twine(LinearLength) +
                   " pins=" + Twine((unsigned)MemberPin.size()) +
                   " no-seq-fallback qualify-or-cut=seated product-off");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }

  HasValidPlan = true;
  LastRejectReason = nullptr;
  // Independent recount: countPlannedParcels walks ExactCommitPlan.
  // A mismatch is an II lie — fail closed, no SWPS stamp.
  MeasuredII = countPlannedParcels();
  if (MeasuredII != II) {
    ++NumMultiStageFail;
    LastRejectReason = "measured-ii-mismatch";
    HasValidPlan = false;
    emitRemark(MBB, "MultiStageReject",
               "rejected: measured-II=" + Twine(MeasuredII) +
                   " != searched II=" + Twine(II) + " ResMII=" +
                   Twine(ResMII) + " no-seq-fallback");
    emitRemark(MBB, "MultiStagePolicy",
               "qualify-or-cut: seated product-off host-live "
               "swpsolver=unavailable hwloop-combined=off "
               "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
    return false;
  }
  ++NumMultiStageQualifySeated;
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: analyze accept II=" << II
                    << " NStages=" << NStages << "\n");
  emitRemark(MBB, "MultiStageAccept",
             "accepted II=" + Twine(II) + " stages=" + Twine(NStages) +
                 " body=" + Twine((unsigned)Body.size()) +
                 " ResMII=" + Twine(ResMII) +
                 " LinearLength=" + Twine(LinearLength) +
                 " measured-II=" + Twine(MeasuredII) +
                 " pins=" + Twine((unsigned)MemberPin.size()) +
                 " strategy=" +
                 Twine(LastStrategyName ? LastStrategyName : "none") +
                 " resource-bias=" +
                 Twine(LastResourceBias ? "applied" : "idle") +
                 " no-seq-fallback qualify-or-cut=seated product-off");
  emitRemark(MBB, "MultiStageQualify",
             "qualify parcels-per-iter=" + Twine(MeasuredII) +
                 " searched-II=" + Twine(II));
  emitRemark(MBB, "MultiStageSWPS",
             "swps observe-only measured-II=" + Twine(MeasuredII) +
                 " searched-II=" + Twine(II) + " no-asm-stamp");
  emitRemark(MBB, "MultiStagePolicy",
             "qualify-or-cut: seated product-off host-live "
             "swpsolver=" +
                 Twine(LastSWPSolverStatus ? LastSWPSolverStatus
                                           : "unavailable") +
                 " hwloop-combined=off "
                 "nat-ipc=measured-miss no-competitive-ipc no-stage0-ib-pp");
  return true;
}

bool HaydnMultiStageSMS::tryAfterOrdinarySchedule(ScheduleDAGMI &TheDAG,
                                                  unsigned IIHint) {
  if (!EnableHaydnMultiStageSMS)
    return false;
  if (!analyze(TheDAG, IIHint)) {
    destroyTwoCopyGraph();
    return false;
  }
  if (!runPreflight()) {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageReject",
                 Twine("preflight reject: ") +
                     (LastRejectReason ? LastRejectReason : "unknown"));
    destroyTwoCopyGraph();
    return false;
  }
  if (HaydnMultiStageSMSAnalysisOnly) {
    ++NumMultiStageAnalysisAccept;
    destroyTwoCopyGraph();
    return false;
  }
  if (HaydnMultiStageSMSForceFail) {
    ++NumMultiStageCertReject;
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageReject",
                 "forced certificate failure after preflight");
    return false;
  }
  return materialize();
}


