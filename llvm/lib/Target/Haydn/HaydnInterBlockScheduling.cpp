//===-- HaydnInterBlockScheduling.cpp - Stage-0 inter-block scheduling ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Stage-0 Haydn inter-block scheduling. See the header for scope
// vs AIE::InterBlockScheduling and the deferred residual list.
//
//===----------------------------------------------------------------------===//

#include "HaydnInterBlockScheduling.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-interblock"

STATISTIC(NumInterBlockMoved,
          "Number of instructions moved across a BB boundary by Stage-0 "
          "Haydn inter-block scheduling");
STATISTIC(NumInterBlockBundled,
          "Number of cross-BB moves that co-issued into a Pred bundle");

namespace llvm {
cl::opt<bool> EnableHaydnInterBlock(
    "haydn-enable-interblock", cl::Hidden, cl::init(false),
    cl::desc(
        "Enable Stage-0 Haydn inter-block scheduling (incomplete; default OFF "
        "until hazard/slot model integrated —)."
        "Acyclic fallthrough pack only; ZOL exit→preheader hoist removed."));
} // namespace llvm

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

// Skip debug/meta noise when scanning (does NOT treat target pseudos as
// skippable — LOAD_ADDR etc. still define regs that motion must honor).
static bool isScanSkippable(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.isPosition() || MI.isKill() ||
         MI.isImplicitDef() || MI.isCFIInstruction();
}

static bool isHwLoopSetup(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return Opc == Haydn::LoopStart || Opc == Haydn::SET_HWLOOP ||
         Opc == Haydn::SET_HWLOOP_REG || Opc == Haydn::SET_HWLOOP_W ||
         Opc == Haydn::SET_HWLOOP_F2_W || Opc == Haydn::SET_HWLOOP_REG_W;
}

static bool hasSelfLoop(const MachineBasicBlock &MBB) {
  for (const MachineBasicBlock *S : MBB.successors())
    if (S == &MBB)
      return true;
  return false;
}

// True if \p MBB is a hardware-loop body.
// After post-RA HaydnHardwareLoops convert, the latch often has **no CFG
// self-edge** (back-edge is implicit in HWLR). hasSelfLoop alone misses
// that shape and Stage-0 IB fallthrough-pack treated the body as a normal
// BB — pulling exit `and32`/`seq32` *into* the ZOL (vec_scale II 3→4).
// Detect bodies by:
// 1. CFG self-loop, or
// 2. Any SET_HWLOOP{,_REG}/LoopStart whose begin MBB (operand 1) is \p MBB.
static bool isHwLoopBody(const MachineBasicBlock &MBB) {
  if (hasSelfLoop(MBB))
    return true;
  const MachineFunction *MF = MBB.getParent();
  if (!MF)
    return false;
  for (const MachineBasicBlock &BB : *MF) {
    for (const MachineInstr &MI : BB) {
      if (!isHwLoopSetup(MI))
        continue;
      // SET_HWLOOP / SET_HWLOOP_REG: imm sel, mbb begin, mbb end, count
      // LoopStart: similar begin mbb at op0 or op1 depending on form — also
      // accept any MBB operand matching MBB (conservative).
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isMBB() && MO.getMBB() == &MBB)
          return true;
      }
    }
  }
  return false;
}

static MachineBasicBlock *getLayoutFallthrough(MachineBasicBlock &MBB) {
  MachineBasicBlock *FT = MBB.getFallThrough();
  return FT;
}

//===----------------------------------------------------------------------===//
// HaydnInterBlockScheduling
//===----------------------------------------------------------------------===//

bool HaydnInterBlockScheduling::isMovableCandidate(const MachineInstr &MI) {
  if (MI.isMetaInstruction() || isScanSkippable(MI))
    return false;
  // Stage-0: only real (non-pseudo) ops. LoopStart/SET_HWLOOP/LOAD_ADDR stay.
  if (MI.isPseudo())
    return false;
  if (MI.isTerminator() || MI.isBranch() || MI.isCall() || MI.isReturn())
    return false;
  if (MI.isBundle() || MI.isBundled())
    return false;
  if (MI.isInlineAsm() || MI.hasUnmodeledSideEffects())
    return false;
  // Conservatively skip FI / stack ops — PEI-sensitive.
  for (const MachineOperand &MO : MI.operands())
    if (MO.isFI())
      return false;
  return true;
}

