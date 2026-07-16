//===-- HaydnPostLegalizerCombiner.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file declares the post-legalizer combiner pass for the Haydn target.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTLEGALIZERCOMBINER_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTLEGALIZERCOMBINER_H

namespace llvm {

class FunctionPass;
class PassRegistry;

// Create a Haydn post-legalizer combiner pass.
FunctionPass *createHaydnPostLegalizerCombiner();

// Initialize the Haydn post-legalizer combiner pass.
void initializeHaydnPostLegalizerCombinerPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTLEGALIZERCOMBINER_H
