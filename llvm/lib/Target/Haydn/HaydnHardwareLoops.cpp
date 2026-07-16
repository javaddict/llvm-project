//===-- HaydnHardwareLoops.cpp - Hardware Loop Detection ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HiFi / NatureDSP oracle contract
// Cadence HiFi forms countable DSP loops as a *preheader fact* then ZOL:
//
// preheader: trip = f(N); e.g. addi a15, a14, -1
// loopnez a15, Lend; hardware count, no soft back-edge
// body:...; SWPS may multi-stage the body
//
// Haydn's competitive form is the same math with Haydn ops:
//
// preheader: set_hwloop[_f2_w] sel, start, end, trip
// body:...; no SEQ/BEQ latch
//
// Post-RA Role B reconstructs that contract from a soft-branch residual when
// IR-level HardwareLoops did not fire. The only sound queries are
// **program-point** resolutions of loop-invariant constants:
//
// Val(R @ Use) = nearest prior def of R before Use
// ADDI/LOADI → imm
// copy → Val(src @ Def)
// LD sp, off → Val(stored @ dominating ST) domain = Dom*(Header) ∪ L
// other → non-constant (stop; never walk past to a stale ancestor)
//
// Searching "loop + getLoopPreheader" alone is wrong: createPreheaderForLoop
// inserts an *empty* synthetic preheader; real ADDI/ST live on the IDom chain.
// Block-global preheader walks are wrong under physreg reuse (limit=0 and
// step=-1 share a hardreg across different slots — divide/CoreMark P9).
//
// Dual roles
// Role A — IR ZOL already formed (do NOT re-convert):
// LoopStart + PseudoLoopEnd (or LoopDec/LoopJNZ). AIE expands LoopStart to
// real setup *before* PostMachineScheduler (AIE2 addPreSched2 order). Haydn
// mirrors that: expandRoleALoopStarts rewrites LoopStart → SET_HWLOOP_REG
// (sel=1, Header/Latch=body) before the post-RA VLIW scheduler so setup is
// a schedulable boundary, not a zero-size pseudo AsmPrinter lowers late.
// FixupHwLoops still pads / demotes. Degenerate shells (empty or store-only
// body after SMS peel) are stripped so Role B can convert the real compute
// loop. PseudoLoopEnd is MCID::Meta — body classifiers match the opcode.
//
// Role B — convert countable soft-branch loops to SET_HWLOOP / SET_HWLOOP_REG.
//
// Algorithm:
// 0. stripEmptyZeroOverheadLoops — remove empty/shell Role A ZOLs
// 0b.expandRoleALoopStarts — AIE-style LoopStart → SET_HWLOOP_REG (+ t−3)
// 1. Walk loops inside-out; skip Role A; validate latch/exit/no-call
// 2. Identify IV + bump + limit at the latch (fused or unfused cmp/br)
// 3. step = Val(step @ bump); limit = Val(limit @ cmp); init at preheader
// 4. Trip cases match HiFi:
// Case 1/4 count-up trip = (limit-init)/step (reg or imm)
// Case 2 count-down trip = IV @ entry when step=-1, limit=0
// Case 3 pointer-IV trip = (end-start)>>log2(stride)
// 5. Insert SET in preheader; erase soft back-edge (+ dead SEQ)
//
//===----------------------------------------------------------------------===//

#include "HaydnHardwareLoops.h"
#include "Haydn.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <algorithm>
#include <optional>

#define DEBUG_TYPE "haydn-hwloops"

STATISTIC(NumHWLoops, "Number of loops converted to hardware loops");
STATISTIC(NumFusedBranchLoops,
          "Number of HW loops detected from fused cmp+branch");
STATISTIC(NumUnfusedBranchLoops,
          "Number of HW loops detected from unfused cmp+branch");
STATISTIC(NumNestedHWLoops,
          "Number of nested hardware loops (outer loop converted)");
STATISTIC(NumHWLoopRangeOverflow,
          "Number of loops declined for HWLOOP (body exceeds offset field)");
STATISTIC(NumEmptyZOLStripped,
          "Number of empty IR-form ZOLs stripped (peeled body)");
STATISTIC(NumRoleAExpanded,
          "Number of Role A LoopStart expanded to SET_HWLOOP_REG pre-sched");

// Local aliases — sole numeric source is HaydnHWLoopContracts.h.
using llvm::haydn::hwloop::Bundle128Bytes;
using llvm::haydn::hwloop::MaxEndOffsetBytes;
using llvm::haydn::hwloop::MaxStartOffsetBytes;
using llvm::haydn::hwloop::MinSetupBundles;

// Compatibility names used throughout this file.
static constexpr int64_t MaxHWLoopStartOffsetBytes = MaxStartOffsetBytes;
static constexpr int64_t MaxHWLoopEndOffsetBytes = MaxEndOffsetBytes;
static constexpr unsigned HWLoopSetupPadBundles = MinSetupBundles;

using namespace llvm;

// Fixed hwloop policy:
// * Dual HWLR free-list always (sel=1 prefer innermost, sel=0 outer).
// * Role A (IR HardwareLoops + expand) is authority for single-BB ZOL
// same split as AIE (TTI decides, MIR expands). Role B is residual only.
// * Always pad short bodies / t−3 with NOPs (spec).
// * Layout-owned setup: useful preheader work stays *before* SET; only
// deficit NOPs after SET (no "smart-setup" after-SET fill).

// Residual Role B soft-branch convert (opt-in). Default OFF = AIE-like
// expand-only for Role A.
static cl::opt<bool> EnableHaydnHwloopRoleB(
    "haydn-hwloop-role-b", cl::init(false), cl::Hidden,
    cl::desc("Residual Role B opt-in: convert countable soft-branch loops "
             "without IR ZOL markers. Default OFF = AIE-like expand-only "
             ""));

char HaydnHardwareLoops::ID = 0;

INITIALIZE_PASS_BEGIN(HaydnHardwareLoops, DEBUG_TYPE,
                      "Haydn Hardware Loop Detection", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(HaydnHardwareLoops, DEBUG_TYPE,
                    "Haydn Hardware Loop Detection", false, false)

HaydnHardwareLoops::HaydnHardwareLoops() : MachineFunctionPass(ID) {
  initializeHaydnHardwareLoopsPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnHardwareLoopsPass() {
  return new HaydnHardwareLoops();
}

MachineBasicBlock *HaydnHardwareLoops::createPreheaderForLoop(MachineLoop *L) {
  // Split the non-backedge predecessor→header edge to create a dedicated
  // preheader for a guarded loop (whose entry block has multiple successors).
  // Post-RA safe: Haydn HWLoops runs after PHI elimination (NoPHIs), so no
  // PHI rewriting is needed. Adapted from HexagonHardwareLoops, simplified
  // for the single-entry, two-predecessor case. See (FIX A).
  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Latch = L->getLoopLatch();
  if (!Header || !Latch || Header->hasAddressTaken())
    return nullptr;

  // Header must have exactly two predecessors: the latch (back-edge) and the
  // single entry (guard) block.
  if (Header->pred_size() != 2)
    return nullptr;

  MachineBasicBlock *Guard = nullptr;
  for (MachineBasicBlock *P : Header->predecessors()) {
    if (P == Latch)
      continue;
    if (Guard) // Multiple non-latch predecessors — not handled.
      return nullptr;
    Guard = P;
  }
  if (!Guard)
    return nullptr;

  const auto *TII =
      Header->getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  MachineBasicBlock *TB = nullptr;
  MachineBasicBlock *FB = nullptr;
  SmallVector<MachineOperand, 2> Cond;
  if (TII->analyzeBranch(*Guard, TB, FB, Cond, /*AllowModify=*/false))
    return nullptr;

  MachineFunction *MF = Header->getParent();
  MachineBasicBlock *NewPH = MF->CreateMachineBasicBlock();

  // Capture the block that was layout-immediately-before Header BEFORE the
  // insert. If a block other than Guard was layout-adjacent to Header, its
  // implicit fallthrough previously went to Header and now goes to NewPH
  // but its successor list still names Header, which leaves a stale CFG edge
  // that later MachineBlockPlacement trips over (assert at
  // MachineBasicBlock.cpp:779). Redirect such blocks too.
  MachineFunction::iterator HeaderIt(Header->getIterator());
  MachineBasicBlock *LayoutPred =
      (HeaderIt != MF->begin()) ? &*std::prev(HeaderIt) : nullptr;

  // Layout NewPH immediately before the header so it falls through.
  MF->insert(Header->getIterator(), NewPH);

  // Redirect the guard's edge Header→NewPH (rewrites branch operands +
  // successor list).
  Guard->ReplaceUsesOfBlockWith(Header, NewPH);
  // If the guard previously fell through to the header (no explicit branch)
  // it now needs an explicit branch to NewPH.
  TB = FB = nullptr;
  Cond.clear();
  if (!TII->analyzeBranch(*Guard, TB, FB, Cond, /*AllowModify=*/false)) {
    if (TB != NewPH && FB != NewPH)
      TII->insertBranch(*Guard, NewPH, nullptr, Cond, DebugLoc());
  }

  // If a non-guard block was layout-adjacent to Header and is NOT the latch
  // its implicit fallthrough must now land on NewPH. Add NewPH as a successor
  // and drop Header from its successor list, then ensure an explicit branch
  // to NewPH if the fallthrough path needs one.
  //
  // If the layout-pred IS the latch (common case: the latch's conditional
  // branch exited the loop and the back-edge was an implicit fallthrough to
  // Header), inserting NewPH between Latch and Header breaks that fallthrough:
  // the Latch now falls through to NewPH instead of Header. Insert an explicit
  // unconditional B to Header so the back-edge survives.
  if (LayoutPred && LayoutPred != Guard &&
      LayoutPred->isSuccessor(Header)) {
    if (LayoutPred == Latch) {
      // Latch's fallthrough back-edge must become explicit.
      TB = FB = nullptr;
      Cond.clear();
      if (!TII->analyzeBranch(*LayoutPred, TB, FB, Cond, /*AllowModify=*/false)) {
        // If the latch has a conditional branch to an exit block and falls
        // through to Header, append an unconditional B to Header so the
        // back-edge is preserved as NewPH is now between them.
        if (TB != Header && FB != Header)
          TII->insertBranch(*LayoutPred, Header, nullptr, {}, DebugLoc());
      }
    } else {
      LayoutPred->ReplaceUsesOfBlockWith(Header, NewPH);
      TB = FB = nullptr;
      Cond.clear();
      if (!TII->analyzeBranch(*LayoutPred, TB, FB, Cond, /*AllowModify=*/false)) {
        if (TB != NewPH && FB != NewPH)
          TII->insertBranch(*LayoutPred, NewPH, nullptr, Cond, DebugLoc());
      }
    }
  }

  // NewPH falls through to the header.
  NewPH->addSuccessor(Header);
  // Inherit live-ins from the header.
  for (const auto &LI : Header->liveins())
    NewPH->addLiveIn(LI);

  // Keep MachineLoopInfo / MachineDominatorTree in sync with the new block.
  // NewPH is *outside* L (a true preheader) but *inside* every parent of L
  // (nested convert inserts it between two outer-loop blocks). Hexagon does
  // the same: ParentLoop->addBasicBlockToLoop adds to that loop and all
  // ancestors (LoopInfoBase). Without this, outer getExitBlocks reports
  // NewPH as a spurious "exit" and multi-BB validation needs phantom filters.
  // MDT: null getNode(NewPH) breaks extractIVBump's dominator walk.
  if (MLI) {
    if (MachineLoop *ParentLoop = L->getParentLoop())
      ParentLoop->addBasicBlockToLoop(NewPH, *MLI);
  }
  if (MDT) {
    if (MachineDomTreeNode *HN = MDT->getNode(Header)) {
      if (MachineDomTreeNode *DHN = HN->getIDom()) {
        MDT->addNewBlock(NewPH, DHN->getBlock());
        MDT->changeImmediateDominator(Header, NewPH);
      }
    }
  }

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Inserted preheader "
                    << printMBBReference(*NewPH) << " between "
                    << printMBBReference(*Guard) << " and "
                    << printMBBReference(*Header) << "\n");
  return NewPH;
}

// Classify an IR-form ZOL body (Role A). PseudoLoopEnd is MCID::Meta — must
// match the opcode explicitly; isMetaInstruction alone would skip it and
// leave store-only shells frozen (poly/alog).
// HiFi never emits a ZOL whose body is empty or store-only while the real
// work lives outside; NatureDSP SMS peel can leave that shape on Haydn.
// Strip those so Role B can convert the actual compute loop.
enum class ZOLBodyKind {
  NotZOL,   //< No PseudoLoopEnd marker.
  Empty,    //< Only PLE + B + meta (SMS full peel).
  Shell,    //< PLE + ≤2 stores/copies/imms, no load/compute.
  Real,     //< Has load or non-store compute — keep Role A.
};

static ZOLBodyKind classifyZOLBody(const MachineBasicBlock &Body) {
  bool SawPseudoLoopEnd = false;
  unsigned RealOps = 0;
  bool HasLoadOrCompute = false;
  for (const MachineInstr &MI : Body) {
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::PseudoLoopEnd) {
      SawPseudoLoopEnd = true;
      continue;
    }
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isPosition())
      continue;
    if (Opc == Haydn::B || Opc == Haydn::NOP)
      continue;
    ++RealOps;
    // Haydn FmtLS defaults mayLoad=1 on the class, then ST32 only sets
    // mayStore=1 — so pure stores report mayLoad&&mayStore. Treat store-only
    // (including that quirk) as shell payload, not "compute". Real loads are
    // mayLoad && !mayStore; ALU/MAC is neither store nor copy/imm.
    if (MI.mayLoad() && !MI.mayStore())
      HasLoadOrCompute = true;
    else if (!MI.mayStore() && !MI.isCopy() && !MI.isMoveImmediate())
      HasLoadOrCompute = true;
  }
  if (!SawPseudoLoopEnd)
    return ZOLBodyKind::NotZOL;
  if (RealOps == 0)
    return ZOLBodyKind::Empty;
  if (!HasLoadOrCompute && RealOps <= 2)
    return ZOLBodyKind::Shell;
  return ZOLBodyKind::Real;
}

static bool isEmptyZOLBody(const MachineBasicBlock &Body) {
  return classifyZOLBody(Body) == ZOLBodyKind::Empty;
}

static bool isShellZOLBody(const MachineBasicBlock &Body) {
  return classifyZOLBody(Body) == ZOLBodyKind::Shell;
}

// Strip empty/shell IR-form zero-overhead loops (AIE-style): LoopStart in a
// preheader paired with a body that is only PseudoLoopEnd (+ maybe B + meta)
// or store-only shell (P5 poly/alog). SMS may peel all real work; Role A then
// freezes a useless ZOL. Remove LoopStart and PseudoLoopEnd and restore a
// normal exit edge.
static bool stripEmptyZeroOverheadLoops(MachineFunction &MF) {
  const auto *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  SmallVector<MachineInstr *, 4> LoopStarts;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == Haydn::LoopStart)
        LoopStarts.push_back(&MI);

  bool Changed = false;
  for (MachineInstr *LS : LoopStarts) {
    MachineBasicBlock *Preheader = LS->getParent();
    MachineBasicBlock *Body = nullptr;
    MachineInstr *PLE = nullptr;

    // Find body + PseudoLoopEnd. PLE is MCID::Meta — may or may not appear in
    // terminators; always full-block scan by opcode (same as classifyZOLBody).
    auto findPLE = [](MachineBasicBlock *BB) -> MachineInstr * {
      if (!BB)
        return nullptr;
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Haydn::PseudoLoopEnd)
          return &MI;
      return nullptr;
    };
    for (const MachineOperand &MO : LS->operands()) {
      if (MO.isMBB()) {
        Body = MO.getMBB();
        break;
      }
    }
    if (Body)
      PLE = findPLE(Body);
    if (!Body || !PLE) {
      Body = nullptr;
      PLE = nullptr;
      for (MachineBasicBlock *Succ : Preheader->successors()) {
        if (MachineInstr *Cand = findPLE(Succ)) {
          Body = Succ;
          PLE = Cand;
          break;
        }
      }
    }
    if (!Body || !PLE)
      continue;
    // Strip empty (SMS peel) and store-only shells (poly/alog).
    ZOLBodyKind Kind = classifyZOLBody(*Body);
    if (Kind != ZOLBodyKind::Empty && Kind != ZOLBodyKind::Shell)
      continue;

    // Exit = non-self successor of the body; else layout fallthrough.
    MachineBasicBlock *ExitBB = nullptr;
    for (MachineBasicBlock *Succ : Body->successors()) {
      if (Succ != Body) {
        ExitBB = Succ;
        break;
      }
    }
    if (!ExitBB) {
      MachineFunction::iterator NextIt = std::next(Body->getIterator());
      if (NextIt != MF.end())
        ExitBB = &*NextIt;
    }
    // Without a resolvable exit, leave the degenerate ZOL alone (AsmPrinter
    // already no-ops empty LoopStart); do not produce a successor-less MBB.
    if (!ExitBB)
      continue;

    LLVM_DEBUG(dbgs() << "HaydnHWLoops: stripping empty IR ZOL "
                      << printMBBReference(*Body) << " (LoopStart in "
                      << printMBBReference(*Preheader) << ")\n");

    DebugLoc DL = LS->getDebugLoc();
    LS->eraseFromParent();
    PLE->eraseFromParent();

    // Drop the self back-edge; keep exit only.
    if (Body->isSuccessor(Body))
      Body->removeSuccessor(Body);
    if (!Body->isSuccessor(ExitBB))
      Body->addSuccessor(ExitBB);

    // Ensure a path to the exit: keep an existing B, else insert one if
    // ExitBB is not layout fallthrough.
    bool HasExitBranch = false;
    for (const MachineInstr &MI : Body->terminators()) {
      if (MI.getOpcode() == Haydn::B) {
        HasExitBranch = true;
        break;
      }
    }
    if (!HasExitBranch) {
      MachineFunction::iterator BodyIt = Body->getIterator();
      MachineFunction::iterator NextIt = std::next(BodyIt);
      bool ExitIsFallthrough =
          (NextIt != MF.end()) && (&*NextIt == ExitBB);
      if (!ExitIsFallthrough)
        TII->insertBranch(*Body, ExitBB, /*FBB=*/nullptr,
                          /*Cond=*/SmallVector<MachineOperand, 0>(), DL);
    }

    ++NumEmptyZOLStripped;
    Changed = true;
  }
  return Changed;
}

// AIE-aligned Role A expand: LoopStart → SET_HWLOOP_REG before post-RA sched.
// AIE2 addPreSched2 runs AIEBaseHardwareLoops (expand LoopStart → LC/LS/LE
// setup) *before* PostMachineScheduler. Haydn previously left LoopStart as a
// zero-size pseudo until AsmPrinter, so postmisched could reorder real work
// across it (lc_dp_merge: s_lw_post with unscaled index as base → ALIGNMENT
// fault at 0x7). Expanding here makes SET a real side-effecting MI the
// scheduler must respect (see isSchedulingBoundary).
// Contract (matches Role B formation):
// Useful preheader work stays *before* SET.
// After SET: only t−3 deficit NOPs (layout-owned setup window).
// PseudoLoopEnd stays in the body (END label / analyzeBranch).
// Empty/shell bodies are already stripped; skip if no real body left.
static bool expandRoleALoopStarts(MachineFunction &MF) {
  const auto *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  SmallVector<MachineInstr *, 4> LoopStarts;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == Haydn::LoopStart)
        LoopStarts.push_back(&MI);

  bool Changed = false;
  for (MachineInstr *LS : LoopStarts) {
    if (!LS->getParent() || !LS->getOperand(0).isReg() ||
        !LS->getOperand(1).isImm())
      continue;

    MachineBasicBlock *Preheader = LS->getParent();
    Register TripReg = LS->getOperand(0).getReg();
    int64_t Adj = LS->getOperand(1).getImm();
    DebugLoc DL = LS->getDebugLoc();

    // Resolve body via PseudoLoopEnd on a preheader successor (AsmPrinter).
    MachineBasicBlock *Body = nullptr;
    auto findPLE = [](MachineBasicBlock *BB) -> MachineInstr * {
      if (!BB)
        return nullptr;
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Haydn::PseudoLoopEnd)
          return &MI;
      return nullptr;
    };
    for (MachineBasicBlock *Succ : Preheader->successors()) {
      if (findPLE(Succ)) {
        Body = Succ;
        break;
      }
    }
    if (!Body) {
      // Layout successor fallback.
      MachineFunction::iterator It = Preheader->getIterator();
      if (std::next(It) != MF.end())
        Body = &*std::next(It);
    }
    if (!Body || classifyZOLBody(*Body) != ZOLBodyKind::Real) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expand skip — no real body "
                           "for LoopStart in "
                        << printMBBReference(*Preheader) << "\n");
      continue;
    }

    // Single-BB ZOL: Header == Latch == Body (IR HardwareLoops contract).
    MachineBasicBlock *Header = Body;
    MachineBasicBlock *Latch = Body;
    Header->setLabelMustBeEmitted();
    Latch->setLabelMustBeEmitted();

    // Pipeliner adj: AIE expands LoopStart as
    // SetLoopCount LC, src, adj; LC = src + adj, **src GPR preserved**
    // SetLoopStart / SetLoopEnd
    // (AIEBaseHardwareLoops::expandLoopStart). Haydn has no separate LC
    // register — SET_HWLOOP_REG takes a GPR count. Never ADDI in-place into
    // TripReg: it is often still live (outer-loop bound N re-used as trip
    // source each row). CoreMark matrix_sum @ -O2: LoopStart $r1, -1 in the
    // outer header clobbered N → set_hwloop(N-1), then N-2, … and MEMORY_FAULT.
    MachineBasicBlock::iterator InsertPt = LS->getIterator();
    Register CountForSet = TripReg;
    if (Adj != 0 && TripReg.isPhysical() && TripReg != Haydn::R0) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const MachineRegisterInfo &MRI = MF.getRegInfo();

      LivePhysRegs LPR(TRI);
      LPR.addLiveOuts(*Preheader);
      for (MachineBasicBlock::iterator II = Preheader->end(); II != InsertPt;) {
        --II;
        LPR.stepBackward(*II);
      }

      // Dest for (src + adj). Prefer free GPR != TripReg. Only reuse TripReg
      // when it is dead at the insert point (true in-place then).
      static constexpr MCPhysReg ScratchPri[] = {
          Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5, Haydn::R6,
          Haydn::R7, Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8,
      };
      Register Dest;
      for (MCPhysReg Cand : ScratchPri) {
        if (Cand == TripReg || MRI.isReserved(Cand))
          continue;
        if (LPR.available(MRI, Cand)) {
          Dest = Cand;
          break;
        }
      }
      if (!Dest && LPR.available(MRI, TripReg))
        Dest = TripReg;
      if (!Dest) {
        // No dead scratch: still must not clobber TripReg. Overwrite some
        // other GPR (last resort — preserves N / outer bound).
        for (MCPhysReg Cand : ScratchPri) {
          if (Cand != TripReg && !MRI.isReserved(Cand)) {
            Dest = Cand;
            break;
          }
        }
        assert(Dest && Dest != TripReg);
        LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A adj: no dead scratch; "
                             "overwriting "
                          << printReg(Dest) << " (TripReg preserved)\n");
      }

      BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::ADDI32_W), Dest)
          .addReg(TripReg)
          .addImm(Adj);
      CountForSet = Dest;
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expand adj=" << Adj
                        << " src=" << printReg(TripReg)
                        << " count=" << printReg(CountForSet)
                        << " (AIE-like preserve src)\n");
    }

    // Innermost ZOL uses sel=1 (same convention as Role A skip path).
    MachineInstr *SetMI =
        BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SET_HWLOOP_REG))
            .addImm(/*Sel=*/1)
            .addMBB(Header)
            .addMBB(Latch)
            .addReg(CountForSet);

    // t−3: only deficit NOPs after SET (layout-owned). Pre-existing post-LS
    // work (rare pre-expand) stays after SET; Fixup may demote if range-bad.
    {
      unsigned FollowingBundles = 0;
      for (MachineBasicBlock::iterator I = std::next(SetMI->getIterator()),
                                       E = Preheader->end();
           I != E; ++I) {
        if (I->isMetaInstruction() || I->isDebugInstr() ||
            I->isImplicitDef() || I->isKill())
          continue;
        if (I->isTerminator() && !I->isCall())
          break;
        unsigned Bytes = TII->getInstSizeInBytes(*I);
        if (Bytes == 0)
          continue;
        FollowingBundles +=
            (Bytes + static_cast<unsigned>(Bundle128Bytes) - 1) /
            static_cast<unsigned>(Bundle128Bytes);
      }
      if (FollowingBundles < HWLoopSetupPadBundles) {
        unsigned Deficit = HWLoopSetupPadBundles - FollowingBundles;
        MachineBasicBlock::iterator AfterSet =
            std::next(SetMI->getIterator());
        for (unsigned I = 0; I < Deficit; ++I)
          BuildMI(*Preheader, AfterSet, DL, TII->get(Haydn::NOP));
        LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expand t−3 pad " << Deficit
                          << " NOP bundle(s)\n");
      }
    }

    // Body length is not a legality floor (BEGIN <= END is valid; t−3 is
    // the hard rule, enforced by deficit NOPs above + Fixup).

    LS->eraseFromParent();
    ++NumRoleAExpanded;
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expanded LoopStart → "
                         "SET_HWLOOP_REG sel=1 body="
                      << printMBBReference(*Body) << " trip="
                      << printReg(TripReg) << "\n");
  }
  return Changed;
}

bool HaydnHardwareLoops::runOnMachineFunction(MachineFunction &MF) {
  const auto &STI = MF.getSubtarget<HaydnSubtarget>();
  if (!STI.hasHWLoop())
    return false;

  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Running on " << MF.getName()
                    << " (Role A expand; Role B residual="
                    << (EnableHaydnHwloopRoleB ? "on" : "off") << ")\n");

  bool Changed = false;

  // Role A cleanup: strip fully-peeled empty ZOLs before convert walk.
  Changed |= stripEmptyZeroOverheadLoops(MF);

  // AIE order: expand surviving Role A LoopStart → SET_HWLOOP_REG *before*
  // PostMachineScheduler (this pass runs in addPreSched2 before postmisched).
  Changed |= expandRoleALoopStarts(MF);

  // Role B residual convert (soft-branch → SET). Off = AIE-like expand-only.
  if (!EnableHaydnHwloopRoleB) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role B disabled — expand-only\n");
    return Changed;
  }

  // Process each top-level loop recursively. The recursive function processes
  // sub-loops (inner) first, then the current loop (outer). This ensures
  // inner loops get sel=1 and outer loops get sel=0 for 2-level nesting.
  // Role A (SET_HWLOOP after expand, or residual LoopDec/LoopJNZ) is skipped
  // inside convertToHardwareLoop; Role B converts the rest.
  for (MachineLoop *L : *MLI) {
    bool Sel0Used = false;
    bool Sel1Used = false;
    Changed |= convertToHardwareLoop(L, MF, Sel0Used, Sel1Used);
  }

  return Changed;
}

bool HaydnHardwareLoops::isSingleBBLoop(const MachineLoop *L) const {
  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Latch = L->getLoopLatch();
  return Header && Latch && Header == Latch;
}

bool HaydnHardwareLoops::hasValidMultiBBStructure(
    const MachineLoop *L) const {
  // Role B multi-BB ZOL contract (Haydn ISA / — stricter than AIE IR):
  // * single latch, latch → header back-edge
  // * exactly one true exit
  // * only the latch may target that exit (no early break out of HWLR region)
  // AIE refuses multi-BB at IR TTI; Haydn Role B still forms multi-BB ZOL when
  // this contract holds (NatureDSP multi-BB bodies). Keep structure rules
  // restatable — not trip-count soft-zero hacks.
  MachineBasicBlock *Latch = L->getLoopLatch();
  if (!Latch) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: No single latch block\n");
    return false;
  }

  MachineBasicBlock *Header = L->getHeader();
  if (!Header) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: No header block\n");
    return false;
  }

  if (!Latch->isSuccessor(Header)) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Latch does not branch to header\n");
    return false;
  }

  // True exit vs interior non-exit (child preheader under stale/partial MLI):
  // interior: has successors AND every successor is inside L
  // true exit: terminal (no successors) OR has a successor outside L
  // createPreheaderForLoop adds NewPH to parent loops via addBasicBlockToLoop
  // so outer contains should hold; filter remains a safety net.
  auto isInteriorNonExit = [&](const MachineBasicBlock *BB) {
    if (BB->succ_empty())
      return false; // terminal return/unreachable — real exit
    for (const MachineBasicBlock *Succ : BB->successors()) {
      if (!L->contains(Succ))
        return false;
    }
    return true;
  };

  SmallVector<MachineBasicBlock *, 4> RawExits;
  L->getExitBlocks(RawExits);
  MachineBasicBlock *ExitBB = nullptr;
  for (MachineBasicBlock *EB : RawExits) {
    if (L->contains(EB))
      continue;
    if (isInteriorNonExit(EB)) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Skipping interior non-exit "
                        << printMBBReference(*EB) << "\n");
      continue;
    }
    if (ExitBB && EB != ExitBB) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Multiple real exit blocks: "
                        << printMBBReference(*ExitBB) << " and "
                        << printMBBReference(*EB) << "\n");
      return false;
    }
    ExitBB = EB;
  }
  if (!ExitBB) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: No real exit block found\n");
    return false;
  }

  // Only latch may leave the HWLR region.
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    for (MachineBasicBlock *Succ : MBB->successors()) {
      if (L->contains(Succ))
        continue;
      if (MBB == Latch && Succ == ExitBB)
        continue;
      if (isInteriorNonExit(Succ))
        continue; // edge into a child preheader still inside outer region
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: non-latch early exit "
                        << printMBBReference(*MBB) << " -> "
                        << printMBBReference(*Succ) << "\n");
      return false;
    }
  }

  return true;
}

bool HaydnHardwareLoops::loopBodyFitsRange(const MachineLoop *L) const {
  // Sum estimated byte sizes of all non-pseudo instructions in the loop body.
  // Pseudos are skipped (expand later or emit nothing). Bundle children are
  // size 0; the BUNDLE root is 16 (Bundle128-only size model).
  const auto *TII =
      L->getHeader()->getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();

  int64_t BodyBytes = 0;
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    for (const MachineInstr &MI : *MBB) {
      if (MI.isPseudo())
        continue;
      BodyBytes += TII->getInstSizeInBytes(MI);
    }
  }

  // START offset is measured from *after* SET. Smart t−3 may move independent
  // preheader precompute after SET, but convert caps that payload so Start
  // stays under MaxHWLoopStartOffsetBytes (see convert). Estimate uses the
  // capped preheader contribution (or the min t−3 pad if preheader is tiny).
  //
  // uimm6_offset1 (START): max 252 B — tight.
  // uimm12_offset2 (END): max 16380 B — loose for normal bodies.
  const int64_t MinSetupBytes =
      static_cast<int64_t>(HWLoopSetupPadBundles) * Bundle128Bytes;
  // Leave one parcel margin for later layout drift (Fixup pads, relax).
  const int64_t MaxAfterSetBytes =
      MaxHWLoopStartOffsetBytes - Bundle128Bytes; // 236
  int64_t AfterSetEstimate = MinSetupBytes;
  if (const MachineBasicBlock *PH = L->getLoopPreheader()) {
    int64_t PHBytes = 0;
    for (const MachineInstr &MI : *PH) {
      if (MI.isTerminator() && !MI.isCall())
        break;
      if (MI.isMetaInstruction() || MI.isDebugInstr())
        continue;
      // Pseudos may still expand to parcels; count non-zero sizes only.
      PHBytes += TII->getInstSizeInBytes(MI);
    }
    // Worst case after-SET payload under the smart t−3 move cap.
    AfterSetEstimate = std::max(MinSetupBytes, std::min(PHBytes, MaxAfterSetBytes));
  }
  const int64_t StartOffsetEstimate = AfterSetEstimate;
  // Inclusive END: body size may be small; t−3 is enforced separately.
  const int64_t EndOffsetEstimate = StartOffsetEstimate + BodyBytes;

  LLVM_DEBUG({
    dbgs() << "HaydnHWLoops: Estimated start offset " << StartOffsetEstimate
           << " bytes (max uimm6 = " << MaxHWLoopStartOffsetBytes << "), "
           << "end offset " << EndOffsetEstimate << " bytes (max uimm12 = "
           << MaxHWLoopEndOffsetBytes << ")\n";
  });

  if (StartOffsetEstimate > MaxHWLoopStartOffsetBytes ||
      EndOffsetEstimate > MaxHWLoopEndOffsetBytes) {
    ++NumHWLoopRangeOverflow;
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Loop body exceeds HWLOOP offset "
                         "field(s) — start(uimm6)=" << StartOffsetEstimate
                      << "/" << MaxHWLoopStartOffsetBytes
                      << ", end(uimm12)=" << EndOffsetEstimate
                      << "/" << MaxHWLoopEndOffsetBytes
                      << " — declining conversion\n");
    return false;
  }

  return true;
}

bool HaydnHardwareLoops::hasIRZOLForm(const MachineLoop *L) const {
  // Role A marker: IR-level HardwareLoops already formed THIS loop, or
  // expandRoleALoopStarts already rewrote LoopStart → SET_HWLOOP_REG.
  // Only scan this loop's own preheader and latch. Do NOT scan the full
  // block set — a converted child's setup lives in a preheader inside the
  // outer's block set (PR7 dual HWLR).
  auto Scan = [](const MachineBasicBlock *MBB) {
    if (!MBB)
      return false;
    for (const MachineInstr &MI : *MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::LoopStart || Opc == Haydn::PseudoLoopEnd ||
          Opc == Haydn::LoopDec || Opc == Haydn::LoopJNZ ||
          Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG)
        return true;
    }
    return false;
  };
  if (Scan(L->getLoopPreheader()))
    return true;
  if (Scan(L->getLoopLatch()))
    return true;
  // Single-BB: header == latch already covered. Multi-BB header rarely holds
  // Role A markers; still check for completeness.
  if (L->getHeader() != L->getLoopLatch() && Scan(L->getHeader()))
    return true;
  return false;
}

bool HaydnHardwareLoops::containsInvalidInstruction(
    const MachineLoop *L, bool AllowChildHwloop) const {
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    for (const MachineInstr &MI : *MBB) {
      if (MI.isCall())
        return true;
      if (MI.isIndirectBranch())
        return true;
      if (MI.isInlineAsm())
        return true;
      if (MI.isBarrier() && !MI.isBranch())
        return true;
      unsigned Opc = MI.getOpcode();
      // Child SET_HWLOOP / SET_HWLOOP_REG live in the child's preheader, which
      // is inside the outer loop's block set. When dual nesting is enabled the
      // outer may legally contain a child's hardware-loop setup — do not treat
      // those as invalid for the parent. Geometry
      // outer BEGIN < inner BEGIN < inner END < outer END
      // is validated later by FixupHwLoops range checks.
      if (Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG) {
        if (AllowChildHwloop)
          continue;
        return true;
      }
      // Role A: IR-level HardwareLoops already formed a loop
      // (LoopStart / PseudoLoopEnd / LoopDec / LoopJNZ). Skip post-RA
      // convert for the loop that owns those markers; when nesting is on
      // tolerate a child's markers so the outer can still convert.
      if (Opc == Haydn::LoopStart || Opc == Haydn::PseudoLoopEnd ||
          Opc == Haydn::LoopDec || Opc == Haydn::LoopJNZ) {
        if (AllowChildHwloop)
          continue;
        return true;
      }
    }
  }
  return false;
}

// Find the LAST def of \p Reg within \p MBB and return true iff it is an
// immediate materialization (ADDI32 Reg, R0, imm or LOADI32 Reg, imm).
// Sets \p ImmVal to the materialized constant.
// soundness: walks BACKWARDS so the LAST def wins, not the FIRST. The
// forward-scan form was unsound when Reg is redefined in the block — e.g.
// r5 = ADDI32 r0,-2 (stale, dead-after-store); r5 = LD32 sp,12 (real init).
// The forward scan returns -2; the backwards scan correctly sees the LD32
// last and returns false (non-immediate). This is the SAME class of bug as
// the / copy-init bug and the preheader-limit bug: the value at
// a program point is the LAST def, not the FIRST.
// Forward decl: isImmediateMaterialization is defined below; the LAST-def
// scan here uses it to test the most recent def in the block.
static bool isImmediateMaterialization(const MachineInstr &MI, Register Reg,
                                       int64_t &ImmVal);

static bool findImmediateDef(MachineBasicBlock *MBB, Register Reg,
                             int64_t &ImmVal) {
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    const MachineInstr &MI = *I;
    bool DefsReg = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
        DefsReg = true;
        break;
      }
    }
    if (!DefsReg)
      continue;
    // This is the LAST def of Reg in MBB — return iff it is an immediate.
    return isImmediateMaterialization(MI, Reg, ImmVal);
  }
  return false;
}

