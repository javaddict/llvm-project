//===- HaydnVerifyBundles.h - Bundle invariant MF pass ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Analysis-only MachineFunction pass that fail-closes on committed-cycle
// invariant violations (haydn::bundle::verifyCommittedBundle). Takes
// AAResultsWrapperPass when present (getAnalysisIfAvailable so limited
// -run-pass stays fail-closed). Proven disjoint store/load packs must
// not fatal.
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
// transients and dest-window seams (verifyMBBDestWindowSeams; not
// verifyCommittedBundle). Freeze identity is bound by the constructor
// argument passed from the addPreEmitPass2 adder (registration), never by
// instance order/count: no cross-instance globals decide Freeze, so
// pipeline census drift and per-thread cloning under parallel codegen
// cannot silently move or disable a freeze wall. The legacy
// default-constructed -run-pass instance is invariant-only;
// -haydn-freeze-verify forces freeze on any instance.
// Inverse records require exact-committed real members.
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
  // Seat identity pinned at construction (D1.13): true only for the
  // instance added at the addPreEmitPass2 seat. Never derived from
  // instance order or count.
  const bool FreezeSeat;

public:
  static char ID;

  // Seat-explicit construction: the addPreEmitPass2 adder passes
  // IsFreezeSeat=true; the three earlier Verify seats pass false.
  explicit HaydnVerifyBundles(bool IsFreezeSeat);

  // Legacy default (RegisterPass / -run-pass): invariant-only.
  HaydnVerifyBundles();

  StringRef getPassName() const override {
    return "Haydn Bundle Invariant Verifier";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

// No default argument on either declaration (here and Haydn.h): every
// seat states its identity at the call site.
FunctionPass *createHaydnVerifyBundlesPass(bool IsFreezeSeat);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNVERIFYBUNDLES_H
