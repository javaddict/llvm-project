//===-- HaydnCopyElim.h - Haydn Redundant Copy Elimination -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-register-allocation MachineFunctionPass that
// eliminates redundant COPY instructions for the Haydn VLIW DSP target:
//
// 1. Identity COPY: COPY rA, rA (source == dest) → remove entirely.
//
// 2. Dead COPY: COPY rA, rB where rA is overwritten before any use
// → remove (the copy's result is never consumed).
//
// 3. COPY to R0: COPY r0, rX → remove (R0 is hardwired to 0
// writes are no-ops).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNCOPYELIM_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNCOPYELIM_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

// HaydnCopyElim - Eliminates redundant COPY instructions after register
// allocation. Runs post-RA so that physical registers are available.
class HaydnCopyElim : public MachineFunctionPass {
public:
  static char ID;

  HaydnCopyElim();

  StringRef getPassName() const override {
    return "Haydn Copy Elimination";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

// Factory function for the pass.
FunctionPass *createHaydnCopyElimPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCOPYELIM_H