// Returns true if \p MI is an immediate materialization of \p Reg
// (ADDI32 Reg, R0, imm or LOADI32 Reg, imm). Sets \p ImmVal to the
// materialized constant.
static bool isImmediateMaterialization(const MachineInstr &MI, Register Reg,
                                       int64_t &ImmVal) {
  // ADDI32_W is the WIDE-only immediate form (RI20); the compact ADDI32 is
  // also accepted for legacy emission. Both have op0=dst, op1=src(R0), op2=imm.
  if ((MI.getOpcode() == Haydn::ADDI32 ||
       MI.getOpcode() == Haydn::ADDI32_W) &&
      MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
      MI.getOperand(1).getReg() == Haydn::R0 && MI.getOperand(2).isImm()) {
    ImmVal = MI.getOperand(2).getImm();
    return true;
  }
  if (MI.getOpcode() == Haydn::LOADI32 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isImm()) {
    ImmVal = MI.getOperand(1).getImm();
    return true;
  }
  return false;
}

// Program-point-aware immediate-def search. Walks backwards from \p BeforeMI
// (exclusive) within \p MBB and returns true only if the NEAREST prior def of
// \p Reg is an immediate materialization (ADDI32 Reg, R0, imm / LOADI32 Reg
// imm). Stops at the first prior def of \p Reg regardless of its form: if the
// nearest prior def is a non-immediate (e.g. a MOVE32 copy, an arithmetic
// op), it clobbers any older constant and \p Reg is not a proven constant at
// \p BeforeMI — return false. If no prior def exists, \p Reg is a block
// live-in / unknown value — return false (do NOT infer an immediate).
// SOUNDNESS (codex-confirmed): the value of a register live at a use
// point is its last def strictly before that point, or the block live-in if
// none. A LATER def in the same block cannot reach an earlier use (program
// order). The block-global `findImmediateDef` (forward / first-match) is
// unsound when the source hardreg of a copy is reused/clobbered later in the
// block — it finds the clobber instead of the live-in. This helper anchors
// the query at the actual use instruction and is the only sound form for
// resolving an IV-init value through a copy chain.
static bool findImmediateDefBefore(MachineBasicBlock *MBB, Register Reg,
                                   const MachineInstr *BeforeMI,
                                   int64_t &ImmVal, bool &FoundDef) {
  // program-point-aware def resolver. FoundDef distinguishes the two
  // false-return cases so the caller can decide whether to fall back to the
  // dominator-chain resolver:
  // FoundDef=false: no def of Reg in MBB before BeforeMI -> value is a
  // live-in from an ancestor block -> caller SHOULD fall back (dom chain).
  // FoundDef=true, return=false: the LAST def is non-immediate -> value at
  // BeforeMI is provably non-constant -> caller must NOT fall back (the
  // block-global resolvers would unsoundly pick up a stale earlier def).
  FoundDef = false;
  if (!MBB || !BeforeMI)
    return false;
  auto It = BeforeMI->getIterator();
  auto Begin = MBB->instr_begin();
  // Walk backwards from BeforeMI (exclusive) toward the block top.
  while (It != Begin) {
    --It;
    const MachineInstr &MI = *It;
    // Does this instruction define Reg? Stop here: it is the nearest prior def.
    bool DefsReg = false;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
        DefsReg = true;
        break;
      }
    }
    if (DefsReg) {
      FoundDef = true;
      return isImmediateMaterialization(MI, Reg, ImmVal);
    }
  }
  // No prior def of Reg in the block — it is a live-in / unknown value.
  return false;
}

// Convenience wrapper for callers that don't need FoundDef.
static bool findImmediateDefBefore(MachineBasicBlock *MBB, Register Reg,
                                   const MachineInstr *BeforeMI,
                                   int64_t &ImmVal) {
  bool FoundDef = false;
  return findImmediateDefBefore(MBB, Reg, BeforeMI, ImmVal, FoundDef);
}

// True if \p MI is a stack-relative 32-bit load of \p Reg from SP (R13).
// Logical LD32 only (: no durable LD32_S1 on MIR).
static bool isSpStackLoad32(const MachineInstr &MI, Register Reg, int &Off) {
  unsigned Opc = MI.getOpcode();
  if (Opc != Haydn::LD32)
    return false;
  if (!MI.getOperand(0).isReg() || MI.getOperand(0).getReg() != Reg)
    return false;
  if (!MI.getOperand(1).isReg() || MI.getOperand(1).getReg() != Haydn::R13)
    return false;
  if (!MI.getOperand(2).isImm())
    return false;
  Off = static_cast<int>(MI.getOperand(2).getImm());
  return true;
}

// Find the SP stack-slot offset that Reg is loaded from in MBB.
// Returns the offset, or -1 if no such load. Walks BACKWARDS so the LAST
// load of Reg wins (value at block end / at a later use is the last def).
static int findStackLoadOffset(MachineBasicBlock *MBB, Register Reg) {
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    int Off = 0;
    if (isSpStackLoad32(*I, Reg, Off))
      return Off;
  }
  return -1;
}

// Find the source register stored to a stack-slot offset in MBB via ST32.
// Returns the source register, or Register if no ST32 to that offset
// exists in MBB. Last store to the slot wins (same last-def discipline).
static Register findStackStoreSrc(MachineBasicBlock *MBB, int Offset) {
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    MachineInstr &MI = *I;
    if (MI.getOpcode() != Haydn::ST32)
      continue;
    if (MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Haydn::R13 &&
        MI.getOperand(2).isImm() &&
        static_cast<int>(MI.getOperand(2).getImm()) == Offset)
      return MI.getOperand(0).getReg();
  }
  return Register();
}

// Variant that also returns the ST32 program point via \p StoreMI. The store
// instruction is needed for program-point-aware resolution of the stored
// register: post-RA the allocator routinely reuses one physreg (e.g. $r1) for
// several constants in the same block, so resolving the stored register
// BLOCK-GLOBALLY (findImmediateDef, which returns the LAST def) picks the
// wrong constant. The value live AT the ST32 is the LAST def strictly BEFORE
// the ST32, resolved via findImmediateDefBefore. See.
static Register findStackStoreSrc(MachineBasicBlock *MBB, int Offset,
                                  MachineInstr *&StoreMI) {
  StoreMI = nullptr;
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    MachineInstr &MI = *I;
    if (MI.getOpcode() != Haydn::ST32)
      continue;
    if (MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Haydn::R13 &&
        MI.getOperand(2).isImm() &&
        static_cast<int>(MI.getOperand(2).getImm()) == Offset) {
      StoreMI = &MI;
      return MI.getOperand(0).getReg();
    }
  }
  return Register();
}

// Domain where a loop-invariant constant used inside \p L may be defined.
// Clean closed form (matches HiFi's model of trip/step as preheader facts):
// Dom*(Header) ∪ blocks(L)
// where Dom*(H) = { B | B dominates H } (IDom chain from H through entry).
// Why not just L + getLoopPreheader?
// createPreheaderForLoop inserts an EMPTY synthetic preheader; the real
// materialization (ADDI -1; ST32 sp,off) lives in the original preheader
// which is the IDom of the synthetic block. Searching only the empty
// preheader systematically misses every post-RA spilled step/limit and is
// why divide/CoreMark stay soft-branch after SMS Found (P9). Dom* is the
// unique correct domain for "values live-in to the loop".
static void collectLoopInvariantDefBlocks(
    const MachineLoop *L, const MachineDominatorTree *MDT,
    SmallVectorImpl<MachineBasicBlock *> &Out) {
  if (!L)
    return;
  SmallPtrSet<MachineBasicBlock *, 16> Seen;
  auto Add = [&](MachineBasicBlock *B) {
    if (B && Seen.insert(B).second)
      Out.push_back(B);
  };
  for (MachineBasicBlock *MBB : L->blocks())
    Add(MBB);
  MachineBasicBlock *H = L->getHeader();
  if (!H || !MDT)
    return;
  for (MachineDomTreeNode *N = MDT->getNode(H); N; N = N->getIDom())
    Add(N->getBlock());
}

// Forward decls used by resolveConstantAtUse (defined later). Defaults only
// on this first declaration.
static bool resolveToImmediateWithStack(
    MachineBasicBlock *MBB, Register Reg, int64_t &ImmVal,
    const SmallVectorImpl<MachineBasicBlock *> &SearchBlocks,
    unsigned Depth = 0, const MachineInstr *StoreMI = nullptr);
static bool findImmediateDefInLoop(const MachineLoop *L, Register Reg,
                                   int64_t &ImmVal,
                                   const MachineDominatorTree *MDT = nullptr);

// Resolve \p Reg to a compile-time constant **at program point** \p UseMI.
// Closed model for IV step (and reusable for limit/init). HiFi oracle
// (vec_divide32x32_fast): trip is a preheader fact then `loopnez`. Post-RA
// Haydn often keeps the same fact only as a stack spill:
// preheader: ADDI t,-1; ST32 t,sp,off
// latch: LD32 s,sp,off; ADD32 iv,iv,s
// Recovery must answer Val(s @ ADD), NOT "does s ever hold a constant on
// the preheader chain" — the same physreg is routinely reused (limit=0 at
// sp,16 loaded into s earlier, then step=-1 at sp,12 reloaded into s at
// the bump). Block-global preheader walks return the stale 0 (P9 root cause).
// Math (nearest prior def of R before Use in same BB):
// imm → that imm
// copy R←S → Val(S @ Def)
// LD R,sp,off → Val(stored @ dominating ST to off) domain Dom*(L)
// other def → ⊥ (proven non-constant)
// no def → live-in: Dom*∪L block search (findImmediateDefInLoop)
static bool resolveConstantAtUse(MachineInstr *UseMI, Register Reg,
                                 int64_t &ImmVal,
                                 const MachineDominatorTree *MDT,
                                 const MachineLoop *L, unsigned Depth = 0) {
  if (!UseMI || Depth > 8)
    return false;
  MachineBasicBlock *MBB = UseMI->getParent();
  if (!MBB)
    return false;

  // Nearest prior def of Reg in MBB (program-point, exclusive of Use).
  MachineInstr *DefMI = nullptr;
  {
    auto It = UseMI->getIterator();
    auto Begin = MBB->instr_begin();
    while (It != Begin) {
      --It;
      for (const MachineOperand &MO : It->operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
          DefMI = &*It;
          break;
        }
      }
      if (DefMI)
        break;
    }
  }

  if (!DefMI) {
    // Live-in to the bump block: loop-invariant domain.
    return findImmediateDefInLoop(L, Reg, ImmVal, MDT);
  }

  if (isImmediateMaterialization(*DefMI, Reg, ImmVal))
    return true;

  if (DefMI->getOpcode() == Haydn::MOVE32 && DefMI->getOperand(1).isReg())
    return resolveConstantAtUse(DefMI, DefMI->getOperand(1).getReg(), ImmVal,
                                MDT, L, Depth + 1);
  if (DefMI->getOpcode() == Haydn::OR32 && DefMI->getOperand(1).isReg() &&
      DefMI->getOperand(2).isReg() &&
      DefMI->getOperand(1).getReg() == DefMI->getOperand(2).getReg())
    return resolveConstantAtUse(DefMI, DefMI->getOperand(1).getReg(), ImmVal,
                                MDT, L, Depth + 1);

  int Off = 0;
  if (isSpStackLoad32(*DefMI, Reg, Off)) {
    SmallVector<MachineBasicBlock *, 16> Search;
    if (L)
      collectLoopInvariantDefBlocks(L, MDT, Search);
    else
      Search.push_back(MBB);
    MachineInstr *StoreMI = nullptr;
    Register SR = findStackStoreSrc(MBB, Off, StoreMI);
    if (SR.isValid() &&
        resolveToImmediateWithStack(MBB, SR, ImmVal, Search, Depth + 1,
                                    StoreMI))
      return true;
    for (MachineBasicBlock *SB : Search) {
      if (SB == MBB)
        continue;
      StoreMI = nullptr;
      SR = findStackStoreSrc(SB, Off, StoreMI);
      if (SR.isValid() &&
          resolveToImmediateWithStack(SB, SR, ImmVal, Search, Depth + 1,
                                      StoreMI))
        return true;
    }
    return false;
  }

  // Nearest def is non-constant — do not consult block-global resolvers.
  return false;
}

// Find the instruction that defines a physical register within the given MBB.
// For post-RA code, this walks backwards from the end of the block looking
// for the last def of the register.
static MachineInstr *findDefInBlock(MachineBasicBlock *MBB, Register Reg) {
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    for (const MachineOperand &MO : I->operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
        return &*I;
    }
  }
  return nullptr;
}

// Follow a register value through copy chains (OR32 rd, rs, rs) to find
// the original source register. Stops at the first non-copy definition.
// Returns the original source register and the defining instruction.
static Register followCopies(MachineBasicBlock *MBB, Register Reg,
                             MachineInstr *&DefMI) {
  DefMI = findDefInBlock(MBB, Reg);
  if (!DefMI)
    return Reg;

  // Follow OR32 rd, rs, rs (identity copy).
  if (DefMI->getOpcode() == Haydn::OR32 &&
      DefMI->getOperand(1).isReg() && DefMI->getOperand(2).isReg() &&
      DefMI->getOperand(1).getReg() == DefMI->getOperand(2).getReg()) {
    return DefMI->getOperand(1).getReg();
  }

  // Follow MOVE32 rd, rs (single-source copy).
  if (DefMI->getOpcode() == Haydn::MOVE32 &&
      DefMI->getOperand(1).isReg()) {
    return DefMI->getOperand(1).getReg();
  }

  return Reg;
}

// Resolve a register to its copy source if the register is the destination of
// an in-block copy (MOVE32 rd, rs or OR32 rd, rs, rs identity). Returns the
// source register, or \p Reg unchanged if no in-block copy def exists.
// Used by findImmediateDefChain / findImmediateDefOnDomChain to carry the
// resolved source register across the predecessor / dominator walk. Without
// this, a loop-invariant constant materialized as
// preheader: $r5 = MOVE32 $r3, $r3
// entry: $r3 = ADDI32 $r0, 0
// is never resolved to 0 when querying for $r5 in the entry block — the walker
// queries $r5 (not $r3) once it crosses the block boundary. See (GAP-2).
static Register resolveCopySourceInBlock(MachineBasicBlock *MBB, Register Reg) {
  MachineInstr *DefMI = findDefInBlock(MBB, Reg);
  if (!DefMI)
    return Reg;
  if (DefMI->getOpcode() == Haydn::OR32 &&
      DefMI->getOperand(1).isReg() && DefMI->getOperand(2).isReg() &&
      DefMI->getOperand(1).getReg() == DefMI->getOperand(2).getReg())
    return DefMI->getOperand(1).getReg();
  if (DefMI->getOpcode() == Haydn::MOVE32 && DefMI->getOperand(1).isReg())
    return DefMI->getOperand(1).getReg();
  return Reg;
}

// Resolve a register to an immediate value, following copy chains and
// stack-spill reloads. Post-RA register allocation spills constants to stack
// slots when register pressure is high (common in CoreMark / DSP kernels);
// this traces ST32 $rX, $r13, off → LD32 rd, $r13, off to recover the
// original immediate def of $rX. See (cause: stack-spilled step/init
// limit). \p SearchBlocks is the set of blocks to search for the
// corresponding ST32 (usually the function blocks reachable before the
// load — we search the loop blocks plus the preheader chain).
// \param StoreMI optional program point: when non-null, \p Reg is the source
// operand of an ST32 at \p StoreMI, and its value must be resolved AT that
// store (the def dominating the ST32 read), not block-globally. This is the
// fix — see the definition.
// (prototype already declared above for resolveConstantAtUse)

// Resolve through copy chains first, then stack loads.
static bool resolveToImmediate(MachineBasicBlock *MBB, Register Reg,
                               int64_t &ImmVal) {
  if (findImmediateDef(MBB, Reg, ImmVal))
    return true;

  // Follow copy chain and try again.
  MachineInstr *DefMI = nullptr;
  Register SrcReg = followCopies(MBB, Reg, DefMI);
  if (SrcReg != Reg)
    return findImmediateDef(MBB, SrcReg, ImmVal);

  return false;
}

static bool resolveToImmediateWithStack(
    MachineBasicBlock *MBB, Register Reg, int64_t &ImmVal,
    const SmallVectorImpl<MachineBasicBlock *> &SearchBlocks,
    unsigned Depth, const MachineInstr *StoreMI) {
  if (Depth > 8)  // Bound recursion to avoid pathological cycles.
    return false;

  // when StoreMI is provided, Reg is the source of an ST32 at StoreMI
  // and must be resolved AT that program point. Post-RA the allocator routinely
  // reuses one physreg for several constants in the same block, e.g.
  // $r1 = ADDI32 $r0, 64; ST32 $r1, $r13, 0; limit slot holds 64
  // $r1 = ADDI32 $r0, 255; ST32 $r1, $r13, 4; mask slot holds 255
  // A block-global findImmediateDef returns the LAST def of $r1 (255) for BOTH
  // slots, so the limit resolves to 255 -> trip count 255/255 = 1. The value
  // live at the ST32 is the LAST def strictly before that ST32, obtained via
  // findImmediateDefBefore (the same discipline applies to IV init/limit
  // in the preheader). A non-immediate def at the store is provably
  // non-constant and must NOT consult block-global resolvers.
  if (StoreMI) {
    bool FoundDef = false;
    if (findImmediateDefBefore(MBB, Reg, StoreMI, ImmVal, FoundDef))
      return true;
    if (FoundDef)
      return false; // Non-immediate def at the store -> provably non-constant.
    // No prior def in MBB -> Reg is a live-in here; fall through to the
    // block-global / stack-spill paths below (sound for live-ins).
  } else if (resolveToImmediate(MBB, Reg, ImmVal)) {
    return true;
  }

  // Try the stack-spill path: is Reg loaded from a stack slot in MBB?
  int Off = findStackLoadOffset(MBB, Reg);
  if (Off < 0)
    return false;

  // Find the corresponding ST32 in any search block and recurse with the
  // store's program point so the stored register is resolved AT that store.
  // Prefer MBB itself (intra-block spill), then the provided search blocks.
  MachineInstr *CurStoreMI = nullptr;
  Register StoredReg = findStackStoreSrc(MBB, Off, CurStoreMI);
  if (StoredReg.isValid() &&
      resolveToImmediateWithStack(MBB, StoredReg, ImmVal, SearchBlocks,
                                  Depth + 1, CurStoreMI))
    return true;
  for (MachineBasicBlock *SB : SearchBlocks) {
    if (SB == MBB)
      continue;
    CurStoreMI = nullptr;
    StoredReg = findStackStoreSrc(SB, Off, CurStoreMI);
    if (StoredReg.isValid() &&
        resolveToImmediateWithStack(SB, StoredReg, ImmVal, SearchBlocks,
                                    Depth + 1, CurStoreMI))
      return true;
  }
  return false;
}

