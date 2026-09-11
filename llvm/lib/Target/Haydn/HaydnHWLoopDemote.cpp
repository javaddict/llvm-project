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
#include "HaydnFrameLowering.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <memory>
#include <numeric>
#include <string>

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

namespace {

bool skipLatchLawMI(const MachineInstr &MI) {
  return MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle() ||
         MI.isCFIInstruction() || MI.isKill() || MI.isImplicitDef() ||
         MI.isPosition();
}

unsigned latchLogical(const MachineInstr &MI) {
  return haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
}

const MachineBasicBlock *latchMBBOperand(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isMBB())
      return MO.getMBB();
  return nullptr;
}

bool isLatchJalr(unsigned Log, unsigned Opc) {
  // Returning JALR_CALL is isCall, not a CFG JALR terminator.
  if (Opc == Haydn::JALR_CALL)
    return false;
  return Log == Haydn::JALR || Log == Haydn::JALR_W;
}

bool isLatchAddi(unsigned Log) {
  return Log == Haydn::ADDI32_W || Log == Haydn::ADDI32;
}

bool isLatchCond(unsigned Log, unsigned /*Opc*/) {
  return Log == Haydn::BEQZ || Log == Haydn::BEQZ_W || Log == Haydn::BNEZ ||
         Log == Haydn::BNEZ_W;
}

bool parseLd32Addr(const MachineInstr &MI, Register &Dest, Register &Base,
                   int64_t &Imm) {
  if (MI.getNumExplicitOperands() < 3)
    return false;
  const MachineOperand &D = MI.getOperand(0);
  const MachineOperand &B = MI.getOperand(1);
  const MachineOperand &I = MI.getOperand(2);
  if (!D.isReg() || !B.isReg() || !I.isImm())
    return false;
  Dest = D.getReg();
  Base = B.getReg();
  Imm = I.getImm();
  return Dest.isPhysical() && Base.isPhysical();
}

// Product catalog aliases of LD32/ST32. lateProductMemberOpcode peels
// ST32 through productSolveLogicalOpcode onto S_SW_WITH_IMM (and LD32
// onto S_LW_WITH_IMM); logicalOpcodeOrSelf of the baked member is the
// catalog name, not ST32/LD32. The Fixup suffix walk must treat those
// as stack-counter glue or it false-fatals a legal LatchScr=r7 demote
// (va-arg-22 ok.mir: S_SW sits between SUBI32 and the counted edge).
bool isLatchCountdownSubi(unsigned Log) { return Log == Haydn::SUBI32; }

bool isLatchCountdownLoad(unsigned Log) {
  return Log == Haydn::LD32 || Log == Haydn::S_LW_WITH_IMM;
}

bool isLatchCountdownStore(unsigned Log) {
  return Log == Haydn::ST32 || Log == Haydn::S_SW_WITH_IMM;
}

bool parseSt32Addr(const MachineInstr &MI, Register &Val, Register &Base,
                   int64_t &Imm) {
  if (MI.getNumExplicitOperands() < 3)
    return false;
  const MachineOperand &V = MI.getOperand(0);
  const MachineOperand &B = MI.getOperand(1);
  const MachineOperand &I = MI.getOperand(2);
  if (!V.isReg() || !B.isReg() || !I.isImm())
    return false;
  Val = V.getReg();
  Base = B.getReg();
  Imm = I.getImm();
  return Val.isPhysical() && Base.isPhysical();
}

bool stackCounterHomeMatches(const MachineFunction &MF,
                             const MachineBasicBlock &Latch, Register Base,
                             int64_t Elem) {
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  const HaydnFrameLowering *TFL =
      MF.getSubtarget<HaydnSubtarget>().getFrameLowering();
  if (!FuncInfo || !TFL)
    return false;
  // D1.102: this latch's assigned FI, not membership in the assigned pool.
  const int FI = FuncInfo->getHwLoopStackCounterFIForLatch(&Latch);
  if (FI < 0)
    return false;
  Register FrameReg;
  const int64_t Off =
      TFL->getFrameIndexReference(MF, FI, FrameReg).getFixed();
  if ((Off % 4) != 0 || !isInt<6>(Off / 4))
    return false;
  return FrameReg == Base && (Off / 4) == Elem;
}

bool isShortExitFollower(const MachineInstr &MI, unsigned Log) {
  if (Log == Haydn::B || Log == Haydn::BEQZ_W || Log == Haydn::NOP)
    return true;
  return Log == Haydn::BEQZ && MI.getNumOperands() >= 1 &&
         MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == Haydn::R0;
}

struct LatchMemAddr {
  Register Data;
  Register Base;
  int64_t Imm;
};

} // namespace

std::string haydn::hwloop::countedSoftwareLatchViolation(
    const MachineBasicBlock &MBB, bool RequireLatch) {
  const MachineFunction *MF = MBB.getParent();
  if (!MF)
    return {};

  SmallPtrSet<const MachineBasicBlock *, 4> LuiMBBs;
  SmallPtrSet<const MachineBasicBlock *, 4> AddiMBBs;
  bool HasJalr = false;
  bool HasSubi = false;
  SmallVector<LatchMemAddr, 4> Loads;
  SmallVector<LatchMemAddr, 4> Stores;
  SmallVector<Register, 4> SubiDests;

  for (const MachineInstr &MI : MBB.instrs()) {
    if (skipLatchLawMI(MI))
      continue;
    const unsigned Opc = MI.getOpcode();
    const unsigned Log = latchLogical(MI);
    if (Log == Haydn::SUBI32) {
      HasSubi = true;
      if (MI.getNumExplicitOperands() >= 1 && MI.getOperand(0).isReg())
        SubiDests.push_back(MI.getOperand(0).getReg());
    }
    if (Log == Haydn::LUI) {
      if (const MachineBasicBlock *T = latchMBBOperand(MI))
        LuiMBBs.insert(T);
    }
    if (isLatchAddi(Log)) {
      if (const MachineBasicBlock *T = latchMBBOperand(MI))
        AddiMBBs.insert(T);
    }
    if (isLatchJalr(Log, Opc))
      HasJalr = true;
    if (isLatchCountdownLoad(Log)) {
      Register D, B;
      int64_t Imm;
      if (parseLd32Addr(MI, D, B, Imm))
        Loads.push_back({D, B, Imm});
    } else if (isLatchCountdownStore(Log)) {
      Register V, B;
      int64_t Imm;
      if (parseSt32Addr(MI, V, B, Imm))
        Stores.push_back({V, B, Imm});
    }
  }

  bool HasStackCounter = false;
  for (const LatchMemAddr &Ld : Loads) {
    bool SubiOf = false;
    for (Register R : SubiDests) {
      if (R == Ld.Data) {
        SubiOf = true;
        break;
      }
    }
    if (!SubiOf)
      continue;
    for (const LatchMemAddr &St : Stores) {
      if (St.Data != Ld.Data || St.Base != Ld.Base || St.Imm != Ld.Imm)
        continue;
      HasStackCounter = true;
      if (!stackCounterHomeMatches(*MF, MBB, Ld.Base, Ld.Imm))
        return "counted software latch: stack-counter LD/ST does not match "
               "this latch's assigned dedicated counter home";
    }
  }

  if (!HasSubi && !HasStackCounter)
    return {};

  const MachineBasicBlock *LongTarget = nullptr;
  for (const MachineBasicBlock *T : LuiMBBs) {
    if (AddiMBBs.contains(T)) {
      LongTarget = T;
      break;
    }
  }
  const bool LongForm = LongTarget && HasJalr;

  enum LongStage {
    LS_Body,
    LS_SeenLUI,
    LS_SeenADDI,
    LS_SeenCond,
    LS_SeenJALR
  } Stage = LS_Body;
  bool SeenCountedEdge = false;

  for (const MachineInstr &MI : MBB.instrs()) {
    if (skipLatchLawMI(MI))
      continue;
    const unsigned Opc = MI.getOpcode();
    const unsigned Log = latchLogical(MI);
    const bool Pad = Log == Haydn::NOP;

    if (LongForm) {
      if (Stage == LS_Body) {
        if (Log == Haydn::LUI && latchMBBOperand(MI) == LongTarget) {
          Stage = LS_SeenLUI;
          continue;
        }
        continue;
      }
      if (Stage == LS_SeenLUI) {
        if (isLatchAddi(Log) && latchMBBOperand(MI) == LongTarget) {
          Stage = LS_SeenADDI;
          continue;
        }
        if (Pad)
          continue;
        // Complete-order pin is LUI before ADDI, not adjacency. S2 may
        // interleave independent body ops between the template parcels
        // while preserving LUI→ADDI RAW. Cond/JALR here means ADDI was
        // skipped or appeared before LUI (the D1.32 misorder).
        if (isLatchCond(Log, Opc) || isLatchJalr(Log, Opc))
          return "counted software latch: ADDI must follow LUI";
        continue;
      }
      if (Stage == LS_SeenADDI) {
        if (isLatchCond(Log, Opc)) {
          Stage = LS_SeenCond;
          SeenCountedEdge = true;
          continue;
        }
        if (Pad)
          continue;
        if (isLatchJalr(Log, Opc))
          return "counted software latch: cond (BEQZ or BNEZ) must follow ADDI";
        continue;
      }
      if (Stage == LS_SeenCond) {
        if (isLatchJalr(Log, Opc)) {
          Stage = LS_SeenJALR;
          continue;
        }
        if (Pad)
          continue;
        return "counted software latch: JALR must follow cond";
      }
      if (Pad)
        continue;
      return "counted software latch: nothing may follow JALR";
    }

    if (isSoftLatchBnezOpcode(Opc)) {
      Register Tested;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.readsReg() && MO.getReg().isPhysical() &&
            MO.getReg() != Haydn::SFR) {
          Tested = MO.getReg();
          break;
        }
      }
      bool Counted = false;
      for (Register R : SubiDests) {
        if (R && Tested && R == Tested) {
          Counted = true;
          break;
        }
      }
      if (!Counted) {
        // D1.105 extra-exit cond of a non-countdown GPR may precede the
        // counted edge (SUBI32 sits before the first terminator).
        if (SeenCountedEdge)
          return "counted software latch: only exit B / BEQZ_W / "
                 "BEQZ-on-R0 / NOP may follow BNEZ";
        continue;
      }
      SeenCountedEdge = true;
      continue;
    }
    if (!SeenCountedEdge)
      continue;
    if (isShortExitFollower(MI, Log))
      continue;
    return "counted software latch: only exit B / BEQZ_W / "
           "BEQZ-on-R0 / NOP may follow BNEZ";
  }

  if (LongForm && Stage != LS_SeenJALR)
    return "counted software latch: incomplete LUI → ADDI → cond → JALR";
  if (RequireLatch && !SeenCountedEdge)
    return "counted software latch: missing counted edge";
  return {};
}

