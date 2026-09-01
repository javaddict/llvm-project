//===-- Haydn.h - Top-level interface for Haydn ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Haydn back-end.
//
// Debt markers: new deferred work uses `// RESIDUAL(goal-N):` so
// llvm/utils/haydn/product_coverage_pin.sh can inventory it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDN_H
#define LLVM_LIB_TARGET_HAYDN_HAYDN_H

namespace llvm {

class FunctionPass;
class MachineInstr;
class PassRegistry;
class Target;

// Target registration
Target &getTheHaydnTarget();

// Pass creation functions (product pipeline only).
// Deleted 2026-07 YOLO phase-out: LoadStoreOpt, CircularBuffer, RedundantCopyElim,
// FormUpdateAddr, PostPipeliner, InterBlock Stage-0 — see TargetMachine comments.
FunctionPass *createHaydnPostLegalizerCombiner();
FunctionPass *createHaydnPostSelectOptimizePass();
FunctionPass *createHaydnExpandPseudosPass();
FunctionPass *createHaydnHardwareLoopsPass();
FunctionPass *createHaydnFixupHwLoopsPass();
FunctionPass *createHaydnEnsureTerminatorsPass();
// AIE createAIEFinalizeBundle peer (AIEFinalizeBundle.h / AIE2TargetMachine:244).
FunctionPass *createHaydnFinalizeBundlePass();
// fail-closed committed-bundle verifier (after FinalizeBundle). The
// IsFreezeSeat argument pins seat identity at the call site: only the
// addPreEmitPass2 adder passes true (D1.13; no default argument).
FunctionPass *createHaydnVerifyBundlesPass(bool IsFreezeSeat);
// Exposed-pipeline Data_Latency stall insert (pre-emit; every opt level).
FunctionPass *createHaydnLatencyStallsPass();
// W68.3R bounded late repair loop (S2 -> stalls -> HWLoop validate ->
// BranchRelaxation-last, to a census fixed point; -haydn-sms2 gated).
FunctionPass *createHaydnLateConvergencePass();
// W70.2 function-entry alignment writer (AIE MachineAlignment peer; after
// the closure Finalize+Verify at addPostBBSections). Pads the committed
// extent with legal generated idle-parcel BUNDLEs; the AsmPrinter label no
// longer grows.
FunctionPass *createHaydnMachineAlignmentPass();

/// -haydn-sms2 (product default ON, G004 flip 2026-08-27). S1 keeps the
/// inter-block DDG for S2 Bot replay; the last scheduler invocation clears
/// it before freeze.
bool haydnSMS2Enabled();

// Pass initialization declarations
void initializeHaydnPostLegalizerCombinerPass(PassRegistry &);
void initializeHaydnPostSelectOptimizePass(PassRegistry &);
void initializeHaydnExpandPseudosPass(PassRegistry &);
void initializeHaydnHardwareLoopsPass(PassRegistry &);
void initializeHaydnFixupHwLoopsPass(PassRegistry &);
void initializeHaydnEnsureTerminatorsPass(PassRegistry &);
void initializeHaydnFinalizeBundlePass(PassRegistry &);
void initializeHaydnVerifyBundlesPass(PassRegistry &);
void initializeHaydnLatencyStallsPass(PassRegistry &);
void initializeHaydnLateConvergencePassPass(PassRegistry &);
void initializeHaydnMachineAlignmentPass(PassRegistry &);
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDN_H
