//===-- HaydnPostRAScratch.cpp - Post-RA GPR scratch + remat --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRAScratch.h"
#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnFrameLowering.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "haydn-postra-scratch"

// Call-clobbered first (R1–R7), then callee-saved temps that may still be
// dead at I (R11…R8). R12 is last and only considered when PreferNotR12 is
// false — never "acquire" fixed R12 as free AT. R0 is absent: reserved
// soft-zero, not a scavenger candidate (see HaydnPostRAScratch.h). Generic
// RegScavenger uses allocation order and skips reserved regs, so it cannot
// express AllowBorrow / NeedsZeroBase / R12-last.
static constexpr MCPhysReg PostRAScratchPriority[] = {
    Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5, Haydn::R6,
    Haydn::R7, Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R12,
};

static bool isExcluded(MCPhysReg Cand, ArrayRef<Register> Exclude) {
  for (Register E : Exclude) {
    if (E.isPhysical() && E.id() == Cand)
      return true;
  }
  return false;
}

// Soft-zero restore idiom: xor32 r0, r0, r0.
static bool isSoftZeroRestore(const MachineInstr &MI) {
  if (MI.getOpcode() != Haydn::XOR32 || MI.getNumExplicitOperands() < 3)
    return false;
  if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isReg())
    return false;
  return MI.getOperand(0).getReg() == Haydn::R0 &&
         MI.getOperand(1).getReg() == Haydn::R0 &&
         MI.getOperand(2).getReg() == Haydn::R0;
}

bool llvm::isSoftZeroR0Clean(const MachineBasicBlock &MBB,
                             MachineBasicBlock::const_iterator I) {
  // Walk backward to the last explicit def of R0 in this MBB.
  for (MachineBasicBlock::const_iterator II = I; II != MBB.begin();) {
    --II;
    if (II->isDebugInstr() || II->isMetaInstruction())
      continue;
    bool DefsR0 = false;
    for (const MachineOperand &MO : II->operands()) {
      if (MO.isReg() && MO.isDef() && !MO.isDead() && MO.getReg() == Haydn::R0) {
        DefsR0 = true;
        break;
      }
    }
    if (!DefsR0)
      continue;
    // Last reaching def: soft-zero restore → clean; anything else → dirty.
    return isSoftZeroRestore(*II);
  }

  // No def of R0 in this MBB before I.
  // Entry: prologue zeros R0 — treat as clean at block start.
  if (MBB.isEntryBlock())
    return true;

  // Non-entry, no local def: live-in R0 is the ABI soft-zero carried in
  // (ExpandPseudos restores after JAL/JALR). Missing live-in is not proof
  // of zero — treat dirty so a later borrow cannot seed MatInt from garbage.
  return MBB.isLiveIn(Haydn::R0);
}

void llvm::restoreSoftZeroR0(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             const TargetInstrInfo &TII,
                             MachineInstr::MIFlag Flag) {
  BuildMI(MBB, I, DL, TII.get(Haydn::XOR32), Haydn::R0)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0)
      .setMIFlag(Flag);
}

void llvm::ensureSoftZeroR0Clean(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL,
                                 const TargetInstrInfo &TII,
                                 MachineInstr::MIFlag Flag) {
  if (!isSoftZeroR0Clean(MBB, I))
    restoreSoftZeroR0(MBB, I, DL, TII, Flag);
  assert(isSoftZeroR0Clean(MBB, I) &&
         "soft-zero R0 must be clean before borrow or MatInt seed");
}

bool llvm::isSoftZeroR0KnownDirty(const MachineBasicBlock &MBB,
                                  MachineBasicBlock::const_iterator I) {
  for (MachineBasicBlock::const_iterator II = I; II != MBB.begin();) {
    --II;
    if (II->isDebugInstr() || II->isMetaInstruction())
      continue;
    bool DefsR0 = false;
    for (const MachineOperand &MO : II->operands()) {
      if (MO.isReg() && MO.isDef() && !MO.isDead() && MO.getReg() == Haydn::R0) {
        DefsR0 = true;
        break;
      }
    }
    if (!DefsR0)
      continue;
    return !isSoftZeroRestore(*II);
  }
  return false;
}

