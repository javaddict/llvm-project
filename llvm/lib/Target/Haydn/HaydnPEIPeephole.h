//===-- HaydnPEIPeephole.h - Haydn Prologue/Epilogue Peephole -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-RA MachineFunctionPass that optimizes prologue
// and epilogue instruction sequences for the Haydn VLIW DSP target:
//
// Frame pointer copy elimination: When the frame pointer is not used
// (hasFP == false), remove the ADDI32 R14, R13, <size> that was
// emitted to set up FP, since it is dead code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPEIPEEPHOLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPEIPEEPHOLE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

// HaydnPEIPeephole - Post-RA peephole optimizer for prologue/epilogue
// instruction sequences.
class HaydnPEIPeephole : public MachineFunctionPass {
public:
  static char ID;

  HaydnPEIPeephole();

  StringRef getPassName() const override {
    return "Haydn PEI Peephole Optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // Remove dead frame-pointer setup instructions. When hasFP is false
  // any ADDI32 R14, R13, <offset> with FrameSetup flag in the entry block
  // is dead and can be removed.
  bool eliminateDeadFPSetup(MachineFunction &MF);

  // Check whether \p Reg is listed in the callee-saved info for \p MF.
  static bool isCalleeSaveReg(const MachineFunction &MF, unsigned Reg);

  // Try to remove a dead FP-setup instruction. Returns true if the
  // instruction writes R14 from R13 and R14 is not callee-saved.
  // \p OpcodeName is used only for DEBUG output.
  static bool tryRemoveDeadFPSetup(MachineFunction &MF, MachineInstr &MI,
                                   StringRef OpcodeName,
                                   SmallVectorImpl<MachineInstr *> &ToRemove);
};

// Factory function for the pass.
FunctionPass *createHaydnPEIPeepholePass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPEIPEEPHOLE_H