// Search for an immediate definition of Reg by walking up the dominator chain
// from MBB. This is needed for nested loops where the step register may be
// defined in the outer loop's preheader rather than the inner loop's preheader.
// When MBB has multiple predecessors (e.g., it's a loop header), only follows
// the unique predecessor from outside the loop if identifiable.
// Also traces stack-spill reloads: when Reg is loaded from a stack slot in
// Cur, searches the whole function for the corresponding ST32 and recurses.
// Essential for post-RA spills under register pressure (CoreMark pattern).
// See.
static bool findImmediateDefChain(MachineBasicBlock *MBB, Register Reg,
                                  int64_t &ImmVal, unsigned MaxDepth = 4) {
  MachineBasicBlock *Cur = MBB;
  Register CurReg = Reg;
  for (unsigned I = 0; I < MaxDepth && Cur; ++I) {
    if (findImmediateDef(Cur, CurReg, ImmVal))
      return true;
    // Follow copy chain in this block.
    MachineInstr *DefMI = nullptr;
    Register SrcReg = followCopies(Cur, CurReg, DefMI);
    if (SrcReg != CurReg && findImmediateDef(Cur, SrcReg, ImmVal))
      return true;
    // Stack-spill path: trace LD32 $r13, off back to ST32 $rX, $r13, off.
    int Off = findStackLoadOffset(Cur, CurReg);
    if (Off >= 0) {
      // Search the whole function for the corresponding store. Post-RA
      // spills live in the same function; the store dominates the load.
      MachineFunction *MF = Cur->getParent();
      SmallVector<MachineBasicBlock *, 16> FnBlocks;
      for (MachineBasicBlock &MBB : *MF)
        FnBlocks.push_back(&MBB);
      // resolve the stored register AT the ST32 program point (see
      // resolveToImmediateWithStack). Block-global resolution of the stored
      // register is unsound when the allocator reuses it for several constants.
      MachineInstr *StoreMI = nullptr;
      Register SR = findStackStoreSrc(Cur, Off, StoreMI);
      if (SR.isValid() &&
          resolveToImmediateWithStack(Cur, SR, ImmVal, FnBlocks,
                                      /*Depth=*/0, StoreMI))
        return true;
      for (MachineBasicBlock *FB : FnBlocks) {
        if (FB == Cur)
          continue;
        StoreMI = nullptr;
        SR = findStackStoreSrc(FB, Off, StoreMI);
        if (SR.isValid() &&
            resolveToImmediateWithStack(FB, SR, ImmVal, FnBlocks,
                                        /*Depth=*/0, StoreMI))
          return true;
      }
      // Nearest def is a stack reload of a non-constant — stop. Do not walk
      // past it to an ancestor ADDI (stale physreg reuse).
      return false;
    }
    // Non-immediate, non-copy def in Cur kills any ancestor constant.
    // Example (divide peel): entry `r7=ADDI 1` then preheader `r7=ADD32 r9,r1`
    // walking past the ADD to the entry wrongly yields Init=1 and kills
    // Case-2 (step=-1,limit=0) trip = IV. See P9 / HiFi loopnez model.
    if (DefMI && SrcReg == CurReg) {
      // DefMI is the last def and is neither imm (checked above) nor copy
      // (SrcReg==CurReg means followCopies did not rewrite). Stop.
      return false;
    }
    if (!DefMI) {
      // No def of CurReg in this block — fall through to pred walk.
    } else if (SrcReg != CurReg) {
      // Copy: continue with source in this block already tried; walk pred
      // with Resolved source below.
    }
    // Resolve in-block copy BEFORE crossing the boundary so the predecessor
    // walk queries the actual source register, not the copy destination.
    // This is the GAP-2 fix: without it, `$r5 = MOVE32 $r3` in the preheader
    // followed by `$r3 = ADDI32 $r0, 0` in the entry is never resolved
    // because the walker keeps querying $r5 in the entry. See.
    Register Resolved = resolveCopySourceInBlock(Cur, CurReg);
    // Walk up to predecessor.
    MachineBasicBlock *Next = nullptr;
    if (Cur->pred_size() == 1) {
      Next = *Cur->pred_begin();
    } else if (Cur->pred_size() == 2 && Cur->isEHPad()) {
      // EH pad — don't follow.
      break;
    } else if (Cur->pred_size() >= 2) {
      // Multiple predecessors — likely a loop header. Try the unique
      // predecessor that is NOT a back-edge (not a successor of Cur).
      MachineBasicBlock *EntryPred = nullptr;
      for (MachineBasicBlock *Pred : Cur->predecessors()) {
        if (!Cur->isSuccessor(Pred)) {
          if (EntryPred) {
            EntryPred = nullptr; // Multiple entry predecessors, stop.
            break;
          }
          EntryPred = Pred;
        }
      }
      Next = EntryPred;
    } else {
      break;
    }
    if (!Next)
      break;
    Cur = Next;
    // Carry the resolved source register across the block boundary.
    if (Resolved != CurReg)
      CurReg = Resolved;
  }
  return false;
}

// Program-point-aware resolution of an IV init value through a copy chain
// (wrong-code fix). The IV is often established by a post-RA copy in the
// preheader or its predecessor chain:
// $r3 = MOVE32 $r1, $r1; IV $r3 = copy of the IV source $r1
// The IV's init value is the value of the SOURCE register ($r1) LIVE AT the
// copy instruction = the last def of $r1 strictly BEFORE the copy, or a block
// live-in if none. The block-global resolvers (findImmediateDefChain
// findImmediateDefOnDomChain) are unsound here: they scan whole blocks and
// will pick up a LATER def of $r1 (e.g. the register allocator reusing $r1
// for an accumulator init `$r1 = ADDI32 $r0, 0` later in the same block)
// wrongly concluding IVInit = 0 and producing a wrong trip count.
// This helper locates the IV's defining copy (MOVE32 / OR32-identity) by
// walking the preheader's single-predecessor chain, anchors an immediate-def
// search at the copy instruction via findImmediateDefBefore, and follows the
// copy source. The tri-state return tells the caller how to treat the result:
// ProvenConstant — the IV init is a PROVEN constant at the copy's program
// point. \p IVInit holds the value; InitIsImm is authoritatively true.
// ProvenNonConstant — a copy chain was found and the init could NOT be
// proven constant (the source is a live-in/unknown or a clobbered reg).
// InitIsImm is authoritatively FALSE; the block-global fallbacks MUST be
// skipped (they are unsound for copy-defined IVs). The caller then falls
// through to the runtime trip-count cases (e.g. pointer-IV Case 3).
// NotApplicable — no copy chain was found for the IV along the preheader
// chain (the IV is directly materialized or undefined here). The caller
// should fall back to the existing block-global resolvers, which are
// sound for non-copy IVs.
// Multi-hop copy chains ($r3 = MOVE32 $r5; $r5 = MOVE32 $r1) are followed by
// re-anchoring at each successive copy's source. See,.
enum class IVInitResolveResult { ProvenConstant, ProvenNonConstant, NotApplicable };

static IVInitResolveResult
findIVInitImmediate(MachineBasicBlock *Preheader, Register IVReg,
                    int64_t &IVInit) {
  if (!Preheader || !IVReg.isValid())
    return IVInitResolveResult::NotApplicable;

  MachineBasicBlock *Cur = Preheader;
  Register CurReg = IVReg;
  bool SawCopy = false; // Did we traverse at least one copy def of the IV?
  // Bounded copy-chain walk (matches the MaxDepth discipline elsewhere).
  for (unsigned Hop = 0; Hop < 8 && Cur; ++Hop) {
    // Find the nearest def of CurReg in Cur (walk backwards from block end).
    MachineInstr *CurDefMI = findDefInBlock(Cur, CurReg);
    if (!CurDefMI) {
      // No def of CurReg in this block. If we have already followed a copy
      // the source is a live-in here — its value is unknown (not a constant
      // unless proven by a dominating def, which the block-global resolvers
      // handle unsoundly across copies, so treat as non-constant). If we have
      // NOT seen a copy, this helper has nothing to anchor on — not applicable.
      if (SawCopy)
        return IVInitResolveResult::ProvenNonConstant;
      // Try to cross to the predecessor to find the IV's materialization.
      if (Cur->pred_size() == 1) {
        Cur = *Cur->pred_begin();
        continue;
      }
      return IVInitResolveResult::NotApplicable;
    }

    // If CurReg's defining instruction is itself an immediate materialization
    // the value is a proven constant at this point — done.
    if (isImmediateMaterialization(*CurDefMI, CurReg, IVInit))
      return IVInitResolveResult::ProvenConstant;

    // Is CurReg defined by a copy (MOVE32 rd, rs or OR32 rd, rs, rs)?
    Register CopySrc = Register();
    if (CurDefMI->getOpcode() == Haydn::MOVE32 &&
        CurDefMI->getOperand(1).isReg())
      CopySrc = CurDefMI->getOperand(1).getReg();
    else if (CurDefMI->getOpcode() == Haydn::OR32 &&
             CurDefMI->getOperand(1).isReg() && CurDefMI->getOperand(2).isReg() &&
             CurDefMI->getOperand(1).getReg() ==
                 CurDefMI->getOperand(2).getReg())
      CopySrc = CurDefMI->getOperand(1).getReg();

    if (CopySrc.isValid() && CopySrc != CurReg) {
      SawCopy = true;
      // The IV value is the value of CopySrc LIVE AT CurDefMI. Anchor the
      // immediate search at CurDefMI: find the nearest prior def of CopySrc
      // strictly before this copy (or none → live-in/unknown).
      if (findImmediateDefBefore(Cur, CopySrc, CurDefMI, IVInit))
        return IVInitResolveResult::ProvenConstant;
      // No immediate at the copy point. Re-anchor at the copy source's own
      // def in this block (it may itself be a copy of a constant), or cross
      // to the predecessor carrying CopySrc.
      CurReg = CopySrc;
      if (Cur->pred_size() == 1) {
        Cur = *Cur->pred_begin();
        continue;
      }
      // Multi-predecessor block: the source value is ambiguous — non-constant.
      return IVInitResolveResult::ProvenNonConstant;
    }

    // CurReg's nearest def is neither an immediate nor a copy. If we arrived
    // here via a copy chain, the source's defining op is non-constant. If not
    // the IV itself is not a copy and this helper is not applicable.
    return SawCopy ? IVInitResolveResult::ProvenNonConstant
                   : IVInitResolveResult::NotApplicable;
  }
  return SawCopy ? IVInitResolveResult::ProvenNonConstant
                 : IVInitResolveResult::NotApplicable;
}

// Resolve a loop-invariant immediate for \p Reg used inside \p L.
// HiFi oracle form for NatureDSP count loops (e.g. vec_divide32x32_fast):
// preheader: trip/step are facts (addi a15, N/2-1; loopnez)
// body: no soft branch on IV
// Post-RA Haydn often lowers the same math as:
// preheader/entry: ADDI step,-1; ST32 sp,off (or limit=0 spill)
// latch: LD32 tmp,sp,off; ADD32 iv,iv,tmp; SEQ/BEQZ
// so the constant is *not* in a register in the preheader at convert time
// it is a memory fact that dominates the loop.
// Search domain = Dom*(Header) ∪ blocks(L) (see collectLoopInvariantDefBlocks).
// Within that domain:
// 1. Immediate materialization of Reg in any block of the domain
// 2. Stack reload of Reg in a loop block → unique ST in the domain
// MDT may be null (legacy callers): falls back to loop blocks only.
static bool findImmediateDefInLoop(const MachineLoop *L, Register Reg,
                                   int64_t &ImmVal,
                                   const MachineDominatorTree *MDT) {
  if (!L)
    return false;
  SmallVector<MachineBasicBlock *, 16> Search;
  collectLoopInvariantDefBlocks(L, MDT, Search);
  // Prefer loop blocks first (Reg is typically defined by LD in the latch).
  for (MachineBasicBlock *MBB : L->blocks()) {
    if (findImmediateDef(MBB, Reg, ImmVal))
      return true;
    if (resolveToImmediateWithStack(MBB, Reg, ImmVal, Search, /*Depth=*/0))
      return true;
  }
  // Then dominating blocks: step still live in a physreg outside the loop.
  for (MachineBasicBlock *MBB : Search) {
    if (L->contains(MBB))
      continue;
    if (findImmediateDef(MBB, Reg, ImmVal))
      return true;
    if (resolveToImmediateWithStack(MBB, Reg, ImmVal, Search, /*Depth=*/0))
      return true;
  }
  return false;
}

// Walk the dominator chain from \p Start up to the function entry block
// searching each dominating block for an immediate definition of \p Reg.
// This is the fallback used when findImmediateDefChain (single-predecessor
// walk, which breaks at loop headers with multiple predecessors) and
// findImmediateDefInLoop (loop blocks only) both miss.
// Soundness: a loop-invariant constant's def dominates the loop (post-RA, a
// physreg holding a loop-invariant constant has exactly one dominating def).
// The dominator chain from the preheader to the entry reaches every block
// that dominates the preheader, which is exactly the set of blocks whose
// defs are available at the loop. See (root cause D) and (this fix).
// Safety: if \p Reg has a conflicting immediate def along the chain (two
// different constant values), the loop-invariant invariant is violated
// reject (return false) rather than guess. A non-immediate def (e.g. a
// MOVE32 copy) is silently skipped here; the single-predecessor chain walker
// already handles copy chains via followCopies, and the dominator walk is a
// last resort for the pure `ADDI32 rd, R0, imm` / `LOADI32 rd, imm` case.
static bool findImmediateDefOnDomChain(MachineBasicBlock *Start, Register Reg,
                                       int64_t &ImmVal,
                                       const MachineDominatorTree *MDT) {
  if (!Start || !MDT)
    return false;

  // Accumulate the unique immediate value seen along the chain. If two
  // different constants are found, reject (ambiguous — not loop-invariant).
  std::optional<int64_t> SeenVal;

  // Collect function blocks once for the stack-spill trace.
  MachineFunction *MF = Start->getParent();
  SmallVector<MachineBasicBlock *, 16> FnBlocks;
  for (MachineBasicBlock &MBB : *MF)
    FnBlocks.push_back(&MBB);

  Register CurReg = Reg;
  for (MachineDomTreeNode *Node = MDT->getNode(Start); Node;
       Node = Node->getIDom()) {
    MachineBasicBlock *MBB = Node->getBlock();
    int64_t Val = 0;
    if (findImmediateDef(MBB, CurReg, Val)) {
      if (SeenVal && *SeenVal != Val)
        return false; // Conflicting defs — reject for safety.
      SeenVal = Val;
      // Nearest imm def on the IDom walk is the live value — done.
      ImmVal = Val;
      return true;
    }
    // Stack-spill path: trace LD32 $r13, off back to ST32 $rX, $r13, off.
    // Post-RA spills can land anywhere along the dom chain. : resolve the
    // stored register AT the ST32 program point (pass StoreMI), not block
    // globally.
    int Off = findStackLoadOffset(MBB, CurReg);
    if (Off >= 0) {
      MachineInstr *StoreMI = nullptr;
      Register SR = findStackStoreSrc(MBB, Off, StoreMI);
      if (SR.isValid() &&
          resolveToImmediateWithStack(MBB, SR, Val, FnBlocks, /*Depth=*/0,
                                      StoreMI)) {
        ImmVal = Val;
        return true;
      }
      for (MachineBasicBlock *FB : FnBlocks) {
        if (FB == MBB)
          continue;
        StoreMI = nullptr;
        SR = findStackStoreSrc(FB, Off, StoreMI);
        if (SR.isValid() &&
            resolveToImmediateWithStack(FB, SR, Val, FnBlocks, /*Depth=*/0,
                                        StoreMI)) {
          ImmVal = Val;
          return true;
        }
      }
      // Reload of non-constant — stop (do not walk past to a stale ADDI).
      return false;
    }
    // Non-imm last def kills ancestor constants (P9 / discipline).
    MachineInstr *DefMI = findDefInBlock(MBB, CurReg);
    if (DefMI) {
      Register Resolved = resolveCopySourceInBlock(MBB, CurReg);
      if (Resolved != CurReg) {
        if (findImmediateDef(MBB, Resolved, Val)) {
          ImmVal = Val;
          return true;
        }
        CurReg = Resolved;
        // Continue IDom walk with copy source.
      } else {
        // Non-copy non-imm def (e.g. ADD32 of runtime IV) — not constant.
        return false;
      }
    }
    if (MBB == &MF->front())
      break; // Reached entry — dominator chain ends.
  }

  if (SeenVal) {
    ImmVal = *SeenVal;
    return true;
  }
  return false;
}

// MLI-scoped variant of findImmediateDefOnDomChain. Identical walk, but skips
// any dominating block that belongs to a SIBLING loop — i.e. a loop that is
// not \p ThisLoop itself and not an ancestor of \p ThisLoop. This fixes the
// dominant multi-loop resolver defeat (G1 §0.3, §4 #2/#3): in real
// NatureDSP kernels several loops share the same physical register for their
// step/init/limit constants, and each sibling loop's preheader redefines that
// register (often with a DIFFERENT value, e.g. loop A sets r8=0, loop B sets
// r8=N). The unscoped walk sees both defs along the dominator chain, flags a
// "conflicting constants" rejection, and the loop bails with "Cannot determine
// trip count" even though the loop-invariant constant for THIS loop is
// unambiguous. The scoped walk only considers blocks whose loop membership is
// null (function-level, e.g. the entry block where hoisted constants live) or
// a loop that contains ThisLoop (the constant is available at ThisLoop).
// Predicate: a block B is in-scope iff
// MLI->getLoopFor(B) == null || MLI->getLoopFor(B)->contains(ThisLoop).
// Returns false (no resolution) rather than walking into a sibling block, so
// the caller can fall back to the unscoped walk or the loop-body search.
static bool findImmediateDefOnDomChainScoped(MachineBasicBlock *Start,
                                             Register Reg, int64_t &ImmVal,
                                             const MachineDominatorTree *MDT,
                                             const MachineLoopInfo *MLI,
                                             const MachineLoop *ThisLoop) {
  if (!Start || !MDT || !MLI || !ThisLoop)
    return false;

  std::optional<int64_t> SeenVal;

  MachineFunction *MF = Start->getParent();
  SmallVector<MachineBasicBlock *, 16> FnBlocks;
  for (MachineBasicBlock &MBB : *MF)
    FnBlocks.push_back(&MBB);

  Register CurReg = Reg;
  for (MachineDomTreeNode *Node = MDT->getNode(Start); Node;
       Node = Node->getIDom()) {
    MachineBasicBlock *MBB = Node->getBlock();
    // Skip sibling-loop blocks: their physreg defs belong to a different loop
    // and are not the loop-invariant value reaching ThisLoop. (An ancestor
    // loop's block is kept — the ancestor dominates ThisLoop and the value is
    // available.) Note we still must resolve in-block copies / spills below
    // even for in-scope blocks, so the filter is applied per-block here.
    MachineLoop *BlockLoop = MLI->getLoopFor(MBB);
    if (BlockLoop && !BlockLoop->contains(ThisLoop)) {
      // Carry the resolved copy source across the skipped boundary so a copy
      // chain that enters the sibling block is not silently lost.
      Register Resolved = resolveCopySourceInBlock(MBB, CurReg);
      if (Resolved != CurReg)
        CurReg = Resolved;
      if (MBB == &MF->front())
        break;
      continue;
    }

    int64_t Val = 0;
    if (findImmediateDef(MBB, CurReg, Val)) {
      ImmVal = Val;
      return true; // Nearest imm on IDom walk (P9).
    }
    int Off = findStackLoadOffset(MBB, CurReg);
    if (Off >= 0) {
      MachineInstr *StoreMI = nullptr;
      Register SR = findStackStoreSrc(MBB, Off, StoreMI);
      if (SR.isValid() &&
          resolveToImmediateWithStack(MBB, SR, Val, FnBlocks, /*Depth=*/0,
                                      StoreMI)) {
        ImmVal = Val;
        return true;
      }
      for (MachineBasicBlock *FB : FnBlocks) {
        if (FB == MBB)
          continue;
        StoreMI = nullptr;
        SR = findStackStoreSrc(FB, Off, StoreMI);
        if (SR.isValid() &&
            resolveToImmediateWithStack(FB, SR, Val, FnBlocks, /*Depth=*/0,
                                        StoreMI)) {
          ImmVal = Val;
          return true;
        }
      }
      return false; // Reload of non-constant.
    }
    MachineInstr *DefMI = findDefInBlock(MBB, CurReg);
    if (DefMI) {
      Register Resolved = resolveCopySourceInBlock(MBB, CurReg);
      if (Resolved != CurReg) {
        if (findImmediateDef(MBB, Resolved, Val)) {
          ImmVal = Val;
          return true;
        }
        CurReg = Resolved;
      } else {
        return false; // Non-copy non-imm def kills ancestor constants.
      }
    }
    if (MBB == &MF->front())
      break;
  }

  if (SeenVal) {
    ImmVal = *SeenVal;
    return true;
  }
  return false;
}

// Return the IV-bump instruction if \p MI is a recognized bump of \p Reg.
// For ADD32/SUB32/ADDI32, Reg must be the destination (operand 0) AND a source
// (self-referencing bump: iv = iv + step). For LD32_POST/LD64_POST, Reg must be
// the base-writeback (operand 1). Returns nullptr if MI is not a bump of Reg.
// The self-reference requirement (operand 1 or 2 == Reg for ADD32/SUB32;
// operand 1 == Reg for ADDI32) distinguishes a true IV bump
// (`$r1 = ADDI32 $r1, 4`) from a derived-temp computation
// (`$r4 = ADDI32 $r1, 4`). Without this, the latter would match as a "bump of
// $r4" and misidentify $r4 (a temp) as the IV. See (copy-following).
static MachineInstr *matchIVBump(MachineInstr &MI, Register Reg) {
  unsigned Opc = MI.getOpcode();
  if (Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) {
    // $rX = ADDI32 $rX, imm — operand 0 (dest) and operand 1 (src) must both
    // be Reg (self-bump).
    if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
        MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Reg)
      return &MI;
    return nullptr;
  }
  if (Opc == Haydn::ADD32 || Opc == Haydn::SUB32) {
    // $rX = ADD32 $rX, step or $rX = ADD32 step, $rX — operand 0 (dest) and
    // one of the source operands must be Reg (self-bump).
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        MI.getOperand(0).getReg() != Reg)
      return nullptr;
    bool Src1IsReg = MI.getOperand(1).isReg() && MI.getOperand(1).getReg() == Reg;
    bool Src2IsReg = MI.getOperand(2).isReg() && MI.getOperand(2).getReg() == Reg;
    if (Src1IsReg || Src2IsReg)
      return &MI;
    return nullptr;
  }
  if (Opc == Haydn::LD32_POST || Opc == Haydn::LD64_POST ||
      Opc == Haydn::S_LW_POST_IMM || Opc == Haydn::D_LDW_POST_IMM) {
    // Pointer-IV: base-writeback (operand 1). Guard operand count.
    // the DB-named S_LW_POST_IMM (scalar i32 -> GPR32)
    // D_LDW_POST_IMM (i64 -> DR64) have the same operand layout as the
    // LD32_POST / LD64_POST aliases and are now emitted by
    // HaydnExpandPostIncEarly as the canonical fused post-inc form.
    if (MI.getNumOperands() >= 2 && MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Reg)
      return &MI;
    return nullptr;
  }
  return nullptr;
}

// Find an IV-bump instruction (ADD32/SUB32/ADDI32, or LD32_POST/LD64_POST
// for pointer-IV loops) that defines \p Reg anywhere in the loop's blocks
// preferring the latch (the canonical IV-bump location) but falling back to a
// body block when the bump lives there. Multi-BB loops often bump the IV in a
// body block, not the latch. See (cause #3).
// For scalar integer IVs, only ADD32/SUB32/ADDI32 defs qualify — these are
// the only opcodes extractIVBump understands. A def by another opcode
// (MOVE32, LD32, etc.) is NOT an IV bump and must be rejected, otherwise the
// caller would misclassify a non-IV operand as the IV.
// For pointer-IV loops (streaming DSP kernels), LD32_POST/LD64_POST are also
// recognized: their base-writeback operand (operand 1, $rs_wb) is the pointer
// being bumped. The 4-operand layout is [$rt(def), $rs_wb(def), $rs(tied use)
// $scaled_imm(imm)]; the byte stride is $scaled_imm (operand 3) shifted left
// by 2 (LD32) or 3 (LD64). See (GAP-3) and (operand-index fix).
// Copy-following : when the direct def of \p Reg is a MOVE32 copy from
// a source register, and that source has a recognized bump, the bump is
// returned and \p RealIVOut is set to the copy source (the loop-carried IV).
// This handles the post-RA shape where the IV-bump is computed into a temp
// then copied to the IV:
// $r4 = ADDI32 $r1, 4; bump computed into temp
// $r1 = LD32 killed $r1, 0; (load clobbers IV transiently)
// $r1 = MOVE32 killed $r4; copy temp → IV (the IV def is a copy)
// Here the compare uses $r4 (next-ptr), but the loop-carried IV is $r1.
// Without copy-following, findIVBumpInLoop($r4) returns null because the def
// of $r4 is ADDI32 (not a "bump of $r4" in the IV sense), and the IV $r1's
// def is a MOVE32 (rejected). Copy-following recognizes $r4 = ADDI32 $r1, imm
// and returns that ADDI32 as the bump, with RealIVOut = $r1.
static MachineInstr *findIVBumpInLoop(const MachineLoop *L, Register Reg,
                                       const MachineBasicBlock *Latch,
                                       Register *RealIVOut = nullptr) {
  if (RealIVOut)
    *RealIVOut = Reg;
  if (!L)
    return nullptr;
  // Prefer the latch (matches existing behavior for single-BB loops).
  // findDefInBlock returns the LAST def in program order; verify it is an
  // ADD32/SUB32/ADDI32 before accepting (a later non-bump def would shadow
  // the bump, meaning Reg is not a simple IV).
  if (Latch) {
    if (MachineInstr *MI = findDefInBlock(
            const_cast<MachineBasicBlock *>(Latch), Reg)) {
      if (MachineInstr *Bump = matchIVBump(*MI, Reg))
        return Bump;
    }
  }
  // Fall back: search all loop blocks. Return the first ADD32/SUB32/ADDI32 or
  // LD32_POST/LD64_POST def of Reg.
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    if (MBB == Latch)
      continue;
    for (MachineInstr &MI : *MBB) {
      if (MachineInstr *Bump = matchIVBump(MI, Reg))
        return Bump;
    }
  }

  // Copy-following fallback : the direct def of Reg may be a MOVE32
  // copy from a source register that itself is bumped, OR Reg may be a
  // derived temp whose source is the loop-carried IV. This handles the
  // post-RA "bump-via-copy-back" shape:
  // $r4 = ADDI32 $r1, 4; bump computed into temp from IV
  // $r1 = LD32 killed $r1, 0; (load transiently clobbers IV)
  // $r1 = MOVE32 killed $r4; copy temp back → IV (net: r1 += 4)
  // The compare uses $r4 (next-ptr); the loop-carried IV is $r1. Without
  // copy-following, findIVBumpInLoop($r4) returns null (the ADDI32's source
  // $r1 ≠ $r4, so matchIVBump's self-reference check fails), and
  // findIVBumpInLoop($r1) returns null ($r1's def is a MOVE32, not a bump).
  //
  // Two sub-patterns are recognized:
  // (a) Reg is defined by ADDI32/ADD32 from SrcReg, AND SrcReg is later
  // restored from Reg via MOVE32 (the circular copy-back). The IV is
  // SrcReg; the bump is the ADDI32/ADD32.
  // (b) Reg is defined by MOVE32 from SrcReg, AND SrcReg is defined by
  // ADDI32/ADD32 from Reg (reverse direction — same cycle). The IV is
  // Reg; the bump is the ADDI32/ADD32 defining SrcReg.
  //
  // Collect all loop blocks for the cross-instruction scan.
  SmallVector<MachineBasicBlock *, 8> SearchBlocks;
  if (Latch)
    SearchBlocks.push_back(const_cast<MachineBasicBlock *>(Latch));
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    if (MBB != Latch)
      SearchBlocks.push_back(MBB);
  }

  // Helper: find a copy def of DstReg (returns the source reg). Recognizes both
  // MOVE32 (single-source copy) and OR32 rd, rs, rs (identity copy), plus a
  // spill/reload pair: ST32 SrcReg, R13, Off followed by LD32 DstReg, R13, Off
  // is a copy edge (post-RA spills of the IV are common in register-pressured
  // loops). See (GAP-B extension).
  auto FindCopyFrom = [&](Register DstReg,
                          Register &SrcOut) -> bool {
    for (MachineBasicBlock *MBB : SearchBlocks) {
      for (MachineInstr &MI : *MBB) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::MOVE32) {
          if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
              MI.getOperand(0).getReg() != DstReg || !MI.getOperand(1).isReg())
            continue;
          SrcOut = MI.getOperand(1).getReg();
          return true;
        }
        if (Opc == Haydn::OR32) {
          // OR32 rd, rs, rs identity copy.
          if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
              MI.getOperand(0).getReg() != DstReg || !MI.getOperand(1).isReg() ||
              !MI.getOperand(2).isReg() ||
              MI.getOperand(1).getReg() != MI.getOperand(2).getReg())
            continue;
          SrcOut = MI.getOperand(1).getReg();
          return true;
        }
      }
    }
    // Spill/reload copy edge: LD32 DstReg, R13, Off where some ST32 SrcReg
    // R13, Off exists in the loop. Scan every loop block for the reload, then
    // look for a matching store of a different register (post-RA spills of the
    // IV are common in register-pressured loops). See (GAP-B extension).
    for (MachineBasicBlock *MBB : SearchBlocks) {
      for (MachineInstr &MI : *MBB) {
        if (MI.getOpcode() != Haydn::LD32 || MI.getNumOperands() < 3 ||
            !MI.getOperand(0).isReg() || MI.getOperand(0).getReg() != DstReg)
          continue;
        if (!MI.getOperand(1).isReg() || MI.getOperand(1).getReg() != Haydn::R13 ||
            !MI.getOperand(2).isImm())
          continue;
        int ThisOff = static_cast<int>(MI.getOperand(2).getImm());
        for (MachineBasicBlock *SB : SearchBlocks) {
          Register SR = findStackStoreSrc(SB, ThisOff);
          if (SR.isValid() && SR != DstReg) {
            SrcOut = SR;
            return true;
          }
        }
      }
    }
    return false;
  };

  // Helper: find an ADDI32/ADD32 def of DstReg where operand(1) is FromReg
  // (returns the instruction).
  auto FindArithFrom = [&](Register DstReg,
                           Register FromReg) -> MachineInstr * {
    for (MachineBasicBlock *MBB : SearchBlocks) {
      for (MachineInstr &MI : *MBB) {
        unsigned Opc = MI.getOpcode();
        if (Opc != Haydn::ADDI32 && Opc != Haydn::ADDI32_W &&
            Opc != Haydn::ADD32)
          continue;
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            MI.getOperand(0).getReg() != DstReg)
          continue;
        if (!MI.getOperand(1).isReg() || MI.getOperand(1).getReg() != FromReg)
          continue;
        return &MI;
      }
    }
    return nullptr;
  };

  // Sub-pattern (a): Reg = ADDI32/ADD32 SrcReg,...; SrcReg = MOVE32 Reg.
  // The IV is SrcReg (loop-carried via the copy-back).
  for (MachineBasicBlock *MBB : SearchBlocks) {
    for (MachineInstr &MI : *MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc != Haydn::ADDI32 && Opc != Haydn::ADDI32_W &&
          Opc != Haydn::ADD32)
        continue;
      if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
          MI.getOperand(0).getReg() != Reg)
        continue;
      if (!MI.getOperand(1).isReg())
        continue;
      Register SrcReg = MI.getOperand(1).getReg();
      if (SrcReg == Reg || SrcReg == Haydn::R0)
        continue;
      // Check copy-back: SrcReg = MOVE32 Reg (the IV is restored from Reg).
      Register CopySrc;
      if (FindCopyFrom(SrcReg, CopySrc) && CopySrc == Reg) {
        if (RealIVOut)
          *RealIVOut = SrcReg;
        return &MI;
      }
    }
  }

  // Sub-pattern (b): Reg is copied (possibly via a multi-hop MOVE32/OR32 +
  // spill/reload chain) to a register that is then defined by ADDI32/ADD32
  // whose source traces back to Reg. The IV is Reg (loop-carried); the bump is
  // the arithmetic def. Multi-hop broadening (GAP-B): the single-hop
  // walk missed post-RA shapes like
  // $r1 = MOVE32 $r5; $r5 = MOVE32 $r9; $r9 = ADDI32 $r1, 4
  // and spill/reload chains
  // $r1 = LD32 R13,off; ST32 $r4, R13,off; $r4 = ADDI32 $r1, 4.
  // Walk up to MaxCopyHops copy edges; at each hop's source, look for an
  // arithmetic def whose operand(1) is Reg.
  constexpr unsigned MaxCopyHops = 4;
  {
    SmallSet<Register, 8> Visited;
    Register Cur = Reg;
    for (unsigned Hop = 0; Hop < MaxCopyHops; ++Hop) {
      Register CopySrc;
      if (!FindCopyFrom(Cur, CopySrc) || CopySrc == Cur || CopySrc == Haydn::R0)
        break;
      if (!Visited.insert(CopySrc).second)
        break; // Cycle guard.
      // Does CopySrc get its value from an arithmetic def of Reg? The
      // arithmetic source must be Reg itself (the loop-carried IV), not an
      // intermediate copy, so the IV remains Reg.
      if (MachineInstr *Arith = FindArithFrom(CopySrc, Reg)) {
        if (RealIVOut)
          *RealIVOut = Reg;
        return Arith;
      }
      Cur = CopySrc;
    }
  }

  return nullptr;
}

// Classify the comparison sense of a two-register branch opcode.
// Returns true if the branch tests "lhs < rhs" (LT sense)
// false if it tests "lhs >= rhs" (GE sense).
// For equality branches, returns true for NE and false for EQ.
// Both legacy 32-bit and WIDE `_W` forms are recognized — the comparison
// sense is identical for a given mnemonic regardless of width (Phase 1b
// follow-up: CodeGen selects `_W`).
static bool isBranchLT(unsigned BrOpc) {
  switch (BrOpc) {
  case Haydn::BLT:
  case Haydn::BLT_W:
  case Haydn::BLTU:
  case Haydn::BLTU_W:
  case Haydn::BNE:
  case Haydn::BNE_W:
    return true;
  case Haydn::BGE:
  case Haydn::BGE_W:
  case Haydn::BGEU:
  case Haydn::BGEU_W:
  case Haydn::BEQ:
  case Haydn::BEQ_W:
    return false;
  default:
    return true; // Default assumption
  }
}

// Check if a branch opcode tests equality (BEQ/BNE). An EQ-sense latch branch
// (`beq iv, limit` / `bne iv, limit`) indicates a count-up loop "while
// iv != limit", which has the SAME trip-count formula as LT sense
// (Limit-Init)/Step — NOT the GE count-down formula. EQ sense is symmetric
// under target inversion (NE == NOT EQ). See.
static bool isBranchEQ(unsigned BrOpc) {
  switch (BrOpc) {
  case Haydn::BEQ:
  case Haydn::BEQ_W:
  case Haydn::BNE:
  case Haydn::BNE_W:
    return true;
  default:
    return false;
  }
}

// Check if a branch opcode is a fused two-register branch (produced by
// the ConditionOptimizer's cmp+branch folding).
static bool isFusedTwoRegBranch(unsigned BrOpc) {
  switch (BrOpc) {
  case Haydn::BLT:
  case Haydn::BLT_W:
  case Haydn::BLTU:
  case Haydn::BLTU_W:
  case Haydn::BGE:
  case Haydn::BGE_W:
  case Haydn::BGEU:
  case Haydn::BGEU_W:
  case Haydn::BEQ:
  case Haydn::BEQ_W:
  case Haydn::BNE:
  case Haydn::BNE_W:
    return true;
  default:
    return false;
  }
}

// Check if a branch opcode is a single-register branch (BEQZ, BNEZ, etc.)
// that tests a comparison result from a separate comparison instruction.
static bool isSingleRegBranch(unsigned BrOpc) {
  switch (BrOpc) {
  case Haydn::BEQZ:
  case Haydn::BEQZ_W:
  case Haydn::BNEZ:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ:
  case Haydn::BLTZ_W:
    return true;
  default:
    return false;
  }
}