void llvm::ensureSoftZeroR0IfKnownDirty(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        const DebugLoc &DL,
                                        const TargetInstrInfo &TII,
                                        MachineInstr::MIFlag Flag) {
  if (!isSoftZeroR0KnownDirty(MBB, I))
    return;
  restoreSoftZeroR0(MBB, I, DL, TII, Flag);
  assert(isSoftZeroR0Clean(MBB, I) &&
         "known-dirty R0 must be clean after restore");
}

bool llvm::insertSoftZeroR0AfterCalls(MachineFunction &MF,
                                      const TargetInstrInfo &TII) {
  bool Modified = false;

  // Indirect transfers that write the link into soft-zero R0 clobber the
  // architectural zero invariant. Re-zero at the head of every successor so
  // PostRA pack and size models see the bytes (not printer injection):
  //   * BR_JT — printer expands to JALR_W r0, addr (JALR_W is isCall; keep
  //     the jump pseudo through MBP/pack)
  //   * JALR_W / JALR with rd=R0 (G_BRINDIRECT pure jump; not a call)
  // RET is also JALR_W r0,lr but has no executable successors that need zero.
  DenseSet<MachineBasicBlock *> SoftZeroTargets;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      bool NeedsSuccRezero = false;
      if (Opc == Haydn::BR_JT) {
        NeedsSuccRezero = true;
      } else if ((Opc == Haydn::JALR_W || Opc == Haydn::JALR) &&
                 MI.getNumExplicitOperands() >= 1 && MI.getOperand(0).isReg() &&
                 MI.getOperand(0).getReg() == Haydn::R0) {
        NeedsSuccRezero = true;
      }
      if (!NeedsSuccRezero)
        continue;
      for (MachineBasicBlock *Succ : MBB.successors())
        SoftZeroTargets.insert(Succ);
    }
  }
  for (MachineBasicBlock *MBB : SoftZeroTargets) {
    MachineBasicBlock::iterator InsertPt = MBB->begin();
    while (InsertPt != MBB->end() &&
           (InsertPt->isMetaInstruction() || InsertPt->isDebugInstr() ||
            InsertPt->isCFIInstruction()))
      ++InsertPt;
    if (InsertPt != MBB->end() && isSoftZeroRestore(*InsertPt))
      continue;
    DebugLoc DL = InsertPt != MBB->end() ? InsertPt->getDebugLoc() : DebugLoc();
    restoreSoftZeroR0(*MBB, InsertPt, DL, TII);
    Modified = true;
    LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: soft-zero R0 at indirect-target "
                         "bb."
                      << MBB->getNumber() << '\n');
  }

  // After calls: JAL/JAL_W, PseudoCALLIndirect, and the W67 fnptr-call
  // clone JALR_MSP (ExpandPseudos lowers PseudoCALLIndirect to it pre-S1).
  // Callee RET is JALR_W r0,lr which clobbers R0. Direct calls are JAL_W
  // from CallLowering.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator MII = MBB.begin(), E = MBB.end();
         MII != E;) {
      MachineInstr &MI = *MII;
      ++MII;
      unsigned Opc = MI.getOpcode();
      bool NeedsPostCallZero =
          Opc == Haydn::JAL || Opc == Haydn::JAL_W ||
          Opc == Haydn::PseudoCALLIndirect || Opc == Haydn::JALR_MSP;
      if (!NeedsPostCallZero)
        continue;
      MachineBasicBlock::iterator Next = MII;
      while (Next != MBB.end() &&
             (Next->isMetaInstruction() || Next->isDebugInstr()))
        ++Next;
      if (Next != MBB.end() && isSoftZeroRestore(*Next))
        continue;
      restoreSoftZeroR0(MBB, MII, MI.getDebugLoc(), TII);
      Modified = true;
      LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: soft-zero R0 after call in bb."
                        << MBB.getNumber() << '\n');
    }
  }

  return Modified;
}

