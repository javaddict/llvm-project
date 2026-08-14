//===-- HaydnHardwareLoops.h - Haydn Hardware Loop Expansion ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface for the Haydn post-RA SCEV-proven hardware-loop expand pass.
// Incomplete retained seats reject fail-closed before mutation (fatal).
// Post-RA physical rediscovery helpers deleted. Default OFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;
class MachineInstr;

bool eraseHardwareLoopSetup(MachineInstr &SetMI);
bool demoteHardwareLoopToSoftware(MachineInstr &SetMI,
                                  const HaydnInstrInfo &TII);

class HaydnHardwareLoops : public MachineFunctionPass {
public:
  static char ID;
  HaydnHardwareLoops();
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "Haydn Hardware Loop Expansion";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

FunctionPass *createHaydnHardwareLoopsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHARDWARELOOPS_H