// Extract the IV bump value from an ADD32/SUB32/ADDI32 instruction.
// Returns the bump value (positive for count-up, negative for count-down)
// or 0 if the bump cannot be determined.
// The step register is resolved in this priority order:
// 1. ADDI32 imm operand (immediate in the instruction).
// 2. findImmediateDefChain(Preheader, StepReg) — the original path, covers
// step materialized in the preheader or a single-predecessor ancestor
// block. BREAKS at a loop header with multiple predecessors.
// 3. findImmediateDefInLoop(L, StepReg) — step materialized anywhere in
// the loop body (e.g. `$r14 = ADDI32 $r0, -1` inside the loop, then
// `$r1 = ADD32 $r1, $r14`). The most common -O2 form. See
// (cause #1).
// 4. findImmediateDefOnDomChain(Preheader, StepReg, MDT) — walks the
// dominator chain from the preheader up to the function entry, reaching
// blocks that dominate the loop but are unreachable via the single-pred
// walker (#2). Fixes the dominant CoreMark/DSP pattern where the step
// constant is materialized ONCE in the entry block (e.g.
// `entry: $r11 = ADDI32 $r0, -1;... loop: $r7 = ADD32 $r7, $r11`).
// See (root cause) and (this fix).
static int64_t extractIVBump(MachineInstr *BumpMI, Register IVReg,
                             MachineBasicBlock *Preheader,
                             const MachineLoop *L,
                             const MachineDominatorTree *MDT,
                             const MachineLoopInfo *MLI) {
  unsigned BumpOpc = BumpMI->getOpcode();

  if (BumpOpc == Haydn::ADD32) {
    if (!BumpMI->getOperand(1).isReg() || !BumpMI->getOperand(2).isReg())
      return 0;

    Register StepReg = BumpMI->getOperand(2).getReg();

    // Verify the IV is also a source operand (iv = iv + step).
    if (BumpMI->getOperand(1).getReg() != IVReg) {
      // Try operand swap: step could be first.
      if (BumpMI->getOperand(2).getReg() == IVReg) {
        StepReg = BumpMI->getOperand(1).getReg();
      } else {
        return 0;
      }
    }

    // Resolution order (closed, program-point first — HiFi-competitive):
    // 1. Val(StepReg @ BumpMI) — only sound query under physreg reuse.
    // 2. Preheader chain / Dom chain — step still live in a physreg with
    // no local def at the bump (reject 0: zero step is not a loop).
    int64_t StepVal = 0;
    if (resolveConstantAtUse(BumpMI, StepReg, StepVal, MDT, L)) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: step@bump = " << StepVal
                        << " reg=" << printReg(StepReg) << "\n");
      return StepVal;
    }
    if (findImmediateDefChain(Preheader, StepReg, StepVal) && StepVal != 0)
      return StepVal;
    if (MLI && findImmediateDefOnDomChainScoped(Preheader, StepReg, StepVal,
                                                MDT, MLI, L) &&
        StepVal != 0)
      return StepVal;
    if (findImmediateDefOnDomChain(Preheader, StepReg, StepVal, MDT) &&
        StepVal != 0)
      return StepVal;
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: step unresolved reg="
                      << printReg(StepReg) << "\n");
    return 0;
  }

  if (BumpOpc == Haydn::SUB32) {
    if (!BumpMI->getOperand(2).isReg())
      return 0;
    Register StepReg = BumpMI->getOperand(2).getReg();

    int64_t StepVal = 0;
    if (resolveConstantAtUse(BumpMI, StepReg, StepVal, MDT, L))
      return -StepVal;
    if (findImmediateDefChain(Preheader, StepReg, StepVal) && StepVal != 0)
      return -StepVal;
    if (MLI && findImmediateDefOnDomChainScoped(Preheader, StepReg, StepVal,
                                                MDT, MLI, L) &&
        StepVal != 0)
      return -StepVal;
    if (findImmediateDefOnDomChain(Preheader, StepReg, StepVal, MDT) &&
        StepVal != 0)
      return -StepVal;
    return 0;
  }

  // ADDI32/ADDI32_W iv, imm — immediate increment (iv = iv + imm).
  if (BumpOpc == Haydn::ADDI32 || BumpOpc == Haydn::ADDI32_W) {
    if (!BumpMI->getOperand(1).isReg() || !BumpMI->getOperand(2).isImm())
      return 0;
    if (BumpMI->getOperand(1).getReg() != IVReg)
      return 0;
    return BumpMI->getOperand(2).getImm();
  }

  // LD32_POST / LD64_POST — pointer-IV bump via post-increment load.
  // Real-instruction operand layout (see HaydnInstrInfo.td LD32_POST / LD64_POST
  // and HaydnExpandPostIncEarly.cpp:124-128 which builds the 4-operand form):
  // operand 0 = $rt (data destination, def)
  // operand 1 = $rs_wb (base writeback, def — tied to $rs)
  // operand 2 = $rs (input base, tied use)
  // operand 3 = $scaled_imm (imm6 element index)
  // The base-writeback (operand 1) is the pointer IV; the byte stride is
  // $scaled_imm << 2 (LD32, 4 bytes/elem) or << 3 (LD64, 8 bytes/elem).
  // Returns the byte stride (always positive — post-inc loops step forward).
  // The caller treats this as a count-up IV in BYTES, so the trip formula
  // (limit - init) / step works with limit/init as byte addresses. See.
  //
  // Bug fixed here (GAP-3): the previous code read the imm at operand 2
  // which is actually the tied $rs register — so extractIVBump always returned
  // 0 for a LD32_POST bump, the pass printed "Cannot determine IV step", and
  // every pointer-IV streaming loop stayed on a BLTU/BLT back-edge. The imm is
  // at operand index 3.
  if (BumpOpc == Haydn::LD32_POST || BumpOpc == Haydn::LD64_POST ||
      BumpOpc == Haydn::S_LW_POST_IMM || BumpOpc == Haydn::D_LDW_POST_IMM) {
    if (BumpMI->getNumOperands() < 4 ||
        !BumpMI->getOperand(1).isReg() || !BumpMI->getOperand(3).isImm())
      return 0;
    if (BumpMI->getOperand(1).getReg() != IVReg)
      return 0;
    int64_t ElemStride = BumpMI->getOperand(3).getImm();
    if (ElemStride <= 0)
      return 0; // Only forward strides (post-increment).
    // D_LDW_POST_IMM / LD64_POST are doubleword (<<3); the scalar word
    // forms (S_LW_POST_IMM / LD32_POST) are <<2.
    bool IsDw = (BumpOpc == Haydn::LD64_POST ||
                 BumpOpc == Haydn::D_LDW_POST_IMM);
    int64_t ByteStride = ElemStride << (IsDw ? 3 : 2);
    return ByteStride;
  }

  return 0;
}

// Identify the IV and limit registers from a fused two-register branch.
// For a branch BLT rs1, rs2, target:
// The IV is the operand that is modified (bumped) within the loop.
// The limit is the operand that stays constant.
// Returns true if IV and limit were identified.
// The bump is searched first in the latch (canonical location) and then in
// the loop body via findIVBumpInLoop. Multi-BB loops often bump the IV in a
// body block, not the latch. See (cause #3).
static bool identifyIVFromFusedBranch(const MachineLoop *L,
                                      MachineBasicBlock *Latch,
                                      Register BrSrc1, Register BrSrc2,
                                      Register &IVReg, Register &LimitReg,
                                      MachineInstr *&BumpMI) {
  // Try each operand as potential IV — look for ADD32/SUB32/ADDI32 def.
  // RealIV captures the loop-carried IV when copy-following fires :
  // the compare operand may be a derived temp ($r4 = ADDI32 $r1, 4) while
  // the real loop-carried IV is $r1.
  Register RealIV1, RealIV2;
  MachineInstr *Def1 = findIVBumpInLoop(L, BrSrc1, Latch, &RealIV1);
  MachineInstr *Def2 = findIVBumpInLoop(L, BrSrc2, Latch, &RealIV2);

  bool Src1IsIV = Def1 != nullptr;
  bool Src2IsIV = Def2 != nullptr;

  if (Src1IsIV && !Src2IsIV) {
    IVReg = RealIV1.isValid() ? RealIV1 : BrSrc1;
    LimitReg = BrSrc2;
    BumpMI = Def1;
    return true;
  }
  if (Src2IsIV && !Src1IsIV) {
    IVReg = RealIV2.isValid() ? RealIV2 : BrSrc2;
    LimitReg = BrSrc1;
    BumpMI = Def2;
    return true;
  }
  if (Src1IsIV && Src2IsIV) {
    // Both have defs — ambiguous. Prefer the first operand as IV
    // since that matches the common pattern BLT iv, limit.
    IVReg = RealIV1.isValid() ? RealIV1 : BrSrc1;
    LimitReg = BrSrc2;
    BumpMI = Def1;
    return true;
  }

  return false;
}

// Identify the IV and limit registers from an unfused comparison
// (SLT32/SLTU32/SEQ32 followed by BNEZ/BEQZ).
static bool identifyIVFromUnfusedCompare(const MachineLoop *L,
                                         MachineBasicBlock *Latch,
                                         MachineInstr *CmpMI,
                                         Register &IVReg,
                                         Register &LimitReg,
                                         MachineInstr *&BumpMI) {
  if (CmpMI->getNumOperands() < 3 || !CmpMI->getOperand(1).isReg() ||
      !CmpMI->getOperand(2).isReg())
    return false;

  Register CmpLHS = CmpMI->getOperand(1).getReg();
  Register CmpRHS = CmpMI->getOperand(2).getReg();

  // RealIV captures the loop-carried IV when copy-following fires.
  Register RealIVL, RealIVR;
  MachineInstr *LHSDef = findIVBumpInLoop(L, CmpLHS, Latch, &RealIVL);
  MachineInstr *RHSDef = findIVBumpInLoop(L, CmpRHS, Latch, &RealIVR);

  if (LHSDef) {
    IVReg = RealIVL.isValid() ? RealIVL : CmpLHS;
    LimitReg = CmpRHS;
    BumpMI = LHSDef;
    return true;
  }
  if (RHSDef) {
    IVReg = RealIVR.isValid() ? RealIVR : CmpRHS;
    LimitReg = CmpLHS;
    BumpMI = RHSDef;
    return true;
  }

  return false;
}

bool HaydnHardwareLoops::findTripCount(MachineLoop *L, int64_t &TripCount,
                                       Register &TripCountReg,
                                       TripComputeKind &ComputeKind,
                                       unsigned &TripShift,
                                       int64_t &ScalarTripInit,
                                       Register &IVRegOut,
                                       Register &LimitRegOut,
                                       MachineInstr *&CmpMIOut) {
  ComputeKind = TripComputeKind::None;
  TripShift = 0;
  ScalarTripInit = 0;
  IVRegOut = Register();
  LimitRegOut = Register();
  CmpMIOut = nullptr;
  MachineBasicBlock *Latch = L->getLoopLatch();
  MachineBasicBlock *Header = L->getHeader();
  if (!Latch || !Header)
    return false;

  // Find the conditional branch at the end of the latch.
  MachineBasicBlock *TrueBB = nullptr;
  MachineBasicBlock *FalseBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;

  const auto *TII =
      Latch->getParent()->getSubtarget<HaydnSubtarget>().getInstrInfo();
  if (TII->analyzeBranch(*Latch, TrueBB, FalseBB, Cond)) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: analyzeBranch failed on latch "
                      << printMBBReference(*Latch) << "\n");
    return false;
  }

  // We need a conditional branch.
  if (!TrueBB) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Latch has no conditional branch (TrueBB "
                         "null)\n");
    return false;
  }

  if (Cond.empty()) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Latch branch has empty Cond "
                         "(unconditional only)\n");
    return false;
  }

  // Pack freeze (placement before pack, AIE2 peer): MBP often layouts the
  // latch as [cond-br Exit] + fallthrough to Header. analyzeBranch reports
  // FBB=nullptr for fallthrough — resolve layout successor so Role B still
  // sees the back-edge. Only accept a layout successor that is already a CFG
  // successor of the latch (never invent edges).
  if (!FalseBB) {
    MachineFunction::iterator NextIt = std::next(Latch->getIterator());
    if (NextIt != Latch->getParent()->end() && Latch->isSuccessor(&*NextIt))
      FalseBB = &*NextIt;
  }

  // Determine which branch target is the loop header (back-edge) vs exit.
  // The branch that goes to the header is the loop-continuation branch.
  MachineBasicBlock *BackEdgeTarget = nullptr;
  bool BranchToHeaderIsTrue = false;
  if (TrueBB == Header || L->contains(TrueBB)) {
    BackEdgeTarget = TrueBB;
    BranchToHeaderIsTrue = true;
  } else if (FalseBB && (FalseBB == Header || L->contains(FalseBB))) {
    BackEdgeTarget = FalseBB;
    BranchToHeaderIsTrue = false;
  }

  // If neither target goes to the header, this is not a loop latch branch.
  if (!BackEdgeTarget) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Branch does not target loop header\n");
    return false;
  }

  Register IVReg;
  Register LimitReg;
  MachineInstr *BumpMI = nullptr;
  bool IsFused = false;
  // True when the latch compare is an equality (SEQ32) on an unfused single-reg
  // branch (BEQZ/BNEZ). The branch opcode itself carries no LT/GE/EQ sense
  // the compare does — so this flag feeds the count-direction logic instead of
  // isBranchEQ(BrOpc) for the unfused case. See.
  bool CmpIsEquality = false;

  // Extract the branch opcode.
  unsigned BrOpc = 0;
  if (Cond[0].isImm()) {
    BrOpc = Cond[0].getImm();
  }

  // Pattern 1: Fused two-register branch (from ConditionOptimizer)
  // BLT/BGE/BLTU/BGEU/BEQ/BNE rs1, rs2, target
  if (BrOpc != 0 && isFusedTwoRegBranch(BrOpc)) {
    if (Cond.size() < 3 || !Cond[1].isReg() || !Cond[2].isReg()) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Fused branch with bad operands\n");
      return false;
    }

    Register BrSrc1 = Cond[1].getReg();
    Register BrSrc2 = Cond[2].getReg();

    if (!identifyIVFromFusedBranch(L, Latch, BrSrc1, BrSrc2, IVReg, LimitReg,
                                   BumpMI)) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Cannot identify IV in fused "
                           "branch\n");
      return false;
    }

    IsFused = true;
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Fused branch pattern: " << BrOpc
                      << " IV=" << printReg(IVReg)
                      << " Limit=" << printReg(LimitReg) << "\n");
  }
  // Pattern 2: Unfused compare+branch
  // SLT32/SLTU32/SEQ32 rd, rs1, rs2; BNEZ/BEQZ rd, target
  else if (BrOpc != 0 && isSingleRegBranch(BrOpc)) {
    if (Cond.size() < 2 || !Cond[1].isReg()) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Single-reg branch with bad "
                           "operands\n");
      return false;
    }

    Register CondReg = Cond[1].getReg();
    if (!CondReg.isValid())
      return false;

    // Find the comparison that defines CondReg in the latch.
    MachineInstr *CmpMI = findDefInBlock(Latch, CondReg);
    if (!CmpMI) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Comparison not found in latch\n");
      return false;
    }

    CmpMIOut = CmpMI;
    unsigned CmpOpc = CmpMI->getOpcode();

    // Support SLT32, SLTU32, and SEQ32 comparisons.
    switch (CmpOpc) {
    default:
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Unsupported comparison opcode: "
                        << CmpOpc << "\n");
      return false;
    case Haydn::SLT32:
    case Haydn::SLTU32:
    case Haydn::SEQ32:
    case Haydn::SLE32:
      break;
    }
    // An equality compare (SEQ32) feeding a single-reg branch is EQ-sense:
    // count-up, symmetric under target inversion. isBranchEQ(BrOpc) does NOT
    // cover BEQZ/BNEZ, so without this the dominant -O2 count-up runtime loop
    // (seq32 iv,limit; beqz) is misrouted to count-down and the pass bails with
    // "Cannot determine trip count". See.
    CmpIsEquality = (CmpOpc == Haydn::SEQ32);

    if (!identifyIVFromUnfusedCompare(L, Latch, CmpMI, IVReg, LimitReg,
                                       BumpMI)) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Cannot identify IV in comparison\n");
      return false;
    }

    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Unfused pattern: " << CmpOpc << "+"
                      << BrOpc << " IV=" << printReg(IVReg)
                      << " Limit=" << printReg(LimitReg) << "\n");
  }
  // Pattern 3: Condition is a raw register (fallback)
  else if (Cond[0].isReg()) {
    Register CondReg = Cond[0].getReg();
    if (!CondReg.isValid())
      return false;

    MachineInstr *CmpMI = findDefInBlock(Latch, CondReg);
    if (!CmpMI)
      return false;

    CmpMIOut = CmpMI;
    unsigned CmpOpc = CmpMI->getOpcode();
    if (CmpOpc != Haydn::SLT32 && CmpOpc != Haydn::SLTU32 &&
        CmpOpc != Haydn::SEQ32 && CmpOpc != Haydn::SLE32)
      return false;

    if (!identifyIVFromUnfusedCompare(L, Latch, CmpMI, IVReg, LimitReg, BumpMI))
      return false;
  } else {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Unrecognized branch condition\n");
    return false;
  }

  if (!BumpMI || IVReg.isValid() == false) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: No IV bump found\n");
    return false;
  }

  // Publish the identified IV and limit registers to the caller (used by the
  // pointer-IV Case 3 to emit SUB32+SRLI32 in the preheader). Valid on every
  // success path from here on.
  IVRegOut = IVReg;
  LimitRegOut = LimitReg;

  // Extract the IV bump value.
  MachineBasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader)
    return false;

  int64_t IVBump = extractIVBump(BumpMI, IVReg, Preheader, L, MDT, MLI);
  if (IVBump == 0) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Cannot determine IV step\n");
    return false;
  }

  // Init / limit: same program-point discipline as step (HiFi contract)
  // Prefer Val(R @ Use) at the latch compare when present; else last def in
  // preheader; else Dom* with stop-on-non-imm (no stale ancestor constants).
  auto resolveImmAtPreheaderLiveIn = [&](Register Reg, int64_t &Imm) -> bool {
    // Live-out of preheader = last def of Reg in Preheader, else live-in.
    if (findImmediateDef(Preheader, Reg, Imm))
      return true;
    if (MachineInstr *D = findDefInBlock(Preheader, Reg)) {
      // Non-imm last def (ADD of runtime trip, etc.) — not a constant.
      (void)D;
      return false;
    }
    return findImmediateDefChain(Preheader, Reg, Imm) ||
           (MLI && findImmediateDefOnDomChainScoped(Preheader, Reg, Imm, MDT,
                                                    MLI, L)) ||
           findImmediateDefOnDomChain(Preheader, Reg, Imm, MDT);
  };

  int64_t IVInit = 0;
  IVInitResolveResult IVInitRes = findIVInitImmediate(Preheader, IVReg, IVInit);
  bool InitIsImm;
  if (IVInitRes == IVInitResolveResult::ProvenConstant) {
    InitIsImm = true;
  } else if (IVInitRes == IVInitResolveResult::ProvenNonConstant) {
    InitIsImm = false;
  } else {
    InitIsImm = resolveImmAtPreheaderLiveIn(IVReg, IVInit);
  }

  int64_t LimitImm = 0;
  bool LimitIsImm = false;
  // Primary: Val(LimitReg @ Cmp) — covers latch LD of spilled 0 (Case 2).
  if (CmpMIOut && resolveConstantAtUse(CmpMIOut, LimitReg, LimitImm, MDT, L)) {
    LimitIsImm = true;
  } else if (BumpMI &&
             resolveConstantAtUse(BumpMI, LimitReg, LimitImm, MDT, L)) {
    // Fused branch: no separate CmpMI; limit may still be reloaded near bump.
    LimitIsImm = true;
  } else {
    LimitIsImm = resolveImmAtPreheaderLiveIn(LimitReg, LimitImm);
  }

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: InitIsImm=" << InitIsImm
                    << " IVInit=" << IVInit
                    << " LimitIsImm=" << LimitIsImm
                    << " LimitImm=" << LimitImm
                    << " IVBump=" << IVBump << "\n");

  // Unroll×2 (niter step ±2): if limit still looks like imm 0 it is almost
  // always a stale soft-zero physreg, not the trip bound — treat as runtime.
  // Case 2 (step=-1, limit=0) is excluded by |bump|>=2.
  if (std::abs(IVBump) >= 2) {
    if (LimitIsImm && LimitImm == 0) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: LimitImm=0 with |bump|>=2 → runtime "
                           "limit (unroll×2 niter)\n");
      LimitIsImm = false;
    }
    if (!InitIsImm && IVInit == 0) {
      InitIsImm = true;
    }
  }

  // Determine if the branch condition represents "less than" or "greater or
  // equal" (or similar), accounting for the branch target semantics.
  // For the back-edge branch (loop continuation):
  // BLT iv, limit → loop while iv < limit (LT sense, positive bump)
  // BGE iv, limit → loop while iv >= limit (GE sense, negative bump)
  // When the branch to the header is the FALSE path (fallthrough), we need
  // to invert the comparison sense.
  // Classify the loop as count-up or count-down.
  // EQ sense (BEQ/BNE / SEQ32+BEQZ): count-up "while iv != limit"
  // symmetric under target inversion. Same formula as LT sense.
  // LT sense (BLT/BLTU or SLT+BNEZ): count-up "while iv < limit".
  // GE sense (BGE/BGEU or SLT+BEQZ): count-down "while iv >= limit".
  // EQ and LT are count-up (trip = (Limit-Init)/Step); GE is count-down
  // (trip = (Init-Limit)/|Step|). Previously BEQ was misrouted into the GE
  // count-down branch (Diff = Init-Limit, negative for a count-up loop) and
  // the pass bailed with "Cannot determine trip count" on every realistic
  // O2 C loop (whose latch lowers to BEQ with a stride). See.
  //
  // Unfused BEQZ/BNEZ: BrOpc carries NO LT/GE sense (isBranchLT's default
  // true mis-classifies them). Sense = compare (SLT/SEQ) + zero-test polarity
  // + which path is the back-edge. Pack freeze (MBP before Role B) frequently
  // layouts SLT; BEQZ Exit; fallthrough Header — must count as LT count-up.
  bool CountUp;
  if (isBranchEQ(BrOpc) || CmpIsEquality) {
    CountUp = true; // EQ sense (incl. SEQ32 unfused): count-up, symmetric.
  } else if (isSingleRegBranch(BrOpc)) {
    // SLT/SLTU/SLE feeding BEQZ/BNEZ: cond true means "less-than" family.
    // BNEZ → continue when cond true (LT); BEQZ → continue when false (GE).
    bool BranchOnCondTrue =
        BrOpc == Haydn::BNEZ || BrOpc == Haydn::BNEZ_W ||
        BrOpc == Haydn::BLTZ || BrOpc == Haydn::BLTZ_W;
    CountUp = BranchOnCondTrue;
    if (!BranchToHeaderIsTrue)
      CountUp = !CountUp;
  } else {
    CountUp = isBranchLT(BrOpc); // fused LT: count-up; GE: count-down.
    // If the header is the FALSE target, invert LT<->GE. EQ is unaffected.
    if (!BranchToHeaderIsTrue)
      CountUp = !CountUp;
  }

  // Count-up loops with |stride|>=2 (runtime unroll×2 niter) may report a
  // negative bump from compare operand order. Normalize only those — do NOT
  // flip step=-1 (dominant count-down-to-zero Case 2 needs IVBump==-1).
  if (CountUp && IVBump <= -2)
    IVBump = -IVBump;

  // Compute trip count.
  if (LimitIsImm && InitIsImm) {
    // Count-up (LT/EQ sense): trip_count = (limit - init) / bump
    // Count-down (GE sense): trip_count = (init - limit) / |bump
    int64_t Diff;
    if (CountUp) {
      Diff = LimitImm - IVInit;
    } else {
      // GE sense: loop runs while iv >= limit.
      // For count-down: trip_count = (init - limit) / |bump
      Diff = IVInit - LimitImm;
      IVBump = -IVBump; // Make bump positive for division
    }

    if (Diff <= 0)
      return false;

    if (Diff % IVBump != 0)
      return false;

    int64_t TC = Diff / IVBump;

    // Haydn HWLOOP count is uimm16 (max 65535).
    if (TC < 1 || TC > 65535)
      return false;

    TripCount = TC;
    TripCountReg = Register();

    if (IsFused)
      ++NumFusedBranchLoops;
    else
      ++NumUnfusedBranchLoops;
    return true;
  }

  // Register-based trip count (runtime value). Five cases are supported;
  // Cases 1/2 set TripCountReg directly (the bound register IS the trip
  // count), Cases 3/4/5 set ComputeKind so the caller emits preheader
  // arithmetic (SUB/ADD/SRLI) into LimitReg. See (Cases 1/2) and
  // (Cases 4/5).
  //
  // Case 1 — count-UP: init=0 (imm), step=+1, runtime limit.
  // trip = limit - 0 = limit → TripCountReg = LimitReg.
  if (CountUp && IVBump == 1 && InitIsImm && IVInit == 0) {
    TripCountReg = LimitReg;
    if (IsFused)
      ++NumFusedBranchLoops;
    else
      ++NumUnfusedBranchLoops;
    return true;
  }
  // Case 2 — count-DOWN via BEQ/BNE: runtime init, step=-1, limit=0 (imm).
  // This is the dominant -O2 pattern: IndVarSimplify folds the limit to
  // constant 0 and runs the IV down from N, producing
  // IV(init=N), step=-1, limit=0; latch: BEQ IV, 0 (continue while IV!=0).
  // BEQ/BNE are symmetric; the direction is set by the bump sign, not the
  // branch sense. trip = init - 0 = init → TripCountReg = IVReg.
  if ((isBranchEQ(BrOpc) || CmpIsEquality) && IVBump == -1 && LimitIsImm &&
      LimitImm == 0 && !InitIsImm) {
    TripCountReg = IVReg;
    if (IsFused)
      ++NumFusedBranchLoops;
    else
      ++NumUnfusedBranchLoops;
    return true;
  }
  // Helper: count non-def uses of \p Reg across all loop blocks. The runtime
  // trip-compute cases (3/4/5) clobber LimitReg with the computed trip count;
  // this is only safe if LimitReg is dead everywhere in the loop except the
  // single latch comparison (exactly one use). See (Case 3 safety gate).
  auto countLoopRegUses = [&](Register Reg) {
    unsigned Uses = 0;
    for (MachineBasicBlock *MBB : L->getBlocks()) {
      for (const MachineInstr &MI : *MBB) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.getReg() == Reg && !MO.isDef())
            ++Uses;
        }
      }
    }
    return Uses;
  };

  // Case 3 — pointer-IV count-up: both init (start ptr) and limit (end ptr)
  // are runtime registers, stride is a power-of-two byte count from a
  // LD32_POST / LD64_POST bump. trip = (end - start) >> log2(stride).
  // The caller emits SUB32 LimitReg, LimitReg, IVReg; SRLI32 LimitReg
  // LimitReg, shift in the preheader, then uses LimitReg as TripCountReg.
  // See (GAP-3 / pointer-IV).
  if (CountUp && IVBump >= 2 && (IVBump & (IVBump - 1)) == 0 &&
      !InitIsImm && !LimitIsImm) {
    unsigned LimitUses = countLoopRegUses(LimitReg);
    // Expect exactly one use: the latch comparison operand. If the limit is
    // used elsewhere (e.g. in a secondary compare or as a general value)
    // bail — the clobber would be unsafe.
    if (LimitUses == 1) {
      // Only power-of-two byte strides (1, 2, 4, 8) — SRLI32 needs log2.
      unsigned Shift = 0;
      for (unsigned B = static_cast<unsigned>(IVBump); B > 1; B >>= 1)
        ++Shift;
      ComputeKind = TripComputeKind::PointerIV;
      TripShift = Shift;
      TripCountReg = LimitReg; // Caller clobbers LimitReg with computed trip.
      if (IsFused)
        ++NumFusedBranchLoops;
      else
        ++NumUnfusedBranchLoops;
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Pointer-IV trip case: trip = ("
                        << printReg(LimitReg) << " - " << printReg(IVReg)
                        << ") >> " << Shift << "\n");
      return true;
    }
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Pointer-IV Case 3 rejected — LimitReg "
                         "has "
                      << LimitUses << " uses (need exactly 1)\n");
  }

  // Case 4 — scalar count-UP, compile-time-constant init, power-of-two bump
  // (>= 1), runtime limit register (not an immediate). This generalizes Case 1
  // (which is init==0, bump==1) to arbitrary constant init and pow2 stride.
  // Dominant shape for -O2 element-wise vector kernels (vec_add/max/scale/...)
  // and many IIR loops: IV init is a constant, the stride is a constant 2/4/8
  // and the limit is a runtime register (e.g. N-1 computed in the preheader).
  // trip = (limit - init) >> log2(bump); emit in the preheader:
  // ADDI32 Tmp, R0, -init (only if init != 0)
  // ADD32 LimitReg, LimitReg, Tmp
  // SRLI32 LimitReg, LimitReg, shift (only if shift > 0)
  // LimitReg is clobbered with the computed trip (gated by single-use safety).
  //
  // Soundness of (limit-init) >> log2(bump): for a count-up loop
  // `iv = init; while (iv < limit) iv += bump`, the exact iteration count is
  // ceil((limit-init)/bump). For the canonical post-IndVarSimplify/LSR form
  // emitted for these kernels, the limit is an exact boundary — the loop
  // processes whole stride-sized elements — so (limit-init) is a multiple of
  // bump and floor == ceil == exact. The standalone repros in the G1 diagnosis
  // (vec_add32x32, latr*) all hit this exact form. We reuse the floor shift
  // (matching Case 3's precedent); a non-multiple limit would be a pessimization
  // (one too few iterations), never wrong-code on the canonical form. The HW
  // loop simply repeats the body N times. See (Case 4).
  if (CountUp && InitIsImm && !LimitIsImm && IVBump >= 1 &&
      (IVBump & (IVBump - 1)) == 0 &&
      // The preheader emits `ADDI32 LimitReg, LimitReg, -init`; ADDI32 takes a
      // simm16 immediate, so -init must fit [-32768, 32767]. The dominant init
      // values on the DSP corpus (0, 1, 2) always fit; large inits are left on
      // a cmp+branch back-edge rather than mis-emit.
      (-IVInit >= -32768 && -IVInit <= 32767)) {
    if (countLoopRegUses(LimitReg) == 1) {
      unsigned Shift = 0;
      for (unsigned B = static_cast<unsigned>(IVBump); B > 1; B >>= 1)
        ++Shift;
      ComputeKind = TripComputeKind::ScalarCountUp;
      TripShift = Shift;
      ScalarTripInit = IVInit;
      TripCountReg = LimitReg; // Caller clobbers LimitReg with computed trip.
      if (IsFused)
        ++NumFusedBranchLoops;
      else
        ++NumUnfusedBranchLoops;
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Scalar count-up Case 4: trip = ("
                        << printReg(LimitReg) << " - " << IVInit << ") >> "
                        << Shift << "\n");
      return true;
    }
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Scalar count-up Case 4 rejected — "
                         "LimitReg not single-use\n");
  }

  // Case 5 — scalar count-DOWN EQ, step = -1, runtime init AND runtime limit
  // (limit != 0). This is the dominant FIR inner-MAC-loop shape where the
  // limit is the filter-tap count M reloaded from a stack slot under register
  // pressure. trip = iv_init - limit; emit in the preheader:
  // SUB32 LimitReg, LimitReg, IVInitReg
  // LimitReg is clobbered with the computed trip (gated by single-use safety).
  //
  // Soundness: count-down `iv -= 1 while iv != limit` runs exactly
  // (init - limit) iterations (every decrement by 1 is one step, no ceil/floor
  // ambiguity). The SEQ32+BEQZ back-edge guarantees finiteness, so init >=
  // limit on entry for any loop that executes at least once. Zero-trip safety
  // (init < limit) mirrors Case 2 (limit=0 imm): unguarded do-while loops
  // execute >= 1 time by construction, and guarded loops place the
  // SET_HWLOOP_REG after the guard (via createPreheaderForLoop), so a
  // zero-trip loop never reaches the trip computation. This is the established
  // shipped Case-2 behavior; Case 5 mirrors it. See (Case 5).
  if ((isBranchEQ(BrOpc) || CmpIsEquality) && IVBump == -1 && !InitIsImm &&
      !LimitIsImm) {
    if (countLoopRegUses(LimitReg) == 1) {
      ComputeKind = TripComputeKind::ScalarCountDown;
      TripCountReg = LimitReg; // Caller clobbers LimitReg with (IVReg - Limit).
      if (IsFused)
        ++NumFusedBranchLoops;
      else
        ++NumUnfusedBranchLoops;
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Scalar count-down EQ Case 5: trip = "
                        << printReg(IVReg) << " - " << printReg(LimitReg)
                        << "\n");
      return true;
    }
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Scalar count-down Case 5 rejected — "
                         "LimitReg not single-use\n");
  }

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Cannot compute trip count\n");
  return false;
}

// Check whether physical register \p Reg is live at insertion point \p InsertPt
// in \p MBB. This pass runs post-RA without LiveIntervals, so liveness at a
// specific program point is reconstructed with LivePhysRegs: seed from the
// block's live-outs and step backward from the block end to \p InsertPt.
// Unlike MachineBasicBlock::isLiveIn (which only reports block-entry
// liveness), this correctly returns false when the register is killed or
// clobbered between the block entry and \p InsertPt.
// This catches the trip-count spill bug : a trip-count register that is
// live-in to the preheader but then spilled (e.g. `ST32 killed $rN`) is NOT
// live at the SET_HWLOOP_REG insertion point (the preheader terminator), yet
// isLiveIn returns true. Emitting SET_HWLOOP_REG with such a reg produces
// the verifier error "Using an undefined physical register". Using LivePhysRegs
// (rather than a kill-flag scan) also handles regmask clobbers (e.g. calls in
// the preheader) and sub-register aliases correctly. See /.
static bool isPhysRegLiveAt(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator InsertPt,
                            MCRegister Reg, const TargetRegisterInfo &TRI) {
  LivePhysRegs LiveRegs(TRI);
  LiveRegs.addLiveOuts(MBB);
  for (auto I = MBB.rbegin(), E = MachineBasicBlock::reverse_iterator(InsertPt);
       I != E; ++I)
    LiveRegs.stepBackward(*I);
  return LiveRegs.contains(Reg);
}

bool HaydnHardwareLoops::convertToHardwareLoop(MachineLoop *L,
                                                MachineFunction &MF,
                                                bool &Sel0Used,
                                                bool &Sel1Used) {
  // Process nested sub-loops first (inside-out). This ensures inner loops
  // are converted before outer loops, which is required for correct sel
  // assignment (inner gets sel=1, outer gets sel=0).
  bool Changed = false;
  bool ChildSel0Used = false;
  bool ChildSel1Used = false;
  for (MachineLoop *SubL : *L) {
    bool SubSel0 = false;
    bool SubSel1 = false;
    Changed |= convertToHardwareLoop(SubL, MF, SubSel0, SubSel1);
    ChildSel0Used |= SubSel0;
    ChildSel1Used |= SubSel1;
  }

  // Role A belt+suspenders: if IR ZOL/JNZD form is already present on THIS
  // loop, do not attempt Role B convert (containsInvalidInstruction also
  // rejects these). Still record sel usage so a parent free-list (PR7) knows
  // the HWLR level is taken — prefer sel=1 for innermost/standalone Role A
  // sel=0 when a child already took a level (outer Role A JNZD is rare).
  if (hasIRZOLForm(L)) {
    // P5: store-only / empty Role A shells must not freeze as ZOL (poly/alog).
    MachineBasicBlock *Latch = L->getLoopLatch();
    MachineBasicBlock *Header = L->getHeader();
    MachineBasicBlock *BodyBB = Latch ? Latch : Header;
    bool ShellOrEmpty =
        BodyBB && (isEmptyZOLBody(*BodyBB) || isShellZOLBody(*BodyBB));
    if (ShellOrEmpty && stripEmptyZeroOverheadLoops(MF)) {
      Changed = true;
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: demoted Role A shell/empty ZOL — "
                           "continue (no re-convert of store shell)\n");
      // Do not Role-B re-convert the same store-only loop — leave soft branch.
      Sel0Used = ChildSel0Used;
      Sel1Used = ChildSel1Used;
      return Changed;
    }
    if (ShellOrEmpty) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A shell body; strip missed — "
                           "skip convert\n");
    } else {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A IR ZOL/JNZD form present — "
                           "skip convert (AsmPrinter expand)\n");
    }
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    if (ChildSel0Used || ChildSel1Used) {
      if (!Sel0Used)
        Sel0Used = true;
      else
        Sel1Used = true;
    } else {
      Sel1Used = true; // innermost Role A uses sel=1 by convention
    }
    return Changed;
  }

  // Decline Role B convert for store-only bodies (P5) — not a DSP core.
  {
    unsigned RealOps = 0;
    bool HasLoadOrCompute = false;
    for (MachineBasicBlock *BB : L->getBlocks()) {
      for (const MachineInstr &MI : *BB) {
        if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
            MI.isKill() || MI.isPosition() || MI.isTerminator() || MI.isBranch())
          continue;
        if (MI.getOpcode() == Haydn::NOP)
          continue;
        ++RealOps;
        if (MI.mayLoad() ||
            (!MI.mayStore() && !MI.isCopy() && !MI.isMoveImmediate()))
          HasLoadOrCompute = true;
      }
    }
    if (RealOps > 0 && RealOps <= 2 && !HasLoadOrCompute) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: store-only loop body — decline "
                           "Role B convert\n");
      Sel0Used = ChildSel0Used;
      Sel1Used = ChildSel1Used;
      return Changed;
    }
  }

  // Free-list of HWLR selectors already consumed by converted children.
  // Index 0 → sel=0 (prefer outer), index 1 → sel=1 (prefer innermost).
  // Marked used only after a successful convert of this loop (below).
  bool SelUsed[2] = {ChildSel0Used, ChildSel1Used};

  // Dual HWLR free-list always: allocate a free sel below (prefer 0 for outer)
  // and tolerate the child's SET_HWLOOP in the outer body.

  // Validate loop structure. We support both single-BB and multi-BB loops
  // as long as they have a single latch, single exit, and well-formed control
  // flow within the loop body.
  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Latch = L->getLoopLatch();
  bool IsSingleBB = Header && Latch && Header == Latch;

  if (!IsSingleBB) {
    // AIE refuses multi-BB ZOL at IR TTI. Haydn multi-BB Role B residual has
    // open correctness issues:
    // * nested outer SET around inner ZOL (CoreMark bitextract MEMORY_FAULT)
    // * soft→HW conversion wrong-answer on multi-BB DP (lc_dp_lis O1/O2
    // exit 2 vs host 4) — fixed by leaving multi-BB soft.
    // Keep multi-BB as soft branch until the multi-BB ZOL contract is solid
    // (structure validation alone is insufficient). Single-BB Role B remains.
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: multi-BB — decline Role B "
                         "(AIE-aligned; soft residual)\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }

  MachineBasicBlock *Preheader = L->getLoopPreheader();

  // Need a preheader to insert the hardware loop setup. A guarded loop
  // (e.g. runtime trip count lowered as `guard: BGE init,limit,exit;
  // fallthrough->header`) has a guard block with two successors, so
  // getLoopPreheader returns null. Split the non-backedge entry edge to
  // create a dedicated preheader. See (FIX A).
  if (!Preheader) {
    Preheader = createPreheaderForLoop(L);
    if (!Preheader) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Skipping loop without preheader\n");
      Sel0Used = ChildSel0Used;
      Sel1Used = ChildSel1Used;
      return Changed;
    }
    Changed = true;
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Created dedicated preheader "
                      << printMBBReference(*Preheader) << "\n");
  }

  // Check for invalid instructions. Tolerate a converted child's SET_HWLOOP
  // LoopStart forms inside this loop's block set (dual free-list).
  const bool AllowChildHwloop = ChildSel0Used || ChildSel1Used;
  if (containsInvalidInstruction(L, AllowChildHwloop)) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Loop contains invalid instructions\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }

  // Range check: the loop body must fit within the HWLOOP PC-relative offset
  // fields. Per the ISA DB the offsets are two UNSIGNED fields of different
  // widths (uimm6_offset1 for START, uimm12_offset2 for END); see and
  // loopBodyFitsRange. If the body is too large, decline conversion and keep
  // the compare-and-branch sequence (which the branch-relaxation pass handles
  // for far targets).
  if (!loopBodyFitsRange(L)) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Loop body out of range for HWLOOP "
                         "offset field, keeping cmp+branch loop\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }

  // Find the trip count.
  int64_t TripCountImm = 0;
  Register TripCountReg;
  TripComputeKind ComputeKind = TripComputeKind::None;
  unsigned TripShift = 0;
  int64_t ScalarTripInit = 0;
  Register IVReg, LimitReg;
  MachineInstr *CmpMI = nullptr;
  if (!findTripCount(L, TripCountImm, TripCountReg, ComputeKind, TripShift,
                     ScalarTripInit, IVReg, LimitReg, CmpMI)) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Cannot determine trip count\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }
  bool NeedsTripCompute =
      ComputeKind != TripComputeKind::None;

  // Correctness gate : when the trip count is a register (runtime
  // value), the register MUST be live at the SET_HWLOOP_REG insertion point
  // (the preheader terminator position) — the instruction reads it there.
  //
  // The check is point-liveness, not just block-live-in. A trip-count reg that
  // is live-in to the preheader but then SPILLED by regalloc (e.g.
  // `ST32 killed $rN` reading+kill it) is dead by the terminator. Emitting
  // SET_HWLOOP_REG with such a reg triggers the verifier error "Using an
  // undefined physical register". isPhysRegLiveAt walks the preheader with
  // LivePhysRegs to evaluate liveness exactly at the insertion point
  // (— bqriir32x32_df1 trip-count spill).
  //
  // For nested loops, the trip-count reg of the OUTER loop may be defined
  // inside the inner loop's body (a live-out), making it NOT live at the outer
  // preheader — also caught here. Reject the conversion in that case
  // (correctness over coverage). For the trip-compute cases (PointerIV
  // ScalarCountUp, ScalarCountDown), the trip is computed in the preheader from
  // LimitReg and (for PointerIV/ScalarCountDown) IVReg; both must be live there.
  const auto &TRI = *MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  if (TripCountReg.isValid()) {
    MachineBasicBlock::iterator InsertPt = Preheader->getFirstTerminator();
    if (!isPhysRegLiveAt(*Preheader, InsertPt,
                         TripCountReg.asMCReg(), TRI)) {
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Trip-count reg "
                        << printReg(TripCountReg)
                        << " not live at SET_HWLOOP_REG insertion point — "
                           "rejecting (would emit undefined trip-count)\n");
      Sel0Used = ChildSel0Used;
      Sel1Used = ChildSel1Used;
      return Changed;
    }
  }
  if (NeedsTripCompute) {
    // Trip-compute cases clobber LimitReg (== TripCountReg, already checked
    // above) and read IVReg once. For PointerIV and ScalarCountDown the IVReg
    // is the runtime init/start register; for ScalarCountUp the init is an
    // immediate (no register read). Verify IVReg liveness only when the kind
    // actually reads it.
    bool ReadsIVReg = ComputeKind == TripComputeKind::PointerIV ||
                      ComputeKind == TripComputeKind::ScalarCountDown;
    if (ReadsIVReg) {
      MachineBasicBlock::iterator InsertPt = Preheader->getFirstTerminator();
      if (!isPhysRegLiveAt(*Preheader, InsertPt, IVReg.asMCReg(), TRI)) {
        LLVM_DEBUG(dbgs() << "HaydnHWLoops: IVReg " << printReg(IVReg)
                          << " not live at trip-compute insertion point — "
                             "rejecting\n");
        Sel0Used = ChildSel0Used;
        Sel1Used = ChildSel1Used;
        return Changed;
      }
    }
  }

  // Ensure the loop has a single exit.
  SmallVector<MachineBasicBlock *, 4> ExitBlocks;
  L->getExitBlocks(ExitBlocks);
  if (ExitBlocks.size() != 1) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Loop has multiple exits\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }

  // Free-list sel assignment (PR7). Haydn has two HWLR levels:
  // prefer sel=1 for innermost / standalone, sel=0 for outer.
  // If both free → assign preferred; if only one free → take it; if none
  // free → skip. SelUsed is only marked after a successful convert.
  unsigned Sel = 0;
  if (SelUsed[0] && SelUsed[1]) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: No free HWLR sel — skipping\n");
    Sel0Used = ChildSel0Used;
    Sel1Used = ChildSel1Used;
    return Changed;
  }
  if (!SelUsed[0] && !SelUsed[1]) {
    // Both free: innermost / standalone prefers sel=1.
    Sel = 1;
  } else if ((ChildSel0Used || ChildSel1Used) && !SelUsed[0]) {
    // Outer with a converted child: prefer sel=0 when free.
    Sel = 0;
  } else if (!SelUsed[1]) {
    Sel = 1;
  } else {
    Sel = 0;
  }

  const auto *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  DebugLoc DL;

  // Insert SET_HWLOOP pseudo in the preheader, before the terminator.
  MachineBasicBlock::iterator InsertPt = Preheader->getFirstTerminator();
  if (InsertPt != Preheader->end())
    DL = InsertPt->getDebugLoc();

  // Force the HWLoop target MBBs to emit labels. After conversion erases the
  // latch terminator (below) and the preheader falls through to the Header
  // both Header and the loop Exit become reachable ONLY by fallthrough.
  // AsmPrinter::shouldEmitLabelForBasicBlock then skips emitting their labels
  // (emitting only a " / %bb.N:" comment). But the SET_HWLOOP(_REG) fixups
  // reference these MBB symbols via PC-relative relocations, and an unemitted
  // temp symbol triggers "Undefined temporary symbol" during ELF symbol-table
  // emission (ELFObjectWriter.cpp:529-530). setLabelMustBeEmitted forces
  // AsmPrinter::emitBasicBlockStart to emitLabel the symbols so they become
  // defined. Must be called before the BuildMI.addMBB below, which calls
  // getSymbol and caches the MCSymbol. See (label emission).
  //
  // HWLR_END is INCLUSIVE — the address of the LAST
  // instruction of the loop body (proven by the authoritative
  // `program/fibonacci_hw_loop.asm`: SET_HWLOOP 0,1,3,14 → END=0x14, the last
  // body bundle). chose ExitBB (exclusive) on the since-disproven
  // assumption that Haydn follows HiFi's exclusive convention — that was the
  // recorded follow-up risk, now realized as the dominant yarpgen
  // differential-test failure. The END operand now carries the Latch MBB; the
  // AsmPrinter emits a temp label at the latch's last real body instruction
  // (getOrCreateHwloopEndSym) so HWLR_END resolves to that instruction's
  // address. For multi-BB loops the Latch is the last block of the body; for
  // single-BB loops Header == Latch.
  MachineBasicBlock *ExitBB = ExitBlocks[0];
  Header->setLabelMustBeEmitted();
  Latch->setLabelMustBeEmitted();
  if (ExitBB != Header)
    ExitBB->setLabelMustBeEmitted();

  // Trip-count computation in the preheader (Cases 3/4/5). LimitReg is dead
  // after the latch branch is removed (below), so clobbering it with the
  // computed trip is safe (gated by the single-use check in findTripCount).
  // The SET_HWLOOP_REG below then reads LimitReg as the trip count.
  // PointerIV (Case 3): trip = (LimitReg - IVReg) >> shift
  // ScalarCountUp (Case 4): trip = (LimitReg - ScalarTripInit) >> shift
  // ScalarCountDown (Case 5): trip = IVReg - LimitReg
  // See (Case 3) and (Cases 4/5).
  switch (ComputeKind) {
  case TripComputeKind::None:
    break;
  case TripComputeKind::PointerIV: {
    // trip = end - start → LimitReg = SUB32 LimitReg, IVReg
    BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SUB32), LimitReg)
        .addReg(LimitReg)
        .addReg(IVReg);
    // shift: trip >>= shift (only if shift > 0; shift=0 means stride==1 byte).
    // For LD32_POST with imm6=1, stride=4 → shift=2. For LD64_POST imm6=1
    // stride=8 → shift=3.
    if (TripShift > 0)
      BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SRLI32), LimitReg)
          .addReg(LimitReg)
          .addImm(TripShift);
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Emitted pointer-IV trip compute "
                      << printReg(LimitReg) << " = (" << printReg(LimitReg)
                      << " - " << printReg(IVReg) << ") >> " << TripShift
                      << "\n");
    break;
  }
  case TripComputeKind::ScalarCountUp: {
    // trip = (limit - init) >> shift, init = ScalarTripInit (compile-time).
    // For init==0 this collapses to SRLI32 (or nothing when shift==0), which
    // generalizes Case 1. findTripCount guarantees -init fits simm16.
    if (ScalarTripInit != 0) {
      // LimitReg = ADDI32 LimitReg, -init
      BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::ADDI32_W), LimitReg)
          .addReg(LimitReg)
          .addImm(-ScalarTripInit);
    }
    if (TripShift > 0)
      BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SRLI32), LimitReg)
          .addReg(LimitReg)
          .addImm(TripShift);
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Emitted scalar count-up trip compute "
                      << printReg(LimitReg) << " = (" << printReg(LimitReg)
                      << " - " << ScalarTripInit << ") >> " << TripShift
                      << "\n");
    break;
  }
  case TripComputeKind::ScalarCountDown: {
    // trip = iv_init - limit → LimitReg = SUB32 LimitReg, IVReg
    // (IVReg holds the runtime IV init for the count-down EQ shape.)
    BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SUB32), LimitReg)
        .addReg(LimitReg)
        .addReg(IVReg);
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: Emitted scalar count-down trip compute "
                      << printReg(LimitReg) << " = " << printReg(LimitReg)
                      << " - " << printReg(IVReg) << "\n");
    break;
  }
  }

  // pass Latch (not ExitBB) as the loop_end MBB. HWLR_END is inclusive;
  // AsmPrinter emits the END label at the latch's last real body instruction.
  MachineInstr *SetMI = nullptr;
  if (TripCountReg.isValid()) {
    SetMI = BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SET_HWLOOP_REG))
                .addImm(Sel)
                .addMBB(Header)
                .addMBB(Latch)
                .addReg(TripCountReg);
  } else {
    SetMI = BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SET_HWLOOP))
                .addImm(Sel)
                .addMBB(Header)
                .addMBB(Latch)
                .addImm(TripCountImm);
  }

  // Setup window (layout-owned)
  // Spec needs ≥3 bundles between SET and body. Only deficit NOP pads after
  // SET. Trip-count math and useful preheader work stay *before* SET so Off1
  // is small by construction (no after-SET fill / Fixup reverse-order risk).
  {
    unsigned FollowingBundles = 0;
    for (MachineBasicBlock::iterator I = std::next(SetMI->getIterator()),
                                     E = Preheader->end();
         I != E; ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef() ||
          I->isKill())
        continue;
      if (I->isTerminator() && !I->isCall())
        break;
      unsigned Bytes = TII->getInstSizeInBytes(*I);
      if (Bytes == 0)
        continue;
      FollowingBundles +=
          (Bytes + static_cast<unsigned>(Bundle128Bytes) - 1) /
          static_cast<unsigned>(Bundle128Bytes);
    }
    if (FollowingBundles < HWLoopSetupPadBundles) {
      unsigned Deficit = HWLoopSetupPadBundles - FollowingBundles;
      MachineBasicBlock::iterator AfterSet = std::next(SetMI->getIterator());
      for (unsigned I = 0; I < Deficit; ++I)
        BuildMI(*Preheader, AfterSet, DL, TII->get(Haydn::NOP));
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: formation t−3 pad " << Deficit
                        << " NOP bundle(s) after SET\n");
    }
  }

  // Body length is not a product legality floor. Inclusive END (BEGIN <= END)
  // is legal; SET t−3 is enforced by the deficit pad above + FixupHwLoops.

  // Remove the conditional branch at the end of the latch and fix successors.
  // The hardware loop handles the back-edge automatically. The latch block
  // should fall through to the exit block.

  // Remove all terminators from the latch (conditional branch to header and
  // fallthrough to exit). The hardware loop handles the back-edge.
  SmallVector<MachineInstr *, 4> ToRemove;
  for (auto I = Latch->getFirstTerminator(), E = Latch->end(); I != E; ++I)
    ToRemove.push_back(&*I);
  for (MachineInstr *MI : ToRemove)
    MI->eraseFromParent();

  // Erase the now-dead loop-compare instruction (SEQ32/SLT32/SLTU32/SLE32).
  // findTripCount identified CmpMI as the compare that fed the conditional
  // back-edge branch we just erased; with the hardware loop supplying the
  // back-edge, the compare is dead. Leaving it wastes an instruction slot every
  // iteration (ISA-34 Gap C: dot_product inner loop had a dead seq32 in the
  // hot body). Erase it directly — we know it's dead because we erased its
  // only consumer (the branch). If CmpMI is already gone (fused compare+branch
  // has no separate compare instr), skip.
  if (CmpMI && CmpMI->getParent()) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: erasing dead loop compare "
                      << *CmpMI << "\n");
    CmpMI->eraseFromParent();
  }

  // P1 residual: pure trip-count IV bump often remains after compare erase
  // (vec_dot add32 r10,r10,fp). Erasing it leaves min-body/MAC-latency NOPs
  // that *lower* fill (2.0→1.5). Keep bump until pad policy co-schedules
  // useful work into that cycle.

  // Update successors: remove all existing successors and add only the
  // fallthrough to the exit block.
  Latch->removeSuccessor(Header);
  Latch->removeSuccessor(ExitBB);
  Latch->addSuccessor(ExitBB);

  // Insert an explicit unconditional branch to ExitBB if ExitBB is NOT the
  // layout fallthrough of the latch. After erasing the latch's terminators
  // above, the latch has no terminator; control reaches ExitBB only by layout
  // fallthrough. When ExitBB is placed earlier in the function layout than the
  // latch (common in nested loops where the inner-loop latch is laid out LAST
  // and the inner exit is the OUTER latch, which sits earlier), there is no
  // fallthrough edge — the latch needs an explicit `B ExitBB`.
  //
  // Bug history : without this branch, the post-conversion MIR has a
  // latch with a successor (ExitBB) but no terminator reaching it, which
  // triggers the MachineVerifier "MBB has unexpected successors which are not
  // branch targets, fallthrough, EHPads, or inlineasm_br targets" error. This
  // was the fir-16tap XFAIL: a nested loop (runtime inner trip) where the
  // inner latch was laid out last and its exit was the outer latch.
  //
  // Layout fallthrough is determined by the next block in the function's
  // block list (MachineFunction::iterator). If the latch is the last block
  // in the function, or its layout successor != ExitBB, an explicit branch
  // is required. insertBranch with empty Cond emits the `B` pseudo
  // (isBarrier=1), which BranchRelaxation and AsmPrinter handle correctly.
  {
    MachineFunction::iterator LatchIt = Latch->getIterator();
    MachineFunction::iterator NextIt = std::next(LatchIt);
    bool ExitIsLayoutFallthrough =
        (NextIt != MF.end()) && (&*NextIt == ExitBB);
    if (!ExitIsLayoutFallthrough) {
      TII->insertBranch(*Latch, ExitBB, /*FBB=*/nullptr,
                        /*Cond=*/SmallVector<MachineOperand, 0>(), DL);
      LLVM_DEBUG(dbgs() << "HaydnHWLoops: Inserted explicit B "
                        << printMBBReference(*ExitBB)
                        << " in latch (ExitBB not layout fallthrough)\n");
    }
  }

  // Mark the sel level as used.
  if (Sel == 0)
    Sel0Used = true;
  else
    Sel1Used = true;
  // Propagate child sel usage too.
  Sel0Used |= ChildSel0Used;
  Sel1Used |= ChildSel1Used;

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Converted loop in "
                    << MF.getName() << " (trip count = "
                    << (TripCountReg.isValid() ? "reg" : Twine(TripCountImm))
                    << ", sel=" << Sel << ")\n");

  if (ChildSel0Used || ChildSel1Used)
    ++NumNestedHWLoops;

  ++NumHWLoops;
  return true;
}