std::string haydn::hwloop::demoteSaveHomePairViolation(
    const MachineBasicBlock &Latch) {
  const MachineFunction *MF = Latch.getParent();
  if (!MF)
    return {};
  auto *FuncInfo = MF->getInfo<HaydnMachineFunctionInfo>();
  if (!FuncInfo)
    return {};
  const int SaveFI = FuncInfo->getHwLoopDemoteSaveFIForLatch(&Latch);
  if (SaveFI < 0)
    return {};
  const int CounterFI = FuncInfo->getHwLoopStackCounterFIForLatch(&Latch);
  if (CounterFI >= 0 && SaveFI == CounterFI)
    return "demote save home aliases this latch's assigned counter FI";
  for (const MachineBasicBlock &Other : *MF) {
    if (&Other == &Latch)
      continue;
    if (FuncInfo->getHwLoopDemoteSaveFIForLatch(&Other) == SaveFI)
      return "demote save home bound to two latches";
  }

  auto mmoFI = [](const MachineInstr &MI) -> int {
    int FI = -1;
    unsigned N = 0;
    for (const MachineMemOperand *MMO : MI.memoperands()) {
      const PseudoSourceValue *PSV = MMO->getPseudoValue();
      if (const auto *FS =
              dyn_cast_or_null<FixedStackPseudoSourceValue>(PSV)) {
        FI = FS->getFrameIndex();
        ++N;
      }
    }
    return N == 1 ? FI : -1;
  };

  bool HasStore = false;
  bool HasLoad = false;
  for (const MachineBasicBlock &BB : *MF) {
    for (const MachineInstr &MI : BB.instrs()) {
      if (MI.getOpcode() == TargetOpcode::BUNDLE)
        continue;
      const unsigned Log = latchLogical(MI);
      if (mmoFI(MI) != SaveFI)
        continue;
      if (isLatchCountdownStore(Log))
        HasStore = true;
      else if (isLatchCountdownLoad(Log))
        HasLoad = true;
    }
  }
  if (!HasStore)
    return "demote save home: missing ST32/S_SW FixedStack pair for this latch";
  // Emitter restores Prefer at Exit->begin() only when isLiveMBB(Exit)
  // (HaydnHardwareLoops.cpp PendingSaveRestore gate). A dead Exit has no
  // matching LD; requiring it here self-fatals a legal demote.
  const MachineBasicBlock *Header = nullptr;
  const MachineBasicBlock *Exit = nullptr;
  for (const MachineInstr &MI : Latch.instrs()) {
    if (MI.getOpcode() == TargetOpcode::BUNDLE)
      continue;
    const unsigned Log = latchLogical(MI);
    const unsigned Opc = MI.getOpcode();
    if (isSoftLatchBnezOpcode(Opc) || isLatchJalr(Log, Opc)) {
      if (const MachineBasicBlock *T = latchMBBOperand(MI))
        Header = T;
    }
    if (Log == Haydn::B || Log == Haydn::BEQZ_W ||
        (Log == Haydn::BEQZ && MI.getNumOperands() >= 1 &&
         MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == Haydn::R0)) {
      if (const MachineBasicBlock *T = latchMBBOperand(MI))
        Exit = T;
    }
  }
  if (!Exit) {
    for (const MachineBasicBlock *S : Latch.successors()) {
      if (S != Header) {
        Exit = S;
        break;
      }
    }
  }
  const bool NeedLoad = Exit ? isLiveMBB(*MF, Exit) : true;
  if (NeedLoad && !HasLoad)
    return "demote save home: missing LD32/S_LW FixedStack pair for this latch";
  return {};
}

bool haydn::hwloop::latchSuffixHasLegalCountdown(
    const MachineBasicBlock &Latch, Register Prefer,
    bool ForbidPreferCountdown) {
  if (!Prefer.isPhysical())
    ForbidPreferCountdown = false;
  bool SeenTerm = false;
  for (MachineBasicBlock::const_instr_iterator I = Latch.instr_end();
       I != Latch.instr_begin();) {
    --I;
    if (I->isTerminator()) {
      SeenTerm = true;
      continue;
    }
    if (!SeenTerm)
      continue;
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isKill() ||
        I->isCFIInstruction() || I->isImplicitDef() || I->isPosition() ||
        I->getOpcode() == TargetOpcode::BUNDLE)
      continue;
    const unsigned Log = latchLogical(*I);
    if (Log == Haydn::NOP || Log == Haydn::LUI || Log == Haydn::ADDI32 ||
        Log == Haydn::ADDI32_W)
      continue;
    if (isLatchCountdownSubi(Log) || isLatchCountdownLoad(Log) ||
        isLatchCountdownStore(Log)) {
      if (ForbidPreferCountdown &&
          (isLatchCountdownSubi(Log) || isLatchCountdownLoad(Log)) &&
          I->getNumOperands() >= 1 && I->getOperand(0).isReg() &&
          I->getOperand(0).getReg() == Prefer)
        report_fatal_error(
            "HaydnFixupHwLoops: occupancy miss used SUBI32/LD32 Prefer as "
            "LatchScr/countdown; never Prefer countdown on a non-countdown "
            "body use",
            /*gen_crash_diag=*/false);
      if (isLatchCountdownSubi(Log))
        return true;
      continue;
    }
    return false;
  }
  return false;
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
    MachineFunction *MF = Live.front()->getParent()
                              ? Live.front()->getParent()->getParent()
                              : nullptr;
    const HaydnMachineFunctionInfo *Info =
        MF ? MF->getInfo<HaydnMachineFunctionInfo>() : nullptr;
    if (Info && Info->hasPostCommitBlockBudget())
      report_fatal_error(
          Twine(DebugPrefix) +
              ": post-stamp singleton-split of coissued survivors is illegal",
          /*gen_crash_diag=*/false);
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
    bool HasRealSibling = false;
    for (MachineInstr *K : Keep) {
      if (!K || K->isMetaInstruction() || K->isDebugInstr() || K->isKill())
        continue;
      if (haydn::format_e::logicalOpcodeOrSelf(K->getOpcode()) == Haydn::NOP)
        continue;
      HasRealSibling = true;
      break;
    }
    // D1.157: demote must not re-choose non-victim sibling packet
    // structure. HexagonConstPropagation.cpp:2508-2512 replaceWithNop.
    if (HasRealSibling) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": SET victim same-row NOP; sibling row kept\n");
      neutralizeSameRowNop(SetMI, TII);
      haydn::bundle::dropBundleImplicitRegsAbsentFromMembers(*Root);
      return;
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

  // Reverse CFG from Latch: every predecessor that is not Preheader.
  // Out.insert is the visited set — do not cap the worklist and admit a
  // truncated region (D1.114).
  if (Header != Latch) {
    SmallVector<const MachineBasicBlock *, 8> Work(Latch->pred_begin(),
                                                   Latch->pred_end());
    while (!Work.empty()) {
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

  // Unique designated Exit: the sole live non-Header successor of Latch.
  // D1.105 refuses demote when this is not unique; the region walk must
  // not swallow a shared return or extra latch exits into LoopBlocks.
  const MachineBasicBlock *DesignatedExit = nullptr;
  unsigned NonHeader = 0;
  for (const MachineBasicBlock *S : Latch->successors()) {
    if (!S || S == Header)
      continue;
    ++NonHeader;
    DesignatedExit = S;
  }
  if (NonHeader != 1)
    DesignatedExit = nullptr;

  if (DesignatedExit) {
    // D1.101: early-exit-only blocks are forward-reachable from Header
    // and are not the designated Exit. Stop at Exit so a shared return
    // does not pull in the rest of the function.
    SmallVector<const MachineBasicBlock *, 8> FWork;
    SmallPtrSet<const MachineBasicBlock *, 16> FSeen;
    FWork.push_back(Header);
    while (!FWork.empty()) {
      const MachineBasicBlock *B = FWork.pop_back_val();
      if (!B || B == Preheader || B == DesignatedExit || !FSeen.insert(B).second)
        continue;
      Out.insert(B);
      for (const MachineBasicBlock *S : B->successors())
        FWork.push_back(S);
    }
  } else {
    SmallVector<const MachineBasicBlock *, 8> Seed(Out.begin(), Out.end());
    for (const MachineBasicBlock *B : Seed) {
      if (!B || B == Latch)
        continue;
      for (const MachineBasicBlock *S : B->successors()) {
        if (!S || S == Preheader)
          continue;
        Out.insert(S);
      }
    }
  }
}

void haydn::hwloop::collectPostRewriteLatchSuccessors(
    MachineBasicBlock &Latch, MachineBasicBlock *Header,
    MachineBasicBlock *Exit, bool KeepExtraSuccs,
    SmallVectorImpl<MachineBasicBlock *> &Out) {
  Out.clear();
  auto add = [&](MachineBasicBlock *S) {
    if (!S)
      return;
    for (MachineBasicBlock *E : Out)
      if (E == S)
        return;
    Out.push_back(S);
  };
  // Designated obligation first: recoverDemoteSetupFrom takes the first
  // PostSuccs entry that is not Latch and not in LoopBlocks as Exit.
  add(Header);
  if (Exit && Exit != Header)
    add(Exit);
  if (KeepExtraSuccs) {
    for (MachineBasicBlock *S : Latch.successors())
      add(S);
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

int64_t haydn::hwloop::estimateLastBodyCycleOffset(
    const MachineFunction &MF, const MachineBasicBlock *Header,
    const MachineBasicBlock *Latch, const MachineBasicBlock *Preheader,
    int64_t HeaderStartOff, const TargetInstrInfo &TII) {
  if (HeaderStartOff < 0 || !isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch))
    return -1;
  if (!blockAtOrAfterInLayout(MF, Header, Latch))
    return -1;
  LoopBlockSet Blocks;
  collectLoopBlocks(Header, Latch, Preheader, Blocks);
  int64_t Cursor = HeaderStartOff;
  int64_t LastCycleStart = -1;
  bool Started = false;
  for (const MachineBasicBlock &MBB : MF) {
    if (&MBB == Header)
      Started = true;
    if (!Started)
      continue;
    const bool InLoop = Blocks.contains(&MBB);
    for (const MachineInstr &MI : MBB) {
      if (InLoop && MI.isTerminator())
        continue;
      const unsigned Bytes = TII.getInstSizeInBytes(MI);
      if (Bytes == 0)
        continue;
      if (InLoop)
        LastCycleStart = Cursor;
      Cursor += static_cast<int64_t>(Bytes);
    }
    if (&MBB == Latch)
      break;
  }
  return LastCycleStart;
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

// Latch-block terminator zero-tests of Reg are not live-in-body reads:
// L2 drops every latch terminator before SUBI32+BNEZ_W is installed.
// Header/interior early-exit BNEZ of Reg stays a body use. Not folded
// into isCountdownStepOf — stripResidualCountdown would erase live conds.
static bool isLatchTerminatorZeroTestOf(const MachineInstr &MI,
                                        const MachineBasicBlock *Latch,
                                        Register Reg) {
  if (!Latch || MI.getParent() != Latch || !MI.isTerminator())
    return false;
  const unsigned Opc = MI.getOpcode();
  const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
  const bool ZeroTest = Opc == Haydn::LoopJNZ || isSoftLatchBnezOpcode(Opc) ||
                        Log == Haydn::BEQZ || Log == Haydn::BEQZ_W;
  if (!ZeroTest)
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.readsReg() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

bool haydn::hwloop::regUsedNonCountdownIn(Register Reg,
                                          const LoopBlockSet &Blocks,
                                          const MachineBasicBlock *Latch) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      if (isCountdownStepOf(MI, Reg))
        continue;
      if (isLatchTerminatorZeroTestOf(MI, Latch, Reg))
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.readsReg() && MO.getReg() == Reg)
          return true;
      }
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

int haydn::hwloop::resolveDemoteSaveHome(int SaveFI, int PostRAScratchFI,
                                         int BranchRelaxationScratchFI,
                                         ArrayRef<int> StackCounterFIs) {
  if (SaveFI < 0)
    return -1;
  if (SaveFI == PostRAScratchFI || SaveFI == BranchRelaxationScratchFI)
    return -1;
  for (int C : StackCounterFIs)
    if (C >= 0 && SaveFI == C)
      return -1;
  return SaveFI;
}

int haydn::hwloop::resolveDemoteSaveHome(int SaveFI, int PostRAScratchFI,
                                         int BranchRelaxationScratchFI,
                                         int StackCounterFI) {
  if (StackCounterFI < 0)
    return resolveDemoteSaveHome(SaveFI, PostRAScratchFI,
                                 BranchRelaxationScratchFI, ArrayRef<int>());
  return resolveDemoteSaveHome(SaveFI, PostRAScratchFI,
                               BranchRelaxationScratchFI,
                               ArrayRef<int>(&StackCounterFI, 1));
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
// No qualifying register ⇒ the stack-counter demote path (dedicated
// pre-PEI counter FI; latch scratch defined entirely after the last
// call) is the sink.

// Rewind to the SET-bundle start so a coissued ADDI r0,imm / r3=sp+off is
// a visible last-def (glueDefToUse places SET last). A top-level iterator
// at a bundled SET skips remaining members; a PastSetup pointer match on
// instrs() always skips From, so when that iterator already sits on the
// after-SET ADDI the dest is dropped (va-arg-22 CountReg/LatchScr steal).
static const MachineInstr *afterSetTailStart(
    const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator From) {
  if (!Preheader || From == Preheader->end())
    return nullptr;
  const MachineInstr *MI = &*From;
  if (MI->isBundledWithPred() || MI->isBundledWithSucc()) {
    while (MI->isBundledWithPred())
      MI = MI->getPrevNode();
  }
  return MI;
}

bool haydn::hwloop::regMentionedInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI) {
  // Preheader tail after SET/LoopStart still executes. Following work is
  // legal ZOL setup distance (SET is not a scheduling boundary), so a GPR
  // scavenged as free at the SET iterator may still be the address scratch
  // before the header (va-arg-22 -O2: r3 = sp+off). Skip the setup MI and
  // the SET member (SET reads Prefer — that is not a tail mention). Do not
  // skip other members of the setup bundle: glueDefToUse places SET last,
  // so r3=sp+off can coissue in the same packet. Do not skip From via a
  // PastSetup pointer match (that drops a bundled after-SET ADDI). Occupancy
  // UseFromSet/DefInTail keep forEachPreheaderTailInstr — do not fold this
  // into that walker.
  if (!Preheader || !Register(Reg).isPhysical() ||
      PreheaderFrom == Preheader->end())
    return false;
  const HaydnInstrInfo *HII = nullptr;
  if (const MachineFunction *MF = Preheader->getParent())
    HII = MF->getSubtarget<HaydnSubtarget>().getInstrInfo();
  for (const MachineInstr *MI = afterSetTailStart(Preheader, PreheaderFrom);
       MI && MI->getParent() == Preheader; MI = MI->getNextNode()) {
    if (MI->isKill() || MI->getOpcode() == TargetOpcode::KILL)
      continue;
    if (MI->isMetaInstruction() || MI->isDebugInstr() ||
        MI->isCFIInstruction() || MI->isImplicitDef() || MI->isBundle())
      continue;
    if (HII && HII->isHardwareLoopSetupInstr(*MI))
      continue;
    for (const MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.getReg().isPhysical() &&
          TRI.regsOverlap(MO.getReg(), Reg))
        return true;
    }
  }
  return false;
}

// Occupancy PastSetup SET/setup-bundle skip (not the any-mention rewind).
// Visit returns true to stop. Do not reuse this for the any-mention
// walker: isSoundDemoteCounter and Prefer-as-LatchScr still need defs.
template <typename VisitFn>
static bool forEachPreheaderTailInstr(
    const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom, VisitFn Visit) {
  if (!Preheader || PreheaderFrom == Preheader->end())
    return false;
  const HaydnInstrInfo *HII = nullptr;
  if (const MachineFunction *MF = Preheader->getParent())
    HII = MF->getSubtarget<HaydnSubtarget>().getInstrInfo();
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
      if (!MI.isBundledWithPred()) {
        InSetupBundle = false;
      } else if (HII && HII->isHardwareLoopSetupInstr(MI)) {
        continue;
      }
    }
    if (MI.isMetaInstruction() || MI.isDebugInstr() ||
        MI.isCFIInstruction() || MI.isImplicitDef() || MI.isKill() ||
        MI.isBundle())
      continue;
    if (Visit(MI))
      return true;
  }
  return false;
}

