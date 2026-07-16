//===-- HaydnBitSimplify.h - Haydn Bit Simplification Pass -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-register-allocation MachineFunctionPass that
// simplifies bit manipulation patterns for the Haydn VLIW DSP target:
//
// 1. Identity masks: AND32 R, 0xFFFFFFFF → remove; OR32 R, 0 → remove;
// XOR32 R, 0 → remove. These are no-ops.
//
// 2. Zero / all-ones: AND32 R, 0 → MOVE32 R, R0 (zero result);
// OR32 R, 0xFFFFFFFF → MOVE32 R, -1 (all-ones).
//
// 3. Redundant AND+OR pair: AND32 R, mask followed by OR32 R, same_mask
// where the OR already sets the bits that AND clears → remove the AND.
//
// 4. Complement synthesis: (A & C1) | C2 where (C1 | C2) == 0xFFFFFFFF
// and (C1 & C2) == 0 → the AND is redundant (OR already covers the
// complement bits).
//
// 5. XORI pair folding: intentionally NOT here. Fold (xor (xor x,C1),C2)
// only on SSA (InstCombine / DAGCombiner / GISel combiners) — same as
// AIE/RISCV. Post-RA physreg chain walks are unsafe.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBITSIMPLIFY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBITSIMPLIFY_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

// HaydnBitSimplify - Post-RA peephole pass that simplifies bit manipulation
// patterns (AND/OR/XOR with constants) for the Haydn VLIW DSP.
class HaydnBitSimplify : public MachineFunctionPass {
public:
  static char ID;

  HaydnBitSimplify();

  StringRef getPassName() const override {
    return "Haydn Bit Simplification";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

// Factory function for the pass.
FunctionPass *createHaydnBitSimplifyPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBITSIMPLIFY_H
