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
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDN_H
#define LLVM_LIB_TARGET_HAYDN_HAYDN_H

namespace llvm {

class FunctionPass;
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
FunctionPass *createHaydnExpandPostIncEarlyPass();
FunctionPass *createHaydnHardwareLoopsPass();
FunctionPass *createHaydnFixupHwLoopsPass();
FunctionPass *createHaydnCFGOptimizerPass();
FunctionPass *createHaydnConditionOptimizerPass();
FunctionPass *createHaydnCopyElimPass();
FunctionPass *createHaydnPEIPeepholePass();
FunctionPass *createHaydnEnsureTerminatorsPass();
FunctionPass *createHaydnBitSimplifyPass();
// AIE createAIEFinalizeBundle peer (AIEFinalizeBundle.h / AIE2TargetMachine:244).
FunctionPass *createHaydnFinalizeBundlePass();
// B1.4 fail-closed committed-bundle verifier (after FinalizeBundle).
FunctionPass *createHaydnVerifyBundlesPass();
FunctionPass *createHaydnLatencyStallsPass();

// Pass initialization declarations
void initializeHaydnPostLegalizerCombinerPass(PassRegistry &);
void initializeHaydnPostSelectOptimizePass(PassRegistry &);
void initializeHaydnExpandPseudosPass(PassRegistry &);
void initializeHaydnExpandPostIncEarlyPass(PassRegistry &);
void initializeHaydnHardwareLoopsPass(PassRegistry &);
void initializeHaydnFixupHwLoopsPass(PassRegistry &);
void initializeHaydnCFGOptimizerPass(PassRegistry &);
void initializeHaydnConditionOptimizerPass(PassRegistry &);
void initializeHaydnCopyElimPass(PassRegistry &);
void initializeHaydnPEIPeepholePass(PassRegistry &);
void initializeHaydnEnsureTerminatorsPass(PassRegistry &);
void initializeHaydnBitSimplifyPass(PassRegistry &);
void initializeHaydnFinalizeBundlePass(PassRegistry &);
void initializeHaydnVerifyBundlesPass(PassRegistry &);
void initializeHaydnLatencyStallsPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDN_H