bool haydn::hwloop::regUsedFromSetInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI) {
  if (!Register(Reg).isPhysical())
    return false;
  bool DefinedInTail = false;
  return forEachPreheaderTailInstr(
      Preheader, PreheaderFrom, [&](const MachineInstr &MI) {
        bool Reads = false;
        bool Defs = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg() || !MO.getReg().isPhysical() ||
              !TRI.regsOverlap(MO.getReg(), Reg))
            continue;
          if (MO.readsReg())
            Reads = true;
          if (MO.isDef())
            Defs = true;
        }
        if (Reads && !DefinedInTail)
          return true;
        if (Defs)
          DefinedInTail = true;
        return false;
      });
}

bool haydn::hwloop::regDefdInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI) {
  if (!Register(Reg).isPhysical())
    return false;
  return forEachPreheaderTailInstr(
      Preheader, PreheaderFrom, [&](const MachineInstr &MI) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical() &&
              TRI.regsOverlap(MO.getReg(), Reg))
            return true;
        }
        return false;
      });
}

bool haydn::hwloop::hasIncomingValue(MCPhysReg Reg,
                                     const MachineFunction &MF) {
  // In-function MRI def or MF.front() live-in. Never FPL CSI: that seed
  // keeps saved CSRs live in every block. Occupancy walkers stay separate.
  if (!Register(Reg).isPhysical())
    return false;
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!MRI.def_empty(Reg))
    return true;
  if (MF.empty())
    return false;
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  for (const MachineBasicBlock::RegisterMaskPair &LI : MF.front().liveins())
    if (TRI.regsOverlap(LI.PhysReg, Reg))
      return true;
  return false;
}

static bool miDefsPhysReg(const MachineInstr &MI, MCPhysReg Reg,
                          const TargetRegisterInfo &TRI) {
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isCFIInstruction() ||
      MI.isImplicitDef() || MI.isKill() || MI.isBundle())
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical() &&
        TRI.regsOverlap(MO.getReg(), Reg))
      return true;
  }
  return false;
}

// SP is R13; FP is R14 only when hasFP (otherwise R14 is a normal GPR).
// AIE SET dest is dedicated LC (AIE2InstrInfo.cpp:1437); Hexagon COPYs the
// trip into a new vreg before LOOP_r (HexagonHardwareLoops.cpp:1284-1291).
static bool srcIsFrameReg(Register Base, const TargetRegisterInfo &TRI,
                          bool HasFP) {
  if (!Base.isPhysical())
    return false;
  if (TRI.regsOverlap(Base, Haydn::R13))
    return true;
  return HasFP && TRI.regsOverlap(Base, Haydn::R14);
}

static unsigned frameAddrLogical(const MachineInstr &MI) {
  return haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
}

static bool miIsCopyLike(const MachineInstr &MI, unsigned Log) {
  return MI.getOpcode() == TargetOpcode::COPY || MI.isCopy() ||
         Log == Haydn::MOVE32;
}

static bool miCopySource(const MachineInstr &MI, unsigned Log, Register &Src) {
  if (!miIsCopyLike(MI, Log))
    return false;
  if (MI.getNumOperands() < 2 || !MI.getOperand(1).isReg() ||
      !MI.getOperand(1).readsReg())
    return false;
  Src = MI.getOperand(1).getReg();
  return Src.isPhysical();
}

// Address arithmetic whose dest is Base ± off. ADDI r0,imm is tail-imm
// (r0 is not a FA base). SUB/SUBI keep the minuend; ADD/ADDI are
// commutable so every GPR use except r0 is a possible base. Used to
// chain LastIsFA through r3+imm dests that are not themselves SP
// last-defs (va-arg-22 fill-loop address temps).
static bool miIsAddressArithLogical(unsigned Log) {
  return Log == Haydn::ADDI32 || Log == Haydn::ADDI32_W || Log == Haydn::ADD32 ||
         Log == Haydn::SUBI32 || Log == Haydn::SUB32;
}

static bool miCollectFaArithBases(const MachineInstr &MI, MCPhysReg Dst,
                                  const TargetRegisterInfo &TRI,
                                  SmallVectorImpl<Register> &Bases) {
  if (!miDefsPhysReg(MI, Dst, TRI))
    return false;
  const unsigned Log = frameAddrLogical(MI);
  if (!miIsAddressArithLogical(Log))
    return false;
  SmallVector<Register, 4> Uses;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg().isPhysical() || !MO.readsReg())
      continue;
    if (MO.isDef() && !MO.isUse())
      continue;
    Register U = MO.getReg();
    if (TRI.regsOverlap(U, Haydn::R0))
      continue;
    if (Haydn::GPR32RegClass.contains(U))
      Uses.push_back(U);
  }
  if (Uses.empty())
    return false;
  if (Log == Haydn::SUBI32 || Log == Haydn::SUB32) {
    Bases.push_back(Uses[0]);
    return true;
  }
  Bases.append(Uses.begin(), Uses.end());
  return !Bases.empty();
}

// Last-def shape: ADDI32 / ADDI32_W / ADD32 / SUBI32 / SUB32 / MOVE32 / COPY
// from SP/FP, or a leftover FI operand. ADD32 is commutable so SP may be
// src2; SUB/SUBI keep the minuend as the address base (SP - off). A dead
// dest is a pass-1a temp. SMS-guard ADDI r0,imm is not this shape.
// r3+imm dests (ADDI of an FA last-def, not SP itself) are chained by
// LastIsFA / predReachingFrameAddress, not here.
static bool miIsFrameAddressMaterialize(const MachineInstr &MI, MCPhysReg Reg,
                                        const TargetRegisterInfo &TRI,
                                        bool HasFP) {
  if (!miDefsPhysReg(MI, Reg, TRI))
    return false;
  if (MI.registerDefIsDead(Reg, &TRI))
    return false;
  const unsigned Log = frameAddrLogical(MI);
  const bool IsCopy = miIsCopyLike(MI, Log);
  const bool IsAdd = Log == Haydn::ADDI32 || Log == Haydn::ADDI32_W ||
                     Log == Haydn::ADD32;
  const bool IsSub = Log == Haydn::SUBI32 || Log == Haydn::SUB32;
  if (!IsCopy && !IsAdd && !IsSub)
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isFI())
      return true;
  }
  SmallVector<Register, 4> Uses;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg().isPhysical() || !MO.readsReg())
      continue;
    if (MO.isDef() && !MO.isUse())
      continue;
    Uses.push_back(MO.getReg());
  }
  if (Uses.empty())
    return false;
  if (IsCopy || IsAdd) {
    for (Register U : Uses)
      if (srcIsFrameReg(U, TRI, HasFP))
        return true;
    return false;
  }
  return srcIsFrameReg(Uses[0], TRI, HasFP);
}

