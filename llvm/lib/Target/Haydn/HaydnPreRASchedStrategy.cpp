//===-- HaydnPreRASchedStrategy.cpp - AIE-style pre-RA MI scheduler -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEPreRASchedStrategy core correctness path:
// initPolicy: OnlyBottomUp + always track pressure
// initialize: clamp PSet thresholds
// isAvailableNode: pressure-based delay with SUDelayerMap (AIE)
// tryCandidate: pressure-first (AIE-ish with public RPDelta)
//
// estimatePressureDiff / getPressureChange / findPressureReducer are local
// ports of AIEMachineScheduler.cpp helpers (same logic; VirtRegOrUnit for
// stock RegisterPressure API).
//
// B4.1: productFeasibleFormatMask is the Pre-RA FormatID frontier (size-1
// Full). No setDesc / no FormatID freeze before RA (plan §7.1). SMS shares
// the same Bundle/ResourceCycle getFeasibleFormatMask adapters.
//
//===----------------------------------------------------------------------===//

#include "HaydnPreRASchedStrategy.h"
#include "HaydnSchedMutations.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <functional>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "haydn-prera-sched"

static cl::opt<bool> EnableHaydnFinerRPTracking(
    "haydn-premisched-finer-rp-tracking", cl::init(true), cl::Hidden,
    cl::desc("Pre-RA: AIE-style pressure-first tryCandidate "
             "(aie-premisched-finer-rp-tracking peer)"));

// AIE enables pressure delay in isAvailableNode by default. On Haydn it
// currently regresses yarpgen seed1 to HOSTCALL_ERROR (guest abort) vs
// sticky-oracle when off — keep the AIE hook wired but default-off until
// MIR-validated. Force on: -haydn-premisched-isavail-delay.
static cl::opt<bool> EnableHaydnIsAvailPressureDelay(
    "haydn-premisched-isavail-delay", cl::init(false), cl::Hidden,
    cl::desc("Pre-RA: AIE isAvailableNode pressure delayer (default off; "
             "seed1 HOSTCALL residual under investigation)"));

static cl::opt<unsigned> HaydnNumCriticalFreeRegs(
    "haydn-premisched-critical-free-regs", cl::init(2), cl::Hidden,
    cl::desc("AIE NumCriticalFreeRegs peer"));

static cl::opt<bool> EnableHaydnPreRAForceBottomUp(
    "haydn-premisched-force-bottom-up", cl::init(true), cl::Hidden,
    cl::desc("Force OnlyBottomUp (AIE PreRA leaveRegion contract)"));

//===----------------------------------------------------------------------===//
// AIE helpers (AIEMachineScheduler.cpp) — stock VirtRegOrUnit API
//===----------------------------------------------------------------------===//

static PressureDiff estimatePressureDiff(const SUnit &SU,
                                         const RegPressureTracker &RPT) {
  const MachineInstr &MI = *SU.getInstr();
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  PressureDiff PDiff;
  const LiveRegSet &LiveRegs = RPT.getLiveRegs();
  LiveRegSet DefinedRegs;
  DefinedRegs.init(MRI);

  for (const MachineOperand &D : MI.defs()) {
    if (D.isReg() && D.getReg().isVirtual()) {
      VirtRegOrUnit VRU(D.getReg());
      PDiff.addPressureChange(VRU, /*IsDec=*/true, &MRI);
      DefinedRegs.insert(VRegMaskOrUnit(VRU, LaneBitmask::getAll()));
    }
  }
  for (const MachineOperand &U : MI.uses()) {
    if (!U.isReg() || !U.getReg().isVirtual())
      continue;
    VirtRegOrUnit VRU(U.getReg());
    LaneBitmask LiveLanes =
        LiveRegs.contains(VRU) & ~DefinedRegs.contains(VRU);
    if (LiveLanes.none())
      PDiff.addPressureChange(VRU, /*IsDec=*/false, &MRI);
  }
  return PDiff;
}

static PressureChange getPressureChange(const PressureDiff &PD,
                                        bool FindMin = true) {
  if (PD.begin() == PD.end())
    return {};
  auto Cmp = [](const PressureChange &Lhs, const PressureChange &Rhs) {
    return Lhs.getUnitInc() < Rhs.getUnitInc();
  };
  return FindMin ? *std::min_element(PD.begin(), PD.end(), Cmp)
                 : *std::max_element(PD.begin(), PD.end(), Cmp);
}

