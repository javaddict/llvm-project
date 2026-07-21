//===-- HaydnPostPipeliner.cpp - Post-RA software pipeliner (Stage-0) -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stage-0 HaydnPostPipeliner — minimal real post-RA SWP for single-BB ZOL.
//
// Algorithm (slim AIE PostPipeliner):
// 1. isPostPipelineCandidate — single-BB ZOL in either form:
// A) LoopStart + PseudoLoopEnd (IR HardwareLoops / lit fixtures)
// B) SET_HWLOOP{,_REG} in fallthrough preheader targeting this MBB
// (post-RA HaydnHardwareLoops convert — real NatureDSP path)
// + unique exit + body size.
// 2. ASAP earliest cycles from DAG SDeps (1-copy; LCD not fully modeled).
// 3. Try II = ResMII.. LinearLength-1: place each body SU at max(Earliest
// prior) in [0,∞) such that footprint fits modulo scoreboard at C%II.
// 4. Accept only NStages >= 2 with II < LinearLength (real overlap win).
// 5. materialize — prologue clones → preheader, kernel reorder by
// (ModuloCycle, Stage desc) + same-cycle BUNDLEs, epilogue clones →
// exit, tripcount adj (LoopStart $adj or ADDI before SET_HWLOOP_REG).
//
// Deferred: 2-copy DAG, multi-heuristic strategies, peelSideEffectFree, Z3.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostPipeliner.h"
#include "Haydn.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "haydn-post-pipeliner"

STATISTIC(NumPostPipelineSuccess,
          "Number of loops successfully post-RA software-pipelined");
STATISTIC(NumPostPipelineFail,
          "Number of post-RA pipeliner attempts that failed closed");

// Defined in namespace llvm to match the extern in HaydnPostPipeliner.h.
namespace llvm {
cl::opt<bool> EnableHaydnPostPipeliner(
    "haydn-enable-post-pipeliner", cl::Hidden, cl::init(false),
    cl::desc(
        "Enable Stage-0 post-RA software pipeliner (HaydnPostPipeliner). "
        "Default OFF until MIR matrix."
        "Incomplete; fails closed to list schedule."));
} // namespace llvm

// Maximum body instructions Stage-0 will attempt (keeps search cheap).
// bkfir32x32 L1 MAC is ~26 real ops; leave headroom for similar FIR kernels.
static constexpr unsigned MaxBodyInstrs = 40;
// Cap II search distance.
static constexpr int MaxIISearch = 24;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static bool isZOLTerminator(const MachineInstr &MI) {
  return MI.getOpcode() == Haydn::PseudoLoopEnd;
}

static bool isSkippableBodyMI(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.isPosition() || MI.isKill() ||
         MI.isImplicitDef() || MI.isCFIInstruction();
}

// LoopStart (IR ZOL) or SET_HWLOOP{,_REG} (post-RA convert / NatureDSP).
static bool isHwLoopSetup(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return Opc == Haydn::LoopStart || Opc == Haydn::SET_HWLOOP ||
         Opc == Haydn::SET_HWLOOP_REG;
}