Register llvm::findPostRAScratchGPR(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    bool PreferNotR12, bool &NeedsSpill,
                                    ArrayRef<Register> Exclude) {
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(MBB);
  for (MachineBasicBlock::iterator II = MBB.end(); II != I;) {
    --II;
    LPR.stepBackward(*II);
  }

  Register FirstPreferred;
  for (MCPhysReg Cand : PostRAScratchPriority) {
    if (PreferNotR12 && Cand == Haydn::R12)
      continue;
    if (MRI.isReserved(Cand))
      continue;
    if (isExcluded(Cand, Exclude))
      continue;
    if (!FirstPreferred)
      FirstPreferred = Cand;
    if (LPR.available(MRI, Cand)) {
      NeedsSpill = false;
      return Cand;
    }
  }

  NeedsSpill = true;
  if (FirstPreferred)
    return FirstPreferred;
  for (MCPhysReg Cand : PostRAScratchPriority) {
    if (PreferNotR12 && Cand == Haydn::R12)
      continue;
    if (isExcluded(Cand, Exclude) || MRI.isReserved(Cand))
      continue;
    return Cand;
  }
  report_fatal_error(
      "Haydn: no post-RA scratch GPR (all candidates reserved/excluded)");
}

Register llvm::findPostRAScratchNoSpill(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, bool PreferNotR12,
    ArrayRef<MachineBasicBlock *> Successors, ArrayRef<Register> Exclude) {
  // One mechanism with findPostRAScratchGPR (same priority order, same
  // LivePhysRegs availability test) minus its NeedsSpill escape: the
  // produced value must outlive the bracket, so a spill bracket is not a
  // fallback but a wrong-code path. Successors seeds the live-out set for
  // callers that rewrite the CFG after this probe (pre-rewrite successors
  // can hide the back edge inside a pseudo).
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  LivePhysRegs LPR(TRI);
  // Seed the live-out set as LivePhysRegs::addLiveOuts would for the
  // effective successor set: the caller's override when non-empty (the CFG
  // will be rewritten to exactly those successors), else the block's real
  // successors. An EMPTY override must NOT mean "no live-outs" — that would
  // judge every live-through register available and clobber it (e.g. loop
  // body arguments live out of a demote preheader).
  SmallVector<const MachineBasicBlock *, 4> EffSuccs;
  if (Successors.empty()) {
    for (const MachineBasicBlock *S : MBB.successors())
      EffSuccs.push_back(S);
  } else {
    for (const MachineBasicBlock *S : Successors)
      EffSuccs.push_back(S);
  }
  for (const MachineBasicBlock *Succ : EffSuccs) {
    if (!Succ)
      continue;
    for (const MachineBasicBlock::RegisterMaskPair &P : Succ->liveins())
      LPR.addReg(P.PhysReg);
  }
  for (MachineBasicBlock::iterator II = MBB.end(); II != I;) {
    --II;
    LPR.stepBackward(*II);
  }

  for (MCPhysReg Cand : PostRAScratchPriority) {
    if (PreferNotR12 && Cand == Haydn::R12)
      continue;
    if (MRI.isReserved(Cand) || isExcluded(Cand, Exclude))
      continue;
    if (LPR.available(MRI, Cand))
      return Cand;
  }
  return Register();
}

namespace {

struct ScratchSpillHome {
  enum Kind { None, FrameIndex, SPBracket } K = None;
  Register FrameReg;
  int64_t Off = 0;
};

} // namespace

void llvm::emitFrameRelativeMemOp(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL,
                                  const TargetInstrInfo &TII, Register Reg,
                                  Register FrameReg, int64_t Off, bool IsStore,
                                  unsigned StoreFlags) {
  // Restatable rule: addressing of any in-frame spill slot uses three monotone
  // tiers tied to offset magnitude. SP is NEVER moved.
  //   tier 1 - short-form element-indexed ST32/LD32 FrameReg, elem
  //            (Off/4 fits isInt<6>)
  //   tier 2 - ADDI32_W R0, FrameReg, Off; ST32/LD32 R0, 0  (Off fits simm20)
  //   tier 3 - MatInt(Off) real ops chained through R0; ADD32 R0, FrameReg, R0;
  //            ST32/LD32 R0, 0  (any remaining Off; mirrors withDR64PackBase's
  //            large-offset rebase in HaydnInstrInfo::eliminateFrameIndex).
  // getFrameIndexReference returns a byte offset. ST32/LD32 immediates are
  // word element indices (EA = base + imm<<2). Tiers 2/3 borrow soft-zero R0
  // as a self-contained scratch (must be clean on entry) and restore it via
  // XOR32 R0,R0,R0 before return.
  if ((Off % 4) != 0)
    report_fatal_error(
        "Haydn: in-frame spill offset not word-aligned for ST32/LD32");
  const int64_t Elem = Off / 4;
  if (isInt<6>(Elem)) {
    // Tier 1 - short-form element-indexed access (byte-identical fast path).
    if (IsStore)
      BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
          .addReg(Reg, StoreFlags)
          .addReg(FrameReg)
          .addImm(Elem);
    else
      BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Reg)
          .addReg(FrameReg)
          .addImm(Elem);
    return;
  }

  // Tiers 2/3: materialise the addressed byte in R0 = FrameReg + Off, then
  // access element 0 through it, then restore R0 to soft-zero. Tier 3
  // MatInt seeds Cur=R0, so R0 must be clean before the borrow window.
  ensureSoftZeroR0Clean(MBB, I, DL, TII);
  const Register Tmp = Haydn::R0;
  if (isInt<20>(Off)) {
    // Tier 2 - simm20 immediate add.
    BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Tmp)
        .addReg(FrameReg)
        .addImm(Off);
  } else {
    // Tier 3 - large offset: materialise full width via real MatInt ops, then
    // add FrameReg. NEVER emit the LOADI32 pseudo here: this helper runs INSIDE
    // expandPostRAPseudo / PostRAScratch, so the pseudo would not be
    // re-expanded and would fatal AsmPrinter's residual cycle-forming pseudo
    // check. One mechanism — HaydnMatInt::generate — same as the LOADI32 case
    // in expandPostRAPseudo and the emitConst32 lambda in HaydnInstrInfo.
    HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Off);
    Register Cur = Haydn::R0;
    for (const HaydnMatInt::Inst &MatInst : Seq) {
      // LUI is dest+imm (logical matches Format E members).
      MachineInstrBuilder MIB = BuildMI(MBB, I, DL, TII.get(MatInst.Opc), Tmp);
      if (MatInst.Opc != Haydn::LUI)
        MIB.addReg(Cur);
      MIB.addImm(MatInst.Imm);
      Cur = Tmp;
    }
    BuildMI(MBB, I, DL, TII.get(Haydn::ADD32), Tmp)
        .addReg(FrameReg)
        .addReg(Tmp);
  }
  if (IsStore)
    BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
        .addReg(Reg, StoreFlags)
        .addReg(Tmp)
        .addImm(0);
  else
    BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Reg).addReg(Tmp).addImm(0);
  restoreSoftZeroR0(MBB, I, DL, TII);
}

namespace {

ScratchSpillHome beginSpill(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            const TargetInstrInfo &TII, const HaydnSubtarget &ST,
                            Register Scr) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int SpillFI = FuncInfo->getBranchRelaxationScratchFI();
  if (SpillFI < 0)
    SpillFI = FuncInfo->getPostRAScratchFI();

  ScratchSpillHome Home;
  if (SpillFI >= 0) {
    const HaydnFrameLowering *TFL = ST.getFrameLowering();
    Home.K = ScratchSpillHome::FrameIndex;
    Home.Off = TFL->getFrameIndexReferenceAt(MF, SpillFI, Home.FrameReg, MBB, I)
                   .getFixed();
    emitFrameRelativeMemOp(MBB, I, DL, TII, Scr, Home.FrameReg, Home.Off,
                           /*IsStore=*/true, /*StoreFlags=*/0);
    return Home;
  }

