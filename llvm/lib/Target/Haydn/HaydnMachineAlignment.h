//===- HaydnMachineAlignment.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License, v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEMachineAlignment (AIEMachineAlignment.cpp:370-424; seat
// AIE2TargetMachine.cpp:247 after createAIEFinalizeBundle).
//
// W70.2 law (GOALS / contracts/pipeline.md "function-alignment is ruled
// post-Kind-B"): function-entry alignment is written here, on committed
// Format E parcels, not grown by the AsmPrinter label. The pass pads the
// function's committed byte extent to a multiple of the required entry
// alignment by inserting legal generated idle-parcel BUNDLEs at the entry
// block. Prefix budgets (BranchRelaxation, HWLoop Off1/Off2 distance walks
// via getInstSizeInBytes) charge the same pad because the parcels are real
// committed BUNDLEs.
//
// AIE pads AND elongates (variable 2^n bundle formats). Haydn's product
// frontier is a single EncodedBytes parcel for both rows, and golden admits
// no underfill/top-pad, so elongation does not exist: pad-only is the whole
// mechanism. Each idle parcel is the full-slot architectural NOP row
// (E96TwoEntry + AllEntriesReal with a pad-NOP child) — the same committed
// idle object LatencyStalls+Finalize produce, not a new NOP form.
//
// Pipeline: addPostBBSections, immediately after the closure
// Finalize+Verify (i.e. after the first Finalize AND after the S2
// closure), before the addPreEmitPass2 freeze verifier. Runs at every opt
// level including optnone — alignment is layout, not optimization. No
// skipFunction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnMachineAlignment : public MachineFunctionPass {
public:
  static char ID;

  HaydnMachineAlignment();

  StringRef getPassName() const override {
    return "Haydn Machine Alignment";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    // Post-RA committed-bundle state (same tolerance as Finalize).
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnMachineAlignmentPass();

void initializeHaydnMachineAlignmentPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H
