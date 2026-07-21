//===-- HaydnCircularBuffer.h - Circular Buffer Detection -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-register-allocation MachineFunctionPass that
// detects circular buffer access patterns in the Haydn VLIW DSP target.
//
// A circular buffer access pattern appears as:
//
// ANDI32 rIdx, rIdx, Imm; idx = idx & (N-1), mask is 2^k - 1
// address computation...
// LD32 rVal, rBase, Offset; load using the masked index
//
// or:
//
// ANDI32 rIdx, rIdx, Imm; mask is 2^k - 1
// address computation...
// ST32 rVal, rBase, Offset; store using the masked index
//
// This pass is ANALYSIS-ONLY: it counts circular-buffer-shaped masks and
// patterns for tuning statistics; it never modifies the function. Native
// CB load/store emission happens exclusively through the LLVM IR intrinsics
// llvm.haydn.ldw.cb.{imm,reg} / llvm.haydn.sdw.cb.{imm,reg} → D_LDW_CB_*
// D_SDW_CB_* (HaydnInstructionSelector.cpp:4324-4434). Auto-detection of
// `(idx+1)&(N-1)` patterns into those intrinsics is a future combiner task
// and out of scope for this pass (see).
//
// The pass runs only when FeatureCircularBuffer is enabled (-mcpu=haydn or
// mattr=+circular-buffer); on the default `generic` CPU there are no CBR
// registers to detect for, so it short-circuits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNCIRCULARBUFFER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNCIRCULARBUFFER_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

// HaydnCircularBuffer - Detects circular buffer access patterns after
// register allocation. Scans for ANDI32/AND32 with power-of-2-minus-1
// masks whose results flow into load/store address computations.
class HaydnCircularBuffer : public MachineFunctionPass {
public:
  static char ID;

  HaydnCircularBuffer();

  StringRef getPassName() const override {
    return "Haydn Circular Buffer Detection";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

// Factory function for the pass.
FunctionPass *createHaydnCircularBufferPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCIRCULARBUFFER_H