  Home.K = ScratchSpillHome::SPBracket;
  BuildMI(MBB, I, DL, TII.get(Haydn::SUBI32), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
  BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
      .addReg(Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  return Home;
}

void endSpill(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
              const DebugLoc &DL, const TargetInstrInfo &TII, Register Scr,
              const ScratchSpillHome &Home) {
  if (Home.K == ScratchSpillHome::None)
    return;
  if (Home.K == ScratchSpillHome::FrameIndex) {
    emitFrameRelativeMemOp(MBB, I, DL, TII, Scr, Home.FrameReg, Home.Off,
                           /*IsStore=*/false, /*StoreFlags=*/0);
    return;
  }
  BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
}

/// Make UseMI a top-level (unbundled) instruction so emit/splice is safe.
void unbundleIfNeeded(MachineInstr &UseMI) {
  if (UseMI.isBundledWithPred())
    UseMI.unbundleFromPred();
  if (UseMI.isBundledWithSucc())
    UseMI.unbundleFromSucc();
}

/// Bundle RematDef with UseMI; mark Dest uses on UseMI as Kill.
/// Both must be top-level and in the same MBB. Leaves UseMI as the use.
void glueDefToUse(MachineInstr &RematDef, MachineInstr &UseMI) {
  assert(RematDef.getParent() == UseMI.getParent());
  MachineBasicBlock &MBB = *RematDef.getParent();

  if (RematDef.getNumExplicitDefs() >= 1 && RematDef.getOperand(0).isReg()) {
    Register Dest = RematDef.getOperand(0).getReg();
    for (MachineOperand &MO : UseMI.operands()) {
      if (MO.isReg() && MO.isUse() && !MO.isImplicit() && MO.getReg() == Dest)
        MO.setIsKill(true);
    }
  }

  MachineBasicBlock::iterator DefIt = RematDef.getIterator();
  MachineBasicBlock::iterator UseIt = UseMI.getIterator();
  if (std::next(DefIt) != UseIt) {
    MachineBasicBlock::iterator InsertAfter = std::next(DefIt);
    while (InsertAfter != MBB.end() && InsertAfter->isDebugInstr())
      ++InsertAfter;
    if (UseIt != InsertAfter)
      MBB.splice(InsertAfter, &MBB, UseIt);
  }

  if (RematDef.isBundledWithSucc() || UseMI.isBundledWithPred())
    return;

  MachineBasicBlock::iterator AfterDef = std::next(RematDef.getIterator());
  while (AfterDef != MBB.end() && AfterDef->isDebugInstr())
    ++AfterDef;
  assert(AfterDef != MBB.end() && &*AfterDef == &UseMI &&
         "remat use not adjacent after splice");

  // Only glue into one product cycle when encode-oracle canAdd accepts both.
  // ADDI remat for SET_HWLOOP_REG is S0-only + SET is S0-only; leave sequential.
  {
    HaydnMCFormats Fmts;
    Haydn::Bundle<MachineInstr> Probe(&Fmts);
    if (!Probe.canAdd(RematDef.getOpcode()) ||
        (Probe.add(const_cast<MachineInstr *>(&RematDef)),
         !Probe.canAdd(UseMI.getOpcode()))) {
      LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: remat def->use not slot-legal "
                           "for one cycle - leave sequential\n");
      return;
    }
  }

  UseMI.bundleWithPred();
  finalizeBundle(MBB, RematDef.getIterator());
  MachineInstr &Root = *getBundleStart(RematDef.getIterator());
  assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
  const TargetInstrInfo &TII =
      *MBB.getParent()->getSubtarget().getInstrInfo();
  const unsigned MemberOps[] = {RematDef.getOpcode(), UseMI.getOpcode()};
  haydn::bundle::BundlePlan Plan = haydn::bundle::makeProductPlanForOpcodes(
      /*Occupied=*/0, MemberOps, TII);
  haydn::bundle::stampBundleCommit(Root, Plan);
  LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: glued remat def->use\n");
}

} // namespace

void llvm::withPostRAScratch(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             const TargetInstrInfo &TII,
                             const HaydnSubtarget &ST, bool PreferNotR12,
                             function_ref<void(Register Scr)> Fn,
                             ArrayRef<Register> Exclude,
                             PostRASoftZero SoftZero) {
  // Soft-zero R0 is never a scavenger candidate (reserved). Borrow is a
  // separate, optional path under AllowBorrow only.
  auto excluded = [&](MCPhysReg R) {
    for (Register E : Exclude)
      if (E.isPhysical() && E.id() == R)
        return true;
    return false;
  };

  // NeedsZeroBase: Fn reads R0 as MatInt/ADDI zero source. Scr must not be
  // R0 — that would clobber the zero mid-sequence (LOADI64 hi=lo bug,
  // VASTART __gr_offs poison). Always scavenger; leave R0 as soft-zero.
  // AllowBorrow: only if R0 is still clean soft-zero and not excluded.
  // Dirty R0 (prior un-restored write) → scavenger, never overwrite.
  if (SoftZero == PostRASoftZero::AllowBorrow && !excluded(Haydn::R0) &&
      isSoftZeroR0Clean(MBB, I)) {
    Fn(Haydn::R0);
    restoreSoftZeroR0(MBB, I, DL, TII);
    return;
  }

  bool NeedsSpill = false;
  const Register Scr =
      findPostRAScratchGPR(MBB, I, PreferNotR12, NeedsSpill, Exclude);
  assert(Scr != Haydn::R0 && "scavenger must not return soft-zero R0");

  if (!NeedsSpill) {
    Fn(Scr);
    return;
  }

  ScratchSpillHome Home = beginSpill(MBB, I, DL, TII, ST, Scr);
  Fn(Scr);
  endSpill(MBB, I, DL, TII, Scr, Home);
}

Register llvm::rematerializeAddImmForUse(MachineInstr &UseMI,
                                         unsigned UseOpIdx, int64_t Adj,
                                         ArrayRef<Register> ExtraExclude) {
  assert(UseOpIdx < UseMI.getNumOperands() && UseMI.getOperand(UseOpIdx).isReg() &&
         "rematerializeAddImmForUse: bad use operand");

  MachineOperand &UseMO = UseMI.getOperand(UseOpIdx);
  Register Src = UseMO.getReg();
  if (Adj == 0)
    return Src;

  assert(Src.isPhysical() && Src != Haydn::R0 &&
         "rematerializeAddImmForUse expects non-R0 phys Src");

  unbundleIfNeeded(UseMI);

  MachineBasicBlock &MBB = *UseMI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const TargetInstrInfo &TII = *ST.getInstrInfo();
  DebugLoc DL = UseMI.getDebugLoc();
  MachineBasicBlock::iterator InsertPt = UseMI.getIterator();

  // Never Dest == Src: remat creates a new value; Src may still be live.
  SmallVector<Register, 4> Exclude;
  Exclude.push_back(Src);
  Exclude.append(ExtraExclude.begin(), ExtraExclude.end());

  bool NeedsSpill = false;
  Register Dest =
      findPostRAScratchGPR(MBB, InsertPt, /*PreferNotR12=*/true, NeedsSpill,
                           Exclude);

  ScratchSpillHome Home;
  if (NeedsSpill) {
    Home = beginSpill(MBB, InsertPt, DL, TII, ST, Dest);
    LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: remat spill " << printReg(Dest)
                      << " for " << printReg(Src) << "+" << Adj << "\n");
  }

  // Src not killed — may still be live after the use.
  MachineInstr *RematDef =
      BuildMI(MBB, InsertPt, DL, TII.get(Haydn::ADDI32_W), Dest)
          .addReg(Src)
          .addImm(Adj);

  UseMO.setReg(Dest);
  UseMO.setIsKill(true);

  // Restore after UseMI consumed Dest, before gluing (top-level iterators).
  if (NeedsSpill) {
    MachineBasicBlock::iterator AfterUse = std::next(UseMI.getIterator());
    endSpill(MBB, AfterUse, DL, TII, Dest, Home);
  }

  glueDefToUse(*RematDef, UseMI);

  LLVM_DEBUG(dbgs() << "HaydnPostRAScratch: remat-for-use " << printReg(Dest)
                    << " = " << printReg(Src) << " + " << Adj << "\n");
  return Dest;
}