// True when a SET_HWLOOP{,_REG} encodes \p Loop as its start MBB.
static bool setupTargetsLoop(const MachineInstr &MI,
                             const MachineBasicBlock &Loop) {
  unsigned Opc = MI.getOpcode();
  if (Opc != Haydn::SET_HWLOOP && Opc != Haydn::SET_HWLOOP_REG)
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

//===----------------------------------------------------------------------===//
// Candidate / ResMII
//===----------------------------------------------------------------------===//

bool HaydnPostPipeliner::isPostPipelineCandidate(MachineBasicBlock &LoopBlock) {
  // 1. Dedicated fallthrough preheader (needed to locate tripcount setup).
  Preheader = findFallThroughPreheader(LoopBlock);
  if (!Preheader) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: no fallthrough preheader\n");
    return false;
  }

  // 2. Tripcount setup: LoopStart (IR ZOL) or SET_HWLOOP{,_REG} (convert).
  TripCountDef = findHwLoopSetup(*Preheader, LoopBlock);
  if (!TripCountDef) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: no hwloop setup in preheader\n");
    return false;
  }

  // 3. ZOL shape.
  // Form A — LoopStart + PseudoLoopEnd terminator (self-loop CFG).
  // Form B — SET_HWLOOP{,_REG} targeting this MBB; body is fallthrough
  // (hardware back-edge; CFG often has no self-loop / no PLE).
  // Multi-BB SET (start MBB != end MBB) is not a Stage-0 PostPipeliner
  // candidate — adjusting trip (NStages-1) on nested multi-BB outer ZOL
  // rewrote CoreMark bitextract trip N→N-3 into a clobberable GPR.
  const unsigned SetupOpc = TripCountDef->getOpcode();
  if (SetupOpc == Haydn::SET_HWLOOP || SetupOpc == Haydn::SET_HWLOOP_REG) {
    if (TripCountDef->getNumOperands() >= 3 &&
        TripCountDef->getOperand(1).isMBB() &&
        TripCountDef->getOperand(2).isMBB() &&
        TripCountDef->getOperand(1).getMBB() !=
            TripCountDef->getOperand(2).getMBB()) {
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: multi-BB SET — skip\n");
      return false;
    }
  }
  auto Term = LoopBlock.getFirstInstrTerminator();
  const bool HasPLE =
      Term != LoopBlock.end() && isZOLTerminator(*Term);
  const bool IsSetForm =
      SetupOpc == Haydn::SET_HWLOOP || SetupOpc == Haydn::SET_HWLOOP_REG;
  if (SetupOpc == Haydn::LoopStart && !HasPLE) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: LoopStart without PseudoLoopEnd\n");
    return false;
  }
  if (!HasPLE && !IsSetForm) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: no ZOL shape\n");
    return false;
  }

  // 4. Unique exit for epilogue clones.
  ExitBB = findUniqueExit(LoopBlock);
  if (!ExitBB) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: no unique exit\n");
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
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: body size " << NBody
                      << " out of range\n");
    return false;
  }

  LoopBB = &LoopBlock;
  LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: candidate " << LoopBlock.getName()
                    << " body=" << NBody
                    << (IsSetForm ? " form=SET_HWLOOP" : " form=LoopStart")
                    << (HasPLE ? "+PLE" : "") << "\n");
  return true;
}

int HaydnPostPipeliner::getResMII(MachineBasicBlock &LoopBlock) const {
  // Issue-width ResMII: ceil(NBody / 3). Slot exclusivity may raise this;
  // Stage-0 accepts a lower bound and lets tryII fail closed on conflicts.
  unsigned NBody = 0;
  for (const MachineInstr &MI : LoopBlock) {
    if (isZOLTerminator(MI) || isSkippableBodyMI(MI))
      continue;
    if (MI.isPseudo() && !MI.isCopy())
      continue;
    ++NBody;
  }
  int MII = std::max(1, static_cast<int>((NBody + 2) / 3));
  LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: ResMII=" << MII << " (NBody="
                    << NBody << ")\n");
  return MII;
}

//===----------------------------------------------------------------------===//
// ASAP earliest + tryII
//===----------------------------------------------------------------------===//

