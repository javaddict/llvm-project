//===-- HaydnTargetInfo.cpp - Haydn Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Haydn target implementation.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheHaydnTarget() {
  static Target TheHaydnTarget;
  return TheHaydnTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnTargetInfo() {
  RegisterTarget<Triple::haydn, /*HasJIT=*/false> X(
      getTheHaydnTarget(), "haydn", "Haydn 3-issue VLIW DSP", "Haydn");
}
