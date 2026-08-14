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
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPostRAScratch.h"
#include "HaydnResourceCycle.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
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
      if (!HR.checkConflict(Scoreboard, MI, Mod))
        return C;
    }
    return std::nullopt;
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
// Cap II search distance.
static constexpr int MaxIISearch = 24;

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

//===----------------------------------------------------------------------===//
// Candidate / ResMII
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Candidate / ResMII
//===----------------------------------------------------------------------===//

static int depLat(const SDep &Dep) {
  // AIE uses SDep::getSignedLatency(); LLVM 22 SDep only has unsigned
  // getLatency(). Negative-latency WAR folding is an AIE fork and is omitted.
  return static_cast<int>(Dep.getLatency());
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
  if (!certificateLifetimesNoSpill())
    return false;
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

  auto computedLiveInHash = [&](MachineBasicBlock &MBB) -> uint64_t {
    LivePhysRegs Live(*TRI);
    Live.addLiveOutsNoPristines(MBB);
    for (MachineInstr &MI : reverse(MBB))
      Live.stepBackward(MI);
    uint64_t H = 0;
    unsigned NLive = 0;
    for (MCPhysReg R : Live) {
      H = (H * 131) ^ static_cast<uint64_t>(R);
      ++NLive;
    }
    return H ^ (static_cast<uint64_t>(NLive) << 32);
  };

  uint64_t Prev = 0;
  bool Stable = false;
  for (unsigned Iter = 0; Iter < 8; ++Iter) {
    uint64_t Cur = 0;
    for (unsigned I = 0, E = Region.size(); I != E; ++I)
      Cur ^= computedLiveInHash(*Region[I]) << I;
    if (Iter && Cur == Prev) {
      Stable = true;
      break;
    }
    Prev = Cur;
  }
  if (!Stable)
    return false;
  if (!proveLivePhysNoSpillSubreg())
    return false;

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
  if (!HasValidPlan || NStages < 2) return false;
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
                   " lcd-as-windows");
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
  return true;
}
bool HaydnMultiStageSMS::preflightBundle() {
  if (forceFailPreflight(HaydnMultiStagePreflightSeat::PF_BUNDLE)) {
    LastRejectReason = "PF-BUNDLE-force";
    return false;
  }
  if (!certificateKernelPlan() || !certificateExactCommitPlan())
    return false;
  if (!DAG)
    return false;
  const TargetSubtargetInfo &ST = DAG->MF.getSubtarget();
  HaydnHazardRecognizer HR(ST.getInstrInfo(), ST.getInstrItineraryData(),
                           /*IsPreRA=*/false);
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
  // Issue-width ResMII: ceil(NBody / ISSUE_SLOT_COUNT). Slot exclusivity may
  // raise this; accept a lower bound and let tryII fail closed on conflicts.
  unsigned NBody = 0;
  for (const MachineInstr &MI : LoopBlock) {
    if (isZOLTerminator(MI) || isSkippableBodyMI(MI))
      continue;
    if (MI.isPseudo() && !MI.isCopy())
      continue;
    ++NBody;
  }
  const unsigned IssueWidth = Haydn::ISSUE_SLOT_COUNT;
  int MII = std::max(
      1, static_cast<int>((NBody + IssueWidth - 1) / IssueWidth));
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: ResMII=" << MII << " (NBody="
                    << NBody << ")\n");
  return MII;
}

//===----------------------------------------------------------------------===//
// Loop-carried windows + pipe schedule (AIE PostPipeliner core)
//===----------------------------------------------------------------------===//

void HaydnMultiStageSMS::computeForward() {
  // AIE PostPipeliner::computeForward (AIEPostPipeliner.cpp:320-360).
  // SlotCounts local-resource bias omitted: Haydn HR owns unit occupancy.
  // Earliest follows Data and memory Order only. LLVM 22 Anti/Output
  // latency is unsigned 0, not AIE getSignedLatency() negative WAR;
  // counting those succs serializes the two-copy body.
  for (int K = 0; K < NInstr; ++K) {
    HaydnMultiStageNodeInfo &Me = Sched[K];
    SUnit &SU = TwoCopyDAG->SUnits[K];
    for (auto &Dep : SU.Preds) {
      if (Dep.getKind() != SDep::Data)
        continue;
      int P = static_cast<int>(Dep.getSUnit()->NodeNum);
      if (P < 0 || P >= NInstr)
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
      if (Dep.getKind() != SDep::Data && !Dep.isNormalMemory())
        continue;
      HaydnMultiStageNodeInfo &SInfo = Sched[static_cast<int>(Succ->NodeNum)];
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
  // Ancestor/offspring SlotCounts bias omitted (Haydn HR owns slots).
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
  }
  computeForward();
  while (computeBackward())
    ;
  computeRecMIIFromDAG();
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
  for (int K = 0; K < NInstr; ++K) {
    const HaydnMultiStageNodeInfo &Node = Sched[K];
    while (Node.Earliest > Node.Latest + ML)
      ML += II;
  }
  return ML;
}

void HaydnMultiStageSMS::schedulePipeNode(SUnit &SU, int Cycle,
                                         HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::scheduleNode (AIEPostPipeliner.cpp:232-282).
  Sched[static_cast<int>(SU.NodeNum)].Cycle = Cycle;
  for (auto &Dep : SU.Succs) {
    if (Dep.getKind() != SDep::Data && !Dep.isNormalMemory())
      continue;
    int Latency = depLat(Dep);
    SUnit *Succ = Dep.getSUnit();
    if (Succ->isBoundaryNode())
      continue;
    const int SNum = static_cast<int>(Succ->NodeNum);
    const int NewEarliest = Cycle + Latency;
    if (NewEarliest > Strategy.earliest(*Succ)) {
      Sched[SNum].LastEarliestPusher = static_cast<int>(SU.NodeNum);
      Sched[static_cast<int>(SU.NodeNum)].NumPushedEarliest++;
      Strategy.setEarliest(SNum, NewEarliest);
      Strategy.setChanged();
    }
  }
  for (auto &Dep : SU.Preds) {
    if (Dep.getKind() != SDep::Data)
      continue;
    int Latency = depLat(Dep);
    SUnit *Pred = Dep.getSUnit();
    if (Pred->isBoundaryNode())
      continue;
    const int PNum = static_cast<int>(Pred->NodeNum);
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
  assert(FirstUnscheduled <= LastUnscheduled);
  while (Sched[FirstUnscheduled].Scheduled)
    ++FirstUnscheduled;
  while (Sched[LastUnscheduled].Scheduled)
    --LastUnscheduled;
  assert(FirstUnscheduled <= LastUnscheduled);
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
  assert(Best >= 0);
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
    // Haydn itinerary pipeline depth is often 1, so AIE's
    // Horizon=min(II+PD, Size-PD) emits only one copy. Always occupy
    // two kernel iterations so first-iter matches other-iter's
    // 2*II+PD resource check (AIEPostPipeliner.cpp:1048-1058).
    int Cycle = ModCycle;
    const int Horizon = std::min(2 * II, ScoreboardSize);
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
  if (NStages >= 2)
    return true;
  // Haydn materialize requires distinct prolog/kernel/epilog (NStages>=2).
  // Force the later half into stage 1 via TweakedEarliest and retry
  // (AIE resetSchedule Static vs Tweaked, AIEPostPipeliner.cpp:827-841).
  if (NInstr < 3)
    return false;
  bool Tweaked = false;
  for (int K = NInstr / 2; K < NInstr; ++K) {
    const int Forced = std::max(Sched[K].StaticEarliest, II);
    if (!Sched[K].TweakedEarliest || *Sched[K].TweakedEarliest < Forced) {
      Sched[K].TweakedEarliest = Forced;
      Tweaked = true;
    }
  }
  if (Tweaked)
    Strategy.setChanged();
  return false;
}

bool HaydnMultiStageSMS::scheduleOtherIterations(
    HaydnMultiStageStrategy &Strategy) {
  // AIE PostPipeliner::scheduleOtherIterations (AIEPostPipeliner.cpp:949-1067).
  auto isOnEarliestChain = [&](int Start, int Target) {
    std::optional<int> Prev = Sched[Start].LastEarliestPusher;
    while (Prev) {
      if (*Prev == Target)
        return true;
      Prev = Sched[*Prev].LastEarliestPusher;
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

bool HaydnMultiStageSMS::tryPipeApproaches(const HaydnHazardRecognizer &HR) {
  // AIE tryApproaches Static vs Tweaked reset/retry (AIEPostPipeliner.cpp:1441-1466).
  // ExtraStages 0/1 is AIE ConfigStrategy.LatestBias = MinLength + Extra*II.
  // Full ConfigStrategy lattice / SWPSolver omitted (do not import AIE solver).
  PipeHR = &HR;
  const int PipeDepth = std::max(HR.getPipelineDepth(), 1);
  ScoreboardSize = 2 * II + PipeDepth;
  PipeScoreboard.config(0, ScoreboardSize - 1);
  constexpr int HeuristicRuns = 8;
  const int ExtraStages[] = {1, 0};
  for (int Extra : ExtraStages) {
    HaydnMultiStageStrategy Strategy(*TwoCopyDAG, Sched,
                                     MinLength + Extra * II);
    resetPipeSchedule(/*FullReset=*/true);
    for (int Run = 0; Run < HeuristicRuns; ++Run) {
      if (scheduleWithStrategy(Strategy))
        return true;
      if (!Strategy.checkAndResetChanged())
        break;
      resetPipeSchedule(/*FullReset=*/false);
    }
  }
  return false;
}

bool HaydnMultiStageSMS::computeASAPEarliest() {
  // Intra-iteration ASAP is computeForward; LCD fold is computeLoopCarriedParameters.
  if (!TwoCopyDAG || NInstr < 1)
    return false;
  computeForward();
  LinearLength = 0;
  for (int I = 0; I < NInstr; ++I)
    LinearLength = std::max(LinearLength, Sched[I].Earliest + 1);
  return LinearLength >= 1;
}

bool HaydnMultiStageSMS::tryII(int TryII, const HaydnHazardRecognizer &HR) {
  assert(TryII >= 1);
  if (TryII < RecMII)
    return false;
  II = TryII;
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

// Loop-carried self-bump (iv = iv + step). Peeling these into the prolog
// is wrong: the dest is not live at insert (P2 vec_scale: ADD32 r14,r14,r6
// aborted materialize after tryII II=2). Leave them in the kernel only.
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
  OrdinarySnapshot.clear();
}

bool HaydnMultiStageSMS::hasSufficientTripCount() const {
  if (!TripCountDef || NStages < 2 || NStages > 4)
    return false;
  // tryII already rejects NStages > 4. Peels consume NStages-1 iterations;
  // require the static trip (when known) to cover full stage depth.
  const int Need = NStages;
  const int PeelIters = NStages - 1;

  if (IsSoftCounted) {
    Register Trip;
    if (!isSoftCountdownBump(*TripCountDef, Trip))
      return false;
    return Trip.isPhysical() && Trip != Haydn::R0 && Preheader != nullptr;
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
  // LoopStart: adj is simm6; require the peel delta stays encodable. Static
  // trip lives in $src and is not always an imm at this point.
  if (Opc == Haydn::LoopStart) {
    if (TripCountDef->getNumOperands() < 2 ||
        !TripCountDef->getOperand(1).isImm())
      return false;
    int64_t NewAdj = TripCountDef->getOperand(1).getImm() - PeelIters;
    return NewAdj >= -32 && NewAdj <= 31;
  }
  // Register-trip: static trip not known; remat absorbs -(NStages-1). Require
  // a non-R0 physreg trip source so remat has a real Src.
  if (TII->isHardwareLoopRegTripOpcode(Opc)) {
    if (TripCountDef->getNumOperands() <= 3 ||
        !TripCountDef->getOperand(3).isReg())
      return false;
    Register Cnt = TripCountDef->getOperand(3).getReg();
    return Cnt.isPhysical() && Cnt != Haydn::R0;
  }
  return false;
}

bool HaydnMultiStageSMS::certificateTripAdjust() const {
  // Same static predicates as hasSufficientTripCount; kept separate so the
  // materialize gate names the trip-adjust certificate explicitly.
  return hasSufficientTripCount();
}

bool HaydnMultiStageSMS::certificateKernelPlan() const {
  assert(HasValidPlan && II >= 1);
  const int N = static_cast<int>(Body.size());
  if (N < 2 || NStages < 2 || NStages > 4 || II < 1)
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

  // Latency deps still hold under absolute cycles (including wrap via stage).
  for (int I = 0; I < N; ++I) {
    for (const SDep &Pred : Body[I]->Preds) {
      if (Pred.getSUnit()->isBoundaryNode())
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

  // Kernel certificate: every modulo group is at most issue-width wide.
  // Multi-MI groups that pass canCoissueProductCycle exact-commit as one
  // Format E parcel; groups that fail coissue stay sequential parcels
  // (still a legal multi-stage schedule via prolog/epilog peels).
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
    if (Count > 3)
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
    if (Group.size() < 2)
      continue;
    const unsigned NBundle = std::min<unsigned>(Group.size(), 3);
    ArrayRef<MachineInstr *> Slice(Group.data(), NBundle);
    ExactCommitPlan[M] = haydn::bundle::canCoissueProductCycle(Slice);
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
  if (!HasValidPlan || II < 1 || Body.empty())
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
  DenseMap<unsigned, int> OpenDef;
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
      auto It = OpenDef.find(Reg.id());
      if (It == OpenDef.end())
        continue;
      if (C >= It->second && (C - It->second) >= II)
        return false;
    }
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
        continue;
      Register Reg = MO.getReg();
      if (Reg == Haydn::R0)
        continue;
      OpenDef.erase(Reg.id());
      if (!MO.isDead())
        OpenDef[Reg.id()] = C;
    }
  }
  return true;
}

bool HaydnMultiStageSMS::certificateDistinctStageOccupancy() const {
  if (!HasValidPlan || NStages < 2 || Body.empty())
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
  if (!HasValidPlan || !LoopBB || !DAG || II < 1 || NStages < 2)
    return;
  auto *MFI = DAG->MF.getInfo<HaydnMachineFunctionInfo>();
  if (!MFI)
    return;
  HaydnMachineFunctionInfo::SMSSWPSInfo SW;
  SW.ResMII = static_cast<unsigned>(std::max(0, LastResMII));
  SW.RecMII = static_cast<unsigned>(std::max(0, RecMII));
  SW.MII = std::max(SW.ResMII, SW.RecMII);
  SW.StageCount = static_cast<unsigned>(NStages);
  SW.NumOps = static_cast<unsigned>(Body.size());
  SW.ScheduledII = static_cast<unsigned>(II);
  MFI->recordSMSLoop(LoopBB, SW);
  emitRemark(*LoopBB, "MultiStageSWPS",
             "swps II=" + Twine(II) + " stages=" + Twine(NStages) +
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
        if (!prologUsesAreAvailable(*Preheader, PrologInsertPt, SimulatedProlog,
                                    *Orig)) {
          if (isLoopCarriedSelfBump(*Orig))
            continue;
          return false;
        }
        SimulatedProlog.push_back(Orig);
      }
    }
  }
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
  auto rejectNoMutate = [&](const char *Why) -> bool {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    LastRejectReason = Why;
    OrdinarySnapshot.clear();
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: force-fail pre-mutation — "
                      << Why << "\n");
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageReject",
                 Twine("force-fail before mutation (") + Why + ")");
    return false;
  };
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
  if (!certificateTripAdjust())
    return rejectCert("trip-adjust");
  if (!certificateKernelPlan())
    return rejectCert("kernel-plan");
  if (ExactCommitPlan.size() != static_cast<size_t>(II) &&
      !certificateExactCommitPlan())
    return rejectCert("exact-commit-plan");

  // Compute prolog insert point and run off-side prolog/epilog certificates.
  MachineBasicBlock::iterator PrologInsertPt =
      Preheader->getFirstTerminator();
  for (MachineInstr &MI : *Preheader) {
    if (isHwLoopSetup(MI))
      PrologInsertPt = std::next(MI.getIterator());
  }
  if (!certificatePrologLiveness(PrologInsertPt))
    return rejectCert("prolog-liveness");
  if (!certificateEpilogUses())
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
  // Prologue (AIE visitPipelineSchedule): for each prologue stage S, emit
  // nodes with ModuloCycle==M && Cycle < (S+1)*II. Cap NStages at 4 in tryII.
  // Skip loop-carried self-bumps (stay in kernel). Certificate already proved
  // every non-bump peel is live; a post-cert liveness miss is fail-closed
  // before any insert (delete orphan clones, leave MF unchanged).
  const int NPrologStages = NStages - 1;
  SmallVector<MachineInstr *, 8> PrologMIs;
  auto discardOrphanClones = [&](SmallVectorImpl<MachineInstr *> &Clones) {
    for (MachineInstr *C : Clones)
      MF.deleteMachineInstr(C);
    Clones.clear();
  };
  for (int S = 0; S < NPrologStages; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Sched[I].ModuloCycle != M || Sched[I].Cycle >= (S + 1) * II)
          continue;
        MachineInstr *Orig = Body[I]->getInstr();
        if (!prologUsesAreAvailable(*Preheader, PrologInsertPt, PrologMIs,
                                    *Orig)) {
          if (isLoopCarriedSelfBump(*Orig)) {
            LLVM_DEBUG(dbgs()
                       << "HaydnMultiStageSMS: skip prolog peel "
                          "(self-bump not live at insert): "
                       << *Orig);
            continue;
          }
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
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_ALLOC))
    return rollback("JM-ALLOC-force");

  for (MachineInstr *Clone : PrologMIs)
    PrologMBB->insert(PrologMBB->end(), Clone);
  for (MachineInstr *Clone : EpilogMIs) {
    EpilogMBB->insert(EpilogMBB->end(), Clone);
    addUsesAsLiveIns(*EpilogMBB, *Clone);
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
  // opcodes are still recognized by isSoftCountdownBump. Reg-trip remat
  // mutates the setup use register; F4 snapshot restores it.
  if (!adjustTripCount(-(NStages - 1)))
    return rollback("JM-TRIP-restore");
  if (forceFailJournal(HaydnMultiStageJournalSeat::JM_TRIP))
    return rollback("JM-TRIP-force");

  unsigned ParcelsCommitted = 0;
  for (int M = 0; M < II; ++M) {
    SmallVector<MachineInstr *, 4> Group;
    for (int Idx : KernelByMod[M]) {
      MachineInstr *MI = Body[Idx]->getInstr();
      if (!MI || isSkippableBodyMI(*MI) || isZOLTerminator(*MI))
        continue;
      Group.push_back(MI);
    }
    if (Group.size() < 2)
      continue;
    const unsigned NBundle = std::min<unsigned>(Group.size(), 3);
    ArrayRef<MachineInstr *> Slice(Group.data(), NBundle);
    const bool WantExact =
        M < (int)ExactCommitPlan.size() && ExactCommitPlan[M];
    if (!WantExact) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: mod=" << M
                        << " sequential (preflight)\n");
      continue;
    }
    if (!haydn::bundle::canCoissueProductCycle(Slice) ||
        !haydn::bundle::commitExactMultiMIProductCycle(Slice)) {
      return rollback("JM-COMMIT-restore");
    }
    ++ParcelsCommitted;
  }
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
  if (LoopBB)
    emitRemark(*LoopBB, "MultiStageStageMBB",
               "stage-mbb prolog=" + Twine(PrologMBB->getNumber()) +
                   " epilog=" + Twine(EpilogMBB->getNumber()) +
                   " stages=" + Twine(NStages));
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

  if (!buildTwoCopyGraph(TheDAG)) {
    ++NumMultiStageFail;
    LastRejectReason = "two-copy-graph";
    emitRemark(MBB, "MultiStageReject",
               "rejected: two-copy buildSchedGraph failed");
    return false;
  }
  RecMII = computeRecMII();
  AAResults *AA =
      static_cast<HaydnScheduleDAGMI &>(TheDAG).getAliasAnalysis();
  const unsigned MemLCD = countMayAliasStoreLoadPairs(Body, AA);
  emitRemark(MBB, "MultiStageLCD",
             "lcd two-iteration RecMII=" + Twine(RecMII) +
                 " edges=" + Twine(static_cast<unsigned>(LCDEdges.size())) +
                 " mem=" + Twine(MemLCD) + " lcd-as-windows");

  if (!computeASAPEarliest()) {
    ++NumMultiStageFail;
    LastRejectReason = "asap-fail";
    emitRemark(MBB, "MultiStageReject",
               "rejected: ASAP earliest placement did not converge");
    return false;
  }

  const TargetSubtargetInfo &ST = TheDAG.MF.getSubtarget();
  const TargetInstrInfo *TII = ST.getInstrInfo();
  const InstrItineraryData *Itin = ST.getInstrItineraryData();
  HaydnHazardRecognizer HR(TII, Itin, /*IsPreRA=*/false);

  int ResMII = getResMII(MBB);
  int StartII = std::max(ResMII, RecMII);
  if (IIHint > 0)
    StartII = static_cast<int>(IIHint);

  bool Found = false;
  // Only accept II < LinearLength so multi-stage is a real overlap win vs list.
  if (LinearLength < 2) {
    LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: LinearLength=" << LinearLength
                      << " — no multi-stage headroom\n");
    ++NumMultiStageFail;
    LastRejectReason = "no-headroom";
    emitRemark(MBB, "MultiStageReject",
               "rejected: no multi-stage headroom (LinearLength=" +
                   Twine(LinearLength) + ")");
    return false;
  }
  const int ListBaseline = LinearLength - 1; // strict: II < LinearLength
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
                   " LinearLength=" + Twine(LinearLength));
    return false;
  }
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: II search [" << StartII << ","
                    << MaxII << "] ResMII=" << ResMII
                    << " LinearLength=" << LinearLength
                    << " ListBaseline=" << ListBaseline << "\n");
  auto acceptII = [&]() {
    if (II >= LinearLength) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject II=" << II
                        << " >= LinearLength=" << LinearLength << "\n");
      return false;
    }
    if (II > ListBaseline) {
      LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: reject II=" << II
                        << " > ListBaseline=" << ListBaseline << "\n");
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
                   " LinearLength=" + Twine(LinearLength));
    return false;
  }

  HasValidPlan = true;
  LastRejectReason = nullptr;
  LLVM_DEBUG(dbgs() << "HaydnMultiStageSMS: analyze accept II=" << II
                    << " NStages=" << NStages << "\n");
  emitRemark(MBB, "MultiStageAccept",
             "accepted II=" + Twine(II) + " stages=" + Twine(NStages) +
                 " body=" + Twine((unsigned)Body.size()) +
                 " ResMII=" + Twine(ResMII) +
                 " LinearLength=" + Twine(LinearLength));
  return true;
}

bool HaydnMultiStageSMS::tryAfterOrdinarySchedule(ScheduleDAGMI &TheDAG,
                                                  unsigned IIHint) {
  if (!EnableHaydnMultiStageSMS)
    return false;
  if (!analyze(TheDAG, IIHint))
    return false;
  if (!runPreflight()) {
    ++NumMultiStageCertReject;
    ++NumMultiStageFail;
    if (LoopBB)
      emitRemark(*LoopBB, "MultiStageReject",
                 Twine("preflight reject: ") +
                     (LastRejectReason ? LastRejectReason : "unknown"));
    return false;
  }
  if (HaydnMultiStageSMSAnalysisOnly) {
    ++NumMultiStageAnalysisAccept;
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


