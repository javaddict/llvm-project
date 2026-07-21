//===-- HaydnPostRAScratch.h - Post-RA GPR scratch + remat -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unified post-RA physical-GPR temporary facility (no free assembler AT).
//
// Two layers, one scavenger:
//
// 1) Scratch — short-lived temp for expand sequences that fully own the reg
//    between acquire and release:
//      findPostRAScratchGPR / withPostRAScratch
//    Free via LivePhysRegs; spill/restore only when none free.
//
// 2) Remat — materialize (Src + Imm) into a scavenged GPR and bind it to an
//    existing use operand, with a correct def→use dep chain:
//      rematerializeAddImmForUse
//    Free-reg or spill; never clobbers Src; glues remat def to the use so
//    later schedule/pack cannot redefine Dest before the consumer.
//
// Consumers (MatInt LOADI64 expand, VASTART, hwloop count adjust, …) must
// call these helpers — do not reimplement LivePhysRegs free-reg pick or
// silent clobber of a live GPR.
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
class MachineInstr;
class TargetInstrInfo;

// --- Layer 1: short-lived scratch ------------------------------------------

// Find a post-RA GPR at \p I (LivePhysRegs). Priority: R1–R7, R11…R8; R12
// only if PreferNotR12 is false. \p Exclude is never chosen. If none free,
// returns first preferred candidate and sets NeedsSpill.
Register findPostRAScratchGPR(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, bool PreferNotR12,
                              bool &NeedsSpill,
                              ArrayRef<Register> Exclude = {});

// Bracket \p Fn with a scratch: free first, else spill → Fn → restore.
// All BuildMIs in Fn insert before \p I. \p Exclude: regs Fn still needs.
void withPostRAScratch(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, const TargetInstrInfo &TII,
                       const HaydnSubtarget &ST, bool PreferNotR12,
                       function_ref<void(Register Scr)> Fn,
                       ArrayRef<Register> Exclude = {});

// --- Layer 2: rematerialize (Src + Imm) into a use -------------------------

// Rewrite UseMI's register operand \p UseOpIdx to hold rematerialized
// (Src + Adj), where Src is the operand's current register.
//
// Contract:
//  * Adj == 0 → no-op, returns Src.
//  * Never clobbers Src (Dest is a distinct scavenged GPR).
//  * Free reg via findPostRAScratchGPR; spill/restore around the remat→use
//    window when none free (never silent clobber).
//  * Emits ADDI32_W Dest, Src, Adj immediately before UseMI, rewrites the
//    use to Dest (Kill), and bundles remat def with UseMI so postmisched
//    cannot insert a redef of Dest between them.
//  * Unbundles UseMI first if it was mid-bundle (safe top-level emit).
//
// Returns Dest (or Src when Adj == 0).
Register rematerializeAddImmForUse(MachineInstr &UseMI, unsigned UseOpIdx,
                                   int64_t Adj,
                                   ArrayRef<Register> ExtraExclude = {});

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTRASCRATCH_H