bool HaydnPostPipeliner::computeASAPEarliest() {
  // Topological ASAP: Earliest[SU] = max over preds (Earliest[P] + Latency).
  // Multiple passes handle any residual order (DAG is acyclic within region).
  for (HaydnPPNodeInfo &N : Info) {
    N.Earliest = 0;
    N.Scheduled = false;
    N.Cycle = 0;
  }

  const int N = static_cast<int>(Body.size());
  bool Changed = true;
  int Guard = N * N + 4;
  while (Changed && Guard--) {
    Changed = false;
    for (int I = 0; I < N; ++I) {
      SUnit *SU = Body[I];
      int E = 0;
      for (const SDep &Pred : SU->Preds) {
        if (Pred.getSUnit()->isBoundaryNode())
          continue;
        // Map predecessor to body index.
        int PredIdx = -1;
        for (int J = 0; J < N; ++J) {
          if (Body[J] == Pred.getSUnit()) {
            PredIdx = J;
            break;
          }
        }
        if (PredIdx < 0)
          continue;
        int Lat = Pred.getLatency();
        // Zero-latency anti/output still force program order (same cycle OK
        // only when WAR-safe; ASAP uses Lat so zero → same-cycle capable).
        E = std::max(E, Info[PredIdx].Earliest + Lat);
      }
      if (E > Info[I].Earliest) {
        Info[I].Earliest = E;
        Changed = true;
      }
    }
  }
  if (Guard < 0) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: ASAP did not converge\n");
    return false;
  }

  LinearLength = 0;
  for (int I = 0; I < N; ++I)
    LinearLength = std::max(LinearLength, Info[I].Earliest + 1);

  LLVM_DEBUG({
    dbgs() << "HaydnPostPipeliner: ASAP LinearLength=" << LinearLength << "\n";
    for (int I = 0; I < N; ++I)
      dbgs() << "  SU" << Body[I]->NodeNum << " Earliest=" << Info[I].Earliest
             << " " << *Body[I]->getInstr();
  });
  // LinearLength == 1 is still worth attempting: resource pressure alone may
  // force multi-cycle / multi-stage placement (NatureDSP SET_HWLOOP bodies).
  return LinearLength >= 1;
}

// Core placement for one II. \p StageBiasFloor: once Place >= Floor, raise
// each SU's search start to at least TryII so the second wave lands in stage
// ≥1 (Stage-0 multi-stage without a 2-copy LCD DAG). Floor == N disables the
// bias (pure ASAP).
static bool placeForII(int TryII, const HaydnHazardRecognizer &HR,
                       ArrayRef<SUnit *> Body, MutableArrayRef<HaydnPPNodeInfo> Info,
                       int StageBiasFloor, int MaxSearch) {
  const int N = static_cast<int>(Body.size());
  ResourceScoreboard<HaydnFuncUnitWrapper> Scoreboard;
  Scoreboard.config(0, TryII - 1);

  for (HaydnPPNodeInfo &Node : Info) {
    Node.Scheduled = false;
    Node.Cycle = 0;
  }

  SmallVector<int, 16> DynEarliest(N);
  for (int I = 0; I < N; ++I)
    DynEarliest[I] = Info[I].Earliest;

  for (int Place = 0; Place < N; ++Place) {
    int Best = -1;
    for (int I = 0; I < N; ++I) {
      if (Info[I].Scheduled)
        continue;
      bool Ready = true;
      for (const SDep &Pred : Body[I]->Preds) {
        if (Pred.getSUnit()->isBoundaryNode())
          continue;
        for (int J = 0; J < N; ++J) {
          if (Body[J] == Pred.getSUnit() && !Info[J].Scheduled) {
            Ready = false;
            break;
          }
        }
        if (!Ready)
          break;
      }
      if (!Ready)
        continue;
      if (Best < 0 || DynEarliest[I] < DynEarliest[Best] ||
          (DynEarliest[I] == DynEarliest[Best] && I < Best))
        Best = I;
    }
    if (Best < 0)
      return false;

    int Earliest = DynEarliest[Best];
    // Second-wave bias: force stage ≥1 for the later half of the place order
    // so resource-bound short ASAP chains still form multi-stage schedules
    // (loads / setup in stage 0, dependent MAC/use in stage 1).
    if (Place >= StageBiasFloor)
      Earliest = std::max(Earliest, TryII);
    const int SearchLimit = Earliest + TryII * 4 + MaxSearch;
    std::optional<int> Chosen;
    MachineInstr &MI = *Body[Best]->getInstr();
    for (int C = Earliest; C <= SearchLimit; ++C) {
      const int Mod = C % TryII;
      if (HR.checkConflict(Scoreboard, MI, Mod))
        continue;
      Chosen = C;
      break;
    }
    if (!Chosen)
      return false;

    const int Actual = *Chosen;
    Info[Best].Cycle = Actual;
    Info[Best].Scheduled = true;
    const int Mod = Actual % TryII;
    HR.emitInScoreboard(Scoreboard, MI, Mod);

    for (const SDep &Succ : Body[Best]->Succs) {
      if (Succ.getSUnit()->isBoundaryNode())
        continue;
      for (int J = 0; J < N; ++J) {
        if (Body[J] != Succ.getSUnit())
          continue;
        int NewE = Actual + static_cast<int>(Succ.getLatency());
        DynEarliest[J] = std::max(DynEarliest[J], NewE);
      }
    }

    LLVM_DEBUG(dbgs() << "  place SU" << Body[Best]->NodeNum << " @ " << Actual
                      << " mod " << Mod
                      << (Place >= StageBiasFloor ? " (wave1)" : "") << "\n");
  }
  return true;
}

