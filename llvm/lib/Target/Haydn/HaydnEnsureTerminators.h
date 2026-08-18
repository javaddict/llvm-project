//===-- HaydnEnsureTerminators.h - Dead-end MBB terminators -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pre-PEI safety net (addPostRegAlloc): every succ-empty MBB must have a
// terminator. Correctness ownership — does not call skipFunction.
//
// IRTranslator leaves empty MBBs for `unreachable` when TrapUnreachable is
// off (no G_TRAP). PEI only inserts epilogues on return blocks, so mid-function
// dead-ends fall through into the next symbol (residual). Emit RET as
// a soft barrier (jalr_w r0, lr, 0) so layout cannot walk past the function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNENSURETERMINATORS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNENSURETERMINATORS_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnEnsureTerminators : public MachineFunctionPass {
public:
  static char ID;

  HaydnEnsureTerminators();

  StringRef getPassName() const override {
    return "Haydn Ensure Dead-End Terminators";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    // addPostRegAlloc (post-RA, pre-PEI) so invented RET can take epilogue.
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnEnsureTerminatorsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNENSURETERMINATORS_H
