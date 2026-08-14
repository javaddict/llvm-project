//===- HaydnLatencyStalls.h - Exposed-pipeline RAW stall insertion -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn has an EXPOSED pipeline: there is no interlock. The ISA rule is
//
//   "If instruction A at bundle t has Data_Latency = N, no instruction in
//    bundles t+1 through t+N-1 may read A's destination register."
//
// Loads (and CSRR, and the MAC family) carry Data_Latency = 2, so a load's
// destination must not be read in the very next bundle. Schedulers already
// see architectural load→use latency (no soften); this pass remains the
// pre-emit correctness net and O1+ auditor:
//
//   * Never calls skipFunction: latency is correctness, not quality.
//   * PostMachineScheduler may skip `optnone` (no reorder). FinalizeBundle
//     still forms singleton Format E commits; this pass is the latency net
//     for both plain O0 and optnone (standalone or BUNDLE cycles).
//   * at -O1+ the schedule should already leave empty cycles; insertions are
//     counted as unexpected and still applied if a later mutation reopens a
//     latency window (zero-unexpected is the product goal, not a hard fail)
//
// BundleSim cannot catch this either — it is a purely functional bundle
// simulator with no timing model, so a violating program still produces the
// right answer in simulation and the wrong answer on hardware.
//
// Walk each block in bundle order and insert NOP stall bundles wherever a
// read would land inside a producer's latency window. Runs at every
// optimization level, before BranchRelaxation and HaydnFixupHwLoops so those
// absorb the size growth and recompute hwloop offsets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnLatencyStalls : public MachineFunctionPass {
public:
  static char ID;
  HaydnLatencyStalls();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Exposed-Pipeline Latency Stalls";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createHaydnLatencyStallsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H