bool HaydnPostPipeliner::tryII(int TryII, const HaydnHazardRecognizer &HR) {
  assert(TryII >= 1);
  const int N = static_cast<int>(Body.size());

  auto finalizePlacement = [&]() -> bool {
    int MaxStage = 0;
    int MaxCycle = 0;
    for (int I = 0; I < N; ++I) {
      Info[I].update(TryII);
      MaxStage = std::max(MaxStage, Info[I].Stage);
      MaxCycle = std::max(MaxCycle, Info[I].Cycle);
    }
    NStages = MaxStage + 1;
    II = TryII;

    if (NStages < 2) {
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: NStages<2 for II=" << TryII
                        << "\n");
      return false;
    }
    if (NStages > 4) {
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: NStages=" << NStages
                        << " too high for Stage-0 materialize\n");
      return false;
    }

    // Validate all deps still hold under the chosen cycles.
    for (int I = 0; I < N; ++I) {
      for (const SDep &Pred : Body[I]->Preds) {
        if (Pred.getSUnit()->isBoundaryNode())
          continue;
        for (int J = 0; J < N; ++J) {
          if (Body[J] != Pred.getSUnit())
            continue;
          int Need = Info[J].Cycle + static_cast<int>(Pred.getLatency());
          if (Info[I].Cycle < Need) {
            LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: latency fail SU"
                              << Body[I]->NodeNum << "\n");
            return false;
          }
        }
      }
    }

    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: tryII success II=" << II
                      << " NStages=" << NStages << " MaxCycle=" << MaxCycle
                      << "\n");
    return true;
  };

  // Pass 1: pure ASAP earliest placement (real multi-stage when latency
  // spans ≥2 IIs — e.g. vec_scale LD→mul→st).
  if (placeForII(TryII, HR, Body, Info, /*StageBiasFloor=*/N, MaxIISearch) &&
      finalizePlacement())
    return true;

  // Pass 2: stage-bias only when ASAP length has multi-II headroom. Forcing
  // stages on resource-bound bodies (raw_corr list II=2, LinearLength=3)
  // invents multi-stage that materializes *sparser* than list pack (II 2→3).
  if (N >= 3 && LinearLength > TryII + 1) {
    const int Floor = N / 2;
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: retry II=" << TryII
                      << " with stage bias floor=" << Floor
                      << " (LinearLength=" << LinearLength << ")\n");
    if (placeForII(TryII, HR, Body, Info, Floor, MaxIISearch) &&
        finalizePlacement())
      return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Materialize
//===----------------------------------------------------------------------===//

