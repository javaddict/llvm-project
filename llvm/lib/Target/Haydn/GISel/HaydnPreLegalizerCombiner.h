//===-- HaydnPreLegalizerCombiner.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file declares the pre-legalizer combiner pass for the Haydn target.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPRELEGALIZERCOMBINER_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPRELEGALIZERCOMBINER_H

namespace llvm {

class FunctionPass;
class PassRegistry;

// Create a Haydn pre-legalizer combiner pass.
FunctionPass *createHaydnPreLegalizerCombiner();

// Initialize the Haydn pre-legalizer combiner pass.
void initializeHaydnPreLegalizerCombinerPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPRELEGALIZERCOMBINER_H
