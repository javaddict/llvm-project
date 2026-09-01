//===-- HaydnHWLoopDemote.cpp - Shared HWLOOP demote/erase helpers ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions for the MI-level helpers shared by hardware-loop setup erase
// and software-loop demotion. Declared in HaydnHWLoopDemote.h; the exported
// entry points (llvm::eraseHardwareLoopSetup /
// llvm::demoteHardwareLoopToSoftware) live in HaydnHardwareLoops.cpp and
// call through to these with a per-pass LLVM_DEBUG prefix.
//
//===----------------------------------------------------------------------===//

#include "HaydnHWLoopDemote.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#include <numeric>

using namespace llvm;
using namespace llvm::haydn::hwloop;

#define DEBUG_TYPE "haydn-hwloop-demote"

// Product default ON: demote-first, not erase-only. Debug OFF skips the
// soft-edge install on a live body (caller fatals). Hexagon FixupHwLoops
// skips conversion when the loop cannot stay legal
// (HexagonFixupHwLoops.cpp:97-148).
static cl::opt<bool> EnableHaydnHwLoopDemote(
    "haydn-enable-hwloop-demote", cl::Hidden, cl::init(true),
    cl::desc("Demote out-of-range / invalid ZOL to software SUBI32+BNEZ_W "
             "when a free counter GPR exists (LivePhysRegs). Default ON "
             "(product demote-first). OFF = do not demote; live body is "
             "fatal rather than erase-only once-through."));

bool haydn::hwloop::isHwLoopDemoteEnabled() {
  return EnableHaydnHwLoopDemote;
}

bool haydn::hwloop::blockContainsUnpublishedHwlrCsr(
    const MachineBasicBlock &BB) {
  // instrs() includes BUNDLE children. Top-level range-for would miss a
  // packed CSRW after post-RA (P6 Fixup). Formation (P3) is typically
  // unbundled; the same walk stays correct. Peer: AIE expand
  // (AIEBaseHardwareLoops.cpp:348-370) walks every MI in the loop
  // blocks; Haydn overlay is this unpublished-window predicate only.
  for (const MachineInstr &MI : BB.instrs()) {
    if (MI.isBundle() || MI.isMetaInstruction())
      continue;
    if (haydnHwloopCsrAddr(MI) >= 0)
      return true;
  }
  return false;
}

bool haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(
    const LoopBlockSet &Blocks) {
  for (const MachineBasicBlock *BB : Blocks) {
    if (BB && blockContainsUnpublishedHwlrCsr(*BB))
      return true;
  }
  return false;
}

// Resolve the LoopStart/SET body from CFG state only. Any SET form
// (logical/wide/member) carries Header as op1. LoopStart: PLE-carrying
// preheader successor (single-BB) or unique successor with a latch PLE
// targeting it (multi-BB). Formation uses this directly — layout-order
// resolution is incomplete retained state and must reject fail-closed.
static MachineInstr *findPseudoLoopEnd(MachineBasicBlock *BB) {
  if (!BB)
    return nullptr;
  for (MachineInstr &MI : *BB)
    if (MI.getOpcode() == Haydn::PseudoLoopEnd)
      return &MI;
  return nullptr;
}

static bool isPLETargetingHeader(const MachineInstr *MI,
                                 const MachineBasicBlock *Header);

/// The block's unique live successor, or null (zero, or ambiguous).
/// CB-166: the classic expander's guarded-prologue branch insertion can
/// register the SAME successor twice (addSuccessor on an edge already
/// present); a duplicated edge is still one successor — dedupe by block.
static MachineBasicBlock *getLoneSuccessor(const MachineBasicBlock &BB) {
  const MachineFunction *MF = BB.getParent();
  MachineBasicBlock *Only = nullptr;
  for (MachineBasicBlock *Succ : BB.successors()) {
    if (!isLiveMBB(*MF, Succ))
      continue;
    if (Only && Only != Succ)
      return nullptr;
    Only = Succ;
  }
  return Only;
}

/// CB-166: does the successor chain starting at \p Cur reach a self-latched
/// kernel (a block whose PseudoLoopEnd targets itself) within the guarded
/// prologue chain? A guarded prologue has two live successors (guard edge
/// to epilog + progress edge); BOTH are explored and any reaching path
/// proves the kernel exists downstream — the caller then discriminates the
/// progress edge by uniqueness. \p Barrier (the preheader) terminates every
/// exploration path: an edge back to the preheader is the OUTER loop's
/// back edge, not this chain's progress (the guard target of a nested-loop
/// peel reaches the kernel only by re-entering the preheader, which is not
/// a proof of this chain). A block carrying ANOTHER LoopStart is also a
/// barrier: sequential/nested loop regions are delimited by their setups,
/// and a skip edge flowing into the NEXT loop's preheader reaches that
/// loop's kernel — not proof of THIS chain. Bounded DFS; \p Visited
/// carries the already-walked chain blocks so a guard edge back into the
/// chain cycles out (refused). Pure CFG proof — never layout order.
static bool
prologueChainReachesKernel(MachineBasicBlock *Cur,
                           SmallPtrSet<MachineBasicBlock *, 8> &Visited,
                           MachineBasicBlock *Barrier = nullptr) {
  SmallVector<MachineBasicBlock *, 8> Worklist;
  SmallPtrSet<MachineBasicBlock *, 8> Seen;
  auto isChainBarrier = [&](const MachineBasicBlock *B) {
    if (B == Barrier)
      return true;
    for (const MachineInstr &MI : *B)
      if (haydnClassifyHwloopSetupOpcode(MI.getOpcode()) !=
          HaydnHwloopSetupFamily::None)
        return true; // another loop's region starts here
    return false;
  };
  Worklist.push_back(Cur);
  while (!Worklist.empty()) {
    MachineBasicBlock *B = Worklist.pop_back_val();
    for (unsigned Hop = 0; Hop < 8 && B; ++Hop) {
      if (isPLETargetingHeader(findPseudoLoopEnd(B), B))
        return true;
      if (isChainBarrier(B))
        break; // outer back edge / next loop region — not this chain
      if (!Seen.insert(B).second || Visited.count(B))
        break;
      MachineFunction *MF = B->getParent();
      MachineBasicBlock *Fallthrough = nullptr;
      unsigned LiveSuccs = 0;
      for (MachineBasicBlock *Succ : B->successors()) {
        if (!isLiveMBB(*MF, Succ) || Seen.count(Succ) || Visited.count(Succ) ||
            isChainBarrier(Succ))
          continue;
        ++LiveSuccs;
        Fallthrough = Succ; // remember the last; explore extras below
      }
      if (LiveSuccs > 1) {
        // Guarded prologue: explore every unvisited live successor; the
        // guard edge fails the kernel test on its own subchain.
        for (MachineBasicBlock *Succ : B->successors()) {
          if (!isLiveMBB(*MF, Succ) || Seen.count(Succ) || Visited.count(Succ) ||
              isChainBarrier(Succ))
            continue;
          Worklist.push_back(Succ);
        }
        break;
      }
      B = Fallthrough;
    }
  }
  return false;
}

MachineBasicBlock *haydn::hwloop::resolveBodyMBBCore(MachineInstr &SetMI) {
  const MachineFunction *MF =
      SetMI.getParent() ? SetMI.getParent()->getParent() : nullptr;
  if (!MF)
    return nullptr;

  unsigned Opc = SetMI.getOpcode();
  if (Opc != Haydn::LoopStart &&
      SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB()) {
    // Any SET form (logical/wide/member) carries Header as op1.
    MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
    return isLiveMBB(*MF, H) ? H : nullptr;
  }

  // LoopStart: prefer a preheader successor that carries PseudoLoopEnd
  // (single-BB Header==Latch). Else the unique successor whose latch PLE
  // targets it (multi-BB Header!=Latch). Never invent a body from layout
  // order when several successors exist and none prove a latch.
  if (Opc == Haydn::LoopStart) {
    MachineBasicBlock *Pre = SetMI.getParent();
    MachineBasicBlock *Single = nullptr;
    MachineBasicBlock *FoundHeader = nullptr;
    for (MachineBasicBlock *Succ : Pre->successors()) {
      if (!isLiveMBB(*MF, Succ))
        continue;
      if (findPseudoLoopEnd(Succ)) {
        if (Single)
          return nullptr;
        Single = Succ;
      }
      if (resolveLoopStartLatch(Succ, Pre)) {
        if (FoundHeader && FoundHeader != Succ)
          return nullptr;
        FoundHeader = Succ;
      }
    }
    if (Single)
      return Single;
    if (FoundHeader)
      return FoundHeader;
    // Generic pre-RA SMS multi-stage peel shape (W68.1): the classic
    // ModuloScheduleExpander inserts PROLOGUES between the (new) preheader
    // and the kernel — preheader -> prologue -> ... -> kernel(self latch,
    // PseudoLoopEnd targets the kernel itself), with the LoopStart $adj
    // already crediting the peeled iterations. A prologue carries the
    // peeled iterations' real instructions and its trip guard, so it is
    // NOT an empty continue-trampoline (those stay Fixup-only: walking
    // them at formation would invent a body from layout). Proof here is
    // CFG shape + real-prologue content, never layout order alone.
    //
    // CB-166 (2026-08-27): with RUNTIME trip counts the prologues AND the
    // preheader carry DYNAMIC guards (createTripCountGreaterCondition), so
    // both may have TWO live successors — the guard-taken EPILOG edge and
    // the fall-through PROGRESS edge into the next prologue/kernel. The
    // lone-successor walk stops at the first guard. Walk the guarded chain
    // instead: try each live preheader successor as the chain entry —
    // exactly one must prove (bounded, cycle-safe, barrier-safe) that it
    // reaches the self-latched kernel; ambiguous -> refuse. Within the
    // chain each hop self-latches (kernel) or its progress edge is proven
    // the same way. The guard edge never proves the chain: it leaves for
    // an epilog/exit and any path back through the preheader or into
    // ANOTHER loop's region (a block carrying a hwloop setup is a
    // barrier) is refused. Pure CFG proof, no layout order, bounded by
    // the PPS-3 stage count.
    {
      MachineBasicBlock *Entry = nullptr;
      SmallPtrSet<MachineBasicBlock *, 8> EntryVisited;
      for (MachineBasicBlock *Succ : Pre->successors()) {
        if (!isLiveMBB(*MF, Succ) || isContinueTrampolineBlock(Succ))
          continue;
        if (prologueChainReachesKernel(Succ, EntryVisited, Pre)) {
          if (Entry && Entry != Succ) {
            Entry = nullptr;
            break;
          }
          Entry = Succ;
        }
      }
      if (Entry) {
        SmallPtrSet<MachineBasicBlock *, 8> Visited;
        MachineBasicBlock *Cur = Entry;
        for (unsigned Hop = 0; Hop < 8 && Cur; ++Hop) {
          if (isPLETargetingHeader(findPseudoLoopEnd(Cur), Cur))
            return Cur; // self-latched kernel
          if (!Visited.insert(Cur).second)
            break; // cycle — refuse
          // Progress edge: the successor that (transitively, as a lone or
          // guarded chain) reaches a self-latched kernel. Try each live
          // successor's chain; the guard edge fails the kernel test.
          MachineBasicBlock *Next = nullptr;
          for (MachineBasicBlock *Succ : Cur->successors()) {
            if (!isLiveMBB(*MF, Succ) || Visited.count(Succ))
              continue;
            if (prologueChainReachesKernel(Succ, Visited, Pre)) {
              if (Next && Next != Succ) {
                Next = nullptr;
                break;
              }
              Next = Succ;
            }
          }
          if (!Next)
            break;
          Cur = Next;
        }
      }
    }
    // No unique-successor-without-proof fallback: a lone preheader
    // successor may be the exit (layout-only body is incomplete).
  }
  return nullptr;
}

MachineBasicBlock *haydn::hwloop::resolveBodyMBBFixup(MachineInstr &SetMI) {
  if (MachineBasicBlock *B = resolveBodyMBBCore(SetMI))
    return B;
  if (SetMI.getOpcode() != Haydn::LoopStart)
    return nullptr;
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return nullptr;
  const MachineFunction *MF = Pre->getParent();
  MachineBasicBlock *Cand = Pre->getNextNode();
  for (unsigned Depth = 0; isLiveMBB(*MF, Cand) && Depth < 8;
       Cand = Cand->getNextNode(), ++Depth) {
    if (resolveLoopStartLatch(Cand, Pre))
      return Cand;
    if (!isContinueTrampolineBlock(Cand) || Cand->succ_size() != 1)
      return nullptr;
  }
  return nullptr;
}

static bool isPLETargetingHeader(const MachineInstr *MI,
                                 const MachineBasicBlock *Header) {
  if (!MI || !Header || MI->getOpcode() != Haydn::PseudoLoopEnd)
    return false;
  return MI->getNumOperands() > 0 && MI->getOperand(0).isMBB() &&
         MI->getOperand(0).getMBB() == Header;
}

MachineBasicBlock *
haydn::hwloop::resolveLoopStartLatch(MachineBasicBlock *Header,
                                     MachineBasicBlock *Preheader) {
  if (!Header)
    return nullptr;
  if (isPLETargetingHeader(findPseudoLoopEnd(Header), Header))
    return Header;
  MachineBasicBlock *Latch = nullptr;
  for (MachineBasicBlock *Pred : Header->predecessors()) {
    if (Pred == Preheader)
      continue;
    if (isPLETargetingHeader(findPseudoLoopEnd(Pred), Header)) {
      if (Latch)
        return nullptr;
      Latch = Pred;
    }
  }
  return Latch;
}

bool haydn::hwloop::isSoftLatchBnezOpcode(unsigned Opc) {
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
  return Log == Haydn::BNEZ_W || Log == Haydn::BNEZ;
}

bool haydn::hwloop::isContinueTrampolineBlock(const MachineBasicBlock *BB) {
  if (!BB)
    return false;
  // Hexagon FixupHwLoops.cpp:97-148 converts or leaves LOOP; it does not
  // invent a body. Haydn overlay: BR may splice an empty / LUI+ADDI+JALR
  // continue trampoline between preheader and body. Those blocks are not
  // a ZOL header — callers walk past them, then require latch proof.
  for (const MachineInstr &MI : BB->instrs()) {
    if (MI.isMetaInstruction() || MI.isCFIInstruction() || MI.isKill() ||
        MI.isImplicitDef() || MI.isBundle() || MI.isDebugInstr() ||
        MI.isPosition())
      continue;
    const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
    if (Log == Haydn::NOP || Log == Haydn::LUI || Log == Haydn::ADDI32_W ||
        Log == Haydn::JALR_W || Log == Haydn::JALR || Log == Haydn::B)
      continue;
    return false;
  }
  return true;
}

bool haydn::hwloop::isLiveMBB(const MachineFunction &MF,
                              const MachineBasicBlock *MBB) {
  return MBB && MBB->getParent() == &MF && MBB->getNumber() >= 0;
}

void haydn::hwloop::eraseEmptyBundleRoot(MachineInstr *BundleRoot) {
  if (!BundleRoot || !BundleRoot->isBundle() || !BundleRoot->getParent())
    return;
  MachineBasicBlock::instr_iterator Next =
      std::next(BundleRoot->getIterator());
  MachineBasicBlock *MBB = BundleRoot->getParent();
  if (Next != MBB->instr_end() && Next->isBundledWithPred())
    return; // still has children
  BundleRoot->eraseFromParent();
}

void haydn::hwloop::eraseInstrSafe(MachineInstr *MI) {
  if (!MI || !MI->getParent())
    return;
  MachineInstr *BundleRoot = nullptr;
  if (MI->isBundledWithPred()) {
    BundleRoot = &*getBundleStart(MI->getIterator());
    MI->unbundleFromPred();
  }
  if (MI->isBundledWithSucc())
    MI->unbundleFromSucc();
  MI->eraseFromParent();
  eraseEmptyBundleRoot(BundleRoot);
}

MachineInstr &haydn::hwloop::topLevelForLayout(MachineInstr &MI) {
  if (MI.isBundledWithPred() || MI.isBundledWithSucc())
    return *getBundleStart(MI.getIterator());
  return MI;
}

unsigned haydn::hwloop::lateMemberOpcode(unsigned LogicalOpc) {
  return haydn::bundle::lateProductMemberOpcode(LogicalOpc);
}

void haydn::hwloop::finalizeExactLateSingleton(MachineInstr &MI) {
  haydn::bundle::finalizeExactLateSingleton(MI);
}

void haydn::hwloop::recommitSurvivingCycleMembers(
    ArrayRef<MachineInstr *> Keep, const HaydnInstrInfo &TII,
    const char *DebugPrefix, AAResults *AA) {
  if (Keep.empty())
    return;

  for (MachineInstr *K : Keep) {
    if (!K || !K->getParent())
      continue;
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }

  SmallVector<MachineInstr *, 3> Live;
  Live.reserve(Keep.size());
  for (MachineInstr *K : Keep)
    if (K && K->getParent())
      Live.push_back(K);
  if (Live.empty())
    return;

  if (Live.size() == 1) {
    MachineInstr *MI = Live[0];
    unsigned Member = lateMemberOpcode(MI->getOpcode());
    if (Member != MI->getOpcode())
      MI->setDesc(TII.get(Member));
    finalizeExactLateSingleton(*MI);
    return;
  }

  // Multi-survivor coissue: the ONE production commit site (probe + exact
  // bake) rebuilds root operands/kills/internal-reads + Format E
  // row/completion. D1.52: never a probeless direct bake here — the probe
  // owns the store/load may-alias law (null AA fail-closed; proven-NoAlias
  // via forwarded AA), and the exact bake re-enforces it defensively. Fall
  // back to per-member singletons rather than leave bare reals if
  // membership is no longer one legal product cycle after SET removal.
  if (!haydn::bundle::commitOneProductCycle(Live, AA)) {
    LLVM_DEBUG(dbgs() << DebugPrefix << ": coissue survivors not one "
                         "legal multi-MI cycle after SET erase — "
                         "singleton exact-commit each\n");
    for (MachineInstr *MI : Live) {
      unsigned Member = lateMemberOpcode(MI->getOpcode());
      if (Member != MI->getOpcode())
        MI->setDesc(TII.get(Member));
      finalizeExactLateSingleton(*MI);
    }
  }
}

void haydn::hwloop::eraseSetMemberAndRecommitSiblings(
    MachineInstr &SetMI, const HaydnInstrInfo &TII, const char *DebugPrefix,
    AAResults *AA) {
  MachineBasicBlock *MBB = SetMI.getParent();
  if (!MBB)
    return;

  SmallVector<MachineInstr *, 3> Keep;
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    MachineInstr *Root = &*getBundleStart(SetMI.getIterator());
    for (MachineInstr *K : haydn::bundle::members(*Root)) {
      if (K != &SetMI)
        Keep.push_back(K);
    }
    // Dissolve every child from the old root before erasing the header so no
    // pass observes a half-unbundled multi-member shell.
    for (MachineInstr *K : Keep) {
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    if (SetMI.isBundledWithPred())
      SetMI.unbundleFromPred();
    if (SetMI.isBundledWithSucc())
      SetMI.unbundleFromSucc();
    Root->eraseFromParent();
  }

  SetMI.eraseFromParent();
  recommitSurvivingCycleMembers(Keep, TII, DebugPrefix, AA);
}

MachineInstrBuilder haydn::hwloop::buildExactLateDef(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &DL, const TargetInstrInfo &TII, unsigned LogicalOpc,
    Register Dest) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)),
                 Dest);
}

MachineInstrBuilder haydn::hwloop::buildExactLate(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &DL, const TargetInstrInfo &TII, unsigned LogicalOpc) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)));
}

void haydn::hwloop::collectLoopBlocks(const MachineBasicBlock *Header,
                                      const MachineBasicBlock *Latch,
                                      const MachineBasicBlock *Preheader,
                                      LoopBlockSet &Out) {
  Out.clear();
  if (!Header || !Latch)
    return;
  Out.insert(Header);
  Out.insert(Latch);
  if (Header == Latch)
    return;

  // Reverse CFG from Latch: every predecessor that is not Preheader and not
  // outside the loop. Cap work to avoid runaway on malformed CFG.
  SmallVector<const MachineBasicBlock *, 8> Work(Latch->pred_begin(),
                                                 Latch->pred_end());
  unsigned Guard = 0;
  while (!Work.empty() && Guard++ < 256) {
    const MachineBasicBlock *B = Work.pop_back_val();
    if (!B || B == Preheader)
      continue;
    if (!Out.insert(B).second)
      continue;
    if (B == Header)
      continue;
    for (const MachineBasicBlock *P : B->predecessors())
      Work.push_back(P);
  }
}

bool haydn::hwloop::isCountdownStepOf(const MachineInstr &MI, Register Reg) {
  if (!Reg.isPhysical() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isBundle())
    return false;
  // Must def Reg.
  bool Defs = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
      Defs = true;
      break;
    }
  }
  if (!Defs)
    return false;

  // Closed predicate: a countdown step is
  // LoopDec, or a same-reg immediate step whose GENERATED LOGICAL opcode is
  // an add/sub with proven ±1 immediate. Mapping through
  // logicalOpcodeOrSelf admits the Format E member forms of exactly those
  // ops and nothing else: any other (Reg, Reg, ±1)-shaped instruction
  // (e.g. SLLI32 Reg, Reg, 1) is real compute, and same-reg ADD32/SUB32
  // Prefer, Prefer, <reg> is an unproven step — post-RA may reuse the dead
  // trip physreg as a pointer bump; classifying either as a countdown lets
  // stripResidualCountdown erase live compute (silent wrong trip under
  // hwloops-ON + demote). Unproven steps are clobbers: demote picks a free
  // counter and materializes. Do not walk a reaching-def chain.
  if (MI.getOpcode() == Haydn::LoopDec)
    return true;

  // Register-rhs ADD32/SUB32 is an unverifiable addend even if a later
  // encoding shape grows an immediate-looking operand. Treat as clobber.
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
  if (Log == Haydn::ADD32 || Log == Haydn::SUB32)
    return false;

  auto logicalStepIsProvenCountdown = [&](int64_t Imm) -> bool {
    if (Log == Haydn::ADDI32 || Log == Haydn::ADDI32_W)
      return Imm == -1;
    if (Log == Haydn::SUBI32)
      return Imm == 1;
    return false;
  };
  if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
      MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm())
    return logicalStepIsProvenCountdown(MI.getOperand(2).getImm());
  return false;
}

