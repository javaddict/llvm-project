//===-- HaydnPostRAScratch.h - Post-RA MatInt GPR scratch -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA physical GPR scratch (MatInt, VASTART/VACOPY address math, …).
//
// RISC-V / AIE model: there is no free assembler temporary.
// Expand chooses an available call-clobbered-first GPR via LivePhysRegs;
// R12 is never preferred (and is excluded when PreferNotR12). If every
// candidate is live, pick a preferred reg and spill/restore it around the
// sequence (BranchRelaxationScratchFI or PostRAScratchFI home, else SP bracket).
//
// Always prefer a free reg; spill only when the scavenger finds none.
// Not free AT, not permanent reserved MatInt AT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class DebugLoc;
class HaydnSubtarget;
class TargetInstrInfo;

// Find a post-RA GPR scratch at \p I using LivePhysRegs.
// Priority (available first): R1–R7, R11, R10, R9, R8; R12 only if
// PreferNotR12 is false and nothing else is free. \p Exclude is never
// chosen (operand regs that Fn still needs). If none are available
// returns the first preferred non-excluded candidate and sets \p NeedsSpill.
Register findPostRAScratchGPR(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, bool PreferNotR12,
                              bool &NeedsSpill,
                              ArrayRef<Register> Exclude = {});

// Bracket \p Fn with a post-RA scratch: free reg first, spill only if needed;
// call Fn(Scr), restore if spilled. All BuildMIs in Fn insert before \p I.
// \p Exclude: registers Fn still uses (e.g. va_list base) — never stolen.
void withPostRAScratch(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, const TargetInstrInfo &TII,
                       const HaydnSubtarget &ST, bool PreferNotR12,
                       function_ref<void(Register Scr)> Fn,
                       ArrayRef<Register> Exclude = {});

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H