bool HaydnInterBlockScheduling::readsRegDefinedIn(
    const MachineInstr &MI, const MachineBasicBlock &DefBB,
    const TargetRegisterInfo *TRI) {
  for (const MachineOperand &UO : MI.operands()) {
    if (!UO.isReg() || !UO.readsReg() || !UO.getReg())
      continue;
    Register UseReg = UO.getReg();
    if (!UseReg.isPhysical())
      continue;
    for (const MachineInstr &DefMI : DefBB) {
      if (isScanSkippable(DefMI) || DefMI.isTerminator())
        continue;
      // Ignore pure flag/SFR dead defs for independence of GPR/DR data.
      if (DefMI.definesRegister(UseReg, TRI) ||
          DefMI.modifiesRegister(UseReg, TRI)) {
        // Still count the def — even dead SFR: ADDI def SFR shouldn't block
        // an ADD that doesn't read SFR. Only signal when UseReg is actually
        // this def's reg (definesRegister already checks UseReg).
        // Skip dead defs that are not the use's register class concern: if
        // every matching def operand is dead and UseReg is SFR-like, ignore.
        bool LiveDef = false;
        for (const MachineOperand &DO : DefMI.operands()) {
          if (!DO.isReg() || !DO.isDef() || !DO.getReg())
            continue;
          if (!TRI->regsOverlap(DO.getReg(), UseReg))
            continue;
          if (!DO.isDead())
            LiveDef = true;
        }
        if (LiveDef)
          return true;
      }
    }
  }
  return false;
}

bool HaydnInterBlockScheduling::canCoIssue(const MachineInstr &A,
                                           const MachineInstr &B,
                                           const TargetRegisterInfo *TRI) {
  if (A.isBundle() || B.isBundle() || A.isBundled() || B.isBundled())
    return false;
  if (A.hasUnmodeledSideEffects() || B.hasUnmodeledSideEffects())
    return false;
  // Memory: two stores, or load+store, cannot share a Haydn bundle (matches
  // HaydnVLIWResourceModel::hasDependence).
  if (A.mayStore() && B.mayStore())
    return false;
  if ((A.mayLoad() && B.mayStore()) || (A.mayStore() && B.mayLoad()))
    return false;

  auto conflicts = [&](const MachineInstr &Writer, const MachineInstr &Other,
                       bool CheckRead) -> bool {
    for (const MachineOperand &MO : Writer.operands()) {
      if (!MO.isReg() || !MO.isDef() || MO.isDead() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      if (CheckRead && Other.readsRegister(Reg, TRI))
        return true;
      if (Other.definesRegister(Reg, TRI))
        return true;
    }
    return false;
  };

  // RAW: A defs, B reads (or reverse).
  if (conflicts(A, B, /*CheckRead=*/true) || conflicts(B, A, /*CheckRead=*/true))
    return false;
  // WAW already covered by definesRegister in conflicts.
  // WAR: B defs, A reads — covered by conflicts(B,A,true).
  return true;
}

bool HaydnInterBlockScheduling::moveAndMaybeBundle(
    MachineInstr &MI, MachineBasicBlock &Dest,
    MachineBasicBlock::iterator InsertBefore) {
  MachineBasicBlock *Src = MI.getParent();
  assert(Src && Src != &Dest && "cross-BB move only");

  const TargetRegisterInfo *TRI =
      Dest.getParent()->getSubtarget().getRegisterInfo();

  // Track defs so we can drop them from Src live-ins if present and add to
  // Dest live-outs implicitly via fallthrough (Succ live-ins: keep uses).
  SmallVector<Register, 4> NewDefs;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() && MO.getReg().isPhysical() &&
        !MO.isDead())
      NewDefs.push_back(MO.getReg());
  }

  LLVM_DEBUG({
    dbgs() << "  haydn-interblock: move ";
    MI.print(dbgs(), /*IsStandalone=*/true, /*SkipOpers=*/false,
             /*SkipDebugLoc=*/true);
    dbgs() << "  from bb." << Src->getNumber() << " -> bb." << Dest.getNumber()
           << "\n";
  });

  Dest.splice(InsertBefore, Src, MI.getIterator());
  ++NumInterBlockMoved;

  // Uses: add MI's used physregs as Dest live-ins if not defined earlier in Dest.
  // Scan with instr iterators — MI may sit next to bundles; constructing a
  // bundle iterator from a non-boundary MI asserts.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.readsReg() || !MO.getReg() || !MO.getReg().isPhysical())
      continue;
    Register Reg = MO.getReg();
    bool DefinedInDest = false;
    for (MachineBasicBlock::instr_iterator DI = Dest.instr_begin(),
                                          DE = MI.getIterator();
         DI != DE; ++DI) {
      if (DI->definesRegister(Reg, TRI) || DI->modifiesRegister(Reg, TRI)) {
        DefinedInDest = true;
        break;
      }
    }
    if (!DefinedInDest && !Dest.isLiveIn(Reg))
      Dest.addLiveIn(Reg);
  }

  // Defs: the moved MI now defines these in Dest; remaining users in Src see
  // them via fallthrough, so Src must list them as live-ins.
  for (Register Def : NewDefs) {
    if (!Src->isLiveIn(Def))
      Src->addLiveIn(Def);
  }

  // Try co-issue with the preceding non-meta instruction in Dest.
  MachineBasicBlock::instr_iterator It = MI.getIterator();
  if (It == Dest.instr_begin())
    return true;
  MachineInstr *Prev = &*std::prev(It);
  // Walk over meta/debug to find a real predecessor.
  while (Prev && (isScanSkippable(*Prev) || Prev->isPosition())) {
    if (Prev == &*Dest.instr_begin()) {
      Prev = nullptr;
      break;
    }
    Prev = &*std::prev(Prev->getIterator());
  }
  if (!Prev || Prev->isTerminator() || Prev->isBundle() || Prev->isBundled())
    return true;

  // Count instructions already in a potential same-cycle group: only pack
  // with a single standalone Prev (Stage-0; multi-member BUNDLE expand is
  // deferred).
  if (!canCoIssue(*Prev, MI, TRI))
    return true;

  MI.bundleWithPred();
  finalizeBundle(Dest, Prev->getIterator());
  ++NumInterBlockBundled;
  LLVM_DEBUG(dbgs() << "  haydn-interblock: co-issued with predecessor\n");
  return true;
}

