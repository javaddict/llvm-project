//===-- HaydnCallingConv.td - Haydn Calling Conventions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Calling conventions for Haydn baremetal ABI.
// R0 is reserved as the soft-zero register (zeroed in prologue, never allocated).
// It is NOT used for argument passing or return values.
// Arguments: GPR R1-R7 for i32/ptr, DR D0-D3 for i64/SIMD.
// Returns: GPR R1-R2 for i32/ptr, DR D0 for i64/SIMD.
// No DR-to-two-GPR fallback when D registers exhausted.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Calling conventions for Haydn baremetal ABI.
// R0 is reserved as the soft-zero register (zeroed in prologue, never allocated).
// It is NOT used for argument passing or return values.
// Arguments: GPR R1-R7 for i32/ptr, DR D0-D3 for i64/SIMD.
// Returns: GPR R1-R2 for i32/ptr, DR D0 for i64/SIMD.
//===----------------------------------------------------------------------===//

// Callee-saved registers: R8-R11, R15(LR), D8-D15.
// R12 is an ordinary caller-saved GPR (AIE model) — not in CSR set.
// MatInt (LOADI64) scavenges a live-checked GPR via HaydnPostRAScratch and
// does not hardcode R12. VASTART may still use R12 + R12ScratchFI for address
// math only.
// R15 (LR) is saved/ restored for non-leaf functions (JAL writes the return
// address into R15). determineCalleeSaves only marks it saved when the
// function contains a call, so leaf functions pay no cost. AR2/AR3 are NOT in
// the CSR list: there is no instruction to spill/restore them, so claiming
// they are callee-saved would be a lie (F32). See CLAUDE.md register map.
def CSR_Haydn : CalleeSavedRegs<(add
  (sequence "R%u", 8, 11),
  R15,
  (sequence "D%u", 8, 15)
)>;

// For getNoPreservedMask
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// Haydn calling convention: independent cursors for GPR and DR
def CC_Haydn : CallingConv<[
  // Promote small integers to i32
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i64, f64, SIMD types use DR registers D0-D3
  CCIfType<[i64], CCAssignToReg<[D0, D1, D2, D3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3]>>,
  CCIfType<[v2i32, v2f32, v4i16], CCAssignToReg<[D0, D1, D2, D3]>>,

  // i32/pointer uses GPR R1-R7 (R0 is reserved as soft-zero)
  CCIfType<[i32], CCAssignToReg<[R1, R2, R3, R4, R5, R6, R7]>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,

  // Stack fallback. Slot size matches natural width; MinAlign is 8 so DR
  // (st64) and i64 stack slots stay 8-byte aligned. i32/f32 use Size=8 (not
  // 4) so a single stack-passed int still advances StackSize by 8 — otherwise
  // ADJCALLSTACKDOWN 4 leaves SP at ≡4 mod 8 and the callee's DR CSR spills
  // MEMORY_FAULT (B1 fir_blms* freestanding dual). Bundle128 is a 16-byte
  // *text* parcel; stack ABI remains 8-byte (DR min). Call-frame SP adjust
  // still rounds up to StackAlign in FrameLowering / CallLowering.
  CCIfType<[i32, f32], CCAssignToStack<8, 8>>,
  CCIfType<[i64, f64, v2i32, v2f32, v4i16], CCAssignToStack<8, 8>>
]>;

// Return value convention: R1-R2 for i32/ptr (R0 is reserved soft-zero).
// D0 for i64/SIMD.
def RetCC_Haydn : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i32/pointer returns in R1, R2 (R0 is reserved as soft-zero)
  CCIfType<[i32], CCAssignToReg<[R1, R2]>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,

  // i64/f64/SIMD returns in D0
  CCIfType<[i64, f64], CCAssignToReg<[D0]>>,
  CCIfType<[v2i32, v2f32, v4i16], CCAssignToReg<[D0]>>,
]>;
