//===-- HaydnTargetInfo.cpp - Haydn Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Haydn target implementation.
// Triple registration is haydn / "Haydn 3-issue VLIW DSP". ELF e_machine
// EM_HAYDN=259 is experimental and collides with official Kalray KVX —
// the object writer stays on that number and distinguishes product
// objects with EF_HAYDN_E96=0x1. Do not invent a replacement here.
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
