//===-- HaydnSchedMutations.cpp - Pre/Post-RA DAG mutations ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Full AIE dual-sched mutation surface for Haydn.
// Roles mirror AIEBaseSubtarget getPreRAMutationsImpl / getPostRAMutationsImpl.
//
// Pre-RA: PropagateIncomingLatencies, EnforceCopyEdges, FuncArgCopyEdges
// ( CopyConstrain in createHaydnPreRAScheduler)
// Post-RA: MemoryEdges (via HaydnInstrInfo::getMemoryLatency), RegionEndEdges
// MachineSchedWAWEdges, SoftMemoryEdges fallback
//
// Parked (no Haydn peer / AIE-only infrastructure):
// LockDelays (AIE lock/DONE opcodes), BiasDepth (InterBlock PerMIExtraDepth)
// EmitFixedSUnits (fixed region model), WAWStickyRegisters (sticky physregs)
//
//===----------------------------------------------------------------------===//

#include "HaydnSchedMutations.h"
#include "Haydn.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineScheduler.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <map>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "haydn-sched-mutations"

static cl::opt<bool> EnableHaydnEnforceCopyEdges(
    "haydn-prera-enforce-copy-edges", cl::init(true), cl::Hidden,
    cl::desc("Pre-RA: weak COPY cluster edges → strong Artificial"));

static cl::opt<bool> EnableHaydnPropagateIncomingLatencies(
    "haydn-prera-propagate-incoming-latencies", cl::init(true), cl::Hidden,
    cl::desc("Pre-RA: shift COPY/REG_SEQUENCE latency across edges"));

static cl::opt<bool> EnableHaydnFuncArgCopyEdges(
    "haydn-prera-func-arg-copy-edges", cl::init(true), cl::Hidden,
    cl::desc("Pre-RA: func-arg phys COPY before scarce RC defs"));

// Peer gap (seed1 MIR): calls are isSchedBoundary, so post-call regions have
// live-in $r1 with no CALL SU for CopyConstrain. AIE/Haydn EnforceCopyEdges
// only strengthens weak COPY cluster edges. This mutation keeps COPYs of
// live-in physregs early (Artificial preds) so return-value glue cannot float
// behind independent loads/ALU. Evidence:.omc/research/seed1-mir-store-chain.md
static cl::opt<bool> EnableHaydnCallReturnCopyEdges(
    "haydn-prera-call-return-copy-edges", cl::init(true), cl::Hidden,
    cl::desc("Pre-RA: pin live-in physreg COPYs (post-call return glue)"));

// Post-RA MemoryEdges ON: uses only TII->getMemoryLatency; no invented
// store→load floor. Product getMemoryLatency is architectural (table
// First/LastMemoryCycle for Slot0_LS/Slot1_LD/Slot01_LD/Slot2_LS →
// Latency=2). A published memory itinerary with no table row fatals (W21
// ExactLatencies; never a silent latency-1 on a no-interlock machine). Soft
// class-agnostic latency-1 is -haydn-accurate-memory-latency=false soak-off
// only. Densify invents remain FATED. RegionEnd and WAWEdges stay default
// off. Dead writes of simplifiable reserved status regs (SFR/CBR) drop
// intra-region Output edges only when -haydn-postra-waw-edges is on (AIE
// AIE2PSRegisterInfo.cpp:775-778 isSimplifiableReservedReg overlay).
// Enabling reorders independent ALU/MOVE that FileCheck pins as NodeOrder.
// Product Latency=2 inserts a full-NOP bubble on pure st32→ld32 chains;
// unit pins cover all four memory itineraries.
static cl::opt<bool> EnableHaydnPostRAMemoryEdges(
    "haydn-postra-memory-edges", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: MemoryEdges via getMemoryLatency "
             "(default ON; product architectural latency)"));

// AIE RegionEndEdges rebuilds ExitSU with MaxLatencyFinder. The conservative
// intra-region finder is ported below (itinerary maxLatency, no inter-block
// successor reduction). Enabling the rebuild without that reduction can
// still drop live-out Data edges that InterBlockScheduling would keep
// (AIE AIEMaxLatencyFinder.cpp:101-191). Default off until that
// successor reduction exists. The post-RA multi-stage host being seated
// does not flip these mutations.
static cl::opt<bool> EnableHaydnPostRARegionEndEdges(
    "haydn-postra-region-end-edges", cl::init(false), cl::Hidden,
    cl::desc("Post-RA: recompute ExitSU edges (MaxLatencyFinder ported; "
             "default off until inter-block reduction)"));

// AIE InterBlock first brick (AIEMaxLatencyFinder.cpp:159 IncludeStages =
// !SuccessorsAreScheduled). ScheduledMBBs is already recorded on
// HaydnScheduleDAGMI. Do not invent PerSuccEdges remaining-latency cuts
// without that graph (AIE ReduceLatency would under-cover). Default off.
static cl::opt<bool> EnableHaydnPostRAInterBlock(
    "haydn-postra-interblock", cl::init(false), cl::Hidden,
    cl::desc("Post-RA: drop ExitSU stage latency when successorsAreScheduled "
             "(AIE IncludeStages; default off; no PerSuccEdges invent)"));

// AIE WAWEdges (AIEBaseSubtarget.cpp:876-955) only simplifies reserved
// status/control writes via isSimplifiableReservedReg. Haydn overlay:
// SFR and CBR0/CBR1. R0/SP/LR stay real data and are never simplified.
static cl::opt<bool> EnableHaydnPostRAWAWEdges(
    "haydn-postra-waw-edges", cl::init(false), cl::Hidden,
    cl::desc("Post-RA: simplify dead reserved-status Output (WAW) edges "
             "(SFR/CBR; AIE isSimplifiableReservedReg overlay; default off)"));

//===----------------------------------------------------------------------===//
// Latency helpers
//===----------------------------------------------------------------------===//

static bool isRegDepKind(SDep::Kind K) {
  return K == SDep::Data || K == SDep::Anti || K == SDep::Output;
}

static bool updatePredLatency(SDep &Dep, SUnit &SuccSU, int Latency) {
  if (Latency < 0)
    Latency = 0;
  unsigned Lat = (unsigned)Latency;
  if (Lat == Dep.getLatency())
    return false;
  Dep.setLatency(Lat);
  SUnit *PredSU = Dep.getSUnit();
  for (SDep &S : PredSU->Succs) {
    if (S.getSUnit() != &SuccSU || S.getKind() != Dep.getKind())
      continue;
    if (isRegDepKind(Dep.getKind()) && S.getReg() != Dep.getReg())
      continue;
    S.setLatency(Lat);
    break;
  }
  SuccSU.setDepthDirty();
  PredSU->setHeightDirty();
  return true;
}

static SmallVector<SDep, 4> getWeakPreds(SUnit &SU) {
  SmallVector<SDep, 4> WeakPreds;
  copy_if(SU.Preds, std::back_inserter(WeakPreds),
          [](const SDep &PredEdge) { return PredEdge.isWeak(); });
  return WeakPreds;
}

static SmallVector<SDep, 4> getPredsCopy(SUnit &SU) {
  SmallVector<SDep, 4> Preds;
  copy(SU.Preds, std::back_inserter(Preds));
  return Preds;
}

static MachineBasicBlock *getDAGMBB(ScheduleDAGInstrs *DAG) {
  if (DAG->SUnits.empty())
    return nullptr;
  return DAG->SUnits.front().getInstr()->getParent();
}

//===----------------------------------------------------------------------===//
// Pre-RA mutations
//===----------------------------------------------------------------------===//

namespace {

class EnforceCopyEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    for (MachineInstr &MI : *DAG) {
      SUnit *SU = DAG->getSUnit(&MI);
      if (!SU || !MI.isCopy())
        continue;
      for (SDep &PredEdge : getWeakPreds(*SU)) {
        if (DAG->canAddEdge(SU, PredEdge.getSUnit())) {
          SDep StrongPred(PredEdge.getSUnit(), SDep::Artificial);
          SU->addPred(StrongPred);
        }
      }
    }
  }
};

class PropagateIncomingLatencies : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    auto IsData = [](const SDep &D) { return D.getKind() == SDep::Data; };
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MI = *SU.getInstr();
      if (!MI.isCopy() && MI.getOpcode() != TargetOpcode::REG_SEQUENCE)
        continue;
      if (any_of(MI.defs(), [](const MachineOperand &MO) {
            return MO.isReg() && MO.getReg().isPhysical();
          }))
        continue;

      const MachineBasicBlock &MBB = *MI.getParent();
      const MachineRegisterInfo &MRI = DAG->MRI;
      auto MayProduceHoistableCopy = [&MBB, &MRI](const MachineInstr &I) {
        if (!I.isRegSequence() || !MRI.isSSA())
          return false;
        const auto NumExternal =
            count_if(I.uses(), [&MBB, &MRI](const MachineOperand &MO) {
              return MO.isReg() && MO.getReg().isVirtual() &&
                     MRI.getVRegDef(MO.getReg())->getParent() != &MBB;
            });
        const auto NumInternal = I.getNumOperands() - 1 - (2 * NumExternal);
        return NumExternal == 1 && NumInternal >= 1;
      };

      const bool MoveLatToSuccessors = !MayProduceHoistableCopy(MI);
      const SDep *MinLatencyDep = nullptr;
      ArrayRef<SDep> SuccsOrPreds = MoveLatToSuccessors ? SU.Preds : SU.Succs;
      for (const SDep &Edge : make_filter_range(SuccsOrPreds, IsData)) {
        if (!MinLatencyDep || Edge.getLatency() < MinLatencyDep->getLatency())
          MinLatencyDep = &Edge;
      }
      if (!MinLatencyDep)
        continue;

      int Amount = MoveLatToSuccessors ? int(MinLatencyDep->getLatency())
                                       : -int(MinLatencyDep->getLatency());
      for (SDep &PredEdge : make_filter_range(SU.Preds, IsData))
        updatePredLatency(PredEdge, SU, int(PredEdge.getLatency()) - Amount);
      for (SDep &SuccEdge : make_filter_range(SU.Succs, IsData)) {
        SUnit *SuccSU = SuccEdge.getSUnit();
        for (SDep &P : SuccSU->Preds) {
          if (P.getSUnit() == &SU && P.getKind() == SDep::Data) {
            updatePredLatency(P, *SuccSU, int(P.getLatency()) + Amount);
            break;
          }
        }
      }
    }
  }
};

class FuncArgCopyEdges : public ScheduleDAGMutation {
  static bool conflictsWithPhysReg(const MachineRegisterInfo &MRI,
                                   const TargetRegisterInfo *TRI,
                                   MCPhysReg PhysReg, const SUnit &SU) {
    return any_of(SU.getInstr()->defs(),
                  [&MRI, TRI, PhysReg](const MachineOperand &DefMO) {
                    if (!DefMO.isReg() || DefMO.getReg().isPhysical())
                      return false;
                    const TargetRegisterClass *RC =
                        MRI.getRegClass(DefMO.getReg());
                    if (RC->getNumRegs() > 2)
                      return false;
                    for (MCPhysReg ClassReg : *RC) {
                      if (TRI->regsOverlap(ClassReg, PhysReg))
                        return true;
                    }
                    return false;
                  });
  }

  void apply(ScheduleDAGInstrs *DAG) override {
    const MachineRegisterInfo &MRI = DAG->MRI;
    const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
    MachineBasicBlock *MBB = getDAGMBB(DAG);
    if (!MBB)
      return;

    SmallVector<std::pair<SUnit *, MCPhysReg>, 8> FuncArgCopies;
    SmallPtrSet<SUnit *, 8> FuncArgCopySUs;
    for (SUnit &SU : DAG->SUnits) {
      const MachineInstr &MI = *SU.getInstr();
      if (!MI.isCopy())
        continue;
      const MachineOperand &SrcMO = MI.getOperand(1);
      if (!SrcMO.isReg() || !SrcMO.getReg().isPhysical())
        continue;
      MCPhysReg PhysReg = SrcMO.getReg().asMCReg();
      if (!MBB->isLiveIn(PhysReg))
        continue;
      FuncArgCopies.emplace_back(&SU, PhysReg);
      FuncArgCopySUs.insert(&SU);
    }
    if (FuncArgCopies.empty())
      return;

    for (SUnit &SU : DAG->SUnits) {
      if (FuncArgCopySUs.contains(&SU))
        continue;
      for (auto &[CopySU, PhysReg] : FuncArgCopies) {
        if (!conflictsWithPhysReg(MRI, TRI, PhysReg, SU))
          continue;
        if (!DAG->canAddEdge(&SU, CopySU))
          continue;
        SDep Dep(CopySU, SDep::Artificial);
        Dep.setLatency(0);
        SU.addPred(Dep, /*Required=*/true);
      }
    }
  }
};

