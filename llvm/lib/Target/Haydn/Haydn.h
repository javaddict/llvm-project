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
FunctionPass *createHaydnEnsureTerminatorsPass();
// AIE createAIEFinalizeBundle peer (AIEFinalizeBundle.h / AIE2TargetMachine:244).
FunctionPass *createHaydnFinalizeBundlePass();
// fail-closed committed-bundle verifier (after FinalizeBundle). The
// IsFreezeSeat argument pins seat identity at the call site: only the
// addPreEmitPass2 adder passes true (D1.13; no default argument).
FunctionPass *createHaydnVerifyBundlesPass(bool IsFreezeSeat);
// Postcommit long-form normalizer. Rewrites far short-branch sites to the
// in-block LUI+ADDI32_W(+cond)+JALR_W form. Generic BranchRelaxation is
// seated only pre-S1 in addPreSched2 (RestoreBB while CFG is mutable).
FunctionPass *createHaydnLongBranchNormalizePass();

/// -haydn-zol-pipelining (product default ON; emergency-disable only).
/// Consumed by shouldUseSchedule (SMS ZOL admission) and
/// HaydnSubtarget::enableWindowScheduler. One accessor — never a cross-TU
/// extern cl::opt redeclaration.
bool haydnZOLPipeliningEnabled();

// Pass initialization declarations
void initializeHaydnPostLegalizerCombinerPass(PassRegistry &);
void initializeHaydnPostSelectOptimizePass(PassRegistry &);
void initializeHaydnExpandPseudosPass(PassRegistry &);
void initializeHaydnHardwareLoopsPass(PassRegistry &);
void initializeHaydnEnsureTerminatorsPass(PassRegistry &);
void initializeHaydnFinalizeBundlePass(PassRegistry &);
void initializeHaydnVerifyBundlesPass(PassRegistry &);
void initializeHaydnLongBranchNormalizePass(PassRegistry &);
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDN_H
