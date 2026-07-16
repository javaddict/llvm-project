//===-- HaydnRedundantCopyElim.h - Condition-based copy elim ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a post-register-allocation MachineFunctionPass that
// eliminates redundant COPY/MOVE32 instructions by leveraging dominating
// condition information for the Haydn VLIW DSP target.
//
// Unlike HaydnCopyElim (which handles identity, dead, and R0-writes), this
// pass uses control-flow conditions to prove copies are redundant:
//
// 1. After BEQZ rs,.Ltarget: on the taken path rs is known to be 0.
// 2. After BNEZ rs,.Ltarget: on the fallthrough path rs is 0.
// 3. After BEQ rs1, rs2,.Ltarget: on the taken path rs1 == rs2.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNREDUNDANTCOPYELIM_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNREDUNDANTCOPYELIM_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

// Factory function for the pass.
FunctionPass *createHaydnRedundantCopyElimPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNREDUNDANTCOPYELIM_H
