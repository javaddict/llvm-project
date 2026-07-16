//===-- HaydnRedundantCopyElim.cpp - Haydn Redundant Copy Elimination -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation pass that eliminates
// redundant COPY and MOVE32 instructions for the Haydn VLIW DSP target.
//
// Unlike HaydnCopyElim (which handles identity copies, dead copies, and
// writes to R0), this pass leverages dominating condition information to
// eliminate copies whose values are implied by the control flow.
//
// Patterns handled:
//
// 1. After BEQZ rs,.Ltarget: on the taken path rs is known to be 0.
// Any MOVE32/COPY rd, R0 (where rd == rs) is redundant since rs is
// already 0. Also, any MOVE32 rd, rs where rd == rs is an identity
// copy already known to be zero.
//
// 2. After BNEZ rs,.Ltarget: on the fallthrough path rs is 0.
// Similar elimination on the fallthrough successor.
//
// 3. After BEQ rs1, rs2,.Ltarget: on the taken path rs1 == rs2.
// A MOVE32 rd, rs2 (where rd == rs1) is redundant since rs1 already
// holds the same value. Symmetrically for rs1/rs2 swap.
//
// 4. After SEQ32/SNE32/SLT32/etc. followed by BNEZ/BEQZ:
// if the comparison sets rd = (rs1 == rs2) and we branch on rd
// we know the relationship between rs1 and rs2 on each path.
//
// This pass is inspired by AArch64RedundantCopyElimination but simplified
// for the Haydn ISA (no NZCV flags, simpler branch model).
//
// The pass requires NoVRegs (runs post-RA) so that all operands are
// physical registers and the analysis is over stable register assignments.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-redundant-copy-elim"

using namespace llvm;

STATISTIC(NumRedundantCopiesEliminated,
          "Number of redundant copies eliminated via condition analysis");

namespace {

// Represents a known relationship between two registers or a known constant
// value for a register at a program point.
struct KnownRegInfo {
  enum KindTy {
    // The register is known to hold a specific constant value.
    KnownConstant,
    // Two registers are known to hold the same value.
    KnownEqual,
  };

  KindTy Kind;

  // For KnownConstant: the register that holds the constant.
  // For KnownEqual: the first register in the equality.
  MCPhysReg Reg1;

  // For KnownConstant: the constant value.
  // For KnownEqual: the second register in the equality.
  union {
    int32_t Constant;
    MCPhysReg Reg2;
  };

  static KnownRegInfo constant(MCPhysReg Reg, int32_t Val) {
    KnownRegInfo KI;
    KI.Kind = KnownConstant;
    KI.Reg1 = Reg;
    KI.Constant = Val;
    return KI;
  }

  static KnownRegInfo equal(MCPhysReg R1, MCPhysReg R2) {
    KnownRegInfo KI;
    KI.Kind = KnownEqual;
    KI.Reg1 = R1;
    KI.Reg2 = R2;
    return KI;
  }
};

class HaydnRedundantCopyElim : public MachineFunctionPass {
  const MachineRegisterInfo *MRI = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const HaydnInstrInfo *HII = nullptr;

  // Track registers clobbered between a condition and the current position.
  LiveRegUnits ClobberedRegs;

  // Track registers used between a condition and the current position.
  LiveRegUnits UsedRegs;

  // Analyze a conditional branch to determine known register values at the
  // entry of a successor block. Returns true if any known values are found.
  bool knownRegValsInBlock(MachineInstr &CondBr, MachineBasicBlock *MBB,
                           SmallVectorImpl<KnownRegInfo> &KnownRegs,
                           MachineBasicBlock::iterator &FirstUse);

  // Try to optimize copies in \p MBB using known register values from
  // the dominating condition.
  bool optimizeBlock(MachineBasicBlock *MBB);

public:
  static char ID;

  HaydnRedundantCopyElim() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Haydn Redundant Copy Elimination";
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

char HaydnRedundantCopyElim::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(HaydnRedundantCopyElim, "haydn-redundant-copy-elim",
                "Haydn Redundant Copy Elimination", false, false)

//===----------------------------------------------------------------------===//
// Implementation
//===----------------------------------------------------------------------===//

bool HaydnRedundantCopyElim::knownRegValsInBlock(
    MachineInstr &CondBr, MachineBasicBlock *MBB,
    SmallVectorImpl<KnownRegInfo> &KnownRegs,
    MachineBasicBlock::iterator &FirstUse) {
  unsigned Opc = CondBr.getOpcode();

  //===--- Handle BEQZ: if taken, source register is 0 ---===
  // Accept both the legacy 32-bit form and the WIDE `_W` form (CodeGen
  // selects `_W`). See Phase 1b follow-up.
  if (Opc == Haydn::BEQZ || Opc == Haydn::BEQZ_W) {
    MachineBasicBlock *BrTarget = CondBr.getOperand(1).getMBB();
    if (BrTarget == MBB) {
      // On the taken path, the tested register is 0.
      MCPhysReg TestReg = CondBr.getOperand(0).getReg();
      FirstUse = CondBr.getIterator();
      KnownRegs.push_back(KnownRegInfo::constant(TestReg, 0));
      return true;
    }
    return false;
  }

  //===--- Handle BNEZ: if NOT taken (fallthrough), reg is 0 ---===
  if (Opc == Haydn::BNEZ || Opc == Haydn::BNEZ_W) {
    MachineBasicBlock *BrTarget = CondBr.getOperand(1).getMBB();
    if (BrTarget != MBB) {
      // On the fallthrough path, the tested register is 0.
      MCPhysReg TestReg = CondBr.getOperand(0).getReg();
      FirstUse = CondBr.getIterator();
      KnownRegs.push_back(KnownRegInfo::constant(TestReg, 0));
      return true;
    }
    return false;
  }

  //===--- Handle BGEZ: if taken, reg >= 0; if fallthrough, reg < 0 ---===
  // For now we don't exploit sign information, but we could in the future.

  //===--- Handle BEQ: if taken, rs1 == rs2 ---===
  if (Opc == Haydn::BEQ || Opc == Haydn::BEQ_W) {
    MachineBasicBlock *BrTarget = CondBr.getOperand(2).getMBB();
    if (BrTarget == MBB) {
      MCPhysReg Rs1 = CondBr.getOperand(0).getReg();
      MCPhysReg Rs2 = CondBr.getOperand(1).getReg();
      FirstUse = CondBr.getIterator();
      KnownRegs.push_back(KnownRegInfo::equal(Rs1, Rs2));
      return true;
    }
    return false;
  }

  //===--- Handle BNE: if fallthrough, rs1 == rs2 ---===
  if (Opc == Haydn::BNE || Opc == Haydn::BNE_W) {
    MachineBasicBlock *BrTarget = CondBr.getOperand(2).getMBB();
    if (BrTarget != MBB) {
      MCPhysReg Rs1 = CondBr.getOperand(0).getReg();
      MCPhysReg Rs2 = CondBr.getOperand(1).getReg();
      FirstUse = CondBr.getIterator();
      KnownRegs.push_back(KnownRegInfo::equal(Rs1, Rs2));
      return true;
    }
    return false;
  }

  //===--- Handle conditional branches preceded by SEQ32/SNE32 ---===
  // Pattern: SEQ32 rd, rs1, rs2 (sets rd = rs1 == rs2 ? 1 : 0)
  // BEQZ/BNEZ rd, target
  // On the BEQZ taken path: rd==0, meaning rs1 != rs2
  // On the BNEZ taken path: rd!=0, meaning rs1 == rs2
  // We currently only exploit the "equal" case for BNEZ-taken.
  if (Opc == Haydn::BEQZ || Opc == Haydn::BNEZ || Opc == Haydn::BEQZ_W ||
      Opc == Haydn::BNEZ_W) {
    // Already handled the simple case above; now check for comparison
    // instruction that feeds the branch.
    MachineBasicBlock *PredMBB = CondBr.getParent();
    MCPhysReg BrReg = CondBr.getOperand(0).getReg();
    MachineBasicBlock *BrTarget = CondBr.getOperand(1).getMBB();

    ClobberedRegs.clear();
    UsedRegs.clear();

    // Scan backward from the branch to find the instruction that defines
    // BrReg (the register being tested).
    for (auto RIt = std::next(CondBr.getReverseIterator());
         RIt != PredMBB->rend(); ++RIt) {
      MachineInstr &PredI = *RIt;

      // If this instruction defines BrReg, check if it's a comparison.
      if (PredI.getOpcode() == Haydn::SEQ32) {
        MCPhysReg DstReg = PredI.getOperand(0).getReg();
        if (DstReg != BrReg)
          return false;

        MCPhysReg Src1 = PredI.getOperand(1).getReg();
        MCPhysReg Src2 = PredI.getOperand(2).getReg();

        // The source registers must not be clobbered between the comparison
        // and the branch.
        if (!ClobberedRegs.available(Src1) || !ClobberedRegs.available(Src2))
          return false;

        // SEQ32: rd = (rs1 == rs2) ? 1 : 0
        // BEQZ taken: rd==0 means rs1 != rs2 -- not useful for copy elim
        // BNEZ taken: rd!=0 means rs1 == rs2 -- rs1 and rs2 are equal
        // Same semantics for the WIDE `_W` forms (CodeGen selects `_W`).
        if ((Opc == Haydn::BNEZ || Opc == Haydn::BNEZ_W) && BrTarget == MBB) {
          FirstUse = PredI.getIterator();
          KnownRegs.push_back(KnownRegInfo::equal(Src1, Src2));
          return true;
        }
        return false;
      }

      // If we see another definition of BrReg that isn't SEQ32, bail out.
      if (PredI.modifiesRegister(BrReg, TRI))
        return false;

      // Track clobbered registers.
      LiveRegUnits::accumulateUsedDefed(PredI, ClobberedRegs, UsedRegs, TRI);
    }
  }

  return false;
}

bool HaydnRedundantCopyElim::optimizeBlock(MachineBasicBlock *MBB) {
  // This pass requires a single predecessor with a conditional branch.
  if (MBB->pred_size() != 1)
    return false;

  MachineBasicBlock *PredMBB = *MBB->pred_begin();
  if (PredMBB->succ_size() != 2)
    return false;

  // Find the last non-debug instruction in the predecessor (the branch).
  auto CondBrIt = PredMBB->getLastNonDebugInstr();
  if (CondBrIt == PredMBB->end())
    return false;

  MachineInstr &CondBr = *CondBrIt;

  // Registers known at the entry of MBB.
  SmallVector<KnownRegInfo, 4> KnownRegs;
  MachineBasicBlock::iterator FirstUse;

  // Determine known register values from the dominating condition.
  if (!knownRegValsInBlock(CondBr, MBB, KnownRegs, FirstUse))
    return false;

  if (KnownRegs.empty())
    return false;

  // Reset clobbered register tracking for the optimization scan.
  ClobberedRegs.clear();
  UsedRegs.clear();

  // Propagate known values backward through COPY chains in the predecessor.
  // If X = COPY Y and Y is known, then X is known too.
  // Snapshot size: never push into KnownRegs while range-iterating it (UB).
  for (auto PredI = CondBr.getIterator();;) {
    if (PredI->isCopy()) {
      MCPhysReg CopyDst = PredI->getOperand(0).getReg();
      MCPhysReg CopySrc = PredI->getOperand(1).getReg();

      SmallVector<KnownRegInfo, 4> NewFacts;
      const unsigned KnownCount = KnownRegs.size();
      for (unsigned KI = 0; KI < KnownCount; ++KI) {
        const KnownRegInfo &Known = KnownRegs[KI];
        if (!ClobberedRegs.available(CopyDst))
          continue;

        if (Known.Kind == KnownRegInfo::KnownEqual) {
          // COPY X = Y; if Y is known equal to Z, then X is also equal to Z.
          if (CopySrc == Known.Reg1 &&
              ClobberedRegs.available(Known.Reg2)) {
            NewFacts.push_back(KnownRegInfo::equal(CopyDst, Known.Reg2));
            break;
          }
          if (CopySrc == Known.Reg2 &&
              ClobberedRegs.available(Known.Reg1)) {
            NewFacts.push_back(KnownRegInfo::equal(CopyDst, Known.Reg1));
            break;
          }
          // COPY X = Y; if X is known equal to Z, then Y is also equal to Z.
          if (CopyDst == Known.Reg1 &&
              ClobberedRegs.available(Known.Reg2)) {
            NewFacts.push_back(KnownRegInfo::equal(CopySrc, Known.Reg2));
            break;
          }
          if (CopyDst == Known.Reg2 &&
              ClobberedRegs.available(Known.Reg1)) {
            NewFacts.push_back(KnownRegInfo::equal(CopySrc, Known.Reg1));
            break;
          }
        }

        if (Known.Kind == KnownRegInfo::KnownConstant) {
          // COPY X = Y; if Y is known constant, then X is known constant.
          if (CopySrc == Known.Reg1) {
            NewFacts.push_back(
                KnownRegInfo::constant(CopyDst, Known.Constant));
            break;
          }
          // COPY X = Y; if X is known constant, then Y is known constant.
          if (CopyDst == Known.Reg1) {
            NewFacts.push_back(
                KnownRegInfo::constant(CopySrc, Known.Constant));
            break;
          }
        }
      }
      KnownRegs.append(NewFacts.begin(), NewFacts.end());
    }

    if (PredI == PredMBB->begin())
      break;
    --PredI;

    LiveRegUnits::accumulateUsedDefed(*PredI, ClobberedRegs, UsedRegs, TRI);

    // Stop if all known regs have been clobbered.
    if (all_of(KnownRegs, [&](const KnownRegInfo &K) {
          MCPhysReg R = (K.Kind == KnownRegInfo::KnownEqual) ? K.Reg1
                                                             : K.Reg1;
          return !ClobberedRegs.available(R);
        }))
      break;
  }

  if (KnownRegs.empty())
    return false;

  bool Changed = false;
  SmallSetVector<unsigned, 4> UsedKnownRegs;
  MachineBasicBlock::iterator LastChange = MBB->begin();

  // Scan forward through MBB, removing redundant copies/moves.
  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end(); I != E;) {
    MachineInstr *MI = &*I;
    ++I;

    // Only consider COPY and MOVE32 instructions.
    bool IsCopy = MI->isCopy();
    bool IsMove32 = (MI->getOpcode() == Haydn::MOVE32);
    if (!IsCopy && !IsMove32) {
      // Remove any known regs clobbered by this instruction (both sides of
      // equality facts — Reg1-only kill left stale a==b when b was redefined).
      for (unsigned RI = 0; RI < KnownRegs.size();) {
        const KnownRegInfo &K = KnownRegs[RI];
        bool Clobbered = MI->modifiesRegister(K.Reg1, TRI);
        if (!Clobbered && K.Kind == KnownRegInfo::KnownEqual)
          Clobbered = MI->modifiesRegister(K.Reg2, TRI);
        if (Clobbered) {
          std::swap(KnownRegs[RI], KnownRegs[KnownRegs.size() - 1]);
          KnownRegs.pop_back();
        } else {
          ++RI;
        }
      }
      if (KnownRegs.empty())
        break;
      continue;
    }

    // Get destination and source registers.
    Register DefReg = MI->getOperand(0).getReg();
    Register SrcReg = MI->getOperand(1).getReg();

    if (!DefReg.isPhysical() || !SrcReg.isPhysical())
      continue;

    // Don't touch reserved registers (e.g., SP, FP, LR).
    if (MRI->isReserved(DefReg))
      continue;

    // Don't remove copies inside bundles -- they need special handling.
    if (MI->isBundled())
      continue;

    bool RemovedMI = false;

    for (const KnownRegInfo &Known : KnownRegs) {
      if (Known.Kind == KnownRegInfo::KnownConstant) {
        // Case: register DefReg is known to be constant, and this copy/move
        // sets DefReg to that same constant value.
        // For a MOVE32/COPY from R0 (zero register), the source value is 0.
        // If DefReg is known to be 0, this copy is redundant.
        if (Known.Reg1 == DefReg && Known.Constant == 0 &&
            (SrcReg == Haydn::R0 || DefReg == SrcReg)) {
          LLVM_DEBUG(dbgs() << "  Removing redundant zero-copy (condition): "
                            << *MI);
          MI->eraseFromParent();
          Changed = true;
          LastChange = I;
          ++NumRedundantCopiesEliminated;
          UsedKnownRegs.insert(Known.Reg1);
          RemovedMI = true;
          break;
        }
      }

      if (Known.Kind == KnownRegInfo::KnownEqual) {
        // Case: registers Reg1 and Reg2 are known to be equal.
        // If we have MOVE32 DefReg, SrcReg where (DefReg,SrcReg) matches
        // the known equality, the copy is redundant.

        // MOVE32 R1, R2 where R1 == R2 is known -> redundant
        if (DefReg == Known.Reg1 && SrcReg == Known.Reg2) {
          LLVM_DEBUG(dbgs() << "  Removing redundant equal-copy (condition): "
                            << *MI);
          MI->eraseFromParent();
          Changed = true;
          LastChange = I;
          ++NumRedundantCopiesEliminated;
          UsedKnownRegs.insert(Known.Reg1);
          RemovedMI = true;
          break;
        }
        // MOVE32 R2, R1 where R1 == R2 is known -> redundant
        if (DefReg == Known.Reg2 && SrcReg == Known.Reg1) {
          LLVM_DEBUG(dbgs() << "  Removing redundant equal-copy (condition): "
                            << *MI);
          MI->eraseFromParent();
          Changed = true;
          LastChange = I;
          ++NumRedundantCopiesEliminated;
          UsedKnownRegs.insert(Known.Reg1);
          RemovedMI = true;
          break;
        }

        // MOVE32 DefReg, SrcReg where DefReg is known equal to SrcReg ->
        // redundant (this is a generalization of the above for transitive
        // equalities discovered through COPY propagation).
        if (DefReg == SrcReg) {
          // Identity copy -- this is already handled by HaydnCopyElim, but
          // we handle it here too for completeness.
          LLVM_DEBUG(dbgs() << "  Removing identity copy (condition): "
                            << *MI);
          MI->eraseFromParent();
          Changed = true;
          LastChange = I;
          ++NumRedundantCopiesEliminated;
          RemovedMI = true;
          break;
        }
      }
    }

    if (RemovedMI)
      continue;

    // The copy was not removed. Check if it clobbers any known register.
    for (unsigned RI = 0; RI < KnownRegs.size();) {
      const KnownRegInfo &K = KnownRegs[RI];
      bool Clobbered = MI->modifiesRegister(K.Reg1, TRI);
      if (!Clobbered && K.Kind == KnownRegInfo::KnownEqual)
        Clobbered = MI->modifiesRegister(K.Reg2, TRI);
      if (Clobbered) {
        std::swap(KnownRegs[RI], KnownRegs[KnownRegs.size() - 1]);
        KnownRegs.pop_back();
      } else {
        ++RI;
      }
    }

    if (KnownRegs.empty())
      break;
  }

  if (!Changed)
    return false;

  // Add newly used regs to the block's live-in list if they aren't there.
  for (MCPhysReg KnownReg : UsedKnownRegs)
    if (!MBB->isLiveIn(KnownReg))
      MBB->addLiveIn(KnownReg);

  // Clear kill flags in the range from FirstUse to end of PredMBB and from
  // MBB begin to LastChange. This is conservative but safe -- kill markers
  // are being phased out in LLVM anyway.
  LLVM_DEBUG(dbgs() << "  Clearing kill flags around removed copies\n");
  for (MachineInstr &MMI : make_range(FirstUse, PredMBB->end()))
    MMI.clearKillInfo();
  for (MachineInstr &MMI : make_range(MBB->begin(), LastChange))
    MMI.clearKillInfo();

  return true;
}

bool HaydnRedundantCopyElim::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn Redundant Copy Elimination: "
                    << MF.getName() << " =====\n");

  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  HII = STI.getInstrInfo();
  TRI = STI.getRegisterInfo();
  MRI = &MF.getRegInfo();

  // Initialize the live register unit trackers once per function.
  ClobberedRegs.init(*TRI);
  UsedRegs.init(*TRI);

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    Changed |= optimizeBlock(&MBB);

  return Changed;
}

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHaydnRedundantCopyElimPass() {
  return new HaydnRedundantCopyElim();
}