// D1.34 ONE estimate law, shared authority. Prior state had three private
// copies (Fixup's estimateMBBDistance-internal pad, the demote's forked
// BackedgeBytes lambda with NO alignment charge, and the normalizer's
// static copy); a disagreement between them is exactly the class that let
// a site measure in-range at one seat while BranchRelaxation's scan
// measured far (the GR2.7 5-red kernels). One function, one law.
//
// The law is the joint parcel-grid / BR-conservative maximum (header
// contract): the charged start must be a whole-parcel multiple (the only
// in-tree input shape — getInstSizeInBytes charges only parcel multiples),
// at or past the first lcm(Align, Parcel) grid point (the MC emitter law,
// HaydnMCELFStreamer::emitCodeAlignment / HaydnMachineAlignment), AND at
// or past generic BranchRelaxation's postOffset model including the
// Alignment > ParentAlign uncertainty term. A pad below ANY of those is a
// span a later seat measures longer — the exact defect class this law
// closes (Bytes=12 / align 16 previously returned 24: neither aligned to
// 16 nor on the 48-byte joint grid).
int64_t haydn::hwloop::padLayoutBytesForMBBAlign(int64_t Bytes,
                                                 const MachineBasicBlock &MBB) {
  const Align A = MBB.getAlignment();
  if (A == Align(1) || Bytes < 0)
    return Bytes;
  const MachineFunction *MF = MBB.getParent();
  const Align PA = MF ? MF->getAlignment() : Align(1);
  const uint64_t Cur = static_cast<uint64_t>(Bytes);
  const uint64_t Parcel = haydn::bundle::productParcelBytes().Value;
  assert(Parcel != 0 && "product EncodedBytes must be non-zero");
  // (b) joint grid: first lcm(Align, Parcel) point at or past the start.
  const uint64_t Grid = std::lcm(A.value(), Parcel);
  const uint64_t GridTarget = alignTo(Cur, Grid);
  // (c) BranchRelaxation postOffset model: alignTo(PO, A), plus the
  // A > ParentAlign uncertainty (BR cannot tell whether extra padding
  // will be inserted, so it assumes the worst). Unrounded here; the
  // parcel rounding below is the one whole-parcel authority.
  const uint64_t BRWorst =
      alignTo(Cur, A) +
      (A > PA ? A.value() - PA.value() : 0);
  // Whole-parcel maximum of the two bounds. ceilProductParcels of the
  // byte gap keeps every charged start a parcel multiple (input shape),
  // and a parcel-rounded BR bound is >= its raw value (monotone max).
  uint64_t Pad = GridTarget - Cur;
  const uint64_t BRPad = BRWorst > Cur ? BRWorst - Cur : 0;
  if (BRPad > Pad)
    Pad = haydn::bundle::productBundlesToBytes(
        haydn::bundle::ceilProductParcels(static_cast<unsigned>(BRPad)));
  return Bytes + static_cast<int64_t>(Pad);
}

// D1.34 ONE byte-walk authority. Per-MBB signed start offsets in layout
// order, charging exactly padLayoutBytesForMBBAlign once per entered
// MBB. The entry block is exempt (function alignment sits outside the
// branch-distance window — the normalizer's convention, now the one
// law). Dead/foreign numbers keep the -1 sentinel so every consumer can
// refuse an unknown span (INV: -1 is never a displacement).
void haydn::hwloop::computeLayoutBlockStarts(
    const MachineFunction &MF, const TargetInstrInfo &TII,
    SmallVectorImpl<int64_t> &Starts) {
  Starts.assign(MF.getNumBlockIDs(), -1);
  int64_t Bytes = 0;
  bool First = true;
  for (const MachineBasicBlock &MBB : MF) {
    if (!First)
      Bytes = padLayoutBytesForMBBAlign(Bytes, MBB);
    First = false;
    if (MBB.getNumber() >= 0 &&
        MBB.getNumber() < static_cast<int>(Starts.size()))
      Starts[MBB.getNumber()] = Bytes;
    for (const MachineInstr &MI : MBB)
      Bytes += TII.getInstSizeInBytes(MI);
  }
}

// Intra-block site offset (BranchRelaxation getInstrOffset law). The skip
// law is getInstSizeInBytes alone (it returns 0 for meta/debug/kill/
// implicit-def/CFI/position); no private hand skip-set beside it.
int64_t haydn::hwloop::estimateLayoutInstrOffset(
    const MachineBasicBlock &MBB, MachineBasicBlock::const_iterator It,
    const TargetInstrInfo &TII) {
  int64_t Bytes = 0;
  for (auto I = MBB.begin(), E = MBB.end(); I != E; ++I) {
    if (I == It)
      return Bytes;
    Bytes += TII.getInstSizeInBytes(*I);
  }
  return Bytes; // It == end(): the whole-block size.
}

// Layout-order membership, not numeric order: empty blocks can share a
// numeric offset, so "To precedes From" is decidable only by walking the
// layout. Keeps the -1 sentinel exact for empty latch-before-header
// shapes (numeric equality would otherwise masquerade as span 0).
static bool blockAtOrAfterInLayout(const MachineFunction &MF,
                                   const MachineBasicBlock *From,
                                   const MachineBasicBlock *To) {
  if (!From || !To)
    return false;
  bool Started = false;
  for (const MachineBasicBlock &MBB : MF) {
    if (&MBB == From)
      Started = true;
    if (Started && &MBB == To)
      return true;
  }
  return false;
}

// D1.34 derivation of the one byte walk: the FromIt-exclusive to-To-start
// distance. Semantics identical to the previous dedicated loop for every
// forward layout (entry pad exempt, per-entered-MBB pad, sizes from
// TII.getInstSizeInBytes); -1 when ToMBB never starts at or after FromIt.
// (To == FromMBB with FromIt past begin now refuses with -1 instead of
// the old remaining-block sum — a same-block "distance to its own start"
// is not a span any caller may consume; refusal is the fail-closed fix.)
int64_t haydn::hwloop::estimateLayoutMBBDistance(
    const MachineFunction &MF, const MachineBasicBlock *FromMBB,
    MachineBasicBlock::const_iterator FromIt, const MachineBasicBlock *ToMBB,
    const TargetInstrInfo &TII) {
  if (!isLiveMBB(MF, FromMBB) || !isLiveMBB(MF, ToMBB))
    return -1;
  if (!blockAtOrAfterInLayout(MF, FromMBB, ToMBB))
    return -1; // To precedes From in layout.
  SmallVector<int64_t, 32> Starts;
  computeLayoutBlockStarts(MF, TII, Starts);
  // isLiveMBB proved both numbers are >= 0 and parented by MF; bound the
  // lookup so a renumbered-out number can never index past the scan (the
  // -1 sentinel is then the only failure shape).
  const auto lookup = [&Starts](const MachineBasicBlock *B) -> int64_t {
    const int64_t N = B->getNumber();
    return (N >= 0 && N < static_cast<int64_t>(Starts.size()))
               ? Starts[N]
               : -1;
  };
  const int64_t FromStart = lookup(FromMBB);
  const int64_t ToStart = lookup(ToMBB);
  if (FromStart < 0 || ToStart < 0)
    return -1;
  const int64_t FromSite = FromStart + estimateLayoutInstrOffset(
                                           *FromMBB, FromIt, TII);
  if (ToStart < FromSite)
    return -1; // To's start precedes the From site inside FromMBB.
  return ToStart - FromSite;
}