// Last-def shape: ADDI32 / ADDI32_W dest, r0, imm. SMS-guard and the
// va-arg-22 live-into-exit steal share this opcode; Exit-path reads
// discriminate. SP/FP add is FA, not this. Do not fold into
// isCountdownStepOf (a ±1 dest+=-1 is not r0,imm).
static bool miIsR0ImmAddi(const MachineInstr &MI, MCPhysReg Reg,
                          const TargetRegisterInfo &TRI) {
  if (!miDefsPhysReg(MI, Reg, TRI))
    return false;
  if (MI.registerDefIsDead(Reg, &TRI))
    return false;
  const unsigned Log = frameAddrLogical(MI);
  if (Log != Haydn::ADDI32 && Log != Haydn::ADDI32_W)
    return false;
  bool HasR0 = false;
  bool HasImm = false;
  bool HasOtherGprUse = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isImm() || MO.isCImm())
      HasImm = true;
    if (!MO.isReg() || !MO.getReg().isPhysical() || !MO.readsReg())
      continue;
    if (MO.isDef() && !MO.isUse())
      continue;
    Register Src = MO.getReg();
    if (TRI.regsOverlap(Src, Haydn::R0))
      HasR0 = true;
    else if (Haydn::GPR32RegClass.contains(Src))
      HasOtherGprUse = true;
  }
  return HasR0 && HasImm && !HasOtherGprUse;
}

// Ends the reaching value without a redef: KILL (a meta opcode), or a
// kill-flag use. A redef is handled by the last-def walk, not here.
static bool miKillsPhysRegValue(const MachineInstr &MI, MCPhysReg Reg,
                                const TargetRegisterInfo &TRI) {
  if (MI.isKill() || MI.getOpcode() == TargetOpcode::KILL) {
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isPhysical() &&
          TRI.regsOverlap(MO.getReg(), Reg))
        return true;
    }
    return false;
  }
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isCFIInstruction() ||
      MI.isImplicitDef() || MI.isBundle())
    return false;
  if (miDefsPhysReg(MI, Reg, TRI))
    return false;
  return MI.killsRegister(Reg, &TRI);
}

// Do not skip KILL: it is a meta opcode, but a frame-address killed before
// SET is a pass-1a dead temp, not an unsound live-through.
static bool skipSetupOrMeta(const MachineInstr &MI, const HaydnInstrInfo *HII) {
  if (MI.isKill() || MI.getOpcode() == TargetOpcode::KILL)
    return false;
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isCFIInstruction() ||
      MI.isImplicitDef() || MI.isBundle())
    return true;
  return HII && HII->isHardwareLoopSetupInstr(MI);
}

// Incoming-value CFG-path read of Reg from Start. This is the Exit-path
// authority for LatchExcl / pickDeadLatchScratch skipCommon: stored
// live-ins and one-block LivePhysRegs are not that proof. A use after a
// local redef is a new value (SMS-guard ADDI then Exit XOR/ST of R14 is
// not live-into-exit). Same-MI def+use on a non-bundle is the new value,
// not incoming (tied dest, member ADDI then ST of that dest). BUNDLE
// header: finalizeBundle ExternUses are incoming even when the same
// header also has aggregated Defs (Reads && Defs is WAR/snapshot;
// HaydnIntraCycleRAW.h:218-219). InternalRead is RAW/new-value and is
// not a read (readsReg skips it). Header Defs-only are not
// KilledIncoming before members. Do not skip headers wholesale
// (header-only implicits have no member). KILL ends the incoming value
// and is not a user. Skip SkipPreheader and every block in SkipBlocks
// (loop-body reads are not Exit-path). Seen bounds well-formed CFGs; do
// not cap the worklist and admit. finalizeBundle does not copy unused
// CSR live-ins onto a header without a member use, so unused R14 stays
// a pass-1a temp. Do not consult LivePhysRegs/addLiveOuts. Shared by
// regIsLiveIntoExitTailImm and LatchExcl. Not a live-through steal.
static bool cfgPathReadsPhysReg(const MachineBasicBlock *Start,
                                const MachineBasicBlock *SkipPreheader,
                                MCPhysReg Reg, const TargetRegisterInfo &TRI,
                                const HaydnInstrInfo *HII,
                                const LoopBlockSet *SkipBlocks) {
  if (!Start || !Register(Reg).isPhysical())
    return false;
  SmallPtrSet<const MachineBasicBlock *, 16> Seen;
  SmallVector<const MachineBasicBlock *, 8> Work;
  Work.push_back(Start);
  while (!Work.empty()) {
    const MachineBasicBlock *B = Work.pop_back_val();
    if (!B || B == SkipPreheader || !Seen.insert(B).second)
      continue;
    if (SkipBlocks && SkipBlocks->contains(B)) {
      for (const MachineBasicBlock *S : B->successors())
        Work.push_back(S);
      continue;
    }
    bool KilledIncoming = false;
    for (const MachineInstr &MI : B->instrs()) {
      if (MI.isKill() || MI.getOpcode() == TargetOpcode::KILL) {
        if (miKillsPhysRegValue(MI, Reg, TRI)) {
          KilledIncoming = true;
          break;
        }
        continue;
      }
      if (MI.isDebugInstr() || MI.isCFIInstruction() || MI.isImplicitDef())
        continue;
      if (MI.isMetaInstruction() && !MI.isBundle())
        continue;
      if (!MI.isBundle() && HII && HII->isHardwareLoopSetupInstr(MI))
        continue;
      bool Reads = false;
      bool Defs = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.getReg().isPhysical() ||
            !TRI.regsOverlap(MO.getReg(), Reg))
          continue;
        // readsReg() skips undef / internal-read.
        if (MO.readsReg())
          Reads = true;
        if (MO.isDef())
          Defs = true;
      }
      // Incoming-value read: a use with no same-MI def, or a BUNDLE-header
      // ExternUse even when the header also has aggregated Defs
      // (WAR/snapshot; HaydnIntraCycleRAW.h:218-219). Same-MI def+use on
      // a non-bundle is the new value (tied dest, member ADDI then ST of
      // that dest). InternalRead is RAW/new-value (readsReg skips it).
      if (Reads && (!Defs || MI.isBundle()))
        return true;
      // Header Defs-only are not a kill before members.
      if (MI.isBundle())
        continue;
      if (Defs) {
        KilledIncoming = true;
        break;
      }
    }
    if (KilledIncoming)
      continue;
    for (const MachineBasicBlock *S : B->successors())
      Work.push_back(S);
  }
  return false;
}

// After-SET tail, bundle-safe. Production Ins is topLevelForLayout(SET)
// (BUNDLE root). Rewind to the SET-bundle start and walk the ilist so a
// coissued ADDI r0,imm (glueDefToUse places SET last) is a visible last-def.
// A PastSetup pointer match on instrs() always skips From: when a bundled
// SET's top-level iterator already sits on the after-SET ADDI r0,imm, that
// dest is dropped and LatchExcl misses unmentioned $r14 (va-arg-22). Visit
// KILL so a killed tail-imm stays a pass-1a dead temp. Do not reuse
// occupancy's forEachPreheaderTailInstr.
template <typename VisitFn>
static void forEachAfterSetTailInstr(
    const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator From, const HaydnInstrInfo *HII,
    VisitFn Visit) {
  if (!Preheader || From == Preheader->end())
    return;
  auto visitOne = [&](const MachineInstr &MI) {
    if (MI.isKill() || MI.getOpcode() == TargetOpcode::KILL) {
      Visit(MI);
      return;
    }
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isCFIInstruction() ||
        MI.isImplicitDef() || MI.isBundle())
      return;
    if (HII && HII->isHardwareLoopSetupInstr(MI))
      return;
    Visit(MI);
  };
  for (const MachineInstr *MI = afterSetTailStart(Preheader, From);
       MI && MI->getParent() == Preheader; MI = MI->getNextNode())
    visitOne(*MI);
}

// Last def of Need that reaches Preheader along predecessor paths. COPY/MOVE
// of another FA last-def is the same value, including a def in an earlier
// predecessor. ADDI/ADD/SUB of an FA last-def is the r3+imm family. A kill
// or non-frame last def ends that path; other preds stay on the worklist.
// Visited (BB, Cur) bounds well-formed CFGs; do not cap the worklist and
// admit "not FA" (D1.114).
static bool predReachingFrameAddress(const MachineBasicBlock *Preheader,
                                     MCPhysReg Need, const HaydnInstrInfo *HII,
                                     const TargetRegisterInfo &TRI,
                                     bool HasFP) {
  SmallSet<std::pair<const MachineBasicBlock *, MCPhysReg>, 16> Visited;
  SmallVector<std::pair<const MachineBasicBlock *, MCPhysReg>, 8> Work;
  for (const MachineBasicBlock *P : Preheader->predecessors())
    Work.emplace_back(P, Need);
  while (!Work.empty()) {
    const MachineBasicBlock *B = Work.back().first;
    MCPhysReg Cur = Work.back().second;
    Work.pop_back();
    if (!B || B == Preheader || !Visited.insert({B, Cur}).second)
      continue;
    bool FoundDef = false;
    for (MachineBasicBlock::const_instr_iterator I = B->instr_end();
         I != B->instr_begin();) {
      --I;
      if (skipSetupOrMeta(*I, HII))
        continue;
      if (miKillsPhysRegValue(*I, Cur, TRI) && !miDefsPhysReg(*I, Cur, TRI)) {
        FoundDef = true;
        break;
      }
      if (!miDefsPhysReg(*I, Cur, TRI))
        continue;
      FoundDef = true;
      if (miIsFrameAddressMaterialize(*I, Cur, TRI, HasFP))
        return true;
      Register Src;
      if (miCopySource(*I, frameAddrLogical(*I), Src)) {
        if (srcIsFrameReg(Src, TRI, HasFP))
          return true;
        // Same-block last-def of the COPY source: r3 = MOVE r7 with
        // r7 = ADDI SP, off is the same FA last-def. If the source is
        // not in this block, walk preds for Src (not Cur).
        Cur = Src.asMCReg();
        FoundDef = false;
        continue;
      }
      SmallVector<Register, 4> Bases;
      if (miCollectFaArithBases(*I, Cur, TRI, Bases)) {
        for (Register Base : Bases)
          if (srcIsFrameReg(Base, TRI, HasFP))
            return true;
        // r4 = ADDI r3, imm of a pred FA last-def is the same FA family as
        // COPY of that FA (va-arg-22 r3+imm addr temps). Follow the
        // first base in this reverse walk; extra ADD sources get a
        // fresh walk of this block from the end.
        Cur = Bases[0].asMCReg();
        for (size_t IBase = 1; IBase < Bases.size(); ++IBase)
          Work.emplace_back(B, Bases[IBase].asMCReg());
        FoundDef = false;
        continue;
      }
      break;
    }
    if (!FoundDef) {
      for (const MachineBasicBlock *P : B->predecessors())
        Work.emplace_back(P, Cur);
    }
  }
  return false;
}