// Pin COPYs of live-in physical registers near the top of a PreRA region.
// Peer family: AIE EnforceCopyEdges / CopyConstrain (AIEBaseSubtarget.cpp:687
// AIEBaseTargetMachine.cpp:417). Addresses the call-boundary gap documented in
// seed1-mir-store-chain.md (JAL→COPY $r1 distance 2 → 7..23 after PreRA).
class CallReturnCopyEdges : public ScheduleDAGMutation {
  static bool isStackAdj(const MachineInstr &MI) {
    unsigned Opc = MI.getOpcode();
    return Opc == Haydn::ADJCALLSTACKDOWN || Opc == Haydn::ADJCALLSTACKUP;
  }

  // True if some SU in the region explicitly defs \p PhysReg.
  static bool physRegDefInRegion(const ScheduleDAGInstrs *DAG,
                                 MCRegister PhysReg) {
    for (const SUnit &SU : DAG->SUnits) {
      for (const MachineOperand &MO : SU.getInstr()->operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical() &&
            MO.getReg().asMCReg() == PhysReg)
          return true;
      }
    }
    return false;
  }

  void apply(ScheduleDAGInstrs *DAG) override {
    // Post-call regions: $r1 is live but not MBB live-in (call is outside the
    // region). Detect COPYs of physregs with no def SU in the region.
    SmallVector<SUnit *, 8> LiveInPhysCopies;
    for (SUnit &SU : DAG->SUnits) {
      const MachineInstr &MI = *SU.getInstr();
      if (!MI.isCopy() || MI.getNumOperands() < 2)
        continue;
      const MachineOperand &Src = MI.getOperand(1);
      if (!Src.isReg() || !Src.getReg().isPhysical())
        continue;
      MCRegister PhysReg = Src.getReg().asMCReg();
      // Region live-in physreg: not defined by any SU here (call/boundary).
      if (physRegDefInRegion(DAG, PhysReg))
        continue;
      LiveInPhysCopies.push_back(&SU);
    }
    if (LiveInPhysCopies.empty())
      return;

    SmallPtrSet<SUnit *, 8> CopySet(LiveInPhysCopies.begin(),
                                    LiveInPhysCopies.end());
    for (SUnit *CopySU : LiveInPhysCopies) {
      for (SUnit &Other : DAG->SUnits) {
        if (CopySet.contains(&Other))
          continue;
        if (isStackAdj(*Other.getInstr()))
          continue;
        // Force CopySU before Other (Other depends on CopySU).
        if (!DAG->canAddEdge(&Other, CopySU))
          continue;
        SDep Dep(CopySU, SDep::Artificial);
        Dep.setLatency(0);
        Other.addPred(Dep, /*Required=*/true);
      }
    }
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: MemoryEdges (AIE-shaped)
//===----------------------------------------------------------------------===//

class MemoryEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const auto *HII = static_cast<const HaydnInstrInfo *>(DAG->TII);
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MI = *SU.getInstr();
      if (!MI.mayLoadOrStore())
        continue;
      for (SDep &PredEdge : SU.Preds) {
        MachineInstr &SrcMI = *PredEdge.getSUnit()->getInstr();
        if (!PredEdge.isNormalMemoryOrBarrier() || !SrcMI.mayLoadOrStore())
          continue;
        // Load-load RAR is architecturally dual-LS (LOADSTORE0 + LOAD1).
        // AIE ignores the edge (AIEBaseSubtarget.cpp:825-828) and leaves the
        // LLVM latency as-is; do not invent a zero-latency override. Store→load
        // and store→store keep the published MemoryCycle latency.
        if (!SrcMI.mayStore() && !MI.mayStore())
          continue;

        // Latency only from TII->getMemoryLatency; no store→load floor here.
        // Product path uses table First/Last (Slot0_LS/Slot1_LD/Slot01_LD/
        // Slot2_LS → Last-First+1, floored at 1). A published Slot*_LS /
        // Slot*_LD class whose cycles are missing is a generator hole and
        // must abort, not silently fall back to 1 — Haydn has no interlock,
        // so a one-cycle-short mem→mem edge is a silicon hazard (W21 /
        // scheduling F1; AIE ExactLatencies peer at
        // AIEBaseSubtarget.cpp:830-842). Any other class (e.g. UA_POST
        // logicals on Slot012_ALU) keeps the local default 1. Soft soak-off
        // (-haydn-accurate-memory-latency=false) returns 1 always and never
        // fatals.
        constexpr bool ExactLatencies = true;
        int Latency = 1;
        unsigned SrcClass = SrcMI.getDesc().getSchedClass();
        unsigned DstClass = MI.getDesc().getSchedClass();
        if (auto MemLat = HII->getMemoryLatency(SrcClass, DstClass)) {
          Latency = *MemLat;
        } else if (ExactLatencies &&
                   ((HaydnInstrInfo::isPublishedMemoryItinerary(SrcClass) &&
                     !HII->getLastMemoryCycle(SrcClass)) ||
                    (HaydnInstrInfo::isPublishedMemoryItinerary(DstClass) &&
                     !HII->getFirstMemoryCycle(DstClass)))) {
          // Only a class that OWES a row may trigger this: a published
          // Slot*_LS / Slot*_LD itinerary with no First/Last row is a
          // generator hole. A cross-pair whose other side is a non-published
          // memory class (e.g. AR writeback on Slot012_ALU) keeps the local
          // default 1 — that class is not table-driven by design.
          LLVM_DEBUG(dbgs() << "Error: no memory latency info for dependency\n"
                       << "  from: " << SrcMI << "    to: " << MI);
          report_fatal_error("Missing memory latency info.");
        }
        updatePredLatency(PredEdge, SU, Latency);
      }
    }
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: ZOL setup → ExitSU distance (AIE LoopSetupDistance peer)
//===----------------------------------------------------------------------===//

// AIE (AIEBaseSubtarget PostRA mutator): setup instrs raise ExitSU latency so
// the region end is far enough after writing LS/LE/LC. Haydn freeze:
//   SetupIssueDistance = 3  (= AIE LoopSetupDistance peer for SET→BEGIN)
//   InterveningCycles  = 2  (= Following floor; SetupIssueDistance - 1)
//
// ExitSU forward latency must be SetupIssueDistance, not InterveningCycles:
// SET at TopReadyCycle C and latency D yields ExitSU.TopReadyCycle = C+D
// via the scheduled SU's Succs edge (top-down releaseSuccessors).
// leaveRegion handleRegionConflicts ExitReady arm then pads Top so
// TopCurr+BotCurr >= ExitReady, materializing D-1 = InterveningCycles
// following cycles after a lone SET at C=0. Dual-zone: BotCurr after the
// seam still counts toward the region end; inter-zone scoreboard pads only
// lengthen Top (never shrink Following). Using InterveningCycles as the
// forward edge would under-pad by one.
//
// Bot-up / bidirectional: the reverse ExitSU.Preds edge is MinGap-1 so SET
// can share bot cycle 0 with ExitSU (AIE RegionEndEdges convention) while
// still forcing BotCurr >= SetupIssueDistance when SET is the critical
// reverse path — Following after SET still meets InterveningCycles.
//
// SET is a real region SU (not a scheduling boundary). Match every
// logical/wide/member form via TII::isHardwareLoopSetupInstr. Single-MI
// regions skipped by the list scheduler never see this flush; Fixup still
// residual-pads (exact-commit NOPs) for those and for short useful-window
// fill.
class ZOLSetupExitLatency : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const auto *HII = static_cast<const HaydnInstrInfo *>(DAG->TII);
    SUnit &ExitSU = DAG->ExitSU;
    // AIE LoopSetupDistance peer: Cycle(BEGIN) - Cycle(SET) lower bound.
    const unsigned MinGap = haydn::hwloop::SetupIssueDistance;
    static_assert(haydn::hwloop::SetupIssueDistance ==
                      haydn::hwloop::InterveningCycles + 1,
                  "ExitSU latency must be Following floor + 1");
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr *MI = SU.getInstr();
      if (!MI || !HII->isHardwareLoopSetupInstr(*MI))
        continue;
      // Raise latency on existing Artificial Exit edge, or create one.
      // Forward edge: SU → ExitSU with latency MinGap (SetupIssueDistance).
      // Succs (not Preds) drive ExitSU.TopReadyCycle on top-down release.
      bool Found = false;
      for (SDep &Succ : SU.Succs) {
        if (Succ.getSUnit() != &ExitSU || !Succ.isArtificial())
          continue;
        Succ.setLatency(std::max(Succ.getLatency(), MinGap));
        Found = true;
      }
      if (!Found) {
        SDep ExitDep(&SU, SDep::Artificial);
        ExitDep.setLatency(MinGap);
        ExitSU.addPred(ExitDep, /*Required=*/true);
      }
      // Reverse ExitSU.Preds edge is a distinct SDep (AIE RegionEndEdges note).
      // Lift stale short Preds through MinGap then store MinGap-1 so bot-up
      // region length covers SetupIssueDistance when SET is critical; never
      // shorten a longer reverse edge (RaisedForward - 1).
      for (SDep &PredEdge : ExitSU.Preds) {
        if (PredEdge.getSUnit() != &SU || !PredEdge.isArtificial())
          continue;
        unsigned Lat = PredEdge.getLatency();
        unsigned RaisedForward = std::max(Lat, MinGap);
        PredEdge.setLatency(RaisedForward - 1);
      }
      // Re-stamp Succs to MinGap after reverse edits so top-down ExitReady
      // cannot under-pad if a later pass mirrored Pred→Succ.
      for (SDep &Succ : SU.Succs) {
        if (Succ.getSUnit() != &ExitSU || !Succ.isArtificial())
          continue;
        Succ.setLatency(std::max(Succ.getLatency(), MinGap));
      }
      ExitSU.setDepthDirty();
      SU.setDepthDirty();
    }
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: MaxLatencyFinder (AIE AIEMaxLatencyFinder.cpp overlay)
//===----------------------------------------------------------------------===//