void HaydnPostPipeliner::adjustTripCount(int Delta) const {
  assert(TripCountDef && Preheader);
  const unsigned Opc = TripCountDef->getOpcode();

  // Form A — LoopStart: $src, simm6:$adj (folded into emitted count).
  if (Opc == Haydn::LoopStart) {
    int64_t Cur = TripCountDef->getOperand(1).getImm();
    TripCountDef->getOperand(1).setImm(Cur + Delta);
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: LoopStart adj " << Cur << " -> "
                      << (Cur + Delta) << "\n");
    return;
  }

  // Form B — SET_HWLOOP: sel, start, end, uimm16:$cnt
  if (Opc == Haydn::SET_HWLOOP) {
    assert(TripCountDef->getOperand(3).isImm());
    int64_t Cur = TripCountDef->getOperand(3).getImm();
    // Keep non-negative; Stage-0 only peels when NStages is small.
    int64_t New = std::max<int64_t>(0, Cur + Delta);
    TripCountDef->getOperand(3).setImm(New);
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: SET_HWLOOP cnt " << Cur << " -> "
                      << New << "\n");
    return;
  }

  // Form B — SET_HWLOOP_REG count adjust: general post-RA remat (not
  // hwloop-private). AIE writes LC; Haydn rematerializeAddImmForUse.
  if (Opc == Haydn::SET_HWLOOP_REG) {
    assert(TripCountDef->getOperand(3).isReg());
    Register Cnt = TripCountDef->getOperand(3).getReg();
    if (!Cnt.isPhysical() || Cnt == Haydn::R0 || Delta == 0)
      return;
    Register Dest =
        rematerializeAddImmForUse(*TripCountDef, /*UseOpIdx=*/3, Delta);
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: SET_HWLOOP_REG cnt "
                      << printReg(Cnt) << " + " << Delta << " -> "
                      << printReg(Dest) << "\n");
    return;
  }

  llvm_unreachable("unexpected hwloop setup opcode");
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
  // Process uses first (kills), then defs — standard bottom-up within MI.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.getReg() != Reg)
      continue;
    if (MO.isDef())
      Live = !MO.isDead();
    else if (MO.readsReg() && MO.isKill())
      Live = false;
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
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: prolog use of " << Reg
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