bool haydn::hwloop::regIsPreheaderTailFrameAddress(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI) {
  if (!Preheader || !Register(Reg).isPhysical() ||
      PreheaderFrom == Preheader->end())
    return false;
  const MachineFunction *MF = Preheader->getParent();
  if (!MF)
    return false;
  const HaydnSubtarget &ST = MF->getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo *HII = ST.getInstrInfo();
  const bool HasFP = ST.getFrameLowering()->hasFP(*MF);

  // Whole-preheader instrs() last-def: SET-bundle coissue, a reaching def
  // before SET, and the post-SET tail are the same walk (glueDefToUse
  // places SET last). Occupancy UseFromSet/DefInTail stay post-SET-member
  // via forEachPreheaderTailInstr — do not reuse that skip here. A later
  // non-frame redef or kill means the header-entry value is no longer
  // sp+off (pass-1a dead temp / bqriir general tail mention). Last-def,
  // not first dest match: COPY/MOVE of a prior FA last-def is the same
  // value (va-arg-22 r3 = move of an SP+off temp). ADDI/ADD/SUB of that
  // FA last-def is the r3+imm addr-temp family (not an SP last-def).
  SmallDenseMap<MCPhysReg, bool, 8> LastIsFA;
  bool SawReachingDef = false;
  bool KilledBeforeHeader = false;
  SmallVector<MCPhysReg, 4> LastDefPendingBases;
  auto noteDef = [&](const MachineInstr &MI, MCPhysReg Dst) {
    bool IsFA = miIsFrameAddressMaterialize(MI, Dst, TRI, HasFP);
    SmallVector<MCPhysReg, 4> Pending;
    auto considerBase = [&](Register Base) {
      if (!Base.isPhysical() || TRI.regsOverlap(Base, Haydn::R0))
        return;
      if (srcIsFrameReg(Base, TRI, HasFP)) {
        IsFA = true;
        Pending.clear();
        return;
      }
      auto It = LastIsFA.find(Base.asMCReg());
      if (It != LastIsFA.end()) {
        if (It->second) {
          IsFA = true;
          Pending.clear();
        }
        return;
      }
      if (!IsFA)
        Pending.push_back(Base.asMCReg());
    };
    if (!IsFA) {
      Register Src;
      if (miCopySource(MI, frameAddrLogical(MI), Src))
        considerBase(Src);
      else {
        SmallVector<Register, 4> Bases;
        if (miCollectFaArithBases(MI, Dst, TRI, Bases))
          for (Register B : Bases)
            considerBase(B);
      }
    }
    if (MI.registerDefIsDead(Dst, &TRI)) {
      IsFA = false;
      Pending.clear();
    }
    LastIsFA[Dst] = IsFA;
    if (TRI.regsOverlap(Dst, Reg))
      LastDefPendingBases = Pending;
  };
  for (const MachineInstr &MI : Preheader->instrs()) {
    if (skipSetupOrMeta(MI, HII))
      continue;
    if (miDefsPhysReg(MI, Reg, TRI)) {
      SawReachingDef = true;
      KilledBeforeHeader = false;
    } else if (miKillsPhysRegValue(MI, Reg, TRI)) {
      LastIsFA[Reg] = false;
      KilledBeforeHeader = true;
      LastDefPendingBases.clear();
    }
    // A kill of an FA temp must not keep LastIsFA true for COPY/MOVE of it.
    const bool IsKillInstr =
        MI.isKill() || MI.getOpcode() == TargetOpcode::KILL;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg().isPhysical())
        continue;
      if (IsKillInstr || MO.isKill())
        LastIsFA[MO.getReg().asMCReg()] = false;
    }
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
        continue;
      noteDef(MI, MO.getReg().asMCReg());
    }
  }
  if (SawReachingDef) {
    if (LastIsFA.lookup(Reg))
      return true;
    // r3 = MOVE r7 of a live-in FA last-def, or r4 = ADDI r3, imm of that
    // live-in: LastIsFA[src] is unset in this block. Pred last-def of the
    // COPY/arith base is the same family (va-arg-22 r3+imm addr temps).
    for (MCPhysReg B : LastDefPendingBases) {
      if (B == Haydn::R0 || B == Haydn::R15)
        continue;
      if (predReachingFrameAddress(Preheader, B, HII, TRI, HasFP))
        return true;
    }
    return false;
  }
  // Killed live-in: the pred frame-address does not reach SET.
  if (KilledBeforeHeader)
    return false;

  // No def in the preheader: only a live-through can be a reaching pred
  // def. A killed prologue r3=sp+off is not live-through and must remain
  // a sound pass-1a dead temp. Stored successor live-ins can be stale;
  // a successor-path read of Reg (va-arg-22 exit use) is the same class.
  auto liveThroughFromPred = [&]() -> bool {
    if (blockLiveInContains(*Preheader, Reg))
      return true;
    for (const MachineBasicBlock *S : Preheader->successors()) {
      if (cfgPathReadsPhysReg(S, Preheader, Reg, TRI, HII,
                              /*SkipBlocks=*/nullptr))
        return true;
    }
    return false;
  };
  if (!liveThroughFromPred())
    return false;

  return predReachingFrameAddress(Preheader, Reg, HII, TRI, HasFP);
}

bool haydn::hwloop::regIsLiveIntoExitTailImm(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const MachineBasicBlock *Exit, const TargetRegisterInfo &TRI,
    const LoopBlockSet *SkipBlocks) {
  if (!Preheader || !Exit || !Register(Reg).isPhysical() ||
      PreheaderFrom == Preheader->end())
    return false;

  const HaydnInstrInfo *HII = nullptr;
  if (const MachineFunction *MF = Preheader->getParent())
    HII = MF->getSubtarget<HaydnSubtarget>().getInstrInfo();

  // Bundle-safe after-SET last-def. Do not skip KILL: a killed tail-imm
  // with no continuing COPY/MOVE is a pass-1a dead temp. Last-def
  // COPY/MOVE of an after-SET ADDI r0,imm is the same family as the ADDI
  // (va-arg-22 $r14=MOVE of that dest; DefInTail kills SET-site occupancy).
  // Look up the copy source before applying this MI's kills. A
  // COPY/MOVE kill of the ADDI dest continues the value in the dest —
  // clearing LastIsTailImm there hides $r14 when the header redefines it
  // (`$r14=COPY $r4`) and Exit reads the COPY dest. Do not walk preds or
  // pre-SET defs.
  SmallDenseMap<MCPhysReg, bool, 8> LastIsTailImm;
  SmallDenseMap<MCPhysReg, MCPhysReg, 8> CopySrcOf;
  forEachAfterSetTailInstr(
      Preheader, PreheaderFrom, HII, [&](const MachineInstr &MI) {
        if (MI.isKill() || MI.getOpcode() == TargetOpcode::KILL) {
          for (const MachineOperand &MO : MI.operands()) {
            if (MO.isReg() && MO.getReg().isPhysical()) {
              const MCPhysReg KR = MO.getReg().asMCReg();
              LastIsTailImm[KR] = false;
              CopySrcOf.erase(KR);
            }
          }
          return;
        }
        Register CopySrc;
        const bool IsCopy = miCopySource(MI, frameAddrLogical(MI), CopySrc);
        bool CopyDstIsImm = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
            continue;
          const MCPhysReg Dst = MO.getReg().asMCReg();
          bool IsImm = miIsR0ImmAddi(MI, Dst, TRI);
          if (!IsImm && IsCopy) {
            auto It = LastIsTailImm.find(CopySrc.asMCReg());
            if (It != LastIsTailImm.end())
              IsImm = It->second;
          }
          if (MI.registerDefIsDead(Dst, &TRI))
            IsImm = false;
          LastIsTailImm[Dst] = IsImm;
          if (IsImm && IsCopy) {
            CopySrcOf[Dst] = CopySrc.asMCReg();
            CopyDstIsImm = true;
          } else {
            CopySrcOf.erase(Dst);
          }
        }
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg() || !MO.getReg().isPhysical())
            continue;
          const MCPhysReg KR = MO.getReg().asMCReg();
          if (CopyDstIsImm && IsCopy && TRI.regsOverlap(KR, CopySrc))
            continue;
          if (miKillsPhysRegValue(MI, KR, TRI)) {
            LastIsTailImm[KR] = false;
            CopySrcOf.erase(KR);
          }
        }
      });

  // Copy-chain component of Reg: ADDI dest, COPY/MOVE dests, and the
  // ADDI dest a COPY killed. Independent SMS-guard ADDI r0,imm last-defs
  // stay their own components. If any member of THIS chain is live into
  // Exit, every member is unsound LatchScr — header `$r14=COPY $r4` is a
  // LoopBlocks mention and an Exit-walk kill of r14, but r14 is still
  // the ADDI dest of the live-into-exit COPY dest.
  SmallSet<MCPhysReg, 16> Members;
  auto admit = [&](MCPhysReg P) {
    if (!Register(P).isPhysical() || P == Haydn::R0 || P == Haydn::R13 ||
        P == Haydn::R15)
      return;
    Members.insert(P);
  };
  for (const auto &KV : LastIsTailImm) {
    if (KV.second)
      admit(KV.first);
  }
  for (const auto &KV : CopySrcOf) {
    if (Members.count(KV.first))
      admit(KV.second);
  }
  if (!Members.count(Reg))
    return false;

  SmallSet<MCPhysReg, 16> Comp;
  SmallVector<MCPhysReg, 8> Work;
  Comp.insert(Reg);
  Work.push_back(Reg);
  while (!Work.empty()) {
    const MCPhysReg P = Work.pop_back_val();
    auto SrcIt = CopySrcOf.find(P);
    if (SrcIt != CopySrcOf.end() && Members.count(SrcIt->second) &&
        Comp.insert(SrcIt->second).second)
      Work.push_back(SrcIt->second);
    for (const auto &KV : CopySrcOf) {
      if (KV.second == P && Members.count(KV.first) &&
          Comp.insert(KV.first).second)
        Work.push_back(KV.first);
    }
  }

  // Actual Exit-path read of any member of this chain. Do not consult
  // LivePhysRegs/addLiveOuts: R14 is a CSR and pristines would
  // false-positive unused R14 (unused live R14 is not live-into-exit;
  // overlay of that is D1.71). KILL is not a user. SMS-guard ADDI
  // with no Exit-path read of this chain stays a latch temp.
  for (MCPhysReg P : Comp) {
    if (cfgPathReadsPhysReg(Exit, Preheader, P, TRI, HII, SkipBlocks))
      return true;
  }
  return false;
}

bool haydn::hwloop::regIsUnsoundLatchScratchAtSet(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const MachineBasicBlock *Exit, const LoopBlockSet &Blocks,
    const TargetRegisterInfo &TRI) {
  if (!Register(Reg).isPhysical())
    return false;
  // Tail-imm copy-chain first: a header redef of $r14 is a loop-block
  // mention and an Exit-walk kill of the ADDI dest. The family walk
  // still reports r14 when a COPY dest is live into Exit.
  if (regIsLiveIntoExitTailImm(Reg, Preheader, PreheaderFrom, Exit, TRI,
                               &Blocks))
    return true;
  // Pass-1a dead temps may also have been a PEI SP+off dest; excluding
  // mentioned GPRs exhausts LatchScr (bqriir). Unmentioned FA last-def
  // is unsound only when Exit actually reads the incoming value —
  // va-arg-22 main() fill-loop has a cluster of those dests (r4–r12
  // address temps live into Exit), not only r3. Unmentioned live-through
  // that is not FA is the skipLatchScrReason / skipCommon Exit-path arm,
  // not this predicate (named reason must stay "preheader-tail frame
  // address"). PEI SP+off dests that Exit does not read, and
  // body-mentioned PEI dests, stay pass-1a. Unused R14 and SMS-guard
  // ADDI r0,imm with no Exit-path read are not this class. Do not fold
  // this into occupiedAtSet or isCountdownStepOf.
  if (regMentionedInBlocks(Register(Reg), Blocks))
    return false;
  if (!regIsPreheaderTailFrameAddress(Reg, Preheader, PreheaderFrom, TRI))
    return false;
  if (!Exit || !Preheader)
    return false;
  const HaydnInstrInfo *HII = nullptr;
  if (const MachineFunction *MF = Preheader->getParent())
    HII = MF->getSubtarget<HaydnSubtarget>().getInstrInfo();
  return cfgPathReadsPhysReg(Exit, Preheader, Reg, TRI, HII, &Blocks);
}

