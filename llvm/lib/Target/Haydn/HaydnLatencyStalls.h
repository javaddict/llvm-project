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
//   * at -O1+ the post-RA HR dest-read window should already serialize
//     readers; insertions are counted as unexpected and still applied if a
//     later mutation reopens a latency window (zero-unexpected is the
//     product goal, not a hard fail). Product seat is addPreSched2 between
//     PostMachineScheduler and the first Finalize so stall NOPs are
//     committed by that same Finalize+Verify lane.
//
// BundleSim cannot catch this either — it is a purely functional bundle
// simulator with no timing model, so a violating program still produces the
// right answer in simulation and the wrong answer on hardware.
//
// Walk each block in bundle order and insert NOP stall bundles wherever a
// read would land inside a producer's latency window. Runs at every
// optimization level in addPreSched2, after pack and before the first
// Finalize, so BranchRelaxation / Fixup absorb any size growth.
//
// D1.16 loop back-edge wrap law (this pass is the always-on emission net):
// a latch MBB (self-successor) re-executes its own cycle 0 immediately
// after its last body cycle — across the HWLR_END -> HWLR_BEGIN wrap for
// ZOL loops, across the backedge branch for soft loops. The required pad
// is the same max-remaining value the exit seam computes
// (HaydnHazardRecognizer::destWindowWrapPadNeed == destWindowExitLeak);
// only the INSERTION POINT differs. ZOL pads go before the END-anchored
// parcel so they execute INSIDE [BEGIN,END] (parcels after END execute
// only on loop exit); soft pads go before the backedge branch, which also
// covers the exit path — a soft latch inserts once, never twice.
//
// Regeneration pin (late repair loop): each invocation strips stall parcels
// this pass previously inserted, then re-inserts the dest-window need of the
// current inventory. Padding is never accumulated across mutating iterations.
// Inserted parcels are charged with TargetInstrInfo::getInstSizeInBytes — the
// same size interface branch / HWLoop range checks consume — plus the
// generated minimum bundle-address alignment remainder. They carry empty
// MMOs (NOP is not a memory op) and no extra kill/dead / implicit liveness.
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

private:
  /// Same availability-aware pin pre-RA / post-RA / HR consume. Pass
  /// member, not a function-local static (one check per pipeline instance).
  bool ResourceAdmissionPinned = false;
};

FunctionPass *createHaydnLatencyStallsPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H