bool HaydnPostPipeliner::materialize() {
  assert(DAG && LoopBB && Preheader && ExitBB && TripCountDef);
  const int N = static_cast<int>(Body.size());
  MachineFunction &MF = *LoopBB->getParent();

  // Build per-modulo-cycle kernel order: for cycle M, emit body nodes with
  // ModuloCycle==M sorted by Stage descending (older iteration first — read
  // previous value before same-cycle WAR write of next).
  SmallVector<SmallVector<int, 4>, 8> KernelByMod(II);
  for (int I = 0; I < N; ++I)
    KernelByMod[Info[I].ModuloCycle].push_back(I);
  for (int M = 0; M < II; ++M) {
    llvm::stable_sort(KernelByMod[M], [&](int A, int B) {
      if (Info[A].Stage != Info[B].Stage)
        return Info[A].Stage > Info[B].Stage;
      return A < B;
    });
  }

  // Insert point for prolog: after last hwloop setup (SET/LoopStart), else
  // before terminators. Computed first so the liveness check matches.
  MachineBasicBlock::iterator PrologInsertPt =
      Preheader->getFirstTerminator();
  for (MachineInstr &MI : *Preheader) {
    if (isHwLoopSetup(MI))
      PrologInsertPt = std::next(MI.getIterator());
  }

  // Prologue (AIE visitPipelineSchedule): for each prologue stage S, emit
  // nodes with ModuloCycle==M && Cycle < (S+1)*II. Stage-0 caps NStages at 4
  // so this stays small.
  //
  // Skip loop-carried self-bumps (stay in kernel). If a non-bump peel fails
  // liveness, fail closed — do not emit a half-broken multi-stage kernel.
  const int NPrologStages = NStages - 1;
  SmallVector<MachineInstr *, 8> PrologMIs;
  for (int S = 0; S < NPrologStages; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Info[I].ModuloCycle == M && Info[I].Cycle < (S + 1) * II) {
          MachineInstr *Orig = Body[I]->getInstr();
          // Fail closed unless uses are live. Exception: loop-carried self-bumps
          // (iv=iv+step) often lack a live dest at insert (vec_scale r14); skip
          // those peels and keep them in the kernel. Other self-adds that *are*
          // live (chain_loop accum peels) still prolog correctly.
          if (!prologUsesAreAvailable(*Preheader, PrologInsertPt, PrologMIs,
                                      *Orig)) {
            if (isLoopCarriedSelfBump(*Orig)) {
              LLVM_DEBUG(dbgs()
                         << "HaydnPostPipeliner: skip prolog peel "
                            "(self-bump not live at insert): "
                         << *Orig);
              continue;
            }
            for (MachineInstr *C : PrologMIs)
              MF.deleteMachineInstr(C);
            LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: abort materialize — "
                                 "unsafe prolog peel\n");
            return false;
          }
          MachineInstr *Clone = MF.CloneMachineInstr(Orig);
          clearKillFlags(Clone);
          PrologMIs.push_back(Clone);
        }
      }
    }
  }

  // Epilogue: nodes with Cycle >= (1+S)*II.
  SmallVector<MachineInstr *, 8> EpilogMIs;
  const int EpiBase = 1;
  for (int S = 0; S < NStages - 1; ++S) {
    for (int M = 0; M < II; ++M) {
      for (int I = 0; I < N; ++I) {
        if (Info[I].ModuloCycle == M &&
            Info[I].Cycle >= (EpiBase + S) * II) {
          MachineInstr *Clone = MF.CloneMachineInstr(Body[I]->getInstr());
          clearKillFlags(Clone);
          EpilogMIs.push_back(Clone);
        }
      }
    }
  }

  // Insert prologue clones at the computed insert point.
  {
    for (MachineInstr *Clone : PrologMIs) {
      Preheader->insert(PrologInsertPt, Clone);
      // Do NOT fabricate live-ins for peels — uses must already be live
      // (enforced by prologUsesAreAvailable). Fake live-ins hide verifier
      // errors when the reg is not actually reaching.
    }
  }

  // Insert epilogue clones at start of exit; declare uses as live-ins.
  {
    MachineBasicBlock::iterator InsertPt = ExitBB->getFirstNonPHI();
    for (MachineInstr *Clone : EpilogMIs) {
      ExitBB->insert(InsertPt, Clone);
      addUsesAsLiveIns(*ExitBB, *Clone);
    }
  }

  // Reorder kernel: collect body MIs in (mod cycle, stage desc) order, splice
  // before PseudoLoopEnd (form A) or before first terminator / end (form B).
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

  // Stage-0 bundle formation: leaveRegion skips HR-auction bundling for
  // PostPipeliner regions (region iterators collapsed). Same-mod-cycle ops
  // are now contiguous — finalize BUNDLEs of 2–3 so.s shows real packing.
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
    // Cap at issue width 3 (extra ops in the same mod cell stay adjacent but
    // unbundled — Stage-0 scoreboard should rarely place >3).
    const unsigned NBundle = std::min<unsigned>(Group.size(), 3);
    for (unsigned I = 1; I < NBundle; ++I)
      Group[I]->bundleWithPred();
    finalizeBundle(*LoopBB, Group.front()->getIterator());
  }

  // Prologue defs feed the kernel: ensure loop live-ins include those regs.
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

  // Mark SUnits scheduled with TopReadyCycle = ModuloCycle for leaveRegion.
  for (int I = 0; I < N; ++I) {
    SUnit *SU = Body[I];
    SU->isScheduled = true;
    SU->TopReadyCycle = static_cast<unsigned>(Info[I].ModuloCycle);
    SU->BotReadyCycle = SU->TopReadyCycle;
  }

  // Tripcount: hardware iterations reduced by NStages-1.
  adjustTripCount(-(NStages - 1));

  LLVM_DEBUG({
    dbgs() << "HaydnPostPipeliner: materialize done II=" << II
           << " NStages=" << NStages << " prolog=" << PrologMIs.size()
           << " epilog=" << EpilogMIs.size() << "\n";
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

//===----------------------------------------------------------------------===//
// schedule
//===----------------------------------------------------------------------===//

bool HaydnPostPipeliner::schedule(ScheduleDAGMI &TheDAG, unsigned IIHint) {
  DAG = &TheDAG;
  // ScheduleDAGInstrs::BB is protected; recover the region block from an SU
  // or from the region's first instruction.
  MachineBasicBlock *MBBPtr = nullptr;
  if (!TheDAG.SUnits.empty() && TheDAG.SUnits[0].getInstr())
    MBBPtr = TheDAG.SUnits[0].getInstr()->getParent();
  if (!MBBPtr && TheDAG.begin() != TheDAG.end())
    MBBPtr = TheDAG.begin()->getParent();
  if (!MBBPtr) {
    ++NumPostPipelineFail;
    return false;
  }
  MachineBasicBlock &MBB = *MBBPtr;

  if (!isPostPipelineCandidate(MBB)) {
    ++NumPostPipelineFail;
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
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: <2 body SUnits\n");
    ++NumPostPipelineFail;
    return false;
  }

  Info.assign(Body.size(), HaydnPPNodeInfo());

  if (!computeASAPEarliest()) {
    ++NumPostPipelineFail;
    return false;
  }

  // Hazard recognizer for resource footprints. Construct a lightweight one
  // from the subtarget (same model as post-RA list sched).
  const TargetSubtargetInfo &ST = TheDAG.MF.getSubtarget();
  const TargetInstrInfo *TII = ST.getInstrInfo();
  const InstrItineraryData *Itin = ST.getInstrItineraryData();
  HaydnHazardRecognizer HR(TII, Itin, /*IsPreRA=*/false);

  int ResMII = getResMII(MBB);
  int StartII = ResMII;
  if (IIHint > 0)
    StartII = static_cast<int>(IIHint);

  bool Found = false;
  // AIE postpipeliner README: II must not grow vs the non-SWP baseline.
  // Fail closed (Wave 2 + P11): only accept II < LinearLength. Equality is
  // stage-bias inventing multi-stage without ASAP compression (raw_corr
  // II 2→3). Inflated ResMII past LinearLength-1 → skip (list wins).
  if (LinearLength < 2) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: LinearLength=" << LinearLength
                      << " — no multi-stage headroom\n");
    ++NumPostPipelineFail;
    return false;
  }
  const int ListBaseline = LinearLength - 1; // strict: II < LinearLength
  const int MaxII = std::min(ListBaseline, MaxIISearch);
  if (StartII > MaxII) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: StartII=" << StartII
                      << " > MaxII=" << MaxII << " (ListBaseline="
                      << ListBaseline << " LinearLength=" << LinearLength
                      << " ResMII=" << ResMII << ") — skip (fail closed)\n");
    ++NumPostPipelineFail;
    return false;
  }
  LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: II search [" << StartII << ","
                    << MaxII << "] ResMII=" << ResMII
                    << " LinearLength=" << LinearLength
                    << " ListBaseline=" << ListBaseline << "\n");
  auto acceptII = [&]() {
    if (II >= LinearLength) {
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: reject II=" << II
                        << " >= LinearLength=" << LinearLength << "\n");
      return false;
    }
    if (II > ListBaseline) {
      LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: reject II=" << II
                        << " > ListBaseline=" << ListBaseline << "\n");
      return false;
    }
    return true;
  };
  for (int Try = StartII; Try <= MaxII; ++Try) {
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: trying II=" << Try << "\n");
    if (tryII(Try, HR) && acceptII()) {
      Found = true;
      break;
    }
  }
  // If IIHint was set and failed, also search from ResMII.
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
    LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: no schedule found\n");
    ++NumPostPipelineFail;
    return false;
  }

  if (!materialize()) {
    // tryII may have "succeeded" but physreg prolog safety rejected the
    // materialize. Fall back to list schedule.
    ++NumPostPipelineFail;
    return false;
  }

  LLVM_DEBUG(dbgs() << "HaydnPostPipeliner: Success II=" << II
                    << " NStages=" << NStages << "\n");
  ++NumPostPipelineSuccess;
  return true;
}