// Conservative intra-region maxLatency (AIE maxLatency at
// AIEMaxLatencyFinder.cpp:32-63). Operand cycles + published memory last
// cycle + optional stage latency. AIE computeEffectiveLatency needs
// PerSuccEdges (AIEMaxLatencyFinder.cpp:101-151); Haydn keeps stage
// latency whenever that graph is absent so ExitSU never under-covers.
class MaxLatencyFinder {
  const HaydnInstrInfo *const TII;
  const InstrItineraryData *const Itineraries;
  const bool IsBottomRegion;
  const bool HasUnknownSuccessors;
  const bool SuccessorsAreScheduled;
  const bool IncludeStages;

  static bool isBottomRegion(ScheduleDAGInstrs *DAG) {
    // AIE MaxLatencyFinder.cpp:67-76. getBB() is protected on this DAG.
    MachineInstr *ExitMI = DAG->ExitSU.getInstr();
    if (!ExitMI)
      return true;
    MachineBasicBlock *BB = getDAGMBB(DAG);
    if (!BB)
      return false;
    MachineBasicBlock::instr_iterator It(ExitMI);
    return std::next(It) == BB->instr_end();
  }

  static unsigned maxOperandCycles(const InstrItineraryData *Itin,
                                   unsigned SchedClass) {
    unsigned Lat = 0;
    if (!Itin || Itin->isEmpty())
      return Lat;
    for (unsigned I = 0;; ++I) {
      std::optional<unsigned> OpLat = Itin->getOperandCycle(SchedClass, I);
      if (!OpLat)
        break;
      Lat = std::max(Lat, *OpLat);
    }
    return Lat;
  }

public:
  explicit MaxLatencyFinder(ScheduleDAGInstrs *DAG)
      : TII(static_cast<const HaydnInstrInfo *>(DAG->TII)),
        Itineraries(DAG->getSchedModel()->getInstrItineraries()),
        IsBottomRegion(isBottomRegion(DAG)),
        HasUnknownSuccessors(getDAGMBB(DAG) && getDAGMBB(DAG)->succ_empty()),
        SuccessorsAreScheduled([&] {
          // Only installed on HaydnScheduleDAGMI (createHaydnPostRAScheduler).
          auto *HDAG = static_cast<HaydnScheduleDAGMI *>(DAG);
          MachineBasicBlock *BB = getDAGMBB(DAG);
          return IsBottomRegion && BB && HDAG->successorsAreScheduled(BB);
        }()),
        // AIE IncludeStages = !SuccessorsAreScheduled (AIEMaxLatencyFinder.cpp:159).
        // InterBlock off keeps stage latency even when successorsAreScheduled
        // so ExitSU never under-covers without PerSuccEdges. InterBlock on
        // drops stages only; it does not invent remaining-latency cuts.
        IncludeStages(!EnableHaydnPostRAInterBlock ||
                      !SuccessorsAreScheduled) {
    LLVM_DEBUG(dbgs() << "MaxLatencyFinder bottom=" << IsBottomRegion
                      << " unknown-succ=" << HasUnknownSuccessors
                      << " succ-sched=" << SuccessorsAreScheduled
                      << " stages=" << IncludeStages
                      << (EnableHaydnPostRAInterBlock ? " (interblock)"
                                                      : " (conservative)")
                      << "\n");
  }

