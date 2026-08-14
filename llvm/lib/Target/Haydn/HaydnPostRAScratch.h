//===-- HaydnPostRAScratch.h - Post-RA GPR scratch + remat -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unified post-RA physical-GPR temporary facility (no free assembler AT on R12).
//
// Why not llvm::RegScavenger
// --------------------------
// Generic RegScavenger never returns a reserved register
// (RegisterScavenging.cpp isRegUsed / findSurvivorBackwards; isReserved in
// RegisterScavenging.h). Haydn reserves R0 as soft-zero
// (HaydnRegisterInfo::getReservedRegs). That is the blocking invariant:
//
//   R0 is not a scavenged temp. MatInt / ADDI zero-source sequences read
//   R0 as 0 (NeedsZeroBase). Un-reserving R0 so FindUnusedReg can pick it
//   (the obvious port: GPR32 order is R0, R1, …) clobbers the zero
//   mid-sequence. AllowBorrow may temporarily use clean R0 and MUST
//   XOR32-restore; generic RS has no such policy.
//
// Adjacent laws generic RS also does not express:
//   * R13=SP, R14=FP, R15=LR reserved; R12 allocatable (no free AT);
//     priority is call-clobbered first, R12 last.
//   * Spill homes use emitFrameRelativeMemOp (3-tier, never move SP).
//   * rematerializeAddImmForUse glues remat into one Format E parcel
//     (GPR 4R/2W / bundle occupancy). RS::spill inserts store/load
//     without a VLIW occupancy check.
//
// PEI large-FI scratch is a different layer: always createVirtualRegister
// (HaydnRegisterInfo eliminateFrameIndex getScratch). Generic RS is already
// used for PEI emergency slots (processFunctionBeforeFrameFinalized
// addScavengingFrameIndex) and insertIndirectBranch (AllowSpill=false).
// hasNoVRegs is not a post-PEI signal. Keep this file while ExpandPseudos /
// remat-glue / R0 borrow still call it.
//
// Soft-zero R0 contract
// ---------------------
// R0 is reserved as *soft*-zero (not hardwired). Invariant outside a borrow
// window: R0 holds 0. Restore is XOR32 R0,R0,R0.
//
// withPostRAScratch policies:
//   AllowBorrow    — if R0 is still soft-zero at I (last reaching def is a
//                    soft-zero restore / entry), borrow it for Scr and XOR
//                    restore after Fn. If R0 is dirty (someone already wrote
//                    a non-zero temp without restore), fall through to the
//                    scavenger. Never silently clobber a dirty R0.
//   NeedsZeroBase  — Fn uses R0 as MatInt / ADDI …, R0, imm *zero source*.
//                    Scr must not be R0 (would destroy the zero mid-Fn).
//                    Always scavenger (spill if needed). Do not hard-Exclude
//                    R0 at call sites; this policy encodes the rule.
//
// Remat Dest stays a scavenged non-R0 GPR (live value into a glued use).
// R12 is never reserved as free AT — only optional scavenger last resort.
//
// Consumers (MatInt LOADI64, VASTART, hwloop count adjust, …) must call these
// helpers — do not reimplement LivePhysRegs free-reg pick or silent clobber.
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

// --- Soft-zero R0 ----------------------------------------------------------

// How withPostRAScratch treats soft-zero R0.
enum class PostRASoftZero : uint8_t {
  // Prefer borrow R0 when clean; XOR restore. Dirty → scavenger.
  AllowBorrow,
  // Fn needs R0 as zero source; never Scr=R0; always scavenger.
  NeedsZeroBase,
};

// True if the soft-zero invariant holds at \p I (R0 is 0 / safe to read as
// zero, and safe to borrow under AllowBorrow). Walks backward for the last
// R0 def: XOR R0,R0,R0 → clean; any other def → dirty; no def in MBB → clean
// only for the entry block (prologue zeros R0) or when R0 is not live-in
// from a dirty path (conservative: non-entry requires a local restore).
bool isSoftZeroR0Clean(const MachineBasicBlock &MBB,
                       MachineBasicBlock::const_iterator I);

// --- Layer 1: short-lived scratch ------------------------------------------

// Find a post-RA GPR at \p I (LivePhysRegs). Priority: R1–R7, R11…R8; R12
// only if PreferNotR12 is false. \p Exclude is never chosen. R0 is never
// returned (reserved; use withPostRAScratch for soft-zero borrow). If none
// free, returns first preferred candidate and sets NeedsSpill.
Register findPostRAScratchGPR(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, bool PreferNotR12,
                              bool &NeedsSpill,
                              ArrayRef<Register> Exclude = {});

// Bracket \p Fn with a scratch: free first, else spill → Fn → restore.
// All BuildMIs in Fn insert before \p I. \p Exclude: regs Fn still needs.
// \p SoftZero selects R0 borrow vs zero-base policy (see PostRASoftZero).
void withPostRAScratch(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                       const DebugLoc &DL, const TargetInstrInfo &TII,
                       const HaydnSubtarget &ST, bool PreferNotR12,
                       function_ref<void(Register Scr)> Fn,
                       ArrayRef<Register> Exclude = {},
                       PostRASoftZero SoftZero = PostRASoftZero::AllowBorrow);

// Emit a frame-relative LSU access (ST32 store or LD32 load) for a post-RA
// in-frame spill slot. One closed rule, three monotone tiers tied to the
// \p Off magnitude; SP is NEVER moved:
//   tier 1 - short-form element-indexed ST32/LD32 FrameReg, elem
//            (Off/4 fits isInt<6>)
//   tier 2 - ADDI32_W R0, FrameReg, Off; ST32/LD32 R0, 0
//            (Off fits simm20)
//   tier 3 - LOADI32 R0, Off; ADD32 R0, FrameReg, R0; ST32/LD32 R0, 0
//            (any remaining Off)
// Tiers 2/3 borrow soft-zero R0 as a self-contained scratch (must be clean on
// entry) and restore it via XOR32 R0,R0,R0 before return. The same closed
// rule governs every in-frame spill slot - withPostRAScratch's ScratchFI and
// HaydnInstrInfo's DR64PackBaseSpillFI - so neither call site fatals on a
// large frame. \p StoreFlags is applied to the stored register (use 0 for
// loads or non-killed stores).
void emitFrameRelativeMemOp(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            const TargetInstrInfo &TII, Register Reg,
                            Register FrameReg, int64_t Off, bool IsStore,
                            unsigned StoreFlags = 0);

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