bool haydn::hwloop::regExitPathReadsPhysReg(
    MCPhysReg Reg, const MachineBasicBlock *Exit,
    const MachineBasicBlock *Preheader, const LoopBlockSet &Blocks,
    const TargetRegisterInfo &TRI) {
  if (!Register(Reg).isPhysical() || !Exit)
    return false;
  const HaydnInstrInfo *HII = nullptr;
  if (const MachineFunction *MF = Exit->getParent())
    HII = MF->getSubtarget<HaydnSubtarget>().getInstrInfo();
  // LatchExcl Exit-path authority: a trampoline Exit with empty stored
  // live-ins still carries a successor ST32 that one-block LivePhysRegs
  // misses. Loop-body reads are not Exit-path (SkipBlocks). Genuine dead
  // temps with no Exit-path use (bqriir PEI dests, SMS-guard ADDI) stay
  // eligible. AIE SET dest is dedicated LC (AIE2InstrInfo.cpp:1338-1346;
  // AIEBaseHardwareLoops.cpp:411-414). Hexagon COPY trip into a new vreg
  // (HexagonHardwareLoops.cpp:1284-1291). RISC-V scavenges Define|Dead
  // AllowSpill=false (RISCVInstrInfo.cpp:1433-1471).
  return cfgPathReadsPhysReg(Exit, Preheader, Reg, TRI, HII, &Blocks);
}

bool haydn::hwloop::calleeSavedLateDefIsSound(
    MCPhysReg Reg, const MachineFunction &MF, const TargetRegisterInfo &TRI) {
  if (!Register(Reg).isPhysical())
    return false;
  bool IsABICalleeSaved = false;
  for (const MCPhysReg *CSR = TRI.getCalleeSavedRegs(&MF); CSR && *CSR; ++CSR)
    if (*CSR == Reg) {
      IsABICalleeSaved = true;
      break;
    }
  if (!IsABICalleeSaved)
    return true;
  for (const CalleeSavedInfo &CI : MF.getFrameInfo().getCalleeSavedInfo())
    if (CI.getReg() == Reg)
      return true;
  return false;
}

bool haydn::hwloop::isSoundDemoteCounter(
    MCPhysReg Reg, const LoopBlockSet &Blocks, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom, const MachineFunction &MF,
    const TargetRegisterInfo &TRI) {
  // Using a tail-mentioned GPR as the countdown leaves the trip clobbered
  // (va-arg-22 -O2: r3 = sp+off then SUBI/BNEZ r3 → MEMORY_FAULT).
  if (regMentionedInPreheaderTail(Reg, Preheader, PreheaderFrom, TRI))
    return false;
  // Unmentioned live-through / reaching-def-before-SET r3=sp+off is not a
  // tail mention; CountReg==r3 still SUBI/BNEZ the va_list cursor.
  if (regIsPreheaderTailFrameAddress(Reg, Preheader, PreheaderFrom, TRI))
    return false;

  // ABI CSR membership: the save list is the inter-procedural contract.
  if (!calleeSavedLateDefIsSound(Reg, MF, TRI))
    return false;
  bool IsABICalleeSaved = false;
  for (const MCPhysReg *CSR = TRI.getCalleeSavedRegs(&MF); CSR && *CSR; ++CSR)
    if (*CSR == Reg)
      IsABICalleeSaved = true;

  if (IsABICalleeSaved)
    return true;

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

bool haydn::hwloop::residualCountdownEquivalentAtLatch(
    const LoopBlockSet &Blocks, Register Reg,
    const MachineBasicBlock *Latch) {
  if (!Reg.isPhysical())
    return true;
  bool ResidualOutsideLatch = false;
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB)
      continue;
    for (const MachineInstr &MI : BB->instrs()) {
      if (!isCountdownStepOf(MI, Reg) || MI.getOpcode() == Haydn::LoopDec)
        continue;
      if (BB != Latch)
        ResidualOutsideLatch = true;
    }
  }
  if (!ResidualOutsideLatch)
    return true;
  // Header/body leftover ±1 plus a live non-countdown use: strip would
  // reinstall SUBI32 at latch end and body reads between those positions
  // would see a per-iteration shift. Latch terminator zero-tests are
  // skipped inside regUsedNonCountdownIn (L2-erased).
  return !regUsedNonCountdownIn(Reg, Blocks, Latch);
}

void haydn::hwloop::stripResidualCountdown(const LoopBlockSet &Blocks,
                                           Register Reg,
                                           const HaydnInstrInfo &TII) {
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

  // Group bundled victims by original BUNDLE root (insertion order). Bare
  // MIs keep eraseInstrSafe (PLE/terminator generic path). Bundled strip
  // reuses the SET-erase transaction: snapshot members, dissolve once,
  // erase the header and victims, recommit survivors. AIE
  // eraseRootFromBlock (AIEBundle.h:215-221) drops the header only;
  // Hexagon moveInstrOut (HexagonVLIWPacketizer.cpp:153-194) leaves a
  // Size>1 remainder as-is — do not port leave-stale-row. Null AA is
  // fail-closed (HaydnFixupHwLoops.cpp sequentializeSetCycleIfMixed).
  SmallVector<MachineInstr *, 4> Roots;
  DenseMap<MachineInstr *, SmallVector<MachineInstr *, 2>> VictimsByRoot;
  SmallPtrSet<const MachineInstr *, 8> VictimSet;
  SmallVector<MachineInstr *, 4> Bare;
  for (MachineInstr *MI : Kill) {
    if (!MI || !MI->getParent())
      continue;
    VictimSet.insert(MI);
    if (MI->isBundledWithPred() || MI->isBundledWithSucc()) {
      MachineInstr *Root = &*getBundleStart(MI->getIterator());
      auto &Vec = VictimsByRoot[Root];
      if (Vec.empty())
        Roots.push_back(Root);
      Vec.push_back(MI);
    } else {
      Bare.push_back(MI);
    }
  }

  for (MachineInstr *MI : Bare)
    eraseInstrSafe(MI);

  MachineFunction *MF = nullptr;
  if (!Kill.empty() && Kill.front() && Kill.front()->getParent())
    MF = Kill.front()->getParent()->getParent();
  const HaydnMachineFunctionInfo *Info =
      MF ? MF->getInfo<HaydnMachineFunctionInfo>() : nullptr;
  const bool PostCommit = Info && Info->hasPostCommitBlockBudget();

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;
    if (!Root->isBundle()) {
      for (MachineInstr *V : VictimsByRoot[Root])
        eraseInstrSafe(V);
      continue;
    }
    if (PostCommit) {
      for (MachineInstr *V : VictimsByRoot[Root]) {
        if (V && V->getParent())
          neutralizeSameRowNop(*V, TII);
      }
      haydn::bundle::dropBundleImplicitRegsAbsentFromMembers(*Root);
      continue;
    }
    SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(*Root);
    SmallVector<MachineInstr *, 3> Keep;
    Keep.reserve(Kids.size());
    for (MachineInstr *K : Kids) {
      if (K && !VictimSet.contains(K))
        Keep.push_back(K);
    }
    for (MachineInstr *K : Kids) {
      if (!K)
        continue;
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    Root->eraseFromParent();
    for (MachineInstr *V : VictimsByRoot[Root]) {
      if (V && V->getParent())
        V->eraseFromParent();
    }
    recommitSurvivingCycleMembers(Keep, TII, "stripResidualCountdown");
  }
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

void haydn::hwloop::addComputedSuccessorLiveIns(LivePhysRegs &Live,
                                                const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  // SeedPristines=false: unused CSRs stay 1b temps. Successor live-ins
  // come from the whole-function owner only. Stored MBB live-ins are
  // cache, never occupancy authority (D1.71r). llvm::computeLiveIns
  // seeds from stored liveouts (LivePhysRegs.cpp:257-266) and misses a
  // Header→E second-hop when E.liveins is stale-empty (D1.138).
  FunctionPhysLiveness FPL;
  FPL.build(MF, /*SeedPristines=*/false);
  for (const MachineBasicBlock *S : MBB.successors()) {
    if (!S)
      continue;
    FPL.addLiveInsTo(Live, *S);
  }
}

void haydn::hwloop::computeBlockLiveInsFromSuccessors(
    LivePhysRegs &Live, const MachineBasicBlock &MBB,
    ArrayRef<const MachineBasicBlock *> Succs) {
  const MachineFunction &MF = *MBB.getParent();
  const TargetRegisterInfo &TRI = *MF.getRegInfo().getTargetRegisterInfo();
  Live.init(TRI);
  LivePhysRegs Out(TRI);
  bool Seeded = false;
  bool HasSelf = false;
  for (const MachineBasicBlock *S : Succs) {
    if (!S)
      continue;
    if (S == &MBB) {
      HasSelf = true;
      continue;
    }
    LivePhysRegs SL;
    llvm::computeLiveIns(SL, *S);
    for (MCPhysReg R : SL)
      Out.addReg(R);
    Seeded = true;
  }
  if (HasSelf) {
    // Self-loop: PostSuccs-restricted LFP is already the live-ins of MBB
    // (self-edge live-outs). Never stored liveins (D1.61r). Do not
    // stepBackward again.
    LivePhysRegs SelfLive(TRI);
    for (bool Grew = true; Grew;) {
      Grew = false;
      const unsigned Before =
          (unsigned)std::distance(SelfLive.begin(), SelfLive.end());
      LivePhysRegs Step(TRI);
      for (MCPhysReg R : Out)
        Step.addReg(R);
      for (MCPhysReg R : SelfLive)
        Step.addReg(R);
      for (const MachineInstr &MI : llvm::reverse(MBB))
        Step.stepBackward(MI);
      for (MCPhysReg R : Step)
        SelfLive.addReg(R);
      if ((unsigned)std::distance(SelfLive.begin(), SelfLive.end()) != Before)
        Grew = true;
    }
    for (MCPhysReg R : SelfLive)
      Live.addReg(R);
    return;
  }
  if (!Seeded) {
    computeBlockLiveIns(Live, MBB);
    return;
  }
  for (MCPhysReg R : Out)
    Live.addReg(R);
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

//===----------------------------------------------------------------------===//
// D1.61: one alias-aware, edge-aware whole-function physical-liveness
// fixed point (the late scratch authority). This replaces the fragmented
// per-candidate successor walks and the stored-livein preference: every
// scratch decision (demote long-latch JALR scratch, LongBranchNormalize
// in-block LUI/ADDI/JALR scratch) consults THESE converged sets.
//===----------------------------------------------------------------------===//

// Guarded-tail transfer step (D1.49 wrong-code repair, ls_reg_scalar
// class), hoisted here as the one shared law. The FIRST terminator of
// the block always executes (its defs kill); every terminator strictly
// AFTER it is skipped whenever an earlier conditional takes its edge —
// a def that may not execute kills nothing, but a use on ANY path is
// live-in. Blocks with 0-1 terminators step identically to
// stepBackward, so this transfer conservatively extends the generic
// walk. The verifier forbids non-terminators after the first
// terminator, so "strictly after FirstTerm" is exactly the guarded
// terminator region.
static void guardedStepBackward(LivePhysRegs &Live,
                                const MachineBasicBlock &MBB) {
  const MachineInstr *FirstTerm = nullptr;
  for (const MachineInstr &MI : MBB) {
    if (MI.isTerminator() || MI.isBranch() || MI.isIndirectBranch()) {
      FirstTerm = &MI;
      break;
    }
  }
  // Reverse walk: MIs seen before FirstTerm are strictly after it.
  bool ReachedFirst = (FirstTerm == nullptr);
  for (const MachineInstr &MI : llvm::reverse(MBB)) {
    if (FirstTerm && &MI == FirstTerm)
      ReachedFirst = true;
    // Guarded tail is positional: everything strictly after the first
    // control may not execute, so defs/regmask kills must not prove a
    // register dead. Product form is a trailing BUNDLE{JALR} whose root
    // is not isTerminator(); stepBackward on that root applied
    // implicit-def kills (d149 POISON / ls_reg_scalar). Body MIs —
    // including exact-commit BUNDLE roots before FirstTerm — still
    // step normally (d137 latch BUNDLE class). The retired inverted
    // form guarded every non-FirstTerm MI in the block and degenerated
    // the converged sets to the all-live seed union.
    if (FirstTerm && !ReachedFirst) {
      Live.addUses(MI);
      continue;
    }
    Live.stepBackward(MI);
  }
}

void haydn::hwloop::FunctionPhysLiveness::build(const MachineFunction &MF,
                                                bool SeedPristines) {
  const TargetRegisterInfo &TRI = *MF.getRegInfo().getTargetRegisterInfo();

  MFPtr = &MF;
  NumBlocks = 0;
  for (const MachineBasicBlock &MBB : MF) {
    const size_t Need = (size_t)MBB.getNumber() + 1;
    if (Need > NumBlocks)
      NumBlocks = Need;
  }
  LiveIn = std::make_unique<LivePhysRegs[]>(NumBlocks);
  for (size_t I = 0; I < NumBlocks; ++I)
    LiveIn[I].init(TRI);

  // Seed set, applied to every block's live-out basis: pristines
  // (valid-CSI unsaved CSRs — live for the caller with no PEI
  // save/restore to repair a clobber) plus the saved-and-restored CSRs
  // (the LivePhysRegs::addLiveOuts return-block law hoisted function-
  // wide: RET carries no explicit CSR uses). Both are the D1.49 seed
  // laws; seeding every edge (not just return edges) is the conservative
  // whole-function form — it can only keep a register LIVE (refusal),
  // never prove a live one dead. SeedPristines=false is the AIE
  // LiveRegs.cpp:37-107 overlay for pass-1a / NoSpill / occupiedAtSet
  // (computed successor LiveIns, no CSI). Default stays CSI.
  SeedRegs.init(TRI);
  // LivePhysRegs::addPristines is private (its public seat is the
  // per-block addLiveOuts); the empty-set form is replicated here with
  // the public API: all CSRs, minus the saved-and-restored ones, are the
  // pristines; the restored CSRs re-enter the seed right after (the
  // return-block law hoisted function-wide). Same sets, no common edit.
  if (SeedPristines) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (MFI.isCalleeSavedInfoValid()) {
      const MachineRegisterInfo &MRI = MF.getRegInfo();
      for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR; ++CSR)
        SeedRegs.addReg(*CSR);
      for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
        SeedRegs.removeReg(Info.getReg());
      for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
        if (Info.isRestored())
          SeedRegs.addReg(Info.getReg());
    }
  }

  // Fixed point: LiveIn[B] = guardedTransfer(B, Seed ∪ ⋃_succ LiveIn[S]).
  // Sets are monotone (only grow), so layout-order passes until a full
  // pass adds nothing converge to the same least fixed point any order
  // reaches (order only affects pass count); layout order also covers
  // unreachable blocks. Self-edges join the converging LiveIn[B]
  // (AIE LivePhysRegs::stepBackward is the per-block transfer —
  // AIEBaseInstrInfo.cpp SpillExpandHelper::computeLiveOutsAt; the
  // whole-function iteration is the Haydn contract overlay). Stored
  // MBB liveins are never read: they may cache this result, they
  // never seed it (BR split tails leave them stale/empty; a stale
  // extra name would over-refuse, an empty list would hide a
  // loop-carried use).
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      const unsigned Num = (unsigned)MBB.getNumber();
      if (Num >= NumBlocks)
        continue;
      LivePhysRegs &Cur = LiveIn[Num];
      const unsigned Before =
          (unsigned)std::distance(Cur.begin(), Cur.end());

      LivePhysRegs Out(TRI);
      for (MCPhysReg R : SeedRegs)
        Out.addReg(R);
      for (const MachineBasicBlock *S : MBB.successors()) {
        const unsigned SN = (unsigned)S->getNumber();
        if (SN < NumBlocks) {
          for (MCPhysReg R : LiveIn[SN])
            Out.addReg(R);
        }
      }
      guardedStepBackward(Out, MBB);
      for (MCPhysReg R : Out)
        Cur.addReg(R);
      if ((unsigned)std::distance(Cur.begin(), Cur.end()) != Before)
        Changed = true;
    }
  }
}

