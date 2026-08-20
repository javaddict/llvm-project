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

// All R0–R15 are GPRs. Soft roles only: R0 soft-zero, R13 SP, R14 frame
// base when hasFP, R15 LR — not separate hardware classes.
//
// CSR bank (RISC-V s0-style): R8–R11, R14, R15, D8–D15.
// R14 is always callee-saved. When hasFP it is reserved as the frame base
// (BP folded into FP) and force-saved by PEI; when !hasFP regalloc may use
// it as a normal callee-saved temp. Single mask — no WithFP dual list.
// Call Defs never list R14 (regmask owns clobber/preserve).
def CSR_Haydn : CalleeSavedRegs<(add
  (sequence "R%u", 8, 11),
  R14,
  R15,
  (sequence "D%u", 8, 15)
)>;

// For getNoPreservedMask
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// Haydn calling convention: independent cursors for GPR and DR.
// i128 and wider integers are not CC types (fail-closed before
// splitToValueTypes invents 2×i64). TLI CanLowerReturn is the Hexagon
// CheckReturn(RetCC_Haydn) seat and also refuses R0 / i128 / half / bfloat.
// inreg / nest / swift* / byref have no Haydn seat (fail-closed at
// CallLowering / PEI). Formal byval is the defined stack-indirect path.
// half / bfloat are storage or libcall types, not CC types. No CC_ISR:
// interrupt stays fail-closed until a legal vector story exists
// (AIE1ISelLowering.cpp:964 / RISCV CC_ISR analog).
// Entry=1 emits llvm::CC_Haydn (not static). Peer: PPCCallingConv.td:44
// (let Entry = 1) / TargetCallingConv.td:215-217.
let Entry = 1 in
def CC_Haydn : CallingConv<[
  // Promote small integers to i32
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

 // i64, f64, 64-bit SIMD use DR registers D0-D3.
  // v8i8 is a native 64-bit SIMD shape (DR64) alongside v2i32/v4i16/v2f32.
  CCIfType<[i64], CCAssignToReg<[D0, D1, D2, D3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3]>>,
  CCIfType<[v2i32, v2f32, v4i16, v8i8], CCAssignToReg<[D0, D1, D2, D3]>>,

  // i32/pointer uses GPR R1-R7 (R0 is reserved as soft-zero).
 // Residual 32-bit SLP vectors (v2i16/v4i8) pack into one GPR32.
  CCIfType<[i32], CCAssignToReg<[R1, R2, R3, R4, R5, R6, R7]>>,
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R1, R2, R3, R4, R5, R6, R7]>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,

  // Stack fallback. Slot size matches natural width; MinAlign is 8 so DR
  // (st64) and i64 stack slots stay 8-byte aligned. i32/f32 use Size=8 (not
  // 4) so a single stack-passed int still advances StackSize by 8 — otherwise
  // ADJCALLSTACKDOWN 4 leaves SP at ≡4 mod 8 and the callee's DR CSR spills
  // MEMORY_FAULT (B1 fir_blms* freestanding dual). Text parcel size is
  // orthogonal; stack ABI remains 8-byte (DR min). Call-frame SP adjust
  // still rounds up to StackAlign in FrameLowering / CallLowering.
  CCIfType<[i32, f32, v2i16, v4i8], CCAssignToStack<8, 8>>,
  CCIfType<[i64, f64, v2i32, v2f32, v4i16, v8i8], CCAssignToStack<8, 8>>
]>;

// Return value convention: R1-R2 for i32/ptr (R0 is reserved soft-zero).
// D0 for i64/SIMD.
let Entry = 1 in
def RetCC_Haydn : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i32/pointer and residual 32-bit SLP vectors return in R1, R2.
  CCIfType<[i32], CCAssignToReg<[R1, R2]>>,
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R1, R2]>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,

  // i64/f64/64-bit SIMD returns in D0
  CCIfType<[i64, f64], CCAssignToReg<[D0]>>,
  CCIfType<[v2i32, v2f32, v4i16, v8i8], CCAssignToReg<[D0]>>,
]>;
