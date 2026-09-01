//===- HaydnVerifyBundles.h - Bundle invariant MF pass ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis-only MachineFunction pass that fail-closes on committed-cycle
// invariant violations (haydn::bundle::verifyCommittedBundle).
//
// Pipeline peer: immediately after HaydnFinalizeBundle in addPreSched2 and
// again after PreEmit late re-commit (AIEFinalizeBundle.cpp:40-59 peer
// order; AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed).
// AIE PreEmit is empty so AIE never re-verifies late; Haydn must.
//
// Covers SMS hard-root exact-commits from post-RA leaveMBB as well as free
// scheduled multi-MI packs and late singleton wraps. Does not invent stages.
//
// Never calls skipFunction: committed-cycle verify is product emission
// ownership, not a quality pass. skipFunction-skipped (optnone / bisect)
// functions cannot escape noncanonical cycles — inverse records still run.
// Fail-closes residual cycle-forming, expand-owned, leftover generic
// COPY/subreg children, leftover B/RET/BR_JT/PseudoCALLIndirect
// representation shells (no printer-expand carve-out), mixed
// logical+private inverse children, and — at the addPreEmitPass2 freeze
// seat — residual logical children plus surviving alternate-map / DDG
// transients. Inverse records require exact-committed real members.
// Optnone bare encode escape and mixed committed-BUNDLE + bare encode MIR
// are fatal. Does not mutate MIR.
// report_fatal_error on violation (no silent skip).
// No MCFlags writers. setDesc is owned by Finalize / materialize / hard-root
// commit-inside-group. Never includes HaydnBundle.h / HaydnBundleFormatSolver.h
// and never calls Bundle.canAdd or selectCompletionForMembersAndPads.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNVERIFYBUNDLES_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNVERIFYBUNDLES_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnVerifyBundles : public MachineFunctionPass {
public:
  static char ID;

  HaydnVerifyBundles();
  ~HaydnVerifyBundles() override;

  StringRef getPassName() const override {
    return "Haydn Bundle Invariant Verifier";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

FunctionPass *createHaydnVerifyBundlesPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNVERIFYBUNDLES_H