bool haydn::hwloop::FunctionPhysLiveness::isLiveIn(
    const MachineBasicBlock &MBB, MCPhysReg Reg) const {
  if (!MFPtr || MBB.getParent() != MFPtr)
    return true; // fail-closed on a foreign block
  const unsigned Num = (unsigned)MBB.getNumber();
  if (Num >= NumBlocks)
    return true; // fail-closed on an unnumbered block
  // available() = not in the set under alias closure AND not reserved:
  // the same probe the retired pickers used, answered from the converged
  // set instead of a per-successor walk.
  return !LiveIn[Num].available(MFPtr->getRegInfo(), Reg);
}

void haydn::hwloop::FunctionPhysLiveness::addLiveInsTo(
    LivePhysRegs &Live, const MachineBasicBlock &MBB) const {
  if (!MFPtr || MBB.getParent() != MFPtr)
    return;
  const unsigned Num = (unsigned)MBB.getNumber();
  if (Num >= NumBlocks)
    return;
  for (MCPhysReg R : LiveIn[Num])
    Live.addReg(R);
}

void haydn::hwloop::FunctionPhysLiveness::addForwardLiveInsTo(
    LivePhysRegs &Live, const MachineBasicBlock &MBB,
    const MachineBasicBlock *Avoid) const {
  if (!MFPtr || MBB.getParent() != MFPtr)
    return;
  const TargetRegisterInfo &TRI = *MFPtr->getRegInfo().getTargetRegisterInfo();
  LivePhysRegs Cur(TRI);
  for (const MachineBasicBlock *S : MBB.successors()) {
    if (!S || S == &MBB || S == Avoid || MBB.isPredecessor(S))
      continue;
    if (Avoid) {
      // Distinct-PostSucc seed: S's live-ins as if Avoid (the origin
      // latch) contributes nothing. FPL LiveIn[S] would join Latch XOR
      // uses through Header→EarlyExit→Latch and extras the rewrite
      // drops. Seed from computed live-ins of S's other successors so
      // Header→Mid→E still sees E (D1.138 worklist depth).
      LivePhysRegs Edge(TRI);
      for (const MachineBasicBlock *SS : S->successors()) {
        if (!SS || SS == Avoid || SS == S)
          continue;
        addLiveInsTo(Edge, *SS);
      }
      for (const MachineInstr &MI : llvm::reverse(*S))
        Edge.stepBackward(MI);
      for (MCPhysReg R : Edge)
        Cur.addReg(R);
    } else {
      addLiveInsTo(Cur, *S);
    }
  }
  for (const MachineInstr &MI : llvm::reverse(MBB))
    Cur.stepBackward(MI);
  for (MCPhysReg R : Cur)
    Live.addReg(R);
}

bool haydn::hwloop::FunctionPhysLiveness::liveOnAllSuccessors(
    const MachineBasicBlock &MBB, MCPhysReg Reg) const {
  for (const MachineBasicBlock *S : MBB.successors())
    if (isLiveIn(*S, Reg))
      return true;
  return false;
}

bool haydn::hwloop::FunctionPhysLiveness::liveOnSuccessor(
    const MachineBasicBlock &MBB, const MachineBasicBlock *DeadOn,
    MCPhysReg Reg) const {
  if (!DeadOn)
    return liveOnAllSuccessors(MBB, Reg);
  return isLiveIn(*DeadOn, Reg);
}