// D1.34 derivation of the one byte walk: the Header-begin..Latch-end span
// the demote LongLatch decision consumes. The old walk charged a private
// hand skip-set (meta/debug/kill/implicit-def/CFI/position) on top of
// getInstSizeInBytes — dead weight: the size oracle already returns 0 for
// exactly those. Deleted; the span is the single-walk expression.
int64_t haydn::hwloop::estimateLayoutSpanBytes(
    const MachineFunction &MF, const MachineBasicBlock *FromMBB,
    const MachineBasicBlock *ToMBB, const TargetInstrInfo &TII) {
  if (!isLiveMBB(MF, FromMBB) || !isLiveMBB(MF, ToMBB))
    return -1;
  if (!blockAtOrAfterInLayout(MF, FromMBB, ToMBB))
    return -1; // To precedes From in layout (latch-before-header).
  SmallVector<int64_t, 32> Starts;
  computeLayoutBlockStarts(MF, TII, Starts);
  const int64_t FromStart =
      (FromMBB->getNumber() >= 0 &&
       FromMBB->getNumber() < static_cast<int>(Starts.size()))
          ? Starts[FromMBB->getNumber()]
          : -1;
  const int64_t ToStart =
      (ToMBB->getNumber() >= 0 &&
       ToMBB->getNumber() < static_cast<int>(Starts.size()))
          ? Starts[ToMBB->getNumber()]
          : -1;
  if (FromStart < 0 || ToStart < 0)
    return -1;
  const int64_t ToEnd =
      ToStart + estimateLayoutInstrOffset(*ToMBB, ToMBB->end(), TII);
  assert(ToEnd >= FromStart && "layout-order-checked span must be monotone");
  return ToEnd - FromStart;
}

bool haydn::hwloop::regMentionedInBlocks(Register Reg,
                                         const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
      }
    }
  }
  return false;
}

bool haydn::hwloop::regClobberedNonCountdownIn(Register Reg,
                                               const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      bool Defs = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
          Defs = true;
          break;
        }
      }
      if (!Defs)
        continue;
      if (isCountdownStepOf(MI, Reg))
        continue;
      return true;
    }
  }
  return false;
}

HwLoopDemoteSaveKind
haydn::hwloop::demoteSavePlacement(bool PreferIsLatchScratch,
                                   bool PreferRedefinedInBody) {
  // The save/restore pair exists only when the demote's own latch-scratch
  // window destroys Prefer's exit value; with any other scratch nothing
  // installed touches Prefer and a reload would overwrite the live exit
  // value (CB-165). Where the save runs is decided by which value must
  // survive: the untouched trip (CB-162) or the body's final def.
  if (!PreferIsLatchScratch)
    return HwLoopDemoteSaveKind::NoSave;
  return PreferRedefinedInBody ? HwLoopDemoteSaveKind::LatchEndSave
                               : HwLoopDemoteSaveKind::PreheaderSave;
}

HwLoopDemoteSaveKind
haydn::hwloop::demoteSavePlacement(Register Prefer, Register LatchScr,
                                   const LoopBlockSet &Blocks) {
  return demoteSavePlacement(/*PreferIsLatchScratch=*/LatchScr == Prefer,
                             regClobberedNonCountdownIn(Prefer, Blocks));
}

// D1.19 admission law for the stack-counter demote arm — the closed case
// matrix is documented at the declaration in HaydnHWLoopDemote.h. The one
// non-obvious cell is (e): Adj!=0 with no PreheaderScr and LatchScr==Prefer.
// The remaining kernel trip is Prefer+Adj, and the only register that could
// hold it for the ST32 is Prefer itself — but the ADDI addend dest must
// never be Prefer (an in-place ADDI destroys the trip value Prefer still
// carries across the loop; same LC-vs-src law that forbids CountReg==Prefer
// when Adj!=0). Storing Prefer unchanged stores the FULL trip N while the
// kernel must run N-S, so the S peeled iterations re-execute (duplicated
// side effects / wrong exit). Refuse; the caller's fail-closed ladders own
// the fatal. Every other cell either has a proved-sound emission above or
// was already refused: (a)/(f) latch/imm-trip scratch laws are unchanged,
// (b) stores Prefer directly (Adj==0: remaining trip IS N), (c) ADDI into
// the probed PreheaderScr != Prefer, (d) copy fallback PreheaderScr=
// LatchScr != Prefer.
bool haydn::hwloop::demoteStackCounterAdmissible(bool LatchScrValid,
                                                 bool HasImm,
                                                 bool PreheaderScrValid,
                                                 bool AdjNonZero,
                                                 bool LatchScrIsPrefer) {
  // (a) No spill-free non-R0 latch scratch -> no sound latch window.
  if (!LatchScrValid)
    return false;
  // (f) Imm-trip materialize needs its own preheader scratch; HasImm never
  // carries an Adj (SET_* rematted the addend at Role-A expand).
  if (HasImm)
    return PreheaderScrValid;
  // (b) Adj==0: remaining trip == full trip; store Prefer directly.
  if (!AdjNonZero)
    return true;
  // (e) Adj!=0 with no PreheaderScr and LatchScr==Prefer: no sound store
  // window (ADDI dest must differ from Prefer). (c)/(d) any other
  // PreheaderScr — probed or copied-from-LatchScr(!=Prefer) — is sound.
  return PreheaderScrValid || !LatchScrIsPrefer;
}

// A demote counter's live range: the loop blocks (every one lies on a
// def->latch-BNEZ path — collectLoopBlocks is reverse-reachable from the
// latch) plus the preheader tail from the materialize point.
// Law — this function must OWN the register across that range:
//  * ABI callee-saved (R8-R11/R14/D8-D15): owned iff this function's
//    prologue actually saves it (CalleeSavedInfo; demote runs post-PEI at
//    both seats — formation addPreSched2 and fixup addPreEmit — so an
//    unsaved CSR write is never later repaired: silent breakage of OUR
//    caller's live value). Saved ⇒ also call-safe: callees restore it and
//    our epilogue restores the caller's value.
//  * caller-saved: freely owned by the callee, but any call on the range
//    clobbers it mid-loop (corrupted trip). A call's clobbers are regmask
//    operands — invisible to the explicit-def walk above (verifier:
//    "Using an undefined physical register"; runtime: wrong loop bound).
// No qualifying register ⇒ the stack-counter demote path (PostRAScratchFI
// home; latch scratch defined entirely after the last call) is the sink.
bool haydn::hwloop::isSoundDemoteCounter(
    MCPhysReg Reg, const LoopBlockSet &Blocks, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom, const MachineFunction &MF,
    const TargetRegisterInfo &TRI) {
  // Preheader tail after SET/LoopStart still executes with the software
  // counter live. Following work is legal ZOL setup distance (SET is not a
  // scheduling boundary), so a GPR scavenged as free at the SET site may
  // still be defined as address scratch before the header. Using it as the
  // countdown leaves the trip clobbered (va-arg-22 -O2: r3 = sp+off then
  // SUBI/BNEZ r3 → MEMORY_FAULT). Skip the setup MI and its bundle.
  if (Preheader && PreheaderFrom != Preheader->end()) {
    bool PastSetup = false;
    bool InSetupBundle = false;
    for (const MachineInstr &MI : Preheader->instrs()) {
      if (!PastSetup) {
        if (&MI == &*PreheaderFrom) {
          PastSetup = true;
          InSetupBundle = MI.isBundle() || MI.isBundledWithSucc();
        }
        continue;
      }
      if (InSetupBundle) {
        if (MI.isBundledWithPred())
          continue;
        InSetupBundle = false;
      }
      if (MI.isMetaInstruction() || MI.isDebugInstr() ||
          MI.isCFIInstruction() || MI.isImplicitDef() || MI.isKill() ||
          MI.isBundle())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg().isPhysical() &&
            TRI.regsOverlap(MO.getReg(), Reg))
          return false;
      }
    }
  }

  // ABI CSR membership: the save list is the inter-procedural contract.
  bool IsABICalleeSaved = false;
  for (const MCPhysReg *CSR = TRI.getCalleeSavedRegs(&MF); CSR && *CSR; ++CSR)
    if (*CSR == Reg)
      IsABICalleeSaved = true;

  if (IsABICalleeSaved) {
    for (const CalleeSavedInfo &CI : MF.getFrameInfo().getCalleeSavedInfo())
      if (CI.getReg() == Reg)
        return true;
    return false;
  }

  // Caller-saved: refuse when any call on the counter's live range fails to
  // preserve it. A call with no regmask at all is underdescribed: refuse.
  auto callOnRangeClobbers = [&]() -> bool {
    for (const MachineBasicBlock *MBB : Blocks) {
      if (!MBB)
        continue;
      for (const MachineInstr &MI : MBB->instrs()) {
        if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle() ||
            !MI.isCall())
          continue;
        bool HasRegMask = false;
        bool Preserved = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isRegMask())
            continue;
          HasRegMask = true;
          if (!MO.clobbersPhysReg(Reg))
            Preserved = true;
        }
        if (!HasRegMask || !Preserved)
          return true;
      }
    }
    if (!Preheader)
      return false;
    for (auto It = PreheaderFrom; It != Preheader->end(); ++It) {
      const MachineInstr &MI = *It;
      if (MI.isMetaInstruction() || MI.isDebugInstr() || !MI.isCall())
        continue;
      bool HasRegMask = false;
      bool Preserved = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;
        HasRegMask = true;
        if (!MO.clobbersPhysReg(Reg))
          Preserved = true;
      }
      if (!HasRegMask || !Preserved)
        return true;
    }
    return false;
  };
  return !callOnRangeClobbers();
}

void haydn::hwloop::stripResidualCountdown(const LoopBlockSet &Blocks,
                                           Register Reg) {
  if (!Reg.isPhysical())
    return;
  SmallVector<MachineInstr *, 4> Kill;
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB)
      continue;
    for (MachineInstr &MI : const_cast<MachineBasicBlock *>(BB)->instrs()) {
      if (isCountdownStepOf(MI, Reg) && MI.getOpcode() != Haydn::LoopDec)
        Kill.push_back(&MI);
    }
  }
  for (MachineInstr *MI : Kill)
    eraseInstrSafe(MI);
}

void haydn::hwloop::computeBlockLiveIns(LivePhysRegs &Live,
                                        const MachineBasicBlock &MBB) {
  // Generic llvm::computeLiveIns is the same walk with addLiveOutsNoPristines.
  // addLiveOuts includes pristines so unused CSRs stay fail-closed occupied.
  const MachineFunction &MF = *MBB.getParent();
  Live.init(*MF.getRegInfo().getTargetRegisterInfo());
  Live.addLiveOuts(MBB);
  for (const MachineInstr &MI : llvm::reverse(MBB))
    Live.stepBackward(MI);
}

void haydn::hwloop::computeBlockLiveInsFromSuccessors(
    LivePhysRegs &Live, const MachineBasicBlock &MBB,
    ArrayRef<const MachineBasicBlock *> Succs) {
  const MachineFunction &MF = *MBB.getParent();
  Live.init(*MF.getRegInfo().getTargetRegisterInfo());
  bool Seeded = false;
  for (const MachineBasicBlock *S : Succs) {
    if (!S)
      continue;
    if (S == &MBB) {
      // Self-loop: stored liveins are the loop-carried set the rewriter
      // keeps. Extra pre-rewrite successors are not in Succs.
      for (const MachineBasicBlock::RegisterMaskPair &LI : MBB.liveins())
        Live.addReg(LI.PhysReg);
      Seeded = true;
      continue;
    }
    LivePhysRegs SL;
    computeBlockLiveIns(SL, *S);
    for (MCPhysReg R : SL)
      Live.addReg(R);
    Seeded = true;
  }
  if (!Seeded) {
    computeBlockLiveIns(Live, MBB);
    return;
  }
  for (const MachineInstr &MI : llvm::reverse(MBB))
    Live.stepBackward(MI);
}

bool haydn::hwloop::blockLiveInContains(const MachineBasicBlock &MBB,
                                        MCPhysReg Reg) {
  LivePhysRegs Live;
  computeBlockLiveIns(Live, MBB);
  return Live.contains(Reg);
}

bool haydn::hwloop::blockLiveInContainsFromSuccessors(
    const MachineBasicBlock &MBB, ArrayRef<const MachineBasicBlock *> Succs,
    MCPhysReg Reg) {
  LivePhysRegs Live;
  computeBlockLiveInsFromSuccessors(Live, MBB, Succs);
  return Live.contains(Reg);
}

Register haydn::hwloop::pickCounterReg(
    const LoopBlockSet &Blocks, Register Prefer, const HaydnSubtarget &ST,
    MachineBasicBlock &Preheader, MachineBasicBlock::iterator InsertPt,
    const char *DebugPrefix) {
  // Free counter must be:
  // 1) not mentioned in any CFG loop block (lc_dp_lis: layout range missed
  //    latch earlier in the function — CFG Blocks is required);
  // 2) available at the SET insert point (LivePhysRegs — AIE/RISC-V style
  //    post-RA scavenge, not "first preferred even if live");
  // 3) owned by this function across the counter's live range (preheader
  //    tail defs/uses after SET, call regmask clobbers / unsaved-CSR):
  //    isSoundDemoteCounter, same law as Prefer;
  // 4) dead on every loop exit edge (CB-162): the software countdown
  //    destroys the register; a value live for later users (the bkfir16x16
  //    descriptor NBLK/M fields) must never be the countdown.
  // Fail-closed: return invalid Register rather than Prefer/R11 when both
  // are live (old code clobbered live-through temps under demote).
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = Preheader.getParent()->getRegInfo();
  const MachineFunction &MF = *Preheader.getParent();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(Preheader);
  for (MachineBasicBlock::iterator II = Preheader.end(); II != InsertPt;) {
    --II;
    LPR.stepBackward(*II);
  }

  auto isUsable = [&](Register R) -> bool {
    if (!R.isPhysical() || R == Haydn::R0 || R == Haydn::R13 || R == Haydn::R15)
      return false;
    if (MRI.isReserved(R))
      return false;
    if (regMentionedInBlocks(R, Blocks))
      return false;
    for (const MachineBasicBlock *B : Blocks) {
      if (!B)
        continue;
      for (const MachineBasicBlock *S : B->successors()) {
        if (Blocks.contains(S))
          continue;
        if (blockLiveInContains(*S, R.asMCReg())) {
          LLVM_DEBUG(dbgs()
                     << DebugPrefix << ": pickCounterReg reject "
                     << printReg(R, &TRI) << " live after loop exit ("
                     << printMBBReference(*S) << ")\n");
          return false;
        }
      }
    }
    // Counter ownership law (calls / callee-saved): the materialize point is
    // InsertPt — the range is the preheader tail from there plus the blocks.
    if (!isSoundDemoteCounter(R.asMCReg(), Blocks, &Preheader,
                              MachineBasicBlock::const_iterator(InsertPt), MF,
                              TRI))
      return false;
    if (!LPR.available(MRI, R))
      return false;
    return true;
  };

  if (isUsable(Prefer))
    return Prefer;

  // Call-clobbered temps first. R12 last (normal GPR; AIE: no free AT).
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R7,
      Haydn::R4,  Haydn::R3,  Haydn::R2, Haydn::R1, Haydn::R12};
  for (MCPhysReg R : CandsGPR) {
    if (Prefer.isPhysical() && R == Prefer)
      continue;
    if (isUsable(R))
      return R;
  }
  LLVM_DEBUG(dbgs() << DebugPrefix << ": pickCounterReg — no free GPR "
                       "(LivePhysRegs + loop mention); demote refuses "
                       "erase-only on live body\n");
  return Register();
}

void haydn::hwloop::materializeTripCount(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt, DebugLoc DL,
    const HaydnInstrInfo &TII, Register Dst, Register SrcReg, int64_t SrcImm,
    bool HasImm) {
  // Every demote trip-materialize MI is exact-committed before the
  // second BranchRelaxation (shared commitLateProductCycle surface).
  if (!HasImm) {
    if (SrcReg == Dst)
      return;
    // Post-RA: real MOVE32 (not generic COPY — expand-pseudos already ran).
    // emitExactLateDef commits the Format E member first; members are
    // dest+src only. Logical MOVE32's vestigial rs2=rs1 must not be added
    // (verifier: extra explicit operand on non-variadic member).
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::MOVE32, Dst,
                     [&](MachineInstrBuilder MIB) { MIB.addReg(SrcReg); });
    return;
  }

  // uimm16 trip counts: XOR-zero then ADDI32_W (expandPostRA already ran).
  if (SrcImm == 0) {
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                     });
    return;
  }
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                   });
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::ADDI32_W, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Dst).addImm(SrcImm);
                   });
}