// True if any MI in \p MBB is a SET_HWLOOP{,_REG}/LoopStart targeting \p Loop.
static bool hasHwLoopSetupTargeting(const MachineBasicBlock &MBB,
                                    const MachineBasicBlock &Loop) {
  for (const MachineInstr &MI : MBB) {
    if (!isHwLoopSetup(MI))
      continue;
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::LoopStart)
      return true; // conservative: treat as setup for this fallthrough
    if ((Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG ||
         Opc == Haydn::SET_HWLOOP_W || Opc == Haydn::SET_HWLOOP_F2_W ||
         Opc == Haydn::SET_HWLOOP_REG_W) &&
        MI.getNumOperands() >= 2 && MI.getOperand(1).isMBB() &&
        MI.getOperand(1).getMBB() == &Loop)
      return true;
  }
  return false;
}

bool HaydnInterBlockScheduling::tryFallthroughPack(MachineBasicBlock &Pred,
                                                   MachineBasicBlock &Succ) {
  // Succ must be uniquely reached from Pred so moving prefix ops into Pred
  // cannot skip them on another path.
  if (Succ.pred_size() != 1 || Succ.getSinglePredecessor() != &Pred)
    return false;
  // Never pull into a ZOL/hwloop body (CFG self-loop OR SET begin target).
  // Post-convert bodies often lack a CFG back-edge; hasSelfLoop alone is
  // insufficient (vec_scale: exit and32 packed into body → II growth).
  if (isHwLoopBody(Pred))
    return false;
  // Never empty a ZOL loop body into its preheader. That leaves SET_HWLOOP
  // pointing at a hollow body and Fixup demotes (NatureDSP gate failure).
  if (isHwLoopBody(Succ))
    return false;
  if (hasHwLoopSetupTargeting(Pred, Succ))
    return false;

  // never fallthrough-pack across a *real* call (JAL/PseudoCALL
  // isCall and not a terminator). Pulling argument materialization (moves
  // into R1/R2) out of Succ, or past a call that clobbers caller-saves in
  // Pred, drops the only defs that reach a later call (Dhrystone Func_2 →
  // strcmp). Haydn returns via JALR are isCall+isTerminator (not isReturn
  // RET is a separate pseudo); those still allow acyclic fallthrough pack
  // (interblock-fallthrough-pack.mir).
  auto hasRealCall = [](const MachineBasicBlock &BB) {
    for (const MachineInstr &MI : BB)
      if (MI.isCall() && !MI.isTerminator() && !MI.isReturn())
        return true;
    return false;
  };
  if (hasRealCall(Succ) || hasRealCall(Pred))
    return false;

  // never pack past a *conditional* branch. Inserting Succ prefix
  // before Pred's terminators makes those ops run on the taken path too, and
  // a def that aliases the branch predicate (e.g. ADDI into the SEQ dest)
  // kills the condition:
  // seq r14, n, 0; bnez r14, skip_clz
  // → seq r14; addi r14, 2; bnez r14 / always taken → wrong clz(n)
  // Observed: compiler-rt __udivsi3 O1+ → 6/3=26 (O0 CRT MATCH). Closed rule:
  // pure fallthrough / unconditional B only.
  for (const MachineInstr &T : Pred.terminators()) {
    if (T.isConditionalBranch())
      return false;
  }

  bool Changed = false;
  // Pull a short prefix of independent MIs (cap keeps Stage-0 cheap).
  constexpr unsigned MaxPull = 4;
  unsigned Pulled = 0;

  const TargetRegisterInfo *TRI =
      Pred.getParent()->getSubtarget().getRegisterInfo();

  SmallVector<MachineInstr *, 4> Candidates;
  for (MachineInstr &MI : Succ) {
    if (MI.isTerminator() || MI.isBranch() || MI.isCall() || MI.isReturn())
      break;
    if (isScanSkippable(MI) || MI.isPosition())
      continue;
    if (!isMovableCandidate(MI))
      break; // preserve relative order past barriers
    // Stage-0 independence: MI must not read any reg defined in Pred. (If it
    // did, we could still place it *after* those defs — deferred.)
    if (readsRegDefinedIn(MI, Pred, TRI))
      break;
    // Belt-and-suspenders with the cond-branch gate: never clobber a reg used
    // by Pred terminators (branch predicate / indirect target).
    bool ClobbersTermUse = false;
    for (const MachineOperand &Def : MI.all_defs()) {
      if (!Def.isReg() || !Def.getReg())
        continue;
      for (const MachineInstr &T : Pred.terminators()) {
        for (const MachineOperand &U : T.operands()) {
          if (U.isReg() && U.readsReg() &&
              TRI->regsOverlap(U.getReg(), Def.getReg())) {
            ClobbersTermUse = true;
            break;
          }
        }
        if (ClobbersTermUse)
          break;
      }
      if (ClobbersTermUse)
        break;
    }
    if (ClobbersTermUse)
      break;
    // Memory: if Pred has any mem op, skip mem candidates (conservative AA).
    if (MI.mayLoadOrStore()) {
      bool PredHasMem = false;
      for (const MachineInstr &P : Pred) {
        if (!P.isTerminator() && P.mayLoadOrStore()) {
          PredHasMem = true;
          break;
        }
      }
      if (PredHasMem)
        break;
    }
    Candidates.push_back(&MI);
    if (Candidates.size() >= MaxPull)
      break;
  }

  MachineBasicBlock::iterator InsertBefore = Pred.getFirstTerminator();
  for (MachineInstr *MI : Candidates) {
    // Re-check parent (previous splice may have changed iterators only).
    if (MI->getParent() != &Succ)
      continue;
    if (moveAndMaybeBundle(*MI, Pred, InsertBefore)) {
      Changed = true;
      ++Pulled;
      // After a move, insert before terminators still; newly moved MI sits
      // just before InsertBefore, so subsequent inserts stay ordered.
      InsertBefore = Pred.getFirstTerminator();
    }
  }

  if (Changed)
    LLVM_DEBUG(dbgs() << "  haydn-interblock: fallthrough pack moved " << Pulled
                      << " MI(s) bb." << Succ.getNumber() << " -> bb."
                      << Pred.getNumber() << "\n");
  return Changed;
}

bool HaydnInterBlockScheduling::runOnMBB(MachineBasicBlock &MBB) {
  if (!EnableHaydnInterBlock)
    return false;

  bool Changed = false;

  // Hwloop body (SET-targeted or CFG self-loop): never fallthrough-pack into
  // or out of the body (vec_scale II growth). Do NOT invent ZOL exit→preheader
  // hoist — AIE InterBlock does not Stage-0-hoist exit MIs either; the reject
  // contract (body + preheader IV uses) cost more than any measured win.
  if (isHwLoopBody(MBB))
    return false;

  // Preheader with SET_HWLOOP: do not fallthrough-pack the loop body into us.
  if (MachineBasicBlock *Succ = getLayoutFallthrough(MBB)) {
    if (!isHwLoopBody(*Succ) && !hasHwLoopSetupTargeting(MBB, *Succ))
      Changed |= tryFallthroughPack(MBB, *Succ);
  }

  return Changed;
}
