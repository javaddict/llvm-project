//===- HaydnTestsTargetInit.cpp - deterministic MC registration -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register the Haydn target/MC once, at static-init time, for the whole
// HaydnTests binary. The golden-placement oracle
// (haydnFormatEPlacementFeasible) resolves opcode names through a
// TargetRegistry-created MCInstrInfo; without process-wide registration its
// availability would depend on which test file's fixture ran first, making
// solver-refinement behavior order-dependent across the binary.
//
//===----------------------------------------------------------------------===//

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

namespace {
struct HaydnTestsTargetInit {
  HaydnTestsTargetInit() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTarget();
    LLVMInitializeHaydnTargetMC();
  }
};
HaydnTestsTargetInit InitOnce;
} // namespace