static const SUnit *findPressureReducer(unsigned CriticalPSet,
                                        ArrayRef<SUnit *> Nodes,
                                        const RegPressureTracker &RPT) {
  for (const SUnit *SU : Nodes) {
    PressureDiff PDiff = estimatePressureDiff(*SU, RPT);
    for (const PressureChange &PC : PDiff) {
      if (PC.isValid() && PC.getPSet() == CriticalPSet && PC.getUnitInc() < 0)
        return SU;
    }
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// HaydnPreRASchedStrategy
//===----------------------------------------------------------------------===//

void HaydnPreRASchedStrategy::initPolicy(MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End,
                                         unsigned NumRegionInstrs) {
  GenericScheduler::initPolicy(Begin, End, NumRegionInstrs);
  if (EnableHaydnPreRAForceBottomUp) {
    RegionPolicy.OnlyBottomUp = true;
    RegionPolicy.OnlyTopDown = false;
  }
  RegionPolicy.ShouldTrackPressure = true;
}

void HaydnPreRASchedStrategy::initialize(ScheduleDAGMI *DAGIn) {
  GenericScheduler::initialize(DAGIn);
  PSetThresholds.clear();
  SUDelayerMap.clear();
  if (!DAG || !DAG->isTrackingPressure() || !Context || !Context->RegClassInfo)
    return;

  const std::vector<unsigned> &RegionMaxPressure =
      DAG->getRegPressure().MaxSetPressure;
  for (unsigned PSet = 0, EndPSet = RegionMaxPressure.size(); PSet < EndPSet;
       ++PSet) {
    unsigned MaxPressure = RegionMaxPressure[PSet];
    unsigned Limit = Context->RegClassInfo->getRegPressureSetLimit(PSet);
    if (MaxPressure > Limit) {
      unsigned Extra = MaxPressure - Limit;
      Limit = (Limit > Extra) ? Limit - Extra : 0;
    }
    PSetThresholds.push_back(Limit);
  }
}

void HaydnPreRASchedStrategy::enterRegion(MachineBasicBlock *BB,
                                          MachineBasicBlock::iterator /*Begin*/,
                                          MachineBasicBlock::iterator /*End*/,
                                          unsigned NumRegionInstrs) {
  CurMBB = BB;
  // AIE resizes to region MI count; NodeNum indexes SUnits (can differ).
  // Over-allocate to max(region, later SUnits) — grow again in isAvailableNode.
  SUDelayerMap.assign(std::max(NumRegionInstrs, 1u), UnknownSUNum);
}

void HaydnPreRASchedStrategy::leaveRegion(const SUnit & /*ExitSU*/) {
  CurMBB = nullptr;
  SUDelayerMap.clear();
}

bool HaydnPreRASchedStrategy::canBeDelayed(const SUnit &DelayedSU,
                                           const SUnit &Delayer) const {
  std::function<bool(unsigned)> Impl = [&](unsigned SUNum) {
    if (SUNum == UnknownSUNum)
      return true;
    if (SUNum == DelayedSU.NodeNum)
      return false;
    if (SUNum >= SUDelayerMap.size())
      return true;
    return Impl(SUDelayerMap[SUNum]);
  };
  return Impl(Delayer.NodeNum);
}

bool HaydnPreRASchedStrategy::isAvailableNode(SUnit &SU, SchedBoundary &Zone,
                                              bool VerifyReadyCycle) {
  // Force cycle check (AIE: always verify ready cycle).
  // Honor caller's VerifyReadyCycle (stock releaseNode passes !IsBuffered).
  // AIE forces true; we match stock until pressure-delay is validated.
  bool Avail =
      MachineSchedStrategy::isAvailableNode(SU, Zone, VerifyReadyCycle);
  // Default: AIE base path only (ready+hazard). Pressure delay is opt-in.
  if (!EnableHaydnIsAvailPressureDelay)
    return Avail;
  if (!Avail || !DAG || !DAG->isTrackingPressure() || PSetThresholds.empty())
    return Avail;

  // Bottom-up zone only for pressure delay (AIE PreRA is OnlyBottomUp).
  if (Zone.isTop())
    return Avail;

  // Ensure delayer map covers all SUnit node numbers.
  if (SU.NodeNum >= SUDelayerMap.size())
    SUDelayerMap.resize(SU.NodeNum + 1, UnknownSUNum);

  const RegPressureTracker &BotRPT = DAG->getBotRPTracker();
  PressureChange WorstPC =
      getPressureChange(estimatePressureDiff(SU, BotRPT), /*FindMin=*/false);
  if (!WorstPC.isValid() || WorstPC.getUnitInc() <= 0)
    return true; // improving or neutral pressure

  unsigned PSet = WorstPC.getPSet();
  if (PSet >= PSetThresholds.size())
    return true;

  unsigned CurrPressure = BotRPT.getRegSetPressureAtPos()[PSet];
  unsigned Inc = static_cast<unsigned>(WorstPC.getUnitInc());
  if (CurrPressure + Inc + (HaydnNumCriticalFreeRegs * Inc) <
      PSetThresholds[PSet])
    return true; // within limit

  // Would likely spill — delay if a pending pressure reducer exists (AIE).
  if (const SUnit *Reducer =
          findPressureReducer(PSet, Zone.Pending.elements(), BotRPT);
      Reducer && canBeDelayed(SU, *Reducer)) {
    SUDelayerMap[SU.NodeNum] = Reducer->NodeNum;
    LLVM_DEBUG(dbgs() << "HaydnPreRA: delay SU(" << SU.NodeNum
                      << ") waiting SU(" << Reducer->NodeNum << ")\n");
    return false;
  }
  return true;
}

bool HaydnPreRASchedStrategy::tryCandidate(SchedCandidate &Cand,
                                           SchedCandidate &TryCand,
                                           SchedBoundary *Zone) const {
  if (!EnableHaydnFinerRPTracking)
    return GenericScheduler::tryCandidate(Cand, TryCand, Zone);

  // AIE tryCandidate (pressure-first subset).
  if (!Cand.isValid()) {
    TryCand.Reason = NodeOrder;
    return true;
  }

  if (tryGreater(biasPhysReg(TryCand.SU, TryCand.AtTop),
                 biasPhysReg(Cand.SU, Cand.AtTop), TryCand, Cand, PhysReg))
    return TryCand.Reason != NoCand;

  if (DAG->isTrackingPressure() &&
      tryPressure(TryCand.RPDelta.Excess, Cand.RPDelta.Excess, TryCand, Cand,
                  RegExcess, TRI, DAG->MF))
    return TryCand.Reason != NoCand;

  if (DAG->isTrackingPressure() &&
      tryPressure(TryCand.RPDelta.CriticalMax, Cand.RPDelta.CriticalMax,
                  TryCand, Cand, RegCritical, TRI, DAG->MF))
    return TryCand.Reason != NoCand;

  if (tryLess(getWeakLeft(TryCand.SU, TryCand.AtTop),
              getWeakLeft(Cand.SU, Cand.AtTop), TryCand, Cand, Weak))
    return TryCand.Reason != NoCand;

  if (DAG->isTrackingPressure() && !PSetThresholds.empty()) {
    const RegPressureTracker &BotRPT = DAG->getBotRPTracker();
    auto IsNearCritical = [&](const PressureChange &PC) {
      if (!PC.isValid())
        return false;
      unsigned PSet = PC.getPSet();
      if (PSet >= PSetThresholds.size())
        return false;
      unsigned Curr = BotRPT.getRegSetPressureAtPos()[PSet];
      unsigned Th = PSetThresholds[PSet];
      unsigned Free =
          HaydnNumCriticalFreeRegs * std::abs(PC.getUnitInc());
      return Th <= Free || Curr >= Th - Free;
    };
    PressureChange TryPC =
        getPressureChange(estimatePressureDiff(*TryCand.SU, BotRPT));
    PressureChange CandPC =
        getPressureChange(estimatePressureDiff(*Cand.SU, BotRPT));
    if ((IsNearCritical(TryPC) || IsNearCritical(CandPC)) &&
        tryPressure(TryPC, CandPC, TryCand, Cand, RegMax, TRI, DAG->MF))
      return TryCand.Reason != NoCand;

    if (tryPressure(TryCand.RPDelta.CurrentMax, Cand.RPDelta.CurrentMax,
                    TryCand, Cand, RegMax, TRI, DAG->MF))
      return TryCand.Reason != NoCand;
  }

  if ((Zone->isTop() && TryCand.SU->NodeNum < Cand.SU->NodeNum) ||
      (!Zone->isTop() && TryCand.SU->NodeNum > Cand.SU->NodeNum)) {
    TryCand.Reason = NodeOrder;
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// HaydnScheduleDAGMILive
//===----------------------------------------------------------------------===//

void HaydnScheduleDAGMILive::enterRegion(MachineBasicBlock *BB,
                                         MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End,
                                         unsigned RegionInstrs) {
  ScheduleDAGMILive::enterRegion(BB, Begin, End, RegionInstrs);
  getSchedImpl()->enterRegion(BB, Begin, End, RegionInstrs);
}

void HaydnScheduleDAGMILive::exitRegion() {
  getSchedImpl()->leaveRegion(ExitSU);
  ScheduleDAGMILive::exitRegion();
}

ScheduleDAGInstrs *llvm::createHaydnPreRAScheduler(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG = new HaydnScheduleDAGMILive(
      C, std::make_unique<HaydnPreRASchedStrategy>(C));
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  for (auto &M : getHaydnPreRAMutations())
    DAG->addMutation(std::move(M));
  return DAG;
}
