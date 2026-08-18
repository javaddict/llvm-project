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
class MachineBasicBlock;
class MachineInstr;

/// \p DebugPrefix selects the per-pass LLVM_DEBUG tag; \p ResolveBody may
/// override body resolution (default: CFG-only resolveBodyMBBCore). Only
/// pre-emit Fixup passes its final-layout tail resolver — layout order is
/// never a formation body proof (resolveRoleABody rejects fail-closed).
bool eraseHardwareLoopSetup(
    MachineInstr &SetMI, const char *DebugPrefix = "HaydnHardwareLoops",
    MachineBasicBlock *(*ResolveBody)(MachineInstr &) = nullptr);
bool demoteHardwareLoopToSoftware(
    MachineInstr &SetMI, const HaydnInstrInfo &TII,
    const char *DebugPrefix = "HaydnHardwareLoops",
    MachineBasicBlock *(*ResolveBody)(MachineInstr &) = nullptr);

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