  unsigned operator()(const MachineInstr &MI) const {
    unsigned Latency = 0;
    if (MI.isBundle()) {
      const MachineBasicBlock *MBB = MI.getParent();
      if (MBB) {
        for (MachineBasicBlock::const_instr_iterator I =
                 std::next(MI.getIterator());
             I != MBB->instr_end() && I->isBundledWithPred(); ++I)
          Latency = std::max(Latency, (*this)(*I));
      }
      return Latency;
    }
    const unsigned SrcClass = MI.getDesc().getSchedClass();
    Latency = maxOperandCycles(Itineraries, SrcClass);
    if (auto Last = TII->getLastMemoryCycle(SrcClass))
      Latency = std::max(Latency, static_cast<unsigned>(*Last) + 1u);
    Latency = std::max(Latency, TII->getMaxResultLatency(MI));
    if (IncludeStages && Itineraries && !Itineraries->isEmpty())
      Latency = std::max(Latency,
                         static_cast<unsigned>(
                             TII->getInstrLatency(Itineraries, MI)));
    return std::max(Latency, 1u);
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: RegionEndEdges (MaxLatencyFinder)
//===----------------------------------------------------------------------===//

// Recompute Artificial edges to ExitSU from each SUnit using MaxLatencyFinder
// (AIE RegionEndEdges / AIEBaseSubtarget.cpp:390-418).
class RegionEndEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const auto *HII = static_cast<const HaydnInstrInfo *>(DAG->TII);
    SUnit &ExitSU = DAG->ExitSU;
    MaxLatencyFinder MaxLatency(DAG);

    // Drop existing ExitSU preds and rebuild (AIE pattern).
    while (!ExitSU.Preds.empty())
      ExitSU.removePred(ExitSU.Preds.back());

    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MI = *SU.getInstr();
      unsigned EdgeLatency = MaxLatency(MI);
      unsigned DelaySlots = HII->getNumDelaySlots(MI);
      if (DelaySlots)
        EdgeLatency = std::max(EdgeLatency, DelaySlots + 1);
      // AIE: ZOL setup raises ExitSU latency so region end is after the
      // min setup→BEGIN gap (Haydn: SetupIssueDistance; Following =
      // InterveningCycles = distance-1). All logical/wide/member forms.
      if (HII->isHardwareLoopSetupInstr(MI))
        EdgeLatency =
            std::max(EdgeLatency, haydn::hwloop::SetupIssueDistance);

      SDep ExitDep(&SU, SDep::Artificial);
      ExitDep.setLatency(EdgeLatency);
      ExitSU.addPred(ExitDep, /*Required=*/true);
    }

    // Backward edge latency = max(0, Lat-1) so issue can share cycle 0 bot-up.
    for (SDep &PredEdge : ExitSU.Preds) {
      if (!PredEdge.isArtificial())
        continue;
      unsigned Backward =
          PredEdge.getLatency() ? PredEdge.getLatency() - 1 : 0;
      PredEdge.setLatency(Backward);
    }
    ExitSU.setDepthDirty();
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: WAWEdges (generic physreg Output simplification)
//===----------------------------------------------------------------------===//

// Drop intra-region Output (WAW) edges on simplifiable reserved status
// registers that are not live after the write (AIE WAWEdges /
// AIEBaseSubtarget.cpp:876-955). R0/SP/LR keep their Output edges.
class MachineSchedWAWEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    MachineFunction &MF = DAG->MF;
    MachineRegisterInfo &MRI = MF.getRegInfo();
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

    LivePhysRegs LiveRegs;
    LiveRegs.init(*TRI);
    if (MachineBasicBlock *MBB = getDAGMBB(DAG)) {
      LiveRegs.addLiveOutsNoPristines(*MBB);
    } else {
      for (const MCPhysReg PhysReg : MRI.getReservedRegs().set_bits())
        if (haydnIsSimplifiableReservedReg(PhysReg))
          LiveRegs.addReg(PhysReg);
    }

    std::map<Register, SUnit *> PhysRegWriters;
    for (SUnit &SU : reverse(DAG->SUnits)) {
      MachineInstr &MI = *SU.getInstr();
      for (MIBundleOperands MO(MI); MO.isValid(); ++MO) {
        if (!MO->isReg() || !MO->isDef() || !MO->getReg().isPhysical())
          continue;
        if (!haydnIsSimplifiableReservedReg(MO->getReg()))
          continue;
        Register PhysReg = MO->getReg();
        if (!LiveRegs.contains(PhysReg)) {
          // Dead write: drop Output preds on this reg, retarget later writer.
          for (const SDep &Dep : getPredsCopy(SU)) {
            if (Dep.getKind() != SDep::Output || Dep.getReg() != PhysReg)
              continue;
            auto It = PhysRegWriters.find(PhysReg);
            if (It != PhysRegWriters.end())
              It->second->addPred(Dep);
            SU.removePred(Dep);
          }
        } else {
          PhysRegWriters[PhysReg] = &SU;
        }
      }
      LiveRegs.stepBackward(MI);
    }
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Factories
//===----------------------------------------------------------------------===//

std::vector<std::unique_ptr<ScheduleDAGMutation>> llvm::getHaydnPreRAMutations() {
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;
  if (EnableHaydnPropagateIncomingLatencies)
    Mutations.emplace_back(std::make_unique<PropagateIncomingLatencies>());
  if (EnableHaydnEnforceCopyEdges)
    Mutations.emplace_back(std::make_unique<EnforceCopyEdges>());
  if (EnableHaydnFuncArgCopyEdges)
    Mutations.emplace_back(std::make_unique<FuncArgCopyEdges>());
  if (EnableHaydnCallReturnCopyEdges)
    Mutations.emplace_back(std::make_unique<CallReturnCopyEdges>());
  return Mutations;
}

std::vector<std::unique_ptr<ScheduleDAGMutation>> llvm::getHaydnPostRAMutations() {
  // Order mirrors AIE getPostRAMutationsImpl (minus Lock/Bias/Fixed/Sticky).
  // ZOLSetupExitLatency always on (AIE LoopSetupDistance peer) even when
  // full RegionEndEdges rebuild is OFF.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;
  Mutations.emplace_back(std::make_unique<ZOLSetupExitLatency>());
  if (EnableHaydnPostRARegionEndEdges || EnableHaydnPostRAInterBlock)
    Mutations.emplace_back(std::make_unique<RegionEndEdges>());
  if (EnableHaydnPostRAMemoryEdges)
    Mutations.emplace_back(std::make_unique<MemoryEdges>());
  if (EnableHaydnPostRAWAWEdges)
    Mutations.emplace_back(std::make_unique<MachineSchedWAWEdges>());
  return Mutations;
}
