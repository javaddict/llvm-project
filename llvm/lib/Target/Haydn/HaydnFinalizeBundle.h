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
// Same as generic MachineInstrBundle finalization, plus wrapping remaining
// standalone real MIs. Haydn overlay stamps Format E BundleFormatRowID +
// CompletionStateID on newly wrapped singletons and copies the earliest
// member DebugLoc onto any BUNDLE root that has none. Already-bundled
// stamped roots are identity. Construction only: no row resettle, name
// peel, DFS/mode retry, keep-map rewrite, or late setDesc.
//
// Mixed-stream code-bearing inline asm is fail-closed. Mixed MemberId +
// leftover FieldSlot is fail-closed. Leftover multi-member logicals refuse
// RAW/WAW/named/trip/may-alias store-load before exactSolve (AA via
// getAnalysisIfAvailable<AAResultsWrapperPass>; missing AA stays
// fail-closed). Finalize does not sequentialize or peel. Unattributed leftover
// implicit-def $sfr is not WAW when the descriptor does not name SFR
// (PackLegality rule 3). Named-SFR writers and GPR dual-write still refuse.
// Reloc leftover CSR stays FieldSlot.
//
// Never calls skipFunction: target-local no-reorder commit for remaining
// bare MIs (including when PostMachineScheduler quality-skips optnone).
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after BR/Fixup/BR
//   * addPostBBSections closure after the common executable tail
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFINALIZEBUNDLE_H

#include "llvm/CodeGen/MachineFunction.h"
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
