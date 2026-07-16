//===-- HaydnPostRAScratch.h - Post-RA MatInt GPR scratch -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA physical GPR scratch for pure MatInt paths (e.g. LOADI64 expand).
//
// RISC-V / AIE model: there is no free assembler temporary for MatInt.
// Expand chooses an available call-clobbered-first GPR via LivePhysRegs;
// R12 is never preferred (and is excluded when PreferNotR12). If every
// candidate is live, pick a preferred reg and spill/restore it around the
// sequence (emergency FI, else temporary SP bracket).
//
// Not free AT, not HaydnATScratch, not permanent reserved MatInt AT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class DebugLoc;
class HaydnSubtarget;
class TargetInstrInfo;

// Find a post-RA GPR scratch at \p I using LivePhysRegs.
// Priority (available first): R1–R7, R11, R10, R9, R8; R12 only if
// PreferNotR12 is false and nothing else is free. If none are available
// returns the first preferred candidate and sets \p NeedsSpill.
Register findPostRAScratchGPR(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, bool PreferNotR12,
                              bool &NeedsSpill);

// Bracket \p Fn with a post-RA MatInt scratch: pick reg, spill if needed
// call Fn(Scr), restore if spilled. All BuildMIs in Fn insert before \p I.
void withPostRAScratch(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, const TargetInstrInfo &TII,
                       const HaydnSubtarget &ST, bool PreferNotR12,
                       function_ref<void(Register Scr)> Fn);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H
