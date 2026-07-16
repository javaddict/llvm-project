//===-- HaydnConditionOptimizer.h - Haydn Condition Opt -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-register-allocation MachineFunctionPass that
// simplifies comparison patterns for the Haydn VLIW DSP target:
//
// 1. Identity comparison: SLT32/SLTU32 r, rX, rX (same src and src)
// → replace with constant 0 (x < x is always false).
//
// 2. Inverse comparison reuse: SLT32 rA, rX, rY followed by
// SLT32 rB, rY, rX → replace the second with XORI32 rB, rA, 1
// (logical NOT of the first comparison result).
//
// 3. Cmp+branch folding: when a comparison instruction's result is only
// used by a single BEQZ/BNEZ, fold into a two-register branch:
// SEQ32 rd, rA, rB + BNEZ rd, target → BNE rA, rB, target
// SEQ32 rd, rA, rB + BEQZ rd, target → BEQ rA, rB, target
// SLT32 rd, rA, rB + BNEZ rd, target → BLT rA, rB, target
// SLT32 rd, rA, rB + BEQZ rd, target → BGE rA, rB, target
// SLTU32 rd, rA, rB + BNEZ rd, target → BLTU rA, rB, target
// SLTU32 rd, rA, rB + BEQZ rd, target → BGEU rA, rB, target
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNCONDITIONOPTIMIZER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNCONDITIONOPTIMIZER_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;

// HaydnConditionOptimizer - Simplifies comparison patterns after register
// allocation. Runs post-RA so that physical registers are available and
// the mapping between comparisons is stable.
class HaydnConditionOptimizer : public MachineFunctionPass {
public:
  static char ID;

  HaydnConditionOptimizer();

  StringRef getPassName() const override {
    return "Haydn Condition Optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  const HaydnInstrInfo *HII = nullptr;

  // Replace self-comparisons (SLT32/SLTU32 r, rX, rX) with constant 0.
  // Returns true if any changes were made.
  bool eliminateSelfComparisons(MachineFunction &MF);

  // Detect inverse comparison pairs (SLT32 rA, rX, rY + SLT32 rB, rY, rX)
  // and replace the second with XORI32 rB, rA, 1.
  // Returns true if any changes were made.
  bool reuseInverseComparisons(MachineFunction &MF);

  // Fold comparison + branch patterns. When a comparison's result is only
  // used by one BNEZ/BEQZ, replace the pair with a two-register branch.
  // For example: SEQ32 rd, rA, rB + BNEZ rd, tgt → BNE rA, rB, tgt.
  // Returns true if any changes were made.
  bool foldCmpBranch(MachineFunction &MF);

  // Narrow two-register conditional branches whose second operand (or, for
  // BEQ/BNE, either operand) is the soft-zero register R0 to the smaller
  // single-register zero-test form:
  // BEQ rA, R0, tgt → BEQZ rA, tgt (or BEQZ rB if first operand is R0)
  // BNE rA, R0, tgt → BNEZ rA, tgt
  // BGE rA, R0, tgt → BGEZ rA, tgt (signed; rA >= 0)
  // BLT rA, R0, tgt → BLTZ rA, tgt (signed; rA < 0)
  // The unsigned variants (BGEU/BLTU vs R0) are left alone because the ISA
  // only exposes signed BGEZ/BLTZ. Returns true if any changes were made.
  bool narrowBranchToZero(MachineFunction &MF);
};

// Factory function for the pass.
FunctionPass *createHaydnConditionOptimizerPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCONDITIONOPTIMIZER_H
