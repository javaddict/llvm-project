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

// Pass creation functions
FunctionPass *createHaydnPostLegalizerCombiner();
FunctionPass *createHaydnPostSelectOptimizePass();
FunctionPass *createHaydnLoadStoreOptimizerPass();
FunctionPass *createHaydnExpandPseudosPass();
FunctionPass *createHaydnExpandPostIncEarlyPass();
FunctionPass *createHaydnHardwareLoopsPass();
FunctionPass *createHaydnFixupHwLoopsPass();
FunctionPass *createHaydnCFGOptimizerPass();
FunctionPass *createHaydnConditionOptimizerPass();
FunctionPass *createHaydnCopyElimPass();
FunctionPass *createHaydnRedundantCopyElimPass();
FunctionPass *createHaydnPEIPeepholePass();
FunctionPass *createHaydnEnsureTerminatorsPass();
FunctionPass *createHaydnCircularBufferPass();
FunctionPass *createHaydnBitSimplifyPass();

// Pass initialization declarations
void initializeHaydnPostLegalizerCombinerPass(PassRegistry &);
void initializeHaydnPostSelectOptimizePass(PassRegistry &);
void initializeHaydnLoadStoreOptimizerPass(PassRegistry &);
void initializeHaydnExpandPseudosPass(PassRegistry &);
void initializeHaydnExpandPostIncEarlyPass(PassRegistry &);
void initializeHaydnHardwareLoopsPass(PassRegistry &);
void initializeHaydnFixupHwLoopsPass(PassRegistry &);
void initializeHaydnCFGOptimizerPass(PassRegistry &);
void initializeHaydnConditionOptimizerPass(PassRegistry &);
void initializeHaydnCopyElimPass(PassRegistry &);
void initializeHaydnRedundantCopyElimPass(PassRegistry &);
void initializeHaydnPEIPeepholePass(PassRegistry &);
void initializeHaydnEnsureTerminatorsPass(PassRegistry &);
void initializeHaydnCircularBufferPass(PassRegistry &);
void initializeHaydnBitSimplifyPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDN_H
