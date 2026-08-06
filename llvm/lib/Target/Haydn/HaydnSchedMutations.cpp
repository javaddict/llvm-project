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
// (+ CopyConstrain in createHaydnPreRAScheduler)
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
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include <map>

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

// Default OFF until Haydn has AIE-peer getFirst/LastMemoryCycle tables.
// AIE MemoryEdges (AIEBaseSubtarget.cpp) uses only TII->getMemoryLatency from
// those cycles; with AccurateMemEdges=false it returns latency 1 with no
// store→load floor. Haydn's prior default-ON class-agnostic path plus an
// invented store→load=2 floor forced empty issue cycles (NatureDSP density
// A/B : mdct 345→306, bkfir 254→235 when this mutation is off;
// restores ~07-15 pre-dual-sched packing for same op counts).
// AIE with AccurateMemEdges=false still runs MemoryEdges with latency 1
// (AIEBaseInstrInfo). Haydn getMemoryLatency returns 1 class-agnostic — peer
// safe to default ON. RegionEnd/WAW stay OFF (incomplete MaxLatencyFinder
// sticky model).
static cl::opt<bool> EnableHaydnPostRAMemoryEdges(
    "haydn-postra-memory-edges", cl::init(true), cl::Hidden,
    cl::desc("Post-RA: MemoryEdges via getMemoryLatency "
             "(default ON; latency-1 until First/LastMemoryCycle tables)"));

// AIE RegionEndEdges rebuilds ExitSU with MaxLatencyFinder. Without that
// stripping ExitSU preds and replacing with getMaxResultLatency-only edges is
// incomplete and can drop live-out Data edges (seed1 HOSTCALL residual).
// Default off until MaxLatencyFinder port exists (AIE peer).
static cl::opt<bool> EnableHaydnPostRARegionEndEdges(
    "haydn-postra-region-end-edges", cl::init(false), cl::Hidden,
    cl::desc("Post-RA: recompute ExitSU edges (needs MaxLatencyFinder; "
             "default off)"));

// Generic WAW without AIE isSimplifiableReservedReg is incomplete; default off.
static cl::opt<bool> EnableHaydnPostRAWAWEdges(
    "haydn-postra-waw-edges", cl::init(false), cl::Hidden,
    cl::desc("Post-RA: simplify dead physreg Output (WAW) edges "
             "(default off; needs reserved-reg model)"));

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
        // Ignore pure load-load RAR.
        if (!SrcMI.mayStore() && !MI.mayStore())
          continue;

        // Peer: AIEBaseSubtarget.cpp MemoryEdges — latency only from
        // TII->getMemoryLatency; no store→load floor here. When Haydn gains
        // First/LastMemoryCycle, getMemoryLatency becomes the accurate peer.
        int Latency = 1;
        if (auto MemLat = HII->getMemoryLatency(SrcMI.getDesc().getSchedClass(),
                                                MI.getDesc().getSchedClass())) {
          Latency = *MemLat;
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
// the region end is far enough after writing LS/LE/LC. Haydn: SET_HWLOOP must
// be at least MinSetupBundles cycles before the preheader exits into BEGIN.
// FixupHwLoops still does residual deficit NOPs if layout is still short.
class ZOLSetupExitLatency : public ScheduleDAGMutation {
  static bool isHWLoopSetup(const MachineInstr &MI) {
    unsigned Opc = MI.getOpcode();
    return Opc == Haydn::SET_HWLOOP_PSEUDO || Opc == Haydn::SET_HWLOOP_F2_PSEUDO ||
           Opc == Haydn::LoopStart;
  }

  void apply(ScheduleDAGInstrs *DAG) override {
    SUnit &ExitSU = DAG->ExitSU;
    const unsigned MinGap = haydn::hwloop::MinSetupBundles;
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr *MI = SU.getInstr();
      if (!MI || !isHWLoopSetup(*MI))
        continue;
      // Raise latency on existing Artificial Exit edge, or create one.
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
      // Keep reverse edge consistent with RegionEndEdges / AIE bot-up.
      for (SDep &PredEdge : ExitSU.Preds) {
        if (PredEdge.getSUnit() != &SU || !PredEdge.isArtificial())
          continue;
        unsigned Lat = PredEdge.getLatency();
        PredEdge.setLatency(Lat ? Lat - 1 : 0);
      }
      ExitSU.setDepthDirty();
      SU.setDepthDirty();
    }
  }
};

//===----------------------------------------------------------------------===//
// Post-RA: RegionEndEdges (simplified MaxLatencyFinder)
//===----------------------------------------------------------------------===//

// Recompute Artificial edges to ExitSU from each SUnit using itinerary max
// result latency (AIE RegionEndEdges without inter-block MaxLatencyFinder).
class RegionEndEdges : public ScheduleDAGMutation {
  void apply(ScheduleDAGInstrs *DAG) override {
    const auto *HII = static_cast<const HaydnInstrInfo *>(DAG->TII);
    SUnit &ExitSU = DAG->ExitSU;

    // Drop existing ExitSU preds and rebuild (AIE pattern).
    while (!ExitSU.Preds.empty())
      ExitSU.removePred(ExitSU.Preds.back());

    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MI = *SU.getInstr();
      unsigned EdgeLatency = HII->getMaxResultLatency(MI);
      unsigned DelaySlots = HII->getNumDelaySlots(MI);
      if (DelaySlots)
        EdgeLatency = std::max(EdgeLatency, DelaySlots + 1);
      // AIE: ZOL setup raises ExitSU latency so region end is after the
      // min setup→loop distance (Haydn: MinSetupBundles to BEGIN).
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::SET_HWLOOP_PSEUDO || Opc == Haydn::SET_HWLOOP_F2_PSEUDO ||
          Opc == Haydn::LoopStart)
        EdgeLatency =
            std::max(EdgeLatency, haydn::hwloop::MinSetupBundles);

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

// Drop intra-region Output (WAW) edges on physical registers that are not
// live after the write (AIE WAWEdges without reserved-sticky special cases).
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
        LiveRegs.addReg(PhysReg);
    }

    std::map<Register, SUnit *> PhysRegWriters;
    for (SUnit &SU : reverse(DAG->SUnits)) {
      MachineInstr &MI = *SU.getInstr();
      for (MIBundleOperands MO(MI); MO.isValid(); ++MO) {
        if (!MO->isReg() || !MO->isDef() || !MO->getReg().isPhysical())
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
  if (EnableHaydnPostRARegionEndEdges)
    Mutations.emplace_back(std::make_unique<RegionEndEdges>());
  if (EnableHaydnPostRAMemoryEdges)
    Mutations.emplace_back(std::make_unique<MemoryEdges>());
  if (EnableHaydnPostRAWAWEdges)
    Mutations.emplace_back(std::make_unique<MachineSchedWAWEdges>());
  return Mutations;
}