bool haydn::hwloop::FunctionPhysLiveness::liveUnderPostRewriteSuccessors(
    const MachineBasicBlock &MBB,
    ArrayRef<const MachineBasicBlock *> PostSuccs, MCPhysReg Reg) const {
  // Fail-closed: no built fixed point or a foreign block must never
  // prove a register dead.
  if (!MFPtr || MBB.getParent() != MFPtr)
    return true;
  const MachineRegisterInfo &MRI = MFPtr->getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();

  // Edge obligation per entry:
  //   * SELF entry: restricted-successor least fixed point of MBB
  //     under PostSuccs (AIE SpillExpandHelper computeLiveOutsAt
  //     transfer, iterated). Never stored liveins (contract: cache
  //     only) and never the full-CFG LiveIn[MBB] (that joins
  //     pre-rewrite successors the rewrite DROPS — Header==Latch
  //     early-exit).
  //   * Distinct entry S: one guarded body walk of S seeded from the
  //     CONVERGED live-ins of S's own real successors.
  LivePhysRegs Acc(TRI);
  bool HasSelf = false;
  for (const MachineBasicBlock *S : PostSuccs) {
    if (!S)
      continue;
    if (S == &MBB) {
      HasSelf = true;
      continue;
    }
    LivePhysRegs Edge(TRI);
    for (MCPhysReg R : SeedRegs)
      Edge.addReg(R);
    for (const MachineBasicBlock *SS : S->successors()) {
      const unsigned SSN = (unsigned)SS->getNumber();
      if (SSN < NumBlocks)
        for (MCPhysReg R : LiveIn[SSN])
          Edge.addReg(R);
    }
    guardedStepBackward(Edge, *S);
    for (MCPhysReg R : Edge)
      Acc.addReg(R);
  }
  if (HasSelf) {
    LivePhysRegs SelfLive(TRI);
    for (bool Grew = true; Grew;) {
      Grew = false;
      const unsigned Before =
          (unsigned)std::distance(SelfLive.begin(), SelfLive.end());
      LivePhysRegs Out(TRI);
      for (MCPhysReg R : SeedRegs)
        Out.addReg(R);
      for (const MachineBasicBlock *P : PostSuccs) {
        if (!P || P == &MBB)
          continue;
        const unsigned PN = (unsigned)P->getNumber();
        if (PN < NumBlocks)
          for (MCPhysReg R : LiveIn[PN])
            Out.addReg(R);
      }
      for (MCPhysReg R : SelfLive)
        Out.addReg(R);
      guardedStepBackward(Out, MBB);
      for (MCPhysReg R : Out)
        SelfLive.addReg(R);
      if ((unsigned)std::distance(SelfLive.begin(), SelfLive.end()) !=
          Before)
        Grew = true;
    }
    for (MCPhysReg R : SelfLive)
      Acc.addReg(R);
  }
  return !Acc.available(MRI, Reg);
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

  // D1.61: the dead-on-every-exit-edge probe consults the whole-function
  // fixed point (transitive; guarded-tail transfer; pristines in the
  // seed) — the retired per-successor one-block walks read each exit's
  // redefinition as a kill and could call a join-through value free.
  FunctionPhysLiveness FPL;
  FPL.build(MF);

  LivePhysRegs LPR(TRI);
  addComputedSuccessorLiveIns(LPR, Preheader);
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
        if (FPL.isLiveIn(*S, R.asMCReg())) {
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
  // D1.61: every allocatable legal GPR candidate — the retired list
  // omitted R5/R6 (the fragmented-candidate defect this owner row
  // closes); each still passes the full isUsable proof above.
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8, Haydn::R7,
      Haydn::R6,  Haydn::R5,  Haydn::R4,  Haydn::R3, Haydn::R2,
      Haydn::R1,  Haydn::R12};
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

// Recover Preheader / SET / Exit so pickDeadLatchScratch skipCommon can
// apply LatchExcl (live-into-exit tail-imm ∪ every incoming GPR
// cfgPathReadsPhysReg cannot prove absent on the Exit path) on every
// pass, including empty Exclude. Empty LatchScr after a correct skip is
// occupancy miss, not a steal of `$r3=sp+off` / `$r14=ADDI r0,imm` / an
// unmentioned trampoline-Exit live-through. Mentioned PEI dests with no
// Exit read stay pass-1a; unused R14 is pass-1b iff CSI-saved. No
// pass-2 steal.
static bool recoverDemoteSetupFrom(
    MachineBasicBlock &Latch, ArrayRef<MachineBasicBlock *> PostSuccs,
    const LoopBlockSet &Blocks, const HaydnInstrInfo *HII,
    const MachineBasicBlock *&Preheader, const MachineBasicBlock *&Exit,
    MachineBasicBlock::const_iterator &From) {
  Preheader = nullptr;
  Exit = nullptr;
  From = {};
  // Designated software Exit is the first PostSuccs entry that is not
  // Latch and not in Blocks. Callers put Header then Exit then kept
  // Extras so Extra cannot steal this slot.
  for (MachineBasicBlock *S : PostSuccs) {
    if (S && S != &Latch && !Blocks.contains(S)) {
      Exit = S;
      break;
    }
  }
  auto trySetup = [&](const MachineBasicBlock *P) -> bool {
    if (!P || Blocks.contains(P))
      return false;
    MachineBasicBlock::const_iterator Cand = P->end();
    for (MachineInstr &MI :
         const_cast<MachineBasicBlock *>(P)->instrs()) {
      if (HII && HII->isHardwareLoopSetupInstr(MI)) {
        MachineInstr &Root = topLevelForLayout(MI);
        Cand = MachineBasicBlock::const_iterator(Root.getIterator());
        break;
      }
    }
    if (Cand == P->end())
      return false;
    Preheader = P;
    From = Cand;
    return true;
  };
  // Require a SET-carrying preheader. The first outside-pred of Latch
  // or of any Blocks member can be Extra/join (KeepExtra multi-exit);
  // taking it without a setup instr set HaveSetup=false and vacated
  // LatchExcl (FA / tail-imm).
  for (const MachineBasicBlock *P : Latch.predecessors()) {
    if (P && P != &Latch && trySetup(P))
      break;
  }
  if (!Preheader) {
    for (const MachineBasicBlock *B : Blocks) {
      if (!B)
        continue;
      for (const MachineBasicBlock *P : B->predecessors()) {
        if (trySetup(P))
          break;
      }
      if (Preheader)
        break;
    }
  }
  return Preheader && Exit && From != Preheader->end();
}

Register haydn::hwloop::pickDeadLatchScratch(
    MachineBasicBlock &Latch, ArrayRef<MachineBasicBlock *> PostSuccs,
    const LoopBlockSet &Blocks, ArrayRef<Register> Exclude,
    const char *DebugPrefix) {
  const MachineFunction &MF = *Latch.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  const HaydnInstrInfo *HII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  const MachineBasicBlock *Preheader = nullptr;
  const MachineBasicBlock *Exit = nullptr;
  MachineBasicBlock::const_iterator From;
  const bool HaveSetup = recoverDemoteSetupFrom(Latch, PostSuccs, Blocks, HII,
                                                Preheader, Exit, From);

  // Dead at latch end vs PostSuccs. Distinct successors contribute
  // FunctionPhysLiveness forward live-ins with SeedPristines=false (AIE
  // LiveRegs.cpp:37-107: computed successor LiveIns, never stored;
  // llvm::computeLiveIns seeds from stored liveouts and misses a
  // Header→E second-hop when E.liveins is stale-empty). Backedges are
  // omitted so Latch XOR dests stay 1a-eligible and dropped extras do
  // not occupy via Header (D1.136). Unused CSRs are pass-1b temps, not
  // Exit-carried. A self successor is the PostSuccs-restricted LFP of
  // Latch, never stored liveins and never FPL.isLiveIn(Latch). D1.61r:
  // never default CSI FPL as 1a/1b seed. After 1a/1b miss, return empty;
  // this seed must not become a live-through steal.
  FunctionPhysLiveness FPL;
  FPL.build(MF, /*SeedPristines=*/false);
  LivePhysRegs LPR(TRI);
  bool HasSelf = false;
  for (MachineBasicBlock *Succ : PostSuccs) {
    if (!Succ)
      continue;
    if (Succ == &Latch) {
      HasSelf = true;
      continue;
    }
    FPL.addForwardLiveInsTo(LPR, *Succ, &Latch);
  }
  if (HasSelf) {
    LivePhysRegs SelfLive(TRI);
    for (bool Grew = true; Grew;) {
      Grew = false;
      const unsigned Before =
          (unsigned)std::distance(SelfLive.begin(), SelfLive.end());
      LivePhysRegs Out(TRI);
      for (MCPhysReg R : LPR)
        Out.addReg(R);
      for (MCPhysReg R : SelfLive)
        Out.addReg(R);
      for (const MachineInstr &MI : llvm::reverse(Latch))
        Out.stepBackward(MI);
      for (MCPhysReg R : Out)
        SelfLive.addReg(R);
      if ((unsigned)std::distance(SelfLive.begin(), SelfLive.end()) != Before)
        Grew = true;
    }
    for (MCPhysReg R : SelfLive)
      LPR.addReg(R);
  }

  auto isExcluded = [&](MCPhysReg R) {
    for (Register E : Exclude) {
      if (E.isPhysical() && TRI.regsOverlap(E, R))
        return true;
    }
    return false;
  };

  // Skip recovered unsound LatchScr (live-into-exit tail-imm ∪ unmentioned
  // Exit-path read) on every pass, including empty Exclude, so 1a/1b
  // cannot keep `$r3=sp+off`, `$r14=ADDI r0,imm`, or an unmentioned
  // trampoline-Exit live-through after NoSpill exhausts. Mentioned PEI
  // dests with no Exit read stay pass-1a. Unused unsaved R14 is
  // occupancy miss (D1.87), not pass-1b. Empty LatchScr after a correct
  // skip is occupancy miss, not a steal. Do not restamp occupiedAtSet.
  auto isUnsoundLatchScr = [&](MCPhysReg R) {
    if (!HaveSetup)
      return false;
    return regIsUnsoundLatchScratchAtSet(R, Preheader, From, Exit, Blocks,
                                         TRI);
  };

  // Unused R14 is allocatable when !hasFP and CSI-saved (D1.87).
  // Incoming value is the exported helper (MRI def or MF.front()
  // live-in; never FPL CSI). A defined body temp (high-pressure R14
  // after prologue save) is a sound LD32 dest at latch end.
  // Exit-path authority: refuse every incoming GPR cfgPathReadsPhysReg
  // cannot prove absent on the Exit path. LPR.available is not that
  // proof (empty stored live-ins / one-block computeLiveIns miss a
  // trampoline-Exit successor read). Genuine dead temps with no Exit
  // read stay 1a/1b. 1b unused temps are not incoming.
  auto skipCommon = [&](MCPhysReg R) {
    if (R == Haydn::R0 || MRI.isReserved(R) || isExcluded(R) ||
        isUnsoundLatchScr(R))
      return true;
    // Same unproven-absent Exit-path class as skipLatchScrReason:
    // unmentioned incoming whose Exit-path read cannot be proven absent.
    // Mentioned PEI dests / SMS-guard ADDI with no Exit use stay 1a/1b.
    if (R == Haydn::R13 || R == Haydn::R15 || !hasIncomingValue(R, MF) ||
        regMentionedInBlocks(Register(R), Blocks))
      return false;
    // Walk every post-rewrite successor that is not the back-edge
    // (designated Exit and kept Extra dests). Header is PostSuccs[0]
    // and may also be in Blocks; Extra early-exit dests can be in
    // Blocks (D1.101) and must still be probed.
    if (HaveSetup) {
      MachineBasicBlock *HeaderSucc =
          PostSuccs.empty() ? nullptr : PostSuccs.front();
      for (MachineBasicBlock *Succ : PostSuccs) {
        if (!Succ || Succ == &Latch || Succ == HeaderSucc)
          continue;
        if (regExitPathReadsPhysReg(R, Succ, Preheader, Blocks, TRI))
          return true;
      }
      return false;
    }
    for (MachineBasicBlock *Succ : PostSuccs) {
      if (!Succ || Blocks.contains(Succ))
        continue;
      if (cfgPathReadsPhysReg(Succ, Preheader, R, TRI, HII, &Blocks))
        return true;
    }
    return false;
  };

  // Pass 1a: dead-at-latch-end with an incoming value. LPR is live-out
  // vs PostSuccs (FPL SeedPristines=false; self-edge is the restricted
  // LFP), so a killed preheader temp is available even when stored
  // liveins still list it. An Exit-carried GPR and a Header→E
  // second-hop use stay occupied. skipCommon's mentioned-reg
  // short-circuit stays: body temps remain 1a-eligible when this seed
  // proves dead-at-end (do not start walking LoopBlocks as Exit-path).
  for (MCPhysReg R : Haydn::GPR32NoSPNoLRRegClass) {
    if (skipCommon(R) || !hasIncomingValue(R, MF) || !LPR.available(MRI, R))
      continue;
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote latch scratch "
                      << printReg(R, &TRI)
                      << " (allocatable, dead at latch end vs "
                         "{Header, Exit})\n");
    return Register(R);
  }

  // Pass 1b: dead-at-latch-end with no incoming value. Unused R14 is
  // a sound LD32 dest only when calleeSavedLateDefIsSound (CSI).
  // Unsaved CSR write is never repaired (demote is post-PEI).
  for (MCPhysReg R : Haydn::GPR32NoSPNoLRRegClass) {
    if (skipCommon(R) || hasIncomingValue(R, MF) || !LPR.available(MRI, R))
      continue;
    if (!calleeSavedLateDefIsSound(R, MF, TRI)) {
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip LatchScr "
                        << printReg(R, &TRI)
                        << " unsaved CSR (no CSI)\n");
      continue;
    }
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote latch scratch "
                      << printReg(R, &TRI)
                      << " (unused dead temp; CSI-sound; no overlay)\n");
    return Register(R);
  }

  // No pass-2 live-through steal. After 1a/1b miss, return empty: the
  // caller last-resorts LatchScr=R0 (soft-zero restore at Header) rather
  // than a live-through or Prefer. skipCommon LatchExcl (tail-imm ∪
  // Exit-path authority) stays above.
  LLVM_DEBUG(dbgs() << DebugPrefix
                    << ": pickDeadLatchScratch — no allocatable GPR dead "
                       "at latch end\n");
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
