//===- HaydnFinalizeBundle.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEFinalizeBundle (AIEFinalizeBundle.h:17-19 / .cpp:22-54).
//
// This pass provides the same functionality as the generic Bundle Finalization
// in MachineInstrBundle, except that it also bundles standalone instructions.
// After PostMachineScheduler multi-MI materialize (B1.1 FormatID stamp), every
// remaining non-meta, non-bundled real MI becomes a singleton BUNDLE with
// durable FormatID::Bundle128Full imm (product encode only).
//
// B4.3: also empty-cycle tryAdd → setDesc on bare multi-slot logicals before
// wrap (AIEMachineScheduler.cpp:1121-1139 peer). Idempotent on already-
// setDesc members / ops without PlacementAlternatives.
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after BR/Fixup/BR (Haydn late firewall; AIE PreEmit empty)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnFinalizeBundle : public MachineFunctionPass {
public:
  static char ID;

  HaydnFinalizeBundle();

  StringRef getPassName() const override {
    return "Haydn Bundle Finalization";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    // Post-RA (after PostMachineScheduler); tolerate either reg form.
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnFinalizeBundlePass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
