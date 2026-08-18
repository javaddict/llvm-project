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
// productFeasibleFormatMask is the Pre-RA FormatID frontier (size-1 Full).
// No setDesc / no FormatID freeze before RA (plan §7.1). SMS shares the same
// Bundle/ResourceCycle getFeasibleFormatMask adapters. MOVE32 port demand
// is per-field 2R1W (same as the descriptor path). tryCandidate also
// consumes the live HR MatchingFrontierScore (nondominated cardinality /
// free-slot scarcity) after pressure/critical and before NodeOrder.
//
// SMS-RESMII pre-RA slice: productExhaustiveResMII /
// productResMIIOverestimate / productResMIIFailsQualification thin-wrap pure
// BundleFormatSolver oracles; portLowerBoundResMII thin-wraps PortModel.
// Soft-exit QoR: productSoftExitIIFloor = max(format ResMII, port ResMII).
// Pre-RA packability metrics: productFormsOneExactCycle /
// productQualKernelCoissuePackable / productQualKernelExactlyPackable are
// pure probes — never freeze FormatID or invent BUNDLE. No MIR mutation /
// setDesc. Sibling SMS owns analyzeLoop / ResourceCycle packability metrics.
//
// Generic-pass dual-run baseline: product defaults (matching-frontier ON,
// finer-RP ON, isavail-delay OFF) vs residual arms
// (-matching-frontier=false; optional -finer-rp-tracking=false). KPI freeze
// lives in prera-format-generic-baseline.ll — spill/reload parity, post-RA
// multi-MI exact finalize, silent split/hard-root, logical-only through
// greedy/pre-postmisched. CreateTargetMIHazardRecognizer always installs the
// target HR; flags never drop to a null factory.
//
// ILP / critical ranking residual attribution: product tryCandidate fires
// ResourceDemand (matching-frontier) only after pressure/critical. Residual
// arm keeps pressure primary (RegMax on critical kernels) and drops
// ResourceDemand. Dual-run -stats live in scheduler-ilp.ll /
// scheduler-critical-path.ll; pure pins on productIlpCriticalDualRunResidualPins.
//
// SMS-HOOK II-wrap false-accept pre-RA slice: pure helpers on
// HaydnPreRASchedStrategy re-export catalog polarity
// (ProductCrossCycleCapacityEnabled=false; productMaxInstrStageCycles=1;
// smsHookRejectsIIWrapFalseAccept). Linear stage-relative HR booking is not
// modulo-II ResourceCycle; sibling owns the issue-time-only differential and
// analyzeLoop force-iiwrap-reject. No setDesc / no multi-cycle product enable.
//
// SMS/post-RA format-acceptance differential pre-RA slice (plan §8.4 #7):
// productExactCanPackSequence / productFormatAcceptanceDifferentialPins pin
// pure exactTryAddProduct polarity shared with ResourceCycle and post-RA HR.
// CreateTargetMIHazardRecognizer IsPreRA expands the same candidate set;
// scoreMatchingFrontier is the list-sched probe. Format is opcode-keyed
// (MI ≡ desc). MOVE32 port demand is 2R1W on both the MI and descriptor
// paths. Sibling SMS owns live ResourceCycle packing. No setDesc.
//
//===----------------------------------------------------------------------===//

#include "HaydnPreRASchedStrategy.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnSchedMutations.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <functional>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "haydn-prera-sched"

// Release-visible phase-firewall counter: every pre-RA region leave is audited
// for new BUNDLE invent. Product expects invent count 0; invent is fatal.
STATISTIC(NumPreRAPhaseFirewallChecked,
          "Number of pre-RA regions phase-firewall-checked for BUNDLE invent");
STATISTIC(NumPreRABUNDLEInvent,
          "Number of pre-RA regions that invented new BUNDLE roots (must be 0)");
STATISTIC(NumPreRAPrivateMemberInvent,
          "Number of pre-RA regions that invented bundled private members "
          "(must be 0)");
STATISTIC(NumPreRAPlacementOpcodeInvent,
          "Number of pre-RA regions that invented private placement opcodes "
          "(must be 0)");

// Product defaults pinned by HaydnPreRASchedStrategy::product*Default and
// dual-run lits prera-format-generic-baseline.ll / scheduler-ilp.ll /
// scheduler-critical-path.ll. Keep cl::init in lockstep with those constexprs.
static cl::opt<bool> EnableHaydnFinerRPTracking(
    "haydn-premisched-finer-rp-tracking",
    cl::init(HaydnPreRASchedStrategy::productFinerRPTrackingDefault), cl::Hidden,
    cl::desc("Pre-RA: AIE-style pressure-first tryCandidate "
             "(aie-premisched-finer-rp-tracking peer)"));

// AIE enables pressure delay in isAvailableNode by default. On Haydn it
// currently regresses yarpgen seed1 to HOSTCALL_ERROR (guest abort) vs
// sticky-oracle when off — keep the AIE hook wired but default-off until
// MIR-validated. Force on: -haydn-premisched-isavail-delay.
// Product default = productIsAvailPressureDelayDefault (false).
static cl::opt<bool> EnableHaydnIsAvailPressureDelay(
    "haydn-premisched-isavail-delay",
    cl::init(HaydnPreRASchedStrategy::productIsAvailPressureDelayDefault),
    cl::Hidden,
    cl::desc("Pre-RA: AIE isAvailableNode pressure delayer (default off; "
             "seed1 HOSTCALL residual under investigation)"));

static cl::opt<unsigned> HaydnNumCriticalFreeRegs(
    "haydn-premisched-critical-free-regs", cl::init(2), cl::Hidden,
    cl::desc("AIE NumCriticalFreeRegs peer"));

static cl::opt<bool> EnableHaydnPreRAForceBottomUp(
    "haydn-premisched-force-bottom-up", cl::init(true), cl::Hidden,
    cl::desc("Force OnlyBottomUp (AIE PreRA leaveRegion contract)"));

// Matching-frontier ranking (plan §5.1): after pressure/critical, prefer the
// ready SU that keeps more nondominated matchings / free slots. Full-only
// product: compact-byte tie is a no-op (size-1). Pure HR probe — no setDesc.
// Dual-run residual: -haydn-premisched-matching-frontier=false (generic
// NodeOrder after pressure; target HR still installed).
static cl::opt<bool> EnableHaydnPreRAMatchingFrontier(
    "haydn-premisched-matching-frontier",
    cl::init(HaydnPreRASchedStrategy::productMatchingFrontierDefault),
    cl::Hidden,
    cl::desc("Pre-RA: rank tryCandidate by matching-frontier cardinality and "
             "free-slot scarcity before NodeOrder"));

// Matching-frontier is two exactTryAdd probes per compare. Large ILP
// ready sets (IIR / CoreMark / Dhrystone) make that dominate compile time
// the same way the post-RA subset auction did. Skip the probe and fall
// through to NodeOrder above this Available count.
static cl::opt<unsigned> HaydnPreRAFrontierSkipReady(
    "haydn-prera-frontier-skip-ready", cl::init(12), cl::Hidden,
    cl::desc("Skip pre-RA matching-frontier ranking when Available exceeds "
             "this count"));

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

namespace {

bool looksLikePrivatePlacementOpcodeName(StringRef Name) {
  if (Name.empty())
    return false;
  if (Name.contains("_E2_") || Name.contains("_E3_"))
    return true;
  return Name.ends_with("_S0") || Name.ends_with("_S1") || Name.ends_with("_S2");
}

void snapshotPreRAPhaseFirewall(const MachineBasicBlock &MBB,
                                const TargetInstrInfo *TII,
                                unsigned &BundleRoots,
                                unsigned &BundledMembers,
                                unsigned &PrivatePlacementOps) {
  BundleRoots = 0;
  BundledMembers = 0;
  PrivatePlacementOps = 0;
  for (const MachineInstr &MI : MBB.instrs()) {
    if (MI.isBundle() && !MI.isBundledWithPred())
      ++BundleRoots;
    if (MI.isBundledWithPred())
      ++BundledMembers;
    if (!TII)
      continue;
    if (looksLikePrivatePlacementOpcodeName(TII->getName(MI.getOpcode())))
      ++PrivatePlacementOps;
  }
}

} // end anonymous namespace

void HaydnPreRASchedStrategy::enterRegion(MachineBasicBlock *BB,
                                          MachineBasicBlock::iterator /*Begin*/,
                                          MachineBasicBlock::iterator /*End*/,
                                          unsigned NumRegionInstrs) {
  CurMBB = BB;
  PreRAEnterBundleRoots = 0;
  PreRAEnterBundledMembers = 0;
  PreRAEnterPrivatePlacementOps = 0;
  if (CurMBB) {
    const TargetInstrInfo *TII =
        CurMBB->getParent()->getSubtarget().getInstrInfo();
    snapshotPreRAPhaseFirewall(*CurMBB, TII, PreRAEnterBundleRoots,
                               PreRAEnterBundledMembers,
                               PreRAEnterPrivatePlacementOps);
  }
  SUDelayerMap.assign(std::max(NumRegionInstrs, 1u), UnknownSUNum);
}

void HaydnPreRASchedStrategy::leaveRegion(const SUnit & /*ExitSU*/) {
  // Phase firewall: no BUNDLE / private-member / placement invent pre-RA.
  if (CurMBB) {
    ++NumPreRAPhaseFirewallChecked;
    static bool ResourcePinsChecked = false;
    if (!ResourcePinsChecked) {
      ResourcePinsChecked = true;
      if (!productPeriodicCertificatePins() ||
          !haydnAvailabilityAwareConsumePinsHold())
        report_fatal_error(
            "Haydn pre-RA product resource admission / certificate pins failed",
            /*GenCrashDiag=*/false);
      // Product StageCount1 containment pin (soft StageCount == 1 only).
      if (productSMSContainmentMaxStageCount != 1u ||
          !smsProductStageCountExceedsContainment(/*StageCount=*/2) ||
          smsProductStageCountExceedsContainment(/*StageCount=*/1) ||
          !smsProductShouldUseScheduleFailsClosed(
              /*IsZOL=*/false, /*PrologueCount=*/1, /*MinTripCount=*/0,
              /*PressureExcess=*/false) ||
          !smsProductShouldUseScheduleAccepts(
              /*IsZOL=*/false, /*PrologueCount=*/0, /*MinTripCount=*/0,
              /*PressureExcess=*/false))
        report_fatal_error(
            "Haydn pre-RA product StageCount1 SMS containment pins failed",
            /*GenCrashDiag=*/false);
      LLVM_DEBUG(dbgs() << "HaydnPreRASched: product StageCount1 containment "
                           "max_stages=1 resource_admission_closed=1\n");
    }
    const TargetInstrInfo *TII =
        CurMBB->getParent()->getSubtarget().getInstrInfo();
    unsigned BundleRoots = 0;
    unsigned BundledMembers = 0;
    unsigned PrivatePlacementOps = 0;
    snapshotPreRAPhaseFirewall(*CurMBB, TII, BundleRoots, BundledMembers,
                               PrivatePlacementOps);
    if (BundleRoots > PreRAEnterBundleRoots) {
      ++NumPreRABUNDLEInvent;
      report_fatal_error(
          "pre-RA Haydn schedule must not invent BUNDLE identity",
          /*GenCrashDiag=*/false);
    }
    if (BundledMembers > PreRAEnterBundledMembers) {
      ++NumPreRAPrivateMemberInvent;
      report_fatal_error(
          "pre-RA Haydn schedule must not invent bundled private members",
          /*GenCrashDiag=*/false);
    }
    if (PrivatePlacementOps > PreRAEnterPrivatePlacementOps) {
      ++NumPreRAPlacementOpcodeInvent;
      report_fatal_error(
          "pre-RA Haydn schedule must not invent private placement opcodes",
          /*GenCrashDiag=*/false);
    }
  }
  CurMBB = nullptr;
  SUDelayerMap.clear();
  PreRAEnterBundleRoots = 0;
  PreRAEnterBundledMembers = 0;
  PreRAEnterPrivatePlacementOps = 0;
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

  // Matching frontier (plan §5.1): after pressure / critical path, prefer the
  // candidate that keeps more nondominated matchings or free scarce slots.
  // Uses a pure copy of the zone HR's live CycleCandidateSet — no setDesc,
  // no AltDesc, no FormatID freeze. ResourceDemand reuses the generic reason
  // slot for "better packing demand / retained options."
  if (EnableHaydnPreRAMatchingFrontier && Zone && Zone->HazardRec &&
      Zone->HazardRec->isEnabled() &&
      Zone->Available.size() <= HaydnPreRAFrontierSkipReady) {
    // CreateTargetMIHazardRecognizer always installs HaydnHazardRecognizer for
    // Haydn (pre-RA and post-RA). Safe static cast — no RTTI on HR base.
    auto *HR = static_cast<HaydnHazardRecognizer *>(Zone->HazardRec);
    const unsigned TryOpc = TryCand.SU->getInstr()->getOpcode();
    const unsigned CandOpc = Cand.SU->getInstr()->getOpcode();
    const auto TryScore = HR->probeMatchingFrontier(TryOpc);
    const auto CandScore = HR->probeMatchingFrontier(CandOpc);

    if (tryGreater(TryScore.Feasible, CandScore.Feasible, TryCand, Cand,
                   ResourceDemand))
      return TryCand.Reason != NoCand;
    if (tryGreater(TryScore.SuccessorMatchings, CandScore.SuccessorMatchings,
                   TryCand, Cand, ResourceDemand))
      return TryCand.Reason != NoCand;
    if (tryGreater(TryScore.FreeSlotsPreferred, CandScore.FreeSlotsPreferred,
                   TryCand, Cand, ResourceDemand))
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
