//===- IntrinsicsHaydn.td - Defines Haydn intrinsics --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the Haydn-specific intrinsics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Definitions for all Haydn intrinsics.
//
// All Haydn intrinsics start with "llvm.haydn.".
//
// C1.3 / TOPIC model §6 — effect categories (pair with BuiltinsHaydn.td Effect=):
//   Pure     → haydn_{binary,unary,ternary}_intrinsic : IntrNoMem+Speculatable
//   SFR      → haydn_sfr_{compare,cmov}_intrinsic / movesfr* : IntrHasSideEffects
//   State    → setcbr/flar/pldwwua (CSR/CBR/AR) : IntrHasSideEffects[+WriteMem]
//   Mem      → Golden LS WITH/POST/PRE : Intr{Read,Write}Mem+ArgMemOnly+NoCapture
//   StateMem → CB/BREV/UA : Intr{Read,Write}Mem + IntrHasSideEffects
// C2.3: ordinary Golden LS is AA-argmem only; CB/UA MMO is MOVolatile (ISel).
// Clang side: only Pure gets Const; SFR/State/Mem/StateMem are [NoThrow] only.
// Attribute parity is gated by clang/test/CodeGen/Haydn/capi-effect-attr-parity-probe.c
//===----------------------------------------------------------------------===//
let TargetPrefix = "haydn" in {

/// Haydn_Intrinsic - Base class for Haydn intrinsics with Clang builtin mapping.
class Haydn_Intrinsic<string GCCIntSuffix, list<LLVMType> ret_types,
                          list<LLVMType> param_types,
                          list<IntrinsicProperty> properties>
  : ClangBuiltin<!strconcat("__builtin_", GCCIntSuffix)>,
    DefaultAttrsIntrinsic<ret_types, param_types, properties>;

// Convenience classes for common patterns.

/// Pure ALU binary: dst = op(src1, src2). Effect=Pure.
class haydn_binary_intrinsic<string name, LLVMType res_ty, LLVMType op_ty>
    : Haydn_Intrinsic<name, [res_ty], [op_ty, op_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// Pure ALU unary: dst = op(src). Effect=Pure.
class haydn_unary_intrinsic<string name, LLVMType res_ty, LLVMType op_ty>
    : Haydn_Intrinsic<name, [res_ty], [op_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// Pure ALU ternary: dst = op(src1, src2, src3). Effect=Pure.
class haydn_ternary_intrinsic<string name, LLVMType res_ty, LLVMType op_ty>
    : Haydn_Intrinsic<name, [res_ty], [op_ty, op_ty, op_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// SFR-setting compare (Effect=SFR / D215 BUG-1): writes implicit SFR.
/// If the return value is unused (as in `(void)haydn_x2slt32(a,b)` before a
/// MOVT32), DCE would delete a pure IntrNoMem call. IntrHasSideEffects keeps
/// the SFR write so subsequent X2MOVT32/X2MOVF16 see the correct flags.
/// Clang: BuiltinsHaydn.td Effect=SFR → [NoThrow] (no Const).
class haydn_sfr_compare_intrinsic<string name, LLVMType res_ty, LLVMType op_ty>
    : Haydn_Intrinsic<name, [res_ty], [op_ty, op_ty],
                       [IntrHasSideEffects, IntrWillReturn]>;

/// C1.2 ambient SFR cmov (Effect=SFR): x2/x4 movt/movf.
/// Reads implicit SFR from a preceding ambient compare. Ordered and
/// non-speculatable so two compare→movt epochs cannot CSE/reorder at -O2.
/// Mirrors scalar movt64/movf64. GISel: G_INTRINSIC_W_SIDE_EFFECTS.
/// Prefer C1.1 SSA (x2/x4cmplt + x2/x4mux) for multi-epoch code.
/// Clang: Effect=SFR → [NoThrow] (no Const).
class haydn_sfr_cmov_intrinsic<string name, LLVMType res_ty, LLVMType op_ty>
    : Haydn_Intrinsic<name, [res_ty], [op_ty, op_ty],
                       [IntrHasSideEffects, IntrWillReturn]>;

/// D400 (Path B): TRUE 2-output SIMD MAC intrinsic. Golden-authoritative
/// (slot1_mac_instruction_list.json): the X2MUL32 / X4MUL16 / X2CMUL32 family
/// and their accumulate/subtract variants write TWO DR64 destinations
/// (rtd1, rtd2 — DR_Write_Port len==2). Both halves are consumed by NatureDSP
/// wrappers (AE_MUL16X4 -> AE_SAT16X4, AE_MULP32X2 -> real/imag split, etc.).
/// The intrinsic therefore returns [i64, i64] so the selector emits a 2-def
/// MachineInstr matching the _D_RR2 / _D_RRA2 _FLEX format classes (every
/// format-class bits-field binds 1:1 to a dag operand — no orphan fields).
/// Non-accum form: {rtd1, rtd2} = op(src1, src2).
/// Golden X2 lanes: sources are packed dual-32 vectors (v2i32).
class haydn_simd_mac_2dest_intrinsic<string name>
    : Haydn_Intrinsic<name,
                       [llvm_i64_ty, llvm_i64_ty],
                       [llvm_v2i32_ty, llvm_v2i32_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// Golden X4 lanes: sources are packed quad-16 vectors (v4i16).
class haydn_simd_mac_2dest_v4i16_intrinsic<string name>
    : Haydn_Intrinsic<name,
                       [llvm_i64_ty, llvm_i64_ty],
                       [llvm_v4i16_ty, llvm_v4i16_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// D400 (Path B): 2-output SIMD MAC accumulate/subtract intrinsic. Both
/// destination registers are also read as accumulators (tied-def in the .td).
/// {rtd1, rtd2} = {acc1 +/- (src1*src2)_high, acc2 +/- (src1*src2)_low}.
/// Accumulators stay i64 (full products); sources are lane vectors.
class haydn_simd_maca_2dest_intrinsic<string name>
    : Haydn_Intrinsic<name,
                       [llvm_i64_ty, llvm_i64_ty],
                       [llvm_i64_ty, llvm_i64_ty, llvm_v2i32_ty, llvm_v2i32_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

class haydn_simd_maca_2dest_v4i16_intrinsic<string name>
    : Haydn_Intrinsic<name,
                       [llvm_i64_ty, llvm_i64_ty],
                       [llvm_i64_ty, llvm_i64_ty, llvm_v4i16_ty, llvm_v4i16_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

/// Dual-32 data → i64 product/MAC (bag result, vector sources).
class haydn_i64_from_v2i32_binary<string name>
    : Haydn_Intrinsic<name, [llvm_i64_ty], [llvm_v2i32_ty, llvm_v2i32_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
class haydn_i64_from_v2i32_mac<string name>
    : Haydn_Intrinsic<name, [llvm_i64_ty],
                       [llvm_i64_ty, llvm_v2i32_ty, llvm_v2i32_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
/// Quad-16 data → i64 product/MAC.
class haydn_i64_from_v4i16_binary<string name>
    : Haydn_Intrinsic<name, [llvm_i64_ty], [llvm_v4i16_ty, llvm_v4i16_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
class haydn_i64_from_v4i16_mac<string name>
    : Haydn_Intrinsic<name, [llvm_i64_ty],
                       [llvm_i64_ty, llvm_v4i16_ty, llvm_v4i16_ty],
                       [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;



//===----------------------------------------------------------------------===//
// 32x32->64 Multiply (MUL64_LL from GPR32) — fundamental DSP multiply
// llvm.haydn.mul64.ll(a: i32, b: i32) -> i64
// Signed 32x32 multiply producing full 64-bit product. Widens i32 operands
// to DR64 and uses MUL64_LL (low-low half multiply).
//===----------------------------------------------------------------------===//

def int_haydn_mul64_ll : Haydn_Intrinsic<"haydn_mul64_ll",
    [llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// 64-bit Multiply (MUL64) — 16 variants
// Instructions: MUL64_LL, MUL64_LH, MUL64_HL, MUL64_HH,
//               MUL64_LUL, MUL64_ULH, MUL64_UHL, MUL64_UHH,
//               MUL64_LUH, MUL64_UHUH, MUL64_HUL, MUL64_UHUL,
//               MUL64_ULUH, MUL64_ULUL, MUL64_UHUL, MUL64_ULL
// All: (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2)
//===----------------------------------------------------------------------===//

// Signed-signed variants
def int_haydn_mul64_ss_ll : haydn_i64_from_v2i32_binary<"haydn_mul64_ss_ll">;
def int_haydn_mul64_ss_lh : haydn_i64_from_v2i32_binary<"haydn_mul64_ss_lh">;
def int_haydn_mul64_ss_hl : haydn_i64_from_v2i32_binary<"haydn_mul64_ss_hl">;
def int_haydn_mul64_ss_hh : haydn_i64_from_v2i32_binary<"haydn_mul64_ss_hh">;

// Signed-unsigned variants
def int_haydn_mul64_su_lul : haydn_i64_from_v2i32_binary<"haydn_mul64_su_lul">;
def int_haydn_mul64_su_ulh : haydn_i64_from_v2i32_binary<"haydn_mul64_su_ulh">;
def int_haydn_mul64_su_uhl : haydn_i64_from_v2i32_binary<"haydn_mul64_su_uhl">;
def int_haydn_mul64_su_uhh : haydn_i64_from_v2i32_binary<"haydn_mul64_su_uhh">;

// Unsigned-signed variants
def int_haydn_mul64_us_luh : haydn_i64_from_v2i32_binary<"haydn_mul64_us_luh">;
def int_haydn_mul64_us_uhuh : haydn_i64_from_v2i32_binary<"haydn_mul64_us_uhuh">;
def int_haydn_mul64_us_hul : haydn_i64_from_v2i32_binary<"haydn_mul64_us_hul">;
def int_haydn_mul64_us_uhul : haydn_i64_from_v2i32_binary<"haydn_mul64_us_uhul">;

// Unsigned-unsigned variants
def int_haydn_mul64_uu_uluh : haydn_i64_from_v2i32_binary<"haydn_mul64_uu_uluh">;
def int_haydn_mul64_uu_ulul : haydn_i64_from_v2i32_binary<"haydn_mul64_uu_ulul">;
def int_haydn_mul64_uu_ull : haydn_i64_from_v2i32_binary<"haydn_mul64_uu_ull">;
def int_haydn_mul64_uu_ulh : haydn_i64_from_v2i32_binary<"haydn_mul64_uu_ulh">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate (MULA64) — 16 variants
// Instructions: MULA64_LL, ..., MULA64_ULUL
// The 4 _ss_ variants are accumulator-form 3-arg (acc, a, b) per D100:
// silicon reads rtd as the accumulator. Other 12 variants stay 2-arg until
// their wrappers are added.
//===----------------------------------------------------------------------===//

def int_haydn_mula64_ss_ll : haydn_i64_from_v2i32_mac<"haydn_mula64_ss_ll">;
def int_haydn_mula64_ss_lh : haydn_i64_from_v2i32_mac<"haydn_mula64_ss_lh">;
def int_haydn_mula64_ss_hl : haydn_i64_from_v2i32_mac<"haydn_mula64_ss_hl">;
def int_haydn_mula64_ss_hh : haydn_i64_from_v2i32_mac<"haydn_mula64_ss_hh">;

def int_haydn_mula64_su_lul : haydn_i64_from_v2i32_mac<"haydn_mula64_su_lul">;
def int_haydn_mula64_su_ulh : haydn_i64_from_v2i32_mac<"haydn_mula64_su_ulh">;
def int_haydn_mula64_su_uhl : haydn_i64_from_v2i32_mac<"haydn_mula64_su_uhl">;
def int_haydn_mula64_su_uhh : haydn_i64_from_v2i32_mac<"haydn_mula64_su_uhh">;

def int_haydn_mula64_us_luh : haydn_i64_from_v2i32_mac<"haydn_mula64_us_luh">;
def int_haydn_mula64_us_uhuh : haydn_i64_from_v2i32_mac<"haydn_mula64_us_uhuh">;
def int_haydn_mula64_us_hul : haydn_i64_from_v2i32_mac<"haydn_mula64_us_hul">;
def int_haydn_mula64_us_uhul : haydn_i64_from_v2i32_mac<"haydn_mula64_us_uhul">;

def int_haydn_mula64_uu_uluh : haydn_i64_from_v2i32_mac<"haydn_mula64_uu_uluh">;
def int_haydn_mula64_uu_ulul : haydn_i64_from_v2i32_mac<"haydn_mula64_uu_ulul">;
def int_haydn_mula64_uu_ull : haydn_i64_from_v2i32_mac<"haydn_mula64_uu_ull">;
def int_haydn_mula64_uu_ulh : haydn_i64_from_v2i32_mac<"haydn_mula64_uu_ulh">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract (MULS64) — 16 variants
//===----------------------------------------------------------------------===//

def int_haydn_muls64_ss_ll : haydn_i64_from_v2i32_mac<"haydn_muls64_ss_ll">;
def int_haydn_muls64_ss_lh : haydn_i64_from_v2i32_mac<"haydn_muls64_ss_lh">;
def int_haydn_muls64_ss_hl : haydn_i64_from_v2i32_mac<"haydn_muls64_ss_hl">;
def int_haydn_muls64_ss_hh : haydn_i64_from_v2i32_mac<"haydn_muls64_ss_hh">;

def int_haydn_muls64_su_lul : haydn_i64_from_v2i32_mac<"haydn_muls64_su_lul">;
def int_haydn_muls64_su_ulh : haydn_i64_from_v2i32_mac<"haydn_muls64_su_ulh">;
def int_haydn_muls64_su_uhl : haydn_i64_from_v2i32_mac<"haydn_muls64_su_uhl">;
def int_haydn_muls64_su_uhh : haydn_i64_from_v2i32_mac<"haydn_muls64_su_uhh">;

def int_haydn_muls64_us_luh : haydn_i64_from_v2i32_mac<"haydn_muls64_us_luh">;
def int_haydn_muls64_us_uhuh : haydn_i64_from_v2i32_mac<"haydn_muls64_us_uhuh">;
def int_haydn_muls64_us_hul : haydn_i64_from_v2i32_mac<"haydn_muls64_us_hul">;
def int_haydn_muls64_us_uhul : haydn_i64_from_v2i32_mac<"haydn_muls64_us_uhul">;

def int_haydn_muls64_uu_uluh : haydn_i64_from_v2i32_mac<"haydn_muls64_uu_uluh">;
def int_haydn_muls64_uu_ulul : haydn_i64_from_v2i32_mac<"haydn_muls64_uu_ulul">;
def int_haydn_muls64_uu_ull : haydn_i64_from_v2i32_mac<"haydn_muls64_uu_ull">;
def int_haydn_muls64_uu_ulh : haydn_i64_from_v2i32_mac<"haydn_muls64_uu_ulh">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate-Subtract (MULAS64) — 16 variants
//===----------------------------------------------------------------------===//

def int_haydn_mulas64_ss_ll  : haydn_ternary_intrinsic<"haydn_mulas64_ss_ll",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ss_lh  : haydn_ternary_intrinsic<"haydn_mulas64_ss_lh",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ss_hl  : haydn_ternary_intrinsic<"haydn_mulas64_ss_hl",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ss_hh  : haydn_ternary_intrinsic<"haydn_mulas64_ss_hh",  llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulas64_su_lul : haydn_ternary_intrinsic<"haydn_mulas64_su_lul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_su_ulh : haydn_ternary_intrinsic<"haydn_mulas64_su_ulh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_su_uhl : haydn_ternary_intrinsic<"haydn_mulas64_su_uhl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_su_uhh : haydn_ternary_intrinsic<"haydn_mulas64_su_uhh", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulas64_us_luh : haydn_ternary_intrinsic<"haydn_mulas64_us_luh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_us_uhuh : haydn_ternary_intrinsic<"haydn_mulas64_us_uhuh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_us_hul : haydn_ternary_intrinsic<"haydn_mulas64_us_hul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_us_uhul : haydn_ternary_intrinsic<"haydn_mulas64_us_uhul", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulas64_uu_uluh : haydn_ternary_intrinsic<"haydn_mulas64_uu_uluh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uu_ulul : haydn_ternary_intrinsic<"haydn_mulas64_uu_ulul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uu_ull : haydn_ternary_intrinsic<"haydn_mulas64_uu_ull",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uu_ulh : haydn_ternary_intrinsic<"haydn_mulas64_uu_ulh", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract-Subtract (MULSS64) — 16 variants
//===----------------------------------------------------------------------===//

def int_haydn_mulss64_ss_ll  : haydn_ternary_intrinsic<"haydn_mulss64_ss_ll",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ss_lh  : haydn_ternary_intrinsic<"haydn_mulss64_ss_lh",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ss_hl  : haydn_ternary_intrinsic<"haydn_mulss64_ss_hl",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ss_hh  : haydn_ternary_intrinsic<"haydn_mulss64_ss_hh",  llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulss64_su_lul : haydn_ternary_intrinsic<"haydn_mulss64_su_lul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_su_ulh : haydn_ternary_intrinsic<"haydn_mulss64_su_ulh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_su_uhl : haydn_ternary_intrinsic<"haydn_mulss64_su_uhl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_su_uhh : haydn_ternary_intrinsic<"haydn_mulss64_su_uhh", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulss64_us_luh : haydn_ternary_intrinsic<"haydn_mulss64_us_luh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_us_uhuh : haydn_ternary_intrinsic<"haydn_mulss64_us_uhuh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_us_hul : haydn_ternary_intrinsic<"haydn_mulss64_us_hul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_us_uhul : haydn_ternary_intrinsic<"haydn_mulss64_us_uhul", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulss64_uu_uluh : haydn_ternary_intrinsic<"haydn_mulss64_uu_uluh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uu_ulul : haydn_ternary_intrinsic<"haydn_mulss64_uu_ulul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uu_ull : haydn_ternary_intrinsic<"haydn_mulss64_uu_ull",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uu_ulh : haydn_ternary_intrinsic<"haydn_mulss64_uu_ulh", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// Saturating Arithmetic
//===----------------------------------------------------------------------===//

// 32-bit saturating add/sub (GPR32 binary)
def int_haydn_add32s : haydn_binary_intrinsic<"haydn_add32s", llvm_i32_ty, llvm_i32_ty>;

// Golden slot0 scalar-32 shifts: (i32 data, i32 amount) -> i32.
// Imm forms (slli32/srli32/srai32/srai32r) require constant amount (ImmArg).
// Reg forms (sll32/srl32/sra32/sra32r) take variable signed amount.
def int_haydn_slli32 : Haydn_Intrinsic<"haydn_slli32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srli32 : Haydn_Intrinsic<"haydn_srli32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srai32 : Haydn_Intrinsic<"haydn_srai32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srai32r : Haydn_Intrinsic<"haydn_srai32r",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_sll32 : Haydn_Intrinsic<"haydn_sll32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srl32 : Haydn_Intrinsic<"haydn_srl32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_sra32 : Haydn_Intrinsic<"haydn_sra32",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_sra32r : Haydn_Intrinsic<"haydn_sra32r",
    [llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_sub32s : haydn_binary_intrinsic<"haydn_sub32s", llvm_i32_ty, llvm_i32_ty>;

// 32-bit saturating absolute/negate (GPR32 unary)
def int_haydn_abs32s  : haydn_unary_intrinsic<"haydn_abs32s",  llvm_i32_ty, llvm_i32_ty>;
def int_haydn_neg32s  : haydn_unary_intrinsic<"haydn_neg32s",  llvm_i32_ty, llvm_i32_ty>;

// 64-bit saturating add/sub (DR64 binary)
def int_haydn_add64s : haydn_binary_intrinsic<"haydn_add64s", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64s : haydn_binary_intrinsic<"haydn_sub64s", llvm_i64_ty, llvm_i64_ty>;

// 64-bit saturating absolute/negate (DR64 unary)
def int_haydn_abs64s  : haydn_unary_intrinsic<"haydn_abs64s",  llvm_i64_ty, llvm_i64_ty>;
def int_haydn_neg64s  : haydn_unary_intrinsic<"haydn_neg64s",  llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// Fractional Multiply (FMUL)
//===----------------------------------------------------------------------===//

// FMUL32S — fractional 32x32->64 multiply (DR64 binary)
def int_haydn_fmul32s_ll : haydn_i64_from_v2i32_binary<"haydn_fmul32s_ll">;
def int_haydn_fmul32s_lh : haydn_i64_from_v2i32_binary<"haydn_fmul32s_lh">;
def int_haydn_fmul32s_hh : haydn_i64_from_v2i32_binary<"haydn_fmul32s_hh">;

// FMULA32S — fractional multiply-accumulate (DR64 ternary, acc read per D100)
def int_haydn_fmula32s_ll : haydn_i64_from_v2i32_mac<"haydn_fmula32s_ll">;
def int_haydn_fmula32s_lh : haydn_i64_from_v2i32_mac<"haydn_fmula32s_lh">;
def int_haydn_fmula32s_hh : haydn_i64_from_v2i32_mac<"haydn_fmula32s_hh">;

// FMULS32S — fractional multiply-subtract (DR64 ternary, acc read per D100)
def int_haydn_fmuls32s_ll : haydn_i64_from_v2i32_mac<"haydn_fmuls32s_ll">;
def int_haydn_fmuls32s_lh : haydn_i64_from_v2i32_mac<"haydn_fmuls32s_lh">;
def int_haydn_fmuls32s_hh : haydn_i64_from_v2i32_mac<"haydn_fmuls32s_hh">;

//===----------------------------------------------------------------------===//
// FF2 Fractional Multiply with Symmetric Rounding (F2MUL32RS)
// These are the critical DSP kernel intrinsics — 14/17 priority kernels
// need FF2 fractional MAC with rounding + saturation.
// F2MUL = "Format-2 Multiply" (dual 32-bit packed in DR64).
//   Suffix 'RS' = with rounding + saturation
//   Suffix 'R'  = with rounding, no saturation
// Lane suffixes: _ll (low*low), _lh (low*high), _hh (high*high)
//===----------------------------------------------------------------------===//

// Saturating variants (with rounding)
def int_haydn_ff2mul32rs_ll : haydn_i64_from_v2i32_binary<"haydn_ff2mul32rs_ll">;
def int_haydn_ff2mul32rs_lh : haydn_i64_from_v2i32_binary<"haydn_ff2mul32rs_lh">;
def int_haydn_ff2mul32rs_hh : haydn_i64_from_v2i32_binary<"haydn_ff2mul32rs_hh">;

def int_haydn_ff2mula32rs_ll : haydn_i64_from_v2i32_mac<"haydn_ff2mula32rs_ll">;
def int_haydn_ff2mula32rs_lh : haydn_i64_from_v2i32_mac<"haydn_ff2mula32rs_lh">;
def int_haydn_ff2mula32rs_hh : haydn_i64_from_v2i32_mac<"haydn_ff2mula32rs_hh">;

def int_haydn_ff2muls32rs_ll : haydn_i64_from_v2i32_mac<"haydn_ff2muls32rs_ll">;
def int_haydn_ff2muls32rs_lh : haydn_i64_from_v2i32_mac<"haydn_ff2muls32rs_lh">;
def int_haydn_ff2muls32rs_hh : haydn_i64_from_v2i32_mac<"haydn_ff2muls32rs_hh">;

// Non-saturating variants (with rounding, no saturation)
def int_haydn_ff2mul32r_ll : haydn_i64_from_v2i32_binary<"haydn_ff2mul32r_ll">;
def int_haydn_ff2mul32r_lh : haydn_i64_from_v2i32_binary<"haydn_ff2mul32r_lh">;
def int_haydn_ff2mul32r_hh : haydn_i64_from_v2i32_binary<"haydn_ff2mul32r_hh">;

def int_haydn_ff2mula32r_ll : haydn_i64_from_v2i32_mac<"haydn_ff2mula32r_ll">;
def int_haydn_ff2mula32r_lh : haydn_i64_from_v2i32_mac<"haydn_ff2mula32r_lh">;
def int_haydn_ff2mula32r_hh : haydn_i64_from_v2i32_mac<"haydn_ff2mula32r_hh">;

// FF2MULS32R — fractional subtract that READS rtd as accumulator (DB
// slot-1 DR_Read_Port = [rsd1, rsd2, rtd]); ternary (acc, a, b) per D187,
// reconciling the arity with the tied-def FmtALU64Acc target opcode.
def int_haydn_ff2muls32r_ll : haydn_i64_from_v2i32_mac<"haydn_ff2muls32r_ll">;
def int_haydn_ff2muls32r_lh : haydn_i64_from_v2i32_mac<"haydn_ff2muls32r_lh">;
def int_haydn_ff2muls32r_hh : haydn_i64_from_v2i32_mac<"haydn_ff2muls32r_hh">;

//===----------------------------------------------------------------------===//
// Cross-Width Fractional MAC: 32x16 -> 32 with round + saturate (X2RAS).
//
// llvm.haydn.mulfp32x16x2ras.{low,high}(acc, a32, b16) -> i64
//
//   acc : DR64 holding two Q1.31 accumulator lanes ([63:32]=hi, [31:0]=lo).
//   a32 : DR64 holding two Q1.31 multiplier lanes.
//   b16 : DR64 holding four Q1.15 lanes (lanes 0..3 from low to high half).
//
//   low  : acc += sat_q31( a32.lo * b16.lane0 ) + sat_q31( a32.hi * b16.lane1 )
//   high : acc += sat_q31( a32.lo * b16.lane2 ) + sat_q31( a32.hi * b16.lane3 )
//
// Per the spec audit (ISA-11), Haydn has NO native 32x16 cross-width
// fractional MAC. The selector decomposes this intrinsic into existing
// scalar primitives: lane unpack, sign-extend of int16, MULQ31 (fractional
// 32x32 saturating multiply), and ADD32 accumulate. See D92 for the
// decomposition recipe and ISA-11 for the hardware gap.
//===----------------------------------------------------------------------===//
def int_haydn_mulfp32x16x2ras_low  : haydn_ternary_intrinsic<"haydn_mulfp32x16x2ras_low",
                                                              llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulfp32x16x2ras_high : haydn_ternary_intrinsic<"haydn_mulfp32x16x2ras_high",
                                                              llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// 32-bit Multiply (high half)
//===----------------------------------------------------------------------===//

// MULL — 32x32->32 low multiply (GPR32 binary)
def int_haydn_mull : haydn_binary_intrinsic<"haydn_mull", llvm_i32_ty, llvm_i32_ty>;

// MULSSH — 32x32->32 signed-signed high multiply (GPR32 binary)
def int_haydn_mulssh : haydn_binary_intrinsic<"haydn_mulssh", llvm_i32_ty, llvm_i32_ty>;

// MULSUH — 32x32->32 signed-unsigned high multiply (GPR32 binary)
def int_haydn_mulsuh : haydn_binary_intrinsic<"haydn_mulsuh", llvm_i32_ty, llvm_i32_ty>;

// MULUUH — 32x32->32 unsigned-unsigned high multiply (GPR32 binary)
def int_haydn_muluuh : haydn_binary_intrinsic<"haydn_muluuh", llvm_i32_ty, llvm_i32_ty>;

//===----------------------------------------------------------------------===//
// Q-format Multiply/Accumulate (MULQ31, MACQ31, MULQ63)
//===----------------------------------------------------------------------===//

// MULQ31 — fractional 32x32->32 saturating multiply (GPR32 ternary)
def int_haydn_mulq31 : haydn_ternary_intrinsic<"haydn_mulq31", llvm_i32_ty, llvm_i32_ty>;

// MACQ31 — fractional 32x32->32 saturating MAC (GPR32 ternary)
def int_haydn_macq31 : haydn_ternary_intrinsic<"haydn_macq31", llvm_i32_ty, llvm_i32_ty>;

// MULQ63 — fractional 64x64->64 saturating multiply (DR64 ternary)
def int_haydn_mulq63 : haydn_ternary_intrinsic<"haydn_mulq63", llvm_i64_ty, llvm_i64_ty>;

// MAC32 — 32x32->32 multiply-accumulate (GPR32 ternary)
def int_haydn_mac32 : haydn_ternary_intrinsic<"haydn_mac32", llvm_i32_ty, llvm_i32_ty>;

//===----------------------------------------------------------------------===//
// SIMD X2 Operations (dual 32-bit)
//===----------------------------------------------------------------------===//

def int_haydn_x2add32s  : haydn_binary_intrinsic<"haydn_x2add32s",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sub32s  : haydn_binary_intrinsic<"haydn_x2sub32s",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2addsub32s  : haydn_binary_intrinsic<"haydn_x2addsub32s",  llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// SIMD X4 Operations (quad 16-bit)
//===----------------------------------------------------------------------===//

def int_haydn_x4add16s  : haydn_binary_intrinsic<"haydn_x4add16s",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4sub16s  : haydn_binary_intrinsic<"haydn_x4sub16s",  llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// SIMD MAC (X2MULA32, X2MULS32, X4MULA16, X4MULS16, X4MULA16S, X4MULS16S)
// D400 (Path B): TRUE 2-output accumulate ops (golden DR_Write_Port len==2).
// Both DR64 destinations are also accumulator inputs (tied-def). Returns
// [i64, i64]; args (acc1, acc2, src1, src2) so the selector ties
// acc1->rtd1, acc2->rtd2.
//===----------------------------------------------------------------------===//

def int_haydn_x2mula32  : haydn_simd_maca_2dest_intrinsic<"haydn_x2mula32">;
def int_haydn_x2muls32  : haydn_simd_maca_2dest_intrinsic<"haydn_x2muls32">;
def int_haydn_x4mula16  : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4mula16">;
def int_haydn_x4muls16  : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4muls16">;
// Four-lane sat 16x16->32 into two dest DRs (not the two-lane FMULAA16 body).
def int_haydn_x4mula16s : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4mula16s">;
def int_haydn_x4muls16s : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4muls16s">;

//===----------------------------------------------------------------------===//
// Transcendental Functions
//===----------------------------------------------------------------------===//

def int_haydn_log2  : haydn_unary_intrinsic<"haydn_log2",  llvm_i32_ty, llvm_i32_ty>;
def int_haydn_exp2  : haydn_unary_intrinsic<"haydn_exp2",  llvm_i32_ty, llvm_i32_ty>;
def int_haydn_recip : haydn_unary_intrinsic<"haydn_recip", llvm_i32_ty, llvm_i32_ty>;
def int_haydn_sqrt  : haydn_unary_intrinsic<"haydn_sqrt",  llvm_i32_ty, llvm_i32_ty>;

//===----------------------------------------------------------------------===//
// Normalization (NSA)
//===----------------------------------------------------------------------===//

def int_haydn_nsa32   : haydn_unary_intrinsic<"haydn_nsa32",   llvm_i32_ty, llvm_i32_ty>;
def int_haydn_nsau32  : haydn_unary_intrinsic<"haydn_nsau32",  llvm_i32_ty, llvm_i32_ty>;
// R_GD: GPR32 dest + DR64 source (match FormatsALU64 members / POPCOUNT64).
def int_haydn_nsa64   : haydn_unary_intrinsic<"haydn_nsa64",   llvm_i32_ty, llvm_i64_ty>;
def int_haydn_nsa16_l : haydn_unary_intrinsic<"haydn_nsa16_l", llvm_i32_ty, llvm_i64_ty>;
def int_haydn_nsa32_l : haydn_unary_intrinsic<"haydn_nsa32_l", llvm_i32_ty, llvm_i64_ty>;
def int_haydn_nsaz64  : haydn_unary_intrinsic<"haydn_nsaz64",  llvm_i32_ty, llvm_i64_ty>;
def int_haydn_nsaz16_l : haydn_unary_intrinsic<"haydn_nsaz16_l", llvm_i32_ty, llvm_i64_ty>;
def int_haydn_nsaz32_l : haydn_unary_intrinsic<"haydn_nsaz32_l", llvm_i32_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// Non-saturating absolute/negate (64-bit)
//===----------------------------------------------------------------------===//

def int_haydn_abs64 : haydn_unary_intrinsic<"haydn_abs64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_neg64 : haydn_unary_intrinsic<"haydn_neg64", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// MULSA32/MULSS32 — Dual 32-bit multiply-accumulate/subtract
// These take two DR64 registers, each containing packed 32-bit values.
// rsd1 = {hi1, lo1}, rsd2 = {hi2, lo2}
// HHLL: rtd += hi1*hi2 + lo1*lo2   (multiply-add, same-lane)
// HLLH: rtd += hi1*lo2 + lo1*hi2   (multiply-add, cross-lane)
// Instructions: MULSA32_HHLL, MULSA32_HLLH, MULSS32_HHLL, MULSS32_HLLH
//===----------------------------------------------------------------------===//

def int_haydn_mulsa32_hhll : haydn_binary_intrinsic<"haydn_mulsa32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulsa32_hllh : haydn_binary_intrinsic<"haydn_mulsa32_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss32_hhll : haydn_binary_intrinsic<"haydn_mulss32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss32_hllh : haydn_binary_intrinsic<"haydn_mulss32_hllh", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// NOTE: satsr64 / packsr32 / packsr32x2_* are NOT golden encodings.
// Composites live in haydn_dsp.h (SRA64/SRA64R/X2SRA32R+X2SEL32_*).
// Do not re-add as LLVM IR intrinsics.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// AE_MAXABS32S: per-lane saturating abs-max of two DR64 SIMD values.
//
//   result.L = SAT32(MAX(SAT_ABS(a.L), SAT_ABS(b.L)))
//   result.H = SAT32(MAX(SAT_ABS(a.H), SAT_ABS(b.H)))
//
// Used 48+ times in NatureDSP FFT kernels (fft_cplx_stages_S2_32x32_hifi3)
// for magnitude tracking, and in IIR/math kernels for overflow detection.
//
// Decomposition (no native Haydn encoding for fused abs-max):
//   tmp_a = X2ABS32S a
//   tmp_b = X2ABS32S b
//   result = X2MAX32 tmp_a, tmp_b
//===----------------------------------------------------------------------===//
def int_haydn_maxabs32s : haydn_binary_intrinsic<"haydn_maxabs32s", llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 2: LC3 BASOP 16x16 fractional multiply
//===----------------------------------------------------------------------===//

// FMUL16_HS00 — Q15*Q15→Q31 fractional multiply (binary DR64)
def int_haydn_fmul16_hs00 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs00">;

// FMULAA16_HS_11_00 — Q15*Q15 MAC into Q31, accumulate-add both lanes (binary DR64)
// $rd is the implicit accumulator (read and written).
def int_haydn_fmulaa16_hs_11_00 : haydn_i64_from_v4i16_mac<"haydn_fmulaa16_hs_11_00">;

// FMULSS16_HS_11_00 — Q15*Q15 MSU into Q31, subtract both lanes (binary DR64)
// $rd is the implicit accumulator (read and written).
def int_haydn_fmulss16_hs_11_00 : haydn_i64_from_v4i16_mac<"haydn_fmulss16_hs_11_00">;

//===----------------------------------------------------------------------===//
// Wave 2: IIR biquad fused dual MAC
//===----------------------------------------------------------------------===//

// F2MULAA32RS_HLLH — Fused add-add dual MAC, cross-lane, sat+round (binary DR64)
def int_haydn_f2mulaa32rs_hllh : haydn_i64_from_v2i32_mac<"haydn_f2mulaa32rs_hllh">;

// F2MULAA32RS_HHLL — Fused add-add dual MAC, same-lane, sat+round (binary DR64)
def int_haydn_f2mulaa32rs_hhll : haydn_i64_from_v2i32_mac<"haydn_f2mulaa32rs_hhll">;

// F2MULSS32RS_HLLH — Fused sub-sub dual MAC, cross-lane, sat+round (binary DR64)
def int_haydn_f2mulss32rs_hllh : haydn_i64_from_v2i32_mac<"haydn_f2mulss32rs_hllh">;

// F2MULSS32RS_HHLL — Fused sub-sub dual MAC, same-lane, sat+round (binary DR64)
def int_haydn_f2mulss32rs_hhll : haydn_i64_from_v2i32_mac<"haydn_f2mulss32rs_hhll">;

//===----------------------------------------------------------------------===//
// Wave 2: Shift with rounding
//===----------------------------------------------------------------------===//

// SRAI64R — 64-bit shift-right with rounding, immediate shift amount
// (i64 accum, i32 shift_imm) -> i64
// Note: The hardware instruction uses simm16 for the shift amount.
// Admitted *r law: shift>0 adds 1<<(shift-1) then ASR; shift 0 is identity.
def int_haydn_srai64r : Haydn_Intrinsic<"haydn_srai64r",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// DB imm 64-bit shifts (RI6). Reg twins: sra64 / sll64 / srl64.
def int_haydn_srai64 : Haydn_Intrinsic<"haydn_srai64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_slli64 : Haydn_Intrinsic<"haydn_slli64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srli64 : Haydn_Intrinsic<"haydn_srli64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 2: Complex multiply
//===----------------------------------------------------------------------===//

// X4FCMUL16RS — Quad 16-bit complex multiply, sat+round (binary DR64)
def int_haydn_x4fcmul16rs : haydn_binary_intrinsic<"haydn_x4fcmul16rs", llvm_v4i16_ty, llvm_v4i16_ty>;

// X4FCMULA16RS — Quad 16-bit complex MAC, sat+round. Ternary: reads rtd as
// accumulator (DB behavior: rtd[lane] = SAT(rtd[lane] + fcmul(a,b))). Was
// misdeclared binary (D211/L185) — the implicit accumulator was dropped,
// producing silent miscompute. Now ternary (acc, a, b) per DB.
def int_haydn_x4fcmula16rs : haydn_ternary_intrinsic<"haydn_x4fcmula16rs", llvm_v4i16_ty, llvm_v4i16_ty>;

// X4FCMUL16RSS — Quad 16-bit complex multiply, sat+round variant (binary DR64)
def int_haydn_x4fcmul16rss : haydn_binary_intrinsic<"haydn_x4fcmul16rss", llvm_v4i16_ty, llvm_v4i16_ty>;

// X4FCMULA16RSS — Quad 16-bit complex MAC, sat+round variant. Ternary
// accumulator (same DB-behavior shape as _RS above — reads rtd). D211/L185.
def int_haydn_x4fcmula16rss : haydn_ternary_intrinsic<"haydn_x4fcmula16rss", llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 3: 32-bit complex multiply (D400 Path B: 2-dest DR64)
// X2CMUL32/X2CMUL32S are TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2],
// DR_Read_Port=[rsd1,rsd2] — NO accumulator read). Real part -> rtd1,
// imaginary part -> rtd2. Returns [i64, i64]; args (src1, src2).
//===----------------------------------------------------------------------===//

// X2CMUL32 — Dual 32-bit complex multiply (2-dest DR64)
def int_haydn_x2cmul32 : haydn_simd_mac_2dest_intrinsic<"haydn_x2cmul32">;

// X2CMUL32S — Dual 32-bit complex multiply with saturation (2-dest DR64)
def int_haydn_x2cmul32s : haydn_simd_mac_2dest_intrinsic<"haydn_x2cmul32s">;

//===----------------------------------------------------------------------===//
// Wave 3: SFR Flag Register Predication
// Compare→SFR→conditional-move pattern for SIMD predication.
//
// Compare ops: compare two DR64 values per-element and set SFR flags.
//   The intrinsic returns the first operand (passthrough) so that the
//   compare→move pattern can be chained in a single expression.
//   Side effect: writes to SFR (modeled as an implicit def).
//
// Move ops: conditionally move source lanes based on SFR flags.
//   MOVF = move if SFR false (SFR == 0 → select src2, else keep src1)
//   MOVT = move if SFR true  (SFR == 1 → select src2, else keep src1)
//   Binary intrinsic: dst = mov(src1, src2) where src1 is the fallthrough
//   value and src2 is the conditional source. The SFR flags must be set
//   by a preceding compare intrinsic.
//===----------------------------------------------------------------------===//

// X2SEQ32 — Dual 32-bit signed equal compare → SFR
def int_haydn_x2seq32 : haydn_sfr_compare_intrinsic<"haydn_x2seq32", llvm_v2i32_ty, llvm_v2i32_ty>;
// X2SLT32 — Dual 32-bit signed less-than compare → SFR
def int_haydn_x2slt32 : haydn_sfr_compare_intrinsic<"haydn_x2slt32", llvm_v2i32_ty, llvm_v2i32_ty>;
// X2SLE32 — Dual 32-bit signed less-or-equal compare → SFR
def int_haydn_x2sle32 : haydn_sfr_compare_intrinsic<"haydn_x2sle32", llvm_v2i32_ty, llvm_v2i32_ty>;
// X2MOVF32 — Dual 32-bit conditional move if SFR false (ambient low-level)
//   dst[i] = (SFR[i] == 0) ? src2[i] : src1[i]
//   Takes two DR64 operands: src1 (fallthrough/false value), src2 (true value).
//   The SFR flags must be set by a preceding X2SEQ32/X2SLT32/X2SLE32 compare.
//   C1.2: IntrHasSideEffects (not pure binary) so multi-epoch ambient cmovs
//   cannot CSE/reorder. Prefer x2mux32 SSA for multi-epoch public code.
def int_haydn_x2movf32 : haydn_sfr_cmov_intrinsic<"haydn_x2movf32", llvm_v2i32_ty, llvm_v2i32_ty>;
// X2MOVT32 — Dual 32-bit conditional move if SFR true (ambient low-level)
//   dst[i] = (SFR[i] == 1) ? src2[i] : src1[i]
def int_haydn_x2movt32 : haydn_sfr_cmov_intrinsic<"haydn_x2movt32", llvm_v2i32_ty, llvm_v2i32_ty>;

// X4SEQ16 — Quad 16-bit signed equal compare → SFR
def int_haydn_x4seq16 : haydn_sfr_compare_intrinsic<"haydn_x4seq16", llvm_v4i16_ty, llvm_v4i16_ty>;
// X4SLT16 — Quad 16-bit signed less-than compare → SFR
def int_haydn_x4slt16 : haydn_sfr_compare_intrinsic<"haydn_x4slt16", llvm_v4i16_ty, llvm_v4i16_ty>;
// X4SLE16 — Quad 16-bit signed less-or-equal compare → SFR
def int_haydn_x4sle16 : haydn_sfr_compare_intrinsic<"haydn_x4sle16", llvm_v4i16_ty, llvm_v4i16_ty>;
// X4MOVF16 — Quad 16-bit conditional move if SFR false (ambient low-level)
//   dst[i] = (SFR[i] == 0) ? src2[i] : src1[i]
def int_haydn_x4movf16 : haydn_sfr_cmov_intrinsic<"haydn_x4movf16", llvm_v4i16_ty, llvm_v4i16_ty>;
// X4MOVT16 — Quad 16-bit conditional move if SFR true (ambient low-level)
//   dst[i] = (SFR[i] == 1) ? src2[i] : src1[i]
def int_haydn_x4movt16 : haydn_sfr_cmov_intrinsic<"haydn_x4movt16", llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// C1.1 / G-PRED-SSA: pure SSA predicate value + fused compare-select
//
// Ambient x2slt32/x2movt32 couple epochs through SFR (C1.2: both ordered /
// IntrHasSideEffects). Prefer these pure SSA APIs for multi-epoch code:
// i32 pred values (Hexagon C2_cmplt / C2_mux peer). IntrNoMem+Speculatable is
// correct here: SFR is only a temporary inside ISel expansion
// (X2/X4SLT + MOVT + MOVESFR2GPR/MOVEGPR2SFR).
//
//   x2cmplt32(a,b) -> i32 pred2     expand: X2SLT32; MOVESFR2GPR
//   x4cmplt16(a,b) -> i32 pred4     expand: X4SLT16; MOVESFR2GPR
//   x2mux32(p,t,f) -> v2i32         expand: MOVEGPR2SFR(p); X2MOVT32(f,t)
//   x4mux16(p,t,f) -> v4i16         expand: MOVEGPR2SFR(p); X4MOVT16(f,t)
//   x2cmpsel32(a,b,t,f) -> v2i32    expand: X2SLT32; X2MOVT32(f,t)
//   x4cmpsel16(a,b,t,f) -> v4i16    expand: X4SLT16; X4MOVT16(f,t)
// Mux/cmpsel arg order matches Hexagon C2_mux(Pu, Rs_true, Rt_false).
//===----------------------------------------------------------------------===//

def int_haydn_x2cmplt32 : Haydn_Intrinsic<"haydn_x2cmplt32",
    [llvm_i32_ty], [llvm_v2i32_ty, llvm_v2i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x4cmplt16 : Haydn_Intrinsic<"haydn_x4cmplt16",
    [llvm_i32_ty], [llvm_v4i16_ty, llvm_v4i16_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x2mux32 : Haydn_Intrinsic<"haydn_x2mux32",
    [llvm_v2i32_ty], [llvm_i32_ty, llvm_v2i32_ty, llvm_v2i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x4mux16 : Haydn_Intrinsic<"haydn_x4mux16",
    [llvm_v4i16_ty], [llvm_i32_ty, llvm_v4i16_ty, llvm_v4i16_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x2cmpsel32 : Haydn_Intrinsic<"haydn_x2cmpsel32",
    [llvm_v2i32_ty],
    [llvm_v2i32_ty, llvm_v2i32_ty, llvm_v2i32_ty, llvm_v2i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x4cmpsel16 : Haydn_Intrinsic<"haydn_x4cmpsel16",
    [llvm_v4i16_ty],
    [llvm_v4i16_ty, llvm_v4i16_ty, llvm_v4i16_ty, llvm_v4i16_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4: X2 SIMD fractional multiply (binary DR64)
// X2FMUL32RS/RSS/TS — dual 32-bit fractional multiply with rounding
//   RS  = with rounding + saturation
//   RSS = with rounding + saturation (alternate encoding)
//   TS  = with rounding + truncation
//===----------------------------------------------------------------------===//

def int_haydn_x2fmul32rs  : haydn_binary_intrinsic<"haydn_x2fmul32rs",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmul32rss : haydn_binary_intrinsic<"haydn_x2fmul32rss", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmul32ts  : haydn_binary_intrinsic<"haydn_x2fmul32ts",  llvm_v2i32_ty, llvm_v2i32_ty>;

def int_haydn_x2fmula32rs  : haydn_ternary_intrinsic<"haydn_x2fmula32rs",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmula32rss : haydn_ternary_intrinsic<"haydn_x2fmula32rss", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmula32ts  : haydn_ternary_intrinsic<"haydn_x2fmula32ts",  llvm_v2i32_ty, llvm_v2i32_ty>;

def int_haydn_x2fmuls32rs  : haydn_ternary_intrinsic<"haydn_x2fmuls32rs",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmuls32rss : haydn_ternary_intrinsic<"haydn_x2fmuls32rss", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fmuls32ts  : haydn_ternary_intrinsic<"haydn_x2fmuls32ts",  llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 4: X4 SIMD fractional multiply (binary DR64)
// X4FMUL16RS/RSS/TS — quad 16-bit fractional multiply with rounding
//===----------------------------------------------------------------------===//

def int_haydn_x4fmul16rs  : haydn_binary_intrinsic<"haydn_x4fmul16rs",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4fmul16rss : haydn_binary_intrinsic<"haydn_x4fmul16rss", llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4fmul16ts  : haydn_binary_intrinsic<"haydn_x4fmul16ts",  llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 4: X2/X4 shift with rounding (binary DR64)
// FRSST = fractional shift-right with saturation + truncation
// FRST  = fractional shift-right with saturation
//===----------------------------------------------------------------------===//

def int_haydn_x2frsst32 : haydn_binary_intrinsic<"haydn_x2frsst32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2frst32  : haydn_binary_intrinsic<"haydn_x2frst32",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x4frsst16 : haydn_binary_intrinsic<"haydn_x4frsst16", llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4frst16  : haydn_binary_intrinsic<"haydn_x4frst16",  llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 4: X2/X4 immediate shift with rounding
// (i64 val, i32 shift_imm) -> i64
//===----------------------------------------------------------------------===//

def int_haydn_x2srai32r : Haydn_Intrinsic<"haydn_x2srai32r",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x4srai16r : Haydn_Intrinsic<"haydn_x4srai16r",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_HS remaining variants (binary DR64)
// Q15*Q15→Q31 fractional multiply, high-half signed, lane-select suffixes.
//===----------------------------------------------------------------------===//

def int_haydn_fmul16_hs01 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs01">;
def int_haydn_fmul16_hs02 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs02">;
def int_haydn_fmul16_hs03 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs03">;
def int_haydn_fmul16_hs11 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs11">;
def int_haydn_fmul16_hs12 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs12">;
def int_haydn_fmul16_hs13 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs13">;
def int_haydn_fmul16_hs22 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs22">;
def int_haydn_fmul16_hs23 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs23">;
def int_haydn_fmul16_hs33 : haydn_i64_from_v4i16_binary<"haydn_fmul16_hs33">;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_LS variants (binary DR64)
// Q15*Q15→Q31 fractional multiply, low-half signed, lane-select suffixes.
//===----------------------------------------------------------------------===//

def int_haydn_fmul16_ls00 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls00">;
def int_haydn_fmul16_ls01 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls01">;
def int_haydn_fmul16_ls02 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls02">;
def int_haydn_fmul16_ls03 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls03">;
def int_haydn_fmul16_ls11 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls11">;
def int_haydn_fmul16_ls12 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls12">;
def int_haydn_fmul16_ls13 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls13">;
def int_haydn_fmul16_ls22 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls22">;
def int_haydn_fmul16_ls23 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls23">;
def int_haydn_fmul16_ls33 : haydn_i64_from_v4i16_binary<"haydn_fmul16_ls33">;

//===----------------------------------------------------------------------===//
// Wave 4: FMULAA16 HS/LS MAC variants
// Q15*Q15 MAC into Q31, accumulate-add, lane-pair select.
// Spec/DB: all AA forms read rtd as accumulator. HS_11_00 and HS_33_22 are
// ternary Acc (FmtALU64Acc); remaining variants still binary-selected pending
// Acc migration (same encoding class as 11_00/33_22).
//===----------------------------------------------------------------------===//

def int_haydn_fmulaa16_hs_13_02 : haydn_i64_from_v4i16_binary<"haydn_fmulaa16_hs_13_02">;
// Ternary Acc: rtd[63:32] = SATQ1.31(rtd[63:32] + p3 + p2). Migrated with
// FMULAA16_HS_33_22 -> FmtALU64Acc for AE_MULAFQ16X2_FIR_1 correctness.
def int_haydn_fmulaa16_hs_33_22 : haydn_i64_from_v4i16_mac<"haydn_fmulaa16_hs_33_22">;
def int_haydn_fmulaa16_ls_11_00 : haydn_i64_from_v4i16_binary<"haydn_fmulaa16_ls_11_00">;
def int_haydn_fmulaa16_ls_13_02 : haydn_i64_from_v4i16_binary<"haydn_fmulaa16_ls_13_02">;
def int_haydn_fmulaa16_ls_33_22 : haydn_i64_from_v4i16_binary<"haydn_fmulaa16_ls_33_22">;

//===----------------------------------------------------------------------===//
// Wave 4: FMULSS16 HS/LS MSU variants (binary DR64)
// Q15*Q15 MSU into Q31, subtract, lane-pair select.
//===----------------------------------------------------------------------===//

def int_haydn_fmulss16_hs_13_02 : haydn_i64_from_v4i16_binary<"haydn_fmulss16_hs_13_02">;
def int_haydn_fmulss16_hs_33_22 : haydn_i64_from_v4i16_binary<"haydn_fmulss16_hs_33_22">;
def int_haydn_fmulss16_ls_11_00 : haydn_i64_from_v4i16_binary<"haydn_fmulss16_ls_11_00">;
def int_haydn_fmulss16_ls_13_02 : haydn_i64_from_v4i16_binary<"haydn_fmulss16_ls_13_02">;
def int_haydn_fmulss16_ls_33_22 : haydn_i64_from_v4i16_binary<"haydn_fmulss16_ls_33_22">;

//===----------------------------------------------------------------------===//
// Wave 4: F2MUL zero-accumulator variants (binary DR64)
// Fused dual MAC with add-subtract or subtract-add accumulator patterns.
// AS = add-subtract (one lane adds, the other subtracts)
// SA = subtract-add (one lane subtracts, the other adds)
//===----------------------------------------------------------------------===//

// Saturating with rounding
def int_haydn_f2mulas32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulas32rs_hhll">;
def int_haydn_f2mulas32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulas32rs_hllh">;
def int_haydn_f2mulsa32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulsa32rs_hhll">;
def int_haydn_f2mulsa32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulsa32rs_hllh">;

// Non-saturating with rounding
def int_haydn_f2mulas32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulas32r_hhll">;
def int_haydn_f2mulas32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulas32r_hllh">;
def int_haydn_f2mulsa32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulsa32r_hhll">;
def int_haydn_f2mulsa32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulsa32r_hllh">;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: X2/X4 SIMD Unary ops
//===----------------------------------------------------------------------===//

// X2 unary (DR64 unary: i64 -> i64)
def int_haydn_x2abs32   : haydn_unary_intrinsic<"haydn_x2abs32",   llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2abs32s  : haydn_unary_intrinsic<"haydn_x2abs32s",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2neg32   : haydn_unary_intrinsic<"haydn_x2neg32",   llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2neg32s  : haydn_unary_intrinsic<"haydn_x2neg32s",  llvm_v2i32_ty, llvm_v2i32_ty>;
// X2NEG32_L / X2NEG32S_L negate only the LOW lane (high lane passes through) -
// cross-halfword semantics, NOT a clean per-lane map. Stay opaque i64 (D208).
def int_haydn_x2neg32_l : haydn_unary_intrinsic<"haydn_x2neg32_l", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2neg32s_l : haydn_unary_intrinsic<"haydn_x2neg32s_l", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2swap32  : haydn_unary_intrinsic<"haydn_x2swap32",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mjswap32  : haydn_unary_intrinsic<"haydn_x2mjswap32",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mjswap32s : haydn_unary_intrinsic<"haydn_x2mjswap32s", llvm_v2i32_ty, llvm_v2i32_ty>;

// X4 unary (DR64 unary: i64 -> i64)
def int_haydn_x4abs16   : haydn_unary_intrinsic<"haydn_x4abs16",   llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4abs16s  : haydn_unary_intrinsic<"haydn_x4abs16s",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4neg16   : haydn_unary_intrinsic<"haydn_x4neg16",   llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4neg16s  : haydn_unary_intrinsic<"haydn_x4neg16s",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4swap16  : haydn_unary_intrinsic<"haydn_x4swap16",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4mjswap16  : haydn_unary_intrinsic<"haydn_x4mjswap16",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4mjswap16s : haydn_unary_intrinsic<"haydn_x4mjswap16s", llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4conj16  : haydn_unary_intrinsic<"haydn_x4conj16",  llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4conj16s : haydn_unary_intrinsic<"haydn_x4conj16s", llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4energy16 : haydn_unary_intrinsic<"haydn_x4energy16", llvm_i64_ty, llvm_v4i16_ty>;
def int_haydn_x4cmul16   : Haydn_Intrinsic<"haydn_x4cmul16",   [llvm_v2i32_ty], [llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmul16s  : Haydn_Intrinsic<"haydn_x4cmul16s",  [llvm_v2i32_ty], [llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmul16s_f2 : Haydn_Intrinsic<"haydn_x4cmul16s_f2", [llvm_v2i32_ty], [llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmul16_f2  : Haydn_Intrinsic<"haydn_x4cmul16_f2",  [llvm_v2i32_ty], [llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// X2/X4 binary (DR64 binary: i64, i64 -> i64)
def int_haydn_x2max32   : haydn_binary_intrinsic<"haydn_x2max32",   llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2min32   : haydn_binary_intrinsic<"haydn_x2min32",   llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2clamp32 : Haydn_Intrinsic<"haydn_x2clamp32", [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_v2i32_ty, llvm_v2i32_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4max16   : haydn_binary_intrinsic<"haydn_x4max16",   llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4min16   : haydn_binary_intrinsic<"haydn_x4min16",   llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4clamp16 : Haydn_Intrinsic<"haydn_x4clamp16", [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: Shuffle/Pack ops
//===----------------------------------------------------------------------===//

// X2SEL32 lane variants (binary DR64)
def int_haydn_x2sel32_hh : haydn_binary_intrinsic<"haydn_x2sel32_hh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sel32_hl : haydn_binary_intrinsic<"haydn_x2sel32_hl", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sel32_lh : haydn_binary_intrinsic<"haydn_x2sel32_lh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sel32_ll : haydn_binary_intrinsic<"haydn_x2sel32_ll", llvm_v2i32_ty, llvm_v2i32_ty>;

// X2ADDSUB32 / X2SUBADD32 variants (binary DR64)
// Note: int_haydn_x2addsub32s already defined above in SIMD X2 section.
def int_haydn_x2addsub32      : haydn_binary_intrinsic<"haydn_x2addsub32",      llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2addsub32_hllh : haydn_binary_intrinsic<"haydn_x2addsub32_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2addsub32s_hllh : haydn_binary_intrinsic<"haydn_x2addsub32s_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2subadd32      : haydn_binary_intrinsic<"haydn_x2subadd32",      llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2subadd32s     : haydn_binary_intrinsic<"haydn_x2subadd32s",     llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2subadd32_hllh : haydn_binary_intrinsic<"haydn_x2subadd32_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2subadd32s_hllh : haydn_binary_intrinsic<"haydn_x2subadd32s_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: X4 Complex Multiply variants (binary DR64)
// These are separate from Wave 2 X4FCMUL16RS — these are per-half-lane
// non-rounding complex multiplies and their conjugated versions.
//===----------------------------------------------------------------------===//

// X4CMUL16S — per-half complex multiply, saturating (binary DR64)
def int_haydn_x4cmul16s_h  : Haydn_Intrinsic<"haydn_x4cmul16s_h",  [llvm_v2i32_ty], [llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmul16s_l  : Haydn_Intrinsic<"haydn_x4cmul16s_l",  [llvm_v2i32_ty], [llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmula16s_h : Haydn_Intrinsic<"haydn_x4cmula16s_h", [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cmula16s_l : Haydn_Intrinsic<"haydn_x4cmula16s_l", [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// X4CJMUL16S — per-half conjugated complex multiply, saturating (binary DR64)
def int_haydn_x4cjmul16s_h  : Haydn_Intrinsic<"haydn_x4cjmul16s_h",  [llvm_v2i32_ty], [llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cjmul16s_l  : Haydn_Intrinsic<"haydn_x4cjmul16s_l",  [llvm_v2i32_ty], [llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cjmula16s_h : Haydn_Intrinsic<"haydn_x4cjmula16s_h", [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4cjmula16s_l : Haydn_Intrinsic<"haydn_x4cjmula16s_l", [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_v4i16_ty, llvm_v4i16_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 2: 64-bit scalar ops
//===----------------------------------------------------------------------===//

// Unary DR64: i64 -> i64
def int_haydn_not64 : haydn_unary_intrinsic<"haydn_not64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_seq64 : Haydn_Intrinsic<"haydn_seq64",
    [llvm_i64_ty], [llvm_i64_ty],
    [IntrHasSideEffects]>;

// Binary DR64: i64, i64 -> i64
def int_haydn_max64   : haydn_binary_intrinsic<"haydn_max64",   llvm_i64_ty, llvm_i64_ty>;
def int_haydn_min64   : haydn_binary_intrinsic<"haydn_min64",   llvm_i64_ty, llvm_i64_ty>;
def int_haydn_add64_h : haydn_binary_intrinsic<"haydn_add64_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_add64_l : haydn_binary_intrinsic<"haydn_add64_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64_h : haydn_binary_intrinsic<"haydn_sub64_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64_l : haydn_binary_intrinsic<"haydn_sub64_l", llvm_i64_ty, llvm_i64_ty>;

// SLL64: golden rtd,rsd,rs — (i64 data, i32 shift) -> i64. Imm twin: slli64.
def int_haydn_sll64 : Haydn_Intrinsic<"haydn_sll64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// D220 Tier 2: DR64 bitwise AND/OR (map AE_AND32/OR32/AND64).
def int_haydn_and64 : haydn_binary_intrinsic<"haydn_and64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_or64  : haydn_binary_intrinsic<"haydn_or64",  llvm_i64_ty, llvm_i64_ty>;

// SRA64/SRL64: golden rtd,rsd,rs — (i64 data, i32 shift) -> i64. Imm twins: srai64/srli64.
def int_haydn_sra64 : Haydn_Intrinsic<"haydn_sra64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_srl64 : Haydn_Intrinsic<"haydn_srl64",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULA16 — Signed 16-bit MAC with lane selection
// SMULA16_* — non-saturating multiply-accumulate (binary DR64)
//   rd += signed_mul_16(rs1_lane_i, rs2_lane_j)
//   Lane pairs: 00, 10, 11, 20, 21, 22, 30, 31, 32, 33
//===----------------------------------------------------------------------===//

def int_haydn_smula16_00 : Haydn_Intrinsic<"haydn_smula16_00", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_10 : Haydn_Intrinsic<"haydn_smula16_10", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_11 : Haydn_Intrinsic<"haydn_smula16_11", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_20 : Haydn_Intrinsic<"haydn_smula16_20", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_21 : Haydn_Intrinsic<"haydn_smula16_21", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_22 : Haydn_Intrinsic<"haydn_smula16_22", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_30 : Haydn_Intrinsic<"haydn_smula16_30", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_31 : Haydn_Intrinsic<"haydn_smula16_31", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_32 : Haydn_Intrinsic<"haydn_smula16_32", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16_33 : Haydn_Intrinsic<"haydn_smula16_33", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULA16S — Signed 16-bit saturating MAC with lane selection
// SMULA16S_* — saturating multiply-accumulate (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_smula16s_00 : Haydn_Intrinsic<"haydn_smula16s_00", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_10 : Haydn_Intrinsic<"haydn_smula16s_10", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_11 : Haydn_Intrinsic<"haydn_smula16s_11", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_20 : Haydn_Intrinsic<"haydn_smula16s_20", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_21 : Haydn_Intrinsic<"haydn_smula16s_21", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_22 : Haydn_Intrinsic<"haydn_smula16s_22", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_30 : Haydn_Intrinsic<"haydn_smula16s_30", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_31 : Haydn_Intrinsic<"haydn_smula16s_31", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_32 : Haydn_Intrinsic<"haydn_smula16s_32", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smula16s_33 : Haydn_Intrinsic<"haydn_smula16s_33", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULS16 — Signed 16-bit multiply-subtract with lane selection
// SMULS16_* — non-saturating multiply-subtract (binary DR64)
//   rd -= signed_mul_16(rs1_lane_i, rs2_lane_j)
//===----------------------------------------------------------------------===//

def int_haydn_smuls16_00 : Haydn_Intrinsic<"haydn_smuls16_00", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_10 : Haydn_Intrinsic<"haydn_smuls16_10", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_11 : Haydn_Intrinsic<"haydn_smuls16_11", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_20 : Haydn_Intrinsic<"haydn_smuls16_20", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_21 : Haydn_Intrinsic<"haydn_smuls16_21", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_22 : Haydn_Intrinsic<"haydn_smuls16_22", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_30 : Haydn_Intrinsic<"haydn_smuls16_30", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_31 : Haydn_Intrinsic<"haydn_smuls16_31", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_32 : Haydn_Intrinsic<"haydn_smuls16_32", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16_33 : Haydn_Intrinsic<"haydn_smuls16_33", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SMULS16S — Signed 16-bit saturating multiply-subtract
// SMULS16S_* — saturating multiply-subtract (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_smuls16s_00 : Haydn_Intrinsic<"haydn_smuls16s_00", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_10 : Haydn_Intrinsic<"haydn_smuls16s_10", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_11 : Haydn_Intrinsic<"haydn_smuls16s_11", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_20 : Haydn_Intrinsic<"haydn_smuls16s_20", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_21 : Haydn_Intrinsic<"haydn_smuls16s_21", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_22 : Haydn_Intrinsic<"haydn_smuls16s_22", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_30 : Haydn_Intrinsic<"haydn_smuls16s_30", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_31 : Haydn_Intrinsic<"haydn_smuls16s_31", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_32 : Haydn_Intrinsic<"haydn_smuls16s_32", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_smuls16s_33 : Haydn_Intrinsic<"haydn_smuls16s_33", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: FMULS16 — Fractional 16-bit multiply-subtract (binary DR64)
// FMULS16_HS_* — high-half signed, lane-select fractional subtract
// FMULS16_LS_* — low-half signed, lane-select fractional subtract
//===----------------------------------------------------------------------===//

def int_haydn_fmuls16_hs00 : haydn_binary_intrinsic<"haydn_fmuls16_hs00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs01 : haydn_binary_intrinsic<"haydn_fmuls16_hs01", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs02 : haydn_binary_intrinsic<"haydn_fmuls16_hs02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs03 : haydn_binary_intrinsic<"haydn_fmuls16_hs03", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs11 : haydn_binary_intrinsic<"haydn_fmuls16_hs11", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs12 : haydn_binary_intrinsic<"haydn_fmuls16_hs12", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs13 : haydn_binary_intrinsic<"haydn_fmuls16_hs13", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs22 : haydn_binary_intrinsic<"haydn_fmuls16_hs22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs23 : haydn_binary_intrinsic<"haydn_fmuls16_hs23", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_hs33 : haydn_binary_intrinsic<"haydn_fmuls16_hs33", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_fmuls16_ls00 : haydn_binary_intrinsic<"haydn_fmuls16_ls00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls01 : haydn_binary_intrinsic<"haydn_fmuls16_ls01", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls02 : haydn_binary_intrinsic<"haydn_fmuls16_ls02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls03 : haydn_binary_intrinsic<"haydn_fmuls16_ls03", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls11 : haydn_binary_intrinsic<"haydn_fmuls16_ls11", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls12 : haydn_binary_intrinsic<"haydn_fmuls16_ls12", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls13 : haydn_binary_intrinsic<"haydn_fmuls16_ls13", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls22 : haydn_binary_intrinsic<"haydn_fmuls16_ls22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls23 : haydn_binary_intrinsic<"haydn_fmuls16_ls23", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmuls16_ls33 : haydn_binary_intrinsic<"haydn_fmuls16_ls33", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: FMULAS32S — Fractional add-subtract dual MAC, saturating
// FMULAS32S = FMUL + Add-Subtract (one lane adds, other subtracts)
// FMULSA32S = FMUL + Subtract-Add (one lane subtracts, other adds)
// Ternary DR64 (acc, a, b): the DB (haydn_instruction_db.json:7356/7397/8375/
// 8416) shows slot-1 reads rtd as an accumulator input
// (`rtd = SATQ1.63(rtd + ...)` / `rtd - ...`), so these are read-modify-
// write. Were mistakenly declared binary (dropping the accumulator — the L97
// silent-miscompute class); reconciled to ternary mirroring D143
// (FMULS32S_LH). No clang builtin (IR-only intrinsics). See D180 A1.
//===----------------------------------------------------------------------===//

def int_haydn_fmulas32s_hhll : haydn_ternary_intrinsic<"haydn_fmulas32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulas32s_hllh : haydn_ternary_intrinsic<"haydn_fmulas32s_hllh", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_fmulsa32s_hhll : haydn_ternary_intrinsic<"haydn_fmulsa32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulsa32s_hllh : haydn_ternary_intrinsic<"haydn_fmulsa32s_hllh", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// Wave 4 Tier 3: SRA64R — 64-bit arithmetic right shift with rounding
// Golden: rtd,rsd (DR64), rs (GPR32) -> full i64. Imm twin: srai64r.
// Pack-to-i32 uses trunc/cast at the call site (haydn_packsr32 in haydn_dsp.h).
// Admitted *r law: shift>0 adds 1<<(shift-1) then ASR; shift 0 is identity.
//===----------------------------------------------------------------------===//

def int_haydn_sra64r : Haydn_Intrinsic<"haydn_sra64r",
    [llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Compare -> SFR
// These compare two DR64 values and set the SFR register accordingly.
// SLT64: SFR = (rsd1 < rsd2) ? 4'b1111 : 4'b0000
// SLE64: SFR = (rsd1 <= rsd2) ? 4'b1111 : 4'b0000
// SEQ64: already defined above (Wave 4 Tier 2).
// They return the operand as passthrough (like SEQ64 pattern).
// Unary DR64 matching the instruction defs: (outs DR64:$rd), (ins DR64:$rs)
//===----------------------------------------------------------------------===//

def int_haydn_slt64 : Haydn_Intrinsic<"haydn_slt64",
    [llvm_i64_ty], [llvm_i64_ty],
    [IntrHasSideEffects]>;
def int_haydn_sle64 : Haydn_Intrinsic<"haydn_sle64",
    [llvm_i64_ty], [llvm_i64_ty],
    [IntrHasSideEffects]>;

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Conditional Move
// MOVT64: rtd = (SFR == 4'b1111) ? rsd : rtd  (move if SFR true)
// MOVF64: rtd = (SFR == 4'b0000) ? rsd : rtd  (move if SFR false)
// Unary DR64 matching the instruction defs: (outs DR64:$rd), (ins DR64:$rs)
//===----------------------------------------------------------------------===//

def int_haydn_movt64 : Haydn_Intrinsic<"haydn_movt64",
    [llvm_i64_ty], [llvm_i64_ty],
    [IntrHasSideEffects]>;
def int_haydn_movf64 : Haydn_Intrinsic<"haydn_movf64",
    [llvm_i64_ty], [llvm_i64_ty],
    [IntrHasSideEffects]>;

//===----------------------------------------------------------------------===//
// Wave 5: SFR Register Transfer
// MOVESFR2GPR: rt = {28'b0, SFR}  — read SFR into GPR32 low bits
// MOVEGPR2SFR: SFR = rs[3:0]      — write SFR from GPR32 low bits
// ZERO_SFR:    SFR = 4'b0000       — clear SFR
// These read/write the SFR side-effect register.
//===----------------------------------------------------------------------===//

def int_haydn_movesfr2gpr : Haydn_Intrinsic<"haydn_movesfr2gpr",
    [llvm_i32_ty], [],
    [IntrHasSideEffects]>;

def int_haydn_movegpr2sfr : Haydn_Intrinsic<"haydn_movegpr2sfr",
    [], [llvm_i32_ty],
    [IntrHasSideEffects]>;

def int_haydn_zero_sfr : Haydn_Intrinsic<"haydn_zero_sfr",
    [], [],
    [IntrHasSideEffects]>;

//===----------------------------------------------------------------------===//
// Cross-RF move: DR64 half-lane -> GPR32 (MOVE32_DR_L / MOVE32_DR_H)
//
// The first real DR->GPR cross-bank move in the ISA (added Rev 2, 2026-06-18):
//   llvm.haydn.move32_dr_l(rsd: i64) -> i32   ; rt = rsd[31:00]
//   llvm.haydn.move32_dr_h(rsd: i64) -> i32   ; rt = rsd[63:32]
// These are the hardware path the MOV_DR64_TO_GPR SP-spill shim approximates.
// Pure (no memory, no implicit state): IntrNoMem is correct here, matching
// the rest of the DR/GPR ALU family.
//===----------------------------------------------------------------------===//

def int_haydn_move32_dr_l : Haydn_Intrinsic<"haydn_move32_dr_l",
    [llvm_i32_ty], [llvm_i64_ty],
    [IntrNoMem]>;

def int_haydn_move32_dr_h : Haydn_Intrinsic<"haydn_move32_dr_h",
    [llvm_i32_ty], [llvm_i64_ty],
    [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Circular Buffer Load/Store (CBR)
//
// Haydn has 2 hardware circular-buffer sets, selected by cbr_sel (0 or 1).
// Each set is configured by a CSR pair (Rev 2, 2026-06-18 — there is NO
// CBR_SIZE register; the size is derived CBR_END - CBR_BEGIN + 1):
//   CBR_BEGIN[cbr_sel] (CSR 0x2C/0x2E) — inclusive start byte address
//   CBR_END[cbr_sel]   (CSR 0x2D/0x2F) — inclusive last-byte address
// programmed via the setcbr_begin / setcbr_end setup intrinsics below.
// The CB load/store instructions automatically wrap the address pointer
// within the circular buffer region.
//
// The intrinsic takes the CBR selector as an i32 immediate (0-3), plus
// the data register and base pointer. The base pointer is updated in-place
// with circular buffer wrapping semantics.
//
// CB loads:  (ptr_base, cbr_sel, stride) -> (data_result, updated_ptr)
// CB stores: (data, ptr_base, cbr_sel, stride) -> updated_ptr
//
// IntrNoMem is used instead of IntrReadMem/IntrWriteMem because GlobalISel
// routes memory-property intrinsics through G_INTRINSIC_W_SIDE_EFFECTS,
// which gets lowered to a libcall (JAL @llvm.haydn.*). Using IntrNoMem
// routes through G_INTRINSIC → selectIntrinsic() for correct inline
// instruction selection. The actual memory side effects are modeled at the
// MC layer via mayLoad/mayStore on the instruction definitions.
//
// F20 (consolidated-fix-list §2): the IntrNoMem choice above made loads
// DCE-able and stores reorderable — a real bug that broke FFT/circular-
// buffer porting. Loads are now IntrReadMem and stores IntrWriteMem so
// MemorySSA / MemoryDependence see the actual memory access. The selector
// must select these via the G_INTRINSIC_W_SIDE_EFFECTS path.
//===----------------------------------------------------------------------===//

// D_LDW_CB_IMM — 64-bit load from circular buffer.
// Intrinsic: (ptr ptr_base, i32 cbr_sel, i32 stride) -> {i64 data, ptr new_ptr}
//
// MODEL (D208, supersedes D207 for freestanding + NatureDSP XC):
// Hardware AGU writes wrap(base+stride) into the base GPR. That result is a
// real SSA value the program may need (freestanding NatureDSP AE_*_XC mutates
// the C pointer). Return BOTH loaded data and the updated cursor so IR models
// the same deterministic step as BundleSim / MI rs_wb.
//
// No ClangBuiltin auto-map: multi-result needs frexp-pattern _pair builtins
// (see EmitHaydnBuiltinExpr). C headers expose .data / .ptr via struct wrapper.
//   Note: cbr_sel is encoded as an immediate (0 or 1).
//   Note: ptr_base is llvm_ptr_ty (p0/GPR32); data is DR64 (i64).
// IntrHasSideEffects: consecutive calls with same base differ (also blocks LICM).
// C2.2: IntrReadMem+IntrArgMemOnly+NoCapture (Hexagon circ/brev peer) so AA
// sees argmem and getTgtMemIntrinsic can attach a concrete MMO.
def int_haydn_ldw_cb_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>, IntrReadMem, IntrArgMemOnly,
     NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;

// D_LDW_CB_REG — 64-bit load from circular buffer, register stride.
// Intrinsic: (ptr ptr_base, i32 cbr_sel, i32 stride_reg) -> {i64 data, ptr new_ptr}
// cbr_sel is encoding ImmArg (uimm1); stride stays variable GPR.
def int_haydn_ldw_cb_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>,
     IntrHasSideEffects, IntrWillReturn]>;

// D_SDW_CB_IMM — 64-bit store to circular buffer.
// Intrinsic: (i64 data, ptr ptr_base, i32 cbr_sel, i32 stride) -> ptr new_ptr
// Returns the AGU-updated base (same as MI rs_wb). Callers that ignore the
// return keep D207-style fire-and-forget stores; freestanding XC assigns it.
def int_haydn_sdw_cb_imm : Haydn_Intrinsic<"haydn_sdw_cb_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, IntrWriteMem, IntrArgMemOnly,
     NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;

// D_SDW_CB_REG — 64-bit store to circular buffer, register stride.
// Intrinsic: (i64 data, ptr ptr_base, i32 cbr_sel, i32 stride_reg) -> ptr new_ptr
def int_haydn_sdw_cb_reg : Haydn_Intrinsic<"haydn_sdw_cb_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>,
     IntrHasSideEffects, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Circular Buffer Setup (CBR programming via CSRW)
//
// Configures a CBR set before any circular-buffer load/store is issued. These
// are side-effecting CSR writes (IntrHasSideEffects + IntrWriteMem) — they
// must NOT be DCE'd or reordered (the same trap F20 hit for CB stores). The
// selector lowers them to the SETCBR_BEGIN / SETCBR_END pseudos, which the
// AsmPrinter expands to `csrw <addr>, rs`:
//   cbr_sel=0 -> CBR_BEGIN = CSR 0x2C, CBR_END = CSR 0x2D
//   cbr_sel=1 -> CBR_BEGIN = CSR 0x2E, CBR_END = CSR 0x2F
// Mirrors Hexagon `m0=rN; cs0=rN` boundary setup (research/
// circular-buffer-cross-arch-study §1.2), but reuses the existing CSR space.
//===----------------------------------------------------------------------===//

// Intrinsic: (i32 cbr_sel, i32 value) -> void
//   Writes CBR_BEGIN[cbr_sel] = value. cbr_sel is encoding ImmArg (uimm1).
def int_haydn_setcbr_begin : Haydn_Intrinsic<"haydn_setcbr_begin",
    [], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, IntrHasSideEffects, IntrWriteMem]>;

// Intrinsic: (i32 cbr_sel, i32 value) -> void
//   Writes CBR_END[cbr_sel] = value. cbr_sel is encoding ImmArg (uimm1).
def int_haydn_setcbr_end : Haydn_Intrinsic<"haydn_setcbr_end",
    [], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, IntrHasSideEffects, IntrWriteMem]>;

//===----------------------------------------------------------------------===//
// AR (Aligned Register) unaligned load/store stream (golden LS #103-#109)
//
// Hardware funnel-shift unaligned access via AR0–AR1 (ar_sel legal domain
// {0,1}; wire field remains 2 bits — selectors 2/3 fail-closed at product
// surfaces until golden classifies unused codes):
//   PLDWWUA       — seed AR from mem64[rs & ~7]
//   D_LQHWUA_POST — load 4×i16 unaligned + post-inc rs1 by ±rs2
//   D_LTWUA_POST  — load 2×i32 unaligned + post-inc
//   FLAR          — flush AR to 0
//   WBARWUA       — write-back residual AR bytes to mem
//   D_SQHWUA_POST — store 4×i16 unaligned + post-inc
//   D_STWUA_POST  — store 2×i32 unaligned + post-inc
//
// Pointer / AGU contract (intentional — do NOT expose HW writeback to IR/C):
//   - The ISA post-increments the AGU base register in hardware.
//   - The intrinsic does NOT return the updated pointer. Select lowers the
//     AGU writeback as a dead def (MC needs the tied-def shape).
//   - The program's next pointer is ordinary C/IR (ptr + stride / GEP). SCEV
//     and loop opts see that GEP chain — that is enough for IP streams.
//   - Users never read "HW writeback" directly; only subsequent ptr uses.
//
// Memory model: loads are IntrReadMem+IntrHasSideEffects (AR state + mem);
// stores are IntrWriteMem+IntrHasSideEffects. SideEffects keep AR sticky
// state ordered and prevent LICM of AR-dependent ops.
//===----------------------------------------------------------------------===//

// AR state is inaccessible to LLVM memory SSA. Always IntrHasSideEffects so
// PLDW/FLAR cannot be DCE'd. NOTE: void + IntrReadMem alone → memory(read)
// which O2 DCE's as a dead pure load — never pair void with only IntrReadMem.
// PLDW writes AR (side effect); model as HasSideEffects (not pure read).
//
// void pldwwua(i32 ar_sel, i32 ptr) — seed AR from mem (must not DCE)
// ar_sel is encoding ImmArg; product legal domain {0,1}.
def int_haydn_pldwwua : Haydn_Intrinsic<"haydn_pldwwua",
    [], [llvm_i32_ty, llvm_ptr_ty],
    [ImmArg<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;

// i64 d_lqhwua_post(i32 ptr, i32 ar_sel, i32 stride, i32 dir_sel)
// ar_sel uimm2 + dir_sel uimm1 are ImmArg; stride stays variable GPR.
def int_haydn_d_lqhwua_post : Haydn_Intrinsic<"haydn_d_lqhwua_post",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<3>>, IntrReadMem, IntrHasSideEffects,
     IntrWillReturn]>;

// i64 d_ltwua_post(i32 ptr, i32 ar_sel, i32 stride, i32 dir_sel)
def int_haydn_d_ltwua_post : Haydn_Intrinsic<"haydn_d_ltwua_post",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<3>>, IntrReadMem, IntrHasSideEffects,
     IntrWillReturn]>;

// void flar(i32 ar_sel) — AR-only state clear; ar_sel ImmArg uimm2.
def int_haydn_flar : Haydn_Intrinsic<"haydn_flar",
    [], [llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;

// void wbarwua(i32 ar_sel, i32 ptr, i32 dir_sel)
def int_haydn_wbarwua : Haydn_Intrinsic<"haydn_wbarwua",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrWriteMem, IntrHasSideEffects,
     IntrWillReturn]>;

// void d_sqhwua_post(i64 data, i32 ptr, i32 ar_sel, i32 stride, i32 dir_sel)
def int_haydn_d_sqhwua_post : Haydn_Intrinsic<"haydn_d_sqhwua_post",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<4>>, IntrWriteMem, IntrHasSideEffects,
     IntrWillReturn]>;

// void d_stwua_post(i64 data, i32 ptr, i32 ar_sel, i32 stride, i32 dir_sel)
def int_haydn_d_stwua_post : Haydn_Intrinsic<"haydn_d_stwua_post",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<4>>, IntrWriteMem, IntrHasSideEffects,
     IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Bit-Reversed Addressing Load/Store
//
// These instructions apply REVERSE32() to the base address before accessing
// memory, then post-increment the original (non-reversed) base register.
// Used for FFT butterfly addressing patterns.
//
// MODEL (matches CB frexp pair / golden AGU writeback):
//   D_LDW_BREV: (i32 ptr, i32 stride) -> {i64 data, i32 new_ptr}
//   S_LW_BREV:  (i32 ptr, i32 stride) -> {i32 data, i32 new_ptr}
//   D_SDW_BREV: (i64 data, i32 ptr, i32 stride) -> i32 new_ptr
//   S_SW_BREV:  (i32 data, i32 ptr, i32 stride) -> i32 new_ptr
// IMM variants mark stride as ImmArg (simm6). Loads have no ClangBuiltin
// auto-map: multi-result needs frexp-pattern _pair builtins.
//===----------------------------------------------------------------------===//

// D_LDW_BREV_IMM — rtd = mem64[REVERSE32(rs)]; rs += imm6<<3
// Intrinsic: (i32 ptr_base, i32 imm_stride) -> {i64 data, i32 new_ptr}
// C2.2: IntrArgMemOnly+NoCapture peer Hexagon L2_load*_pbr + getTgtMemIntrinsic.
def int_haydn_ldw_brev_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects,
     IntrWillReturn, ImmArg<ArgIndex<1>>]>;

// D_LDW_BREV_REG — rtd = mem64[REVERSE32(rs1)]; rs1 += rs2
def int_haydn_ldw_brev_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects,
     IntrWillReturn]>;

// S_LW_BREV_IMM — rt = mem32[REVERSE32(rs)]; rs += imm6<<2
// Intrinsic: (i32 ptr_base, i32 imm_stride) -> {i32 data, i32 new_ptr}
def int_haydn_lw_brev_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects,
     IntrWillReturn, ImmArg<ArgIndex<1>>]>;

// S_LW_BREV_REG — rt = mem32[REVERSE32(rs1)]; rs1 += rs2
def int_haydn_lw_brev_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects,
     IntrWillReturn]>;

// D_SDW_BREV_IMM — mem64[REVERSE32(rs)] = rtd; rs += imm6<<3
// Intrinsic: (i64 data, i32 ptr_base, i32 imm_stride) -> i32 new_ptr
def int_haydn_sdw_brev_imm : Haydn_Intrinsic<"haydn_sdw_brev_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects,
     IntrWillReturn, ImmArg<ArgIndex<2>>]>;

// D_SDW_BREV_REG — mem64[REVERSE32(rs1)] = rtd; rs1 += rs2
def int_haydn_sdw_brev_reg : Haydn_Intrinsic<"haydn_sdw_brev_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects,
     IntrWillReturn]>;

// S_SW_BREV_IMM — mem32[REVERSE32(rs)] = rt; rs += imm6<<2
// Intrinsic: (i32 data, i32 ptr_base, i32 imm_stride) -> i32 new_ptr
def int_haydn_sw_brev_imm : Haydn_Intrinsic<"haydn_sw_brev_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects,
     IntrWillReturn, ImmArg<ArgIndex<2>>]>;

// S_SW_BREV_REG — mem32[REVERSE32(rs1)] = rt; rs1 += rs2
def int_haydn_sw_brev_reg : Haydn_Intrinsic<"haydn_sw_brev_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects,
     IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// BREV32 — Bit-reversed add for FFT butterfly addressing.
// Semantics: rt = bitreverse(bitreverse(rs1) + rs2).
// Maps 1:1 to NatureDSP AE_ADDBRBA32(idx, stride). Used in FFT kernels to
// walk a stage buffer in bit-reversed index order with a linear stride.
//===----------------------------------------------------------------------===//

def int_haydn_addbrba32 : haydn_binary_intrinsic<"haydn_addbrba32",
    llvm_i32_ty, llvm_i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD non-saturating arithmetic (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2add32 : haydn_binary_intrinsic<"haydn_x2add32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sub32 : haydn_binary_intrinsic<"haydn_x2sub32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x4add16 : haydn_binary_intrinsic<"haydn_x4add16", llvm_v4i16_ty, llvm_v4i16_ty>;
def int_haydn_x4sub16 : haydn_binary_intrinsic<"haydn_x4sub16", llvm_v4i16_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD HLLH cross-lane variants (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2add32_hllh  : haydn_binary_intrinsic<"haydn_x2add32_hllh",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2add32s_hllh : haydn_binary_intrinsic<"haydn_x2add32s_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sub32_hllh  : haydn_binary_intrinsic<"haydn_x2sub32_hllh",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2sub32s_hllh : haydn_binary_intrinsic<"haydn_x2sub32s_hllh", llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD register shifts (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2sll32 : Haydn_Intrinsic<"haydn_x2sll32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2sra32 : Haydn_Intrinsic<"haydn_x2sra32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2srl32 : Haydn_Intrinsic<"haydn_x2srl32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4sll16 : Haydn_Intrinsic<"haydn_x4sll16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4sra16 : Haydn_Intrinsic<"haydn_x4sra16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4srl16 : Haydn_Intrinsic<"haydn_x4srl16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD immediate shifts (i64 val, i32 shift_imm) -> i64
//===----------------------------------------------------------------------===//

def int_haydn_x2slli32 : Haydn_Intrinsic<"haydn_x2slli32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2srai32 : Haydn_Intrinsic<"haydn_x2srai32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2srli32 : Haydn_Intrinsic<"haydn_x2srli32",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4slli16 : Haydn_Intrinsic<"haydn_x4slli16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4srai16 : Haydn_Intrinsic<"haydn_x4srai16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4srli16 : Haydn_Intrinsic<"haydn_x4srli16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD shift with rounding (binary DR64)
// Same admitted *r law as SRA64R: shift>0 adds 1<<(shift-1) then ASR.
//===----------------------------------------------------------------------===//

def int_haydn_x2sra32r : Haydn_Intrinsic<"haydn_x2sra32r",
    [llvm_v2i32_ty], [llvm_v2i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x4sra16r : Haydn_Intrinsic<"haydn_x4sra16r",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD horizontal reductions (unary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2hadd32_h  : haydn_unary_intrinsic<"haydn_x2hadd32_h",  llvm_i64_ty, llvm_v2i32_ty>;
def int_haydn_x2hadd32s_h : haydn_unary_intrinsic<"haydn_x2hadd32s_h", llvm_i64_ty, llvm_v2i32_ty>;
def int_haydn_x2hadd32_l  : haydn_unary_intrinsic<"haydn_x2hadd32_l",  llvm_i64_ty, llvm_v2i32_ty>;
def int_haydn_x2hadd32s_l : haydn_unary_intrinsic<"haydn_x2hadd32s_l", llvm_i64_ty, llvm_v2i32_ty>;
def int_haydn_x4hadd16_h  : haydn_unary_intrinsic<"haydn_x4hadd16_h",  llvm_i64_ty, llvm_v4i16_ty>;
def int_haydn_x4hadd16_l  : haydn_unary_intrinsic<"haydn_x4hadd16_l",  llvm_i64_ty, llvm_v4i16_ty>;
// R_GD horizontal max/min: scalar GPR32 result from DR64 vector source.
def int_haydn_x2hmax32 : haydn_unary_intrinsic<"haydn_x2hmax32", llvm_i32_ty, llvm_v2i32_ty>;
def int_haydn_x2hmin32 : haydn_unary_intrinsic<"haydn_x2hmin32", llvm_i32_ty, llvm_v2i32_ty>;
def int_haydn_x4hmax16 : haydn_unary_intrinsic<"haydn_x4hmax16", llvm_i32_ty, llvm_v4i16_ty>;
def int_haydn_x4hmin16 : haydn_unary_intrinsic<"haydn_x4hmin16", llvm_i32_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD dot product (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2dot32 : haydn_binary_intrinsic<"haydn_x2dot32", llvm_i64_ty, llvm_v2i32_ty>;
def int_haydn_x4dot16 : haydn_binary_intrinsic<"haydn_x4dot16", llvm_i64_ty, llvm_v4i16_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD multiply (D400 Path B: 2-dest DR64)
// TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). X2MUL32 -> high/low
// 32-bit lane products; X4MUL16 -> high/low 16-bit lane-product pairs.
// Returns [i64, i64]; args (src1, src2).
//===----------------------------------------------------------------------===//

def int_haydn_x2mul32 : haydn_simd_mac_2dest_intrinsic<"haydn_x2mul32">;
def int_haydn_x4mul16 : haydn_simd_mac_2dest_v4i16_intrinsic<"haydn_x4mul16">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD multiply-pair variants (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2mulph32  : haydn_binary_intrinsic<"haydn_x2mulph32",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mulpl32  : haydn_binary_intrinsic<"haydn_x2mulpl32",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mulaph32 : haydn_ternary_intrinsic<"haydn_x2mulaph32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mulapl32 : haydn_ternary_intrinsic<"haydn_x2mulapl32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mulsph32 : haydn_ternary_intrinsic<"haydn_x2mulsph32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2mulspl32 : haydn_ternary_intrinsic<"haydn_x2mulspl32", llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 32-bit complex fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2fcmul32rs  : haydn_binary_intrinsic<"haydn_x2fcmul32rs",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fcmul32rss : haydn_binary_intrinsic<"haydn_x2fcmul32rss", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fcmula32rs  : haydn_ternary_intrinsic<"haydn_x2fcmula32rs",  llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2fcmula32rss : haydn_ternary_intrinsic<"haydn_x2fcmula32rss", llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X2 CMUL F2 variants (D400 Path B: 2-dest DR64)
// TRUE 2-output complex multiply, per-half (F2) form. Golden 2-dest.
//===----------------------------------------------------------------------===//

def int_haydn_x2cmul32_f2  : haydn_simd_mac_2dest_intrinsic<"haydn_x2cmul32_f2">;
def int_haydn_x2cmul32s_f2 : haydn_simd_mac_2dest_intrinsic<"haydn_x2cmul32s_f2">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 FF2 shift variants (binary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x2ff2rsst32 : haydn_binary_intrinsic<"haydn_x2ff2rsst32", llvm_v2i32_ty, llvm_v2i32_ty>;
def int_haydn_x2ff2rst32  : haydn_binary_intrinsic<"haydn_x2ff2rst32",  llvm_v2i32_ty, llvm_v2i32_ty>;

//===----------------------------------------------------------------------===//
// Wave 5: X4 FF2MUL variants (ternary DR64)
//===----------------------------------------------------------------------===//

def int_haydn_x4ff2mul16s : haydn_simd_mac_2dest_v4i16_intrinsic<"haydn_x4ff2mul16s">;
def int_haydn_x4ff2mula16s : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4ff2mula16s">;
def int_haydn_x4ff2muls16s : haydn_simd_maca_2dest_v4i16_intrinsic<"haydn_x4ff2muls16s">;

//===----------------------------------------------------------------------===//
// Wave 5: X4 pack/sat/sel
//===----------------------------------------------------------------------===//

// Golden X4SAT32T16: four 32-bit lanes (two v2i32) → four saturated i16 (v4i16).
def int_haydn_x4sat32t16 : Haydn_Intrinsic<"haydn_x4sat32t16",
    [llvm_v4i16_ty], [llvm_v2i32_ty, llvm_v2i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_x4seli16 : Haydn_Intrinsic<"haydn_x4seli16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_v4i16_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
// Reg twin: mask in GPR (DB X4SEL16).
def int_haydn_x4sel16 : Haydn_Intrinsic<"haydn_x4sel16",
    [llvm_v4i16_ty], [llvm_v4i16_ty, llvm_v4i16_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// MOVDA family — GPR32 <-> DR64 lane transfers.
//
// The Haydn ISA has no native GPR->DR lane-insert (ISA-63; golden has only
// MOVE32_DR_L/H for DR->GPR extract). Each movda intrinsic lowers to
// MOV_GPR_TO_DR64. Two live GPR halves expand post-RA through the
// per-function DR64PackFI slot (ST32 lo, ST32 hi, LD64) — a memory
// round-trip, not a register insert. One-half-R0 packs stay stackless
// (SEXT+shift). movad32_{low,high} select native MOVE32_DR_L/H.
//
//   llvm.haydn.movda32(x: i32)         -> i64   ; place i32 in low lane, high=0
//   llvm.haydn.movda32x2(lo: i32, hi: i32) -> i64 ; place 2x i32 into the two lanes
//   llvm.haydn.movda16(x: i32)         -> i64   ; same as movda32 (lanes are i32-wide)
//   llvm.haydn.movad32_low(dr: i64)    -> i32   ; extract low 32-bit lane
//   llvm.haydn.movad32_high(dr: i64)   -> i32   ; extract high 32-bit lane
//===----------------------------------------------------------------------===//

def int_haydn_movda32 : Haydn_Intrinsic<"haydn_movda32",
    [llvm_i64_ty], [llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_movda32x2 : Haydn_Intrinsic<"haydn_movda32x2",
    [llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_movda16 : Haydn_Intrinsic<"haydn_movda16",
    [llvm_i64_ty], [llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_movad32_low : Haydn_Intrinsic<"haydn_movad32_low",
    [llvm_i32_ty], [llvm_i64_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_haydn_movad32_high : Haydn_Intrinsic<"haydn_movad32_high",
    [llvm_i32_ty], [llvm_i64_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===//
// Complex 32x16 MAC with rounding + saturation (FFT butterfly core).
//
// NatureDSP AE_MULFC32X16RAS_{L,H}: complex 32x16 multiply-accumulate with
// rounding-and-saturation. The single hottest intrinsic in upstream NatureDSP
// FFT kernels (494 uses). The Haydn ISA has no native instruction for this
// exact operation (see ~/haydn-plans/isa-improve/ISA-08-mulfc32x16ras.md and
// decision D90-mulfc32x16ras-intrinsic.md). The C wrapper in haydn.h
// performs the 16->32 lane widen of the twiddle (b) using AE_CVT32X2F16_*
// semantics, then dispatches to llvm.haydn.x2fcmula32rs (32x32 complex MAC
// with round+saturation). The ISel selector maps these intrinsics onto the
// existing X2FCMULA32RS instruction, treating b as the already-widened
// 32-bit complex twiddle.
//
//   llvm.haydn.mulfc32x16ras.low(acc: i64, a: i64, b: i64) -> i64
//   llvm.haydn.mulfc32x16ras.high(acc: i64, a: i64, b: i64) -> i64
//
// Args:
//   acc : DR64 accumulator holding two saturated 32-bit Q1.31 lanes
//   a   : DR64 holding 32-bit complex data (re in low lane, im in high lane)
//   b   : DR64 holding the *pre-widened* 32-bit complex twiddle; the C wrapper
//         sign-extends the 16-bit twiddle to 32-bit before invoking this
//         intrinsic because Haydn lacks a cross-lane 16->32 pack instruction.
//===----------------------------------------------------------------------===//

def int_haydn_mulfc32x16ras_low : haydn_ternary_intrinsic<
    "haydn_mulfc32x16ras_low", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_mulfc32x16ras_high : haydn_ternary_intrinsic<
    "haydn_mulfc32x16ras_high", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// FIR-specific MAC family (decomposed — see D91-fir-mac-family-decomposition)
//
// These model the HiFi3 dual/quad-output FIR MAC intrinsics used by the
// NatureDSP FIR kernel family (bkfir16x16, convol16x16, blms16x16, etc.).
// Haydn has no native dual/quad-output FIR MAC op (see ISA-09), so each
// intrinsic lowers to a single building-block MAC step that the kernel
// caller issues twice (once per output accumulator). The `acc` operand is
// the in/out accumulator lane; the init variants ignore its prior value.
//
// Args:
//   acc : DR64 accumulator lane (read + written)
//   a   : DR64 data operand
//   b   : DR64 coefficient operand
//===----------------------------------------------------------------------===//

// AE_MULAFD32X16X2_FIR_HH: 32x32 fractional MAC, high-high lanes (Q1.31).
// Lowers to FMULA32S_HH acc, a, b. The backing op is 32x32 (DB :6898); the
// old name `mulafd32x16x2` was a width lie (it claimed 32x16). Renamed to
// `mulaa32s_fir` to match the actual 32x32 FMULA32S backing instruction; the
// C AE_MUL*FD32X16* macro names are preserved via a compat shim in
// haydn_dsp.h for NatureDSP source compatibility. See D180 A2.
def int_haydn_mulaa32s_fir_hh : haydn_ternary_intrinsic<
    "haydn_mulaa32s_fir_hh", llvm_i64_ty, llvm_i64_ty>;

// AE_MULAFD32X16X2_FIR_HL: 32x32 fractional MAC, low-high lanes (Q1.31).
// Lowers to FMULA32S_LH acc, a, b. Renamed from `mulafd32x16x2_fir_hl` (the
// 32x16 claim was wrong); see D180 A2.
def int_haydn_mulaa32s_fir_hl : haydn_ternary_intrinsic<
    "haydn_mulaa32s_fir_hl", llvm_i64_ty, llvm_i64_ty>;

// AE_MULFQ16X2_FIR_3: 16x16 fractional multiply init, lane-pair 0/1.
// Non-accumulating: overwrites rtd[63:32] with the Q1.31 product.
// Lowers to FMULZAA16_HS_11_00 (zero-acc dual product, lanes 1+0).
def int_haydn_mulfq16x2_fir_3 : haydn_ternary_intrinsic<
    "haydn_mulfq16x2_fir_3", llvm_i64_ty, llvm_i64_ty>;

// AE_MULFQ16X2_FIR_1: 16x16 fractional dual-product init, lane-pair 3+2.
// Lowers to FMULZAA16_HS_33_22 (zero-acc dual product).
def int_haydn_mulfq16x2_fir_1 : haydn_ternary_intrinsic<
    "haydn_mulfq16x2_fir_1", llvm_i64_ty, llvm_i64_ty>;

// AE_MULAFQ16X2_FIR_3: 16x16 fractional MAC, lane-pair 1+0 → Q1.31.
// Lowers to FMULAA16_HS_11_00 acc, a, b (FmtALU64Acc).
def int_haydn_mulafq16x2_fir_3 : haydn_ternary_intrinsic<
    "haydn_mulafq16x2_fir_3", llvm_i64_ty, llvm_i64_ty>;

// AE_MULAFQ16X2_FIR_1: 16x16 fractional MAC, lane-pair 3+2 → Q1.31.
// Lowers to FMULAA16_HS_33_22 acc, a, b (FmtALU64Acc; Acc migration B8).
def int_haydn_mulafq16x2_fir_1 : haydn_ternary_intrinsic<
    "haydn_mulafq16x2_fir_1", llvm_i64_ty, llvm_i64_ty>;

//===----------------------------------------------------------------------===//
// D208: DB-coverage completion — 158 intrinsics matching DB mnemonics that had no
// LLVM modeling. Names EXACTLY match the DB instruction lowercased.
// Binary for pure-multiply/zero-accumulate, ternary for accumulate variants.
//===----------------------------------------------------------------------===//

def int_haydn_brev32 : haydn_binary_intrinsic<"haydn_brev32", llvm_i32_ty, llvm_i32_ty>;
def int_haydn_f2mulaa32r_hhll : haydn_i64_from_v2i32_mac<"haydn_f2mulaa32r_hhll">;
def int_haydn_f2mulaa32r_hllh : haydn_i64_from_v2i32_mac<"haydn_f2mulaa32r_hllh">;
def int_haydn_f2mulss32r_hhll : haydn_i64_from_v2i32_mac<"haydn_f2mulss32r_hhll">;
def int_haydn_f2mulss32r_hllh : haydn_i64_from_v2i32_mac<"haydn_f2mulss32r_hllh">;
def int_haydn_f2mulzaa32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzaa32rs_hhll">;
def int_haydn_f2mulzaa32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzaa32rs_hllh">;
def int_haydn_f2mulzaa32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzaa32r_hhll">;
def int_haydn_f2mulzaa32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzaa32r_hllh">;
def int_haydn_f2mulzas32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzas32rs_hhll">;
def int_haydn_f2mulzas32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzas32rs_hllh">;
def int_haydn_f2mulzas32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzas32r_hhll">;
def int_haydn_f2mulzas32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzas32r_hllh">;
def int_haydn_f2mulzsa32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzsa32rs_hhll">;
def int_haydn_f2mulzsa32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzsa32rs_hllh">;
def int_haydn_f2mulzsa32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzsa32r_hhll">;
def int_haydn_f2mulzsa32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzsa32r_hllh">;
def int_haydn_f2mulzss32rs_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzss32rs_hhll">;
def int_haydn_f2mulzss32rs_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzss32rs_hllh">;
def int_haydn_f2mulzss32r_hhll : haydn_i64_from_v2i32_binary<"haydn_f2mulzss32r_hhll">;
def int_haydn_f2mulzss32r_hllh : haydn_i64_from_v2i32_binary<"haydn_f2mulzss32r_hllh">;
def int_haydn_fmula16_hs00 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs00">;
def int_haydn_fmula16_hs01 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs01">;
def int_haydn_fmula16_hs02 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs02">;
def int_haydn_fmula16_hs03 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs03">;
def int_haydn_fmula16_hs11 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs11">;
def int_haydn_fmula16_hs12 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs12">;
def int_haydn_fmula16_hs13 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs13">;
def int_haydn_fmula16_hs22 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs22">;
def int_haydn_fmula16_hs23 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs23">;
def int_haydn_fmula16_hs33 : haydn_i64_from_v4i16_mac<"haydn_fmula16_hs33">;
def int_haydn_fmula16_ls00 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls00">;
def int_haydn_fmula16_ls01 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls01">;
def int_haydn_fmula16_ls02 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls02">;
def int_haydn_fmula16_ls03 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls03">;
def int_haydn_fmula16_ls11 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls11">;
def int_haydn_fmula16_ls12 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls12">;
def int_haydn_fmula16_ls13 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls13">;
def int_haydn_fmula16_ls22 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls22">;
def int_haydn_fmula16_ls23 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls23">;
def int_haydn_fmula16_ls33 : haydn_i64_from_v4i16_mac<"haydn_fmula16_ls33">;
def int_haydn_fmulaa32s_hhll : haydn_i64_from_v2i32_mac<"haydn_fmulaa32s_hhll">;
def int_haydn_fmulaa32s_hllh : haydn_i64_from_v2i32_mac<"haydn_fmulaa32s_hllh">;
def int_haydn_fmulss32s_hhll : haydn_ternary_intrinsic<"haydn_fmulss32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulss32s_hllh : haydn_ternary_intrinsic<"haydn_fmulss32s_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_hs_11_00 : haydn_binary_intrinsic<"haydn_fmulzaa16_hs_11_00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_hs_13_02 : haydn_binary_intrinsic<"haydn_fmulzaa16_hs_13_02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_hs_33_22 : haydn_binary_intrinsic<"haydn_fmulzaa16_hs_33_22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_ls_11_00 : haydn_binary_intrinsic<"haydn_fmulzaa16_ls_11_00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_ls_13_02 : haydn_binary_intrinsic<"haydn_fmulzaa16_ls_13_02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa16_ls_33_22 : haydn_binary_intrinsic<"haydn_fmulzaa16_ls_33_22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32s_hhll : haydn_binary_intrinsic<"haydn_fmulzaa32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32s_hllh : haydn_binary_intrinsic<"haydn_fmulzaa32s_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzas32s_hhll : haydn_binary_intrinsic<"haydn_fmulzas32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzas32s_hllh : haydn_binary_intrinsic<"haydn_fmulzas32s_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzsa32s_hhll : haydn_binary_intrinsic<"haydn_fmulzsa32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzsa32s_hllh : haydn_binary_intrinsic<"haydn_fmulzsa32s_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_hs_11_00 : haydn_binary_intrinsic<"haydn_fmulzss16_hs_11_00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_hs_13_02 : haydn_binary_intrinsic<"haydn_fmulzss16_hs_13_02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_hs_33_22 : haydn_binary_intrinsic<"haydn_fmulzss16_hs_33_22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_ls_11_00 : haydn_binary_intrinsic<"haydn_fmulzss16_ls_11_00", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_ls_13_02 : haydn_binary_intrinsic<"haydn_fmulzss16_ls_13_02", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss16_ls_33_22 : haydn_binary_intrinsic<"haydn_fmulzss16_ls_33_22", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss32s_hhll : haydn_binary_intrinsic<"haydn_fmulzss32s_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss32s_hllh : haydn_binary_intrinsic<"haydn_fmulzss32s_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul16aq : haydn_ternary_intrinsic<"haydn_mul16aq", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul16zaq : haydn_binary_intrinsic<"haydn_mul16zaq", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul64_hh : haydn_i64_from_v2i32_binary<"haydn_mul64_hh">;
def int_haydn_mul64_hl : haydn_i64_from_v2i32_binary<"haydn_mul64_hl">;
def int_haydn_mul64_huh : haydn_i64_from_v2i32_binary<"haydn_mul64_huh">;
def int_haydn_mul64_hul : haydn_i64_from_v2i32_binary<"haydn_mul64_hul">;
def int_haydn_mul64_lh : haydn_i64_from_v2i32_binary<"haydn_mul64_lh">;
def int_haydn_mul64_luh : haydn_i64_from_v2i32_binary<"haydn_mul64_luh">;
def int_haydn_mul64_lul : haydn_i64_from_v2i32_binary<"haydn_mul64_lul">;
def int_haydn_mul64_uhh : haydn_i64_from_v2i32_binary<"haydn_mul64_uhh">;
def int_haydn_mul64_uhl : haydn_i64_from_v2i32_binary<"haydn_mul64_uhl">;
def int_haydn_mul64_uhuh : haydn_i64_from_v2i32_binary<"haydn_mul64_uhuh">;
def int_haydn_mul64_uhul : haydn_i64_from_v2i32_binary<"haydn_mul64_uhul">;
def int_haydn_mul64_ulh : haydn_i64_from_v2i32_binary<"haydn_mul64_ulh">;
def int_haydn_mul64_ull : haydn_i64_from_v2i32_binary<"haydn_mul64_ull">;
def int_haydn_mul64_uluh : haydn_i64_from_v2i32_binary<"haydn_mul64_uluh">;
def int_haydn_mul64_ulul : haydn_i64_from_v2i32_binary<"haydn_mul64_ulul">;
def int_haydn_mula64_hh : haydn_i64_from_v2i32_mac<"haydn_mula64_hh">;
def int_haydn_mula64_hl : haydn_i64_from_v2i32_mac<"haydn_mula64_hl">;
def int_haydn_mula64_huh : haydn_i64_from_v2i32_mac<"haydn_mula64_huh">;
def int_haydn_mula64_hul : haydn_i64_from_v2i32_mac<"haydn_mula64_hul">;
def int_haydn_mula64_lh : haydn_i64_from_v2i32_mac<"haydn_mula64_lh">;
def int_haydn_mula64_ll : haydn_i64_from_v2i32_mac<"haydn_mula64_ll">;
def int_haydn_mula64_luh : haydn_i64_from_v2i32_mac<"haydn_mula64_luh">;
def int_haydn_mula64_lul : haydn_i64_from_v2i32_mac<"haydn_mula64_lul">;
def int_haydn_mula64_uhh : haydn_i64_from_v2i32_mac<"haydn_mula64_uhh">;
def int_haydn_mula64_uhl : haydn_i64_from_v2i32_mac<"haydn_mula64_uhl">;
def int_haydn_mula64_uhuh : haydn_i64_from_v2i32_mac<"haydn_mula64_uhuh">;
def int_haydn_mula64_uhul : haydn_i64_from_v2i32_mac<"haydn_mula64_uhul">;
def int_haydn_mula64_ulh : haydn_i64_from_v2i32_mac<"haydn_mula64_ulh">;
def int_haydn_mula64_ull : haydn_i64_from_v2i32_mac<"haydn_mula64_ull">;
def int_haydn_mula64_uluh : haydn_i64_from_v2i32_mac<"haydn_mula64_uluh">;
def int_haydn_mula64_ulul : haydn_i64_from_v2i32_mac<"haydn_mula64_ulul">;
def int_haydn_mulaa32_hhll : haydn_i64_from_v2i32_mac<"haydn_mulaa32_hhll">;
def int_haydn_mulaa32_hllh : haydn_i64_from_v2i32_mac<"haydn_mulaa32_hllh">;
def int_haydn_mulas32_hhll : haydn_ternary_intrinsic<"haydn_mulas32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas32_hllh : haydn_ternary_intrinsic<"haydn_mulas32_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_hh : haydn_ternary_intrinsic<"haydn_mulas64_hh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_hl : haydn_ternary_intrinsic<"haydn_mulas64_hl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_huh : haydn_ternary_intrinsic<"haydn_mulas64_huh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_hul : haydn_ternary_intrinsic<"haydn_mulas64_hul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_lh : haydn_ternary_intrinsic<"haydn_mulas64_lh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ll : haydn_ternary_intrinsic<"haydn_mulas64_ll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_luh : haydn_ternary_intrinsic<"haydn_mulas64_luh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_lul : haydn_ternary_intrinsic<"haydn_mulas64_lul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uhh : haydn_ternary_intrinsic<"haydn_mulas64_uhh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uhl : haydn_ternary_intrinsic<"haydn_mulas64_uhl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uhuh : haydn_ternary_intrinsic<"haydn_mulas64_uhuh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uhul : haydn_ternary_intrinsic<"haydn_mulas64_uhul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ulh : haydn_ternary_intrinsic<"haydn_mulas64_ulh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ull : haydn_ternary_intrinsic<"haydn_mulas64_ull", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_uluh : haydn_ternary_intrinsic<"haydn_mulas64_uluh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulas64_ulul : haydn_ternary_intrinsic<"haydn_mulas64_ulul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_muls64_hh : haydn_i64_from_v2i32_mac<"haydn_muls64_hh">;
def int_haydn_muls64_hl : haydn_i64_from_v2i32_mac<"haydn_muls64_hl">;
def int_haydn_muls64_huh : haydn_i64_from_v2i32_mac<"haydn_muls64_huh">;
def int_haydn_muls64_hul : haydn_i64_from_v2i32_mac<"haydn_muls64_hul">;
def int_haydn_muls64_lh : haydn_i64_from_v2i32_mac<"haydn_muls64_lh">;
def int_haydn_muls64_ll : haydn_i64_from_v2i32_mac<"haydn_muls64_ll">;
def int_haydn_muls64_luh : haydn_i64_from_v2i32_mac<"haydn_muls64_luh">;
def int_haydn_muls64_lul : haydn_i64_from_v2i32_mac<"haydn_muls64_lul">;
def int_haydn_muls64_uhh : haydn_i64_from_v2i32_mac<"haydn_muls64_uhh">;
def int_haydn_muls64_uhl : haydn_i64_from_v2i32_mac<"haydn_muls64_uhl">;
def int_haydn_muls64_uhuh : haydn_i64_from_v2i32_mac<"haydn_muls64_uhuh">;
def int_haydn_muls64_uhul : haydn_i64_from_v2i32_mac<"haydn_muls64_uhul">;
def int_haydn_muls64_ulh : haydn_i64_from_v2i32_mac<"haydn_muls64_ulh">;
def int_haydn_muls64_ull : haydn_i64_from_v2i32_mac<"haydn_muls64_ull">;
def int_haydn_muls64_uluh : haydn_i64_from_v2i32_mac<"haydn_muls64_uluh">;
def int_haydn_muls64_ulul : haydn_i64_from_v2i32_mac<"haydn_muls64_ulul">;
def int_haydn_mulss64_hh : haydn_ternary_intrinsic<"haydn_mulss64_hh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_hl : haydn_ternary_intrinsic<"haydn_mulss64_hl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_huh : haydn_ternary_intrinsic<"haydn_mulss64_huh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_hul : haydn_ternary_intrinsic<"haydn_mulss64_hul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_lh : haydn_ternary_intrinsic<"haydn_mulss64_lh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ll : haydn_ternary_intrinsic<"haydn_mulss64_ll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_luh : haydn_ternary_intrinsic<"haydn_mulss64_luh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_lul : haydn_ternary_intrinsic<"haydn_mulss64_lul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uhh : haydn_ternary_intrinsic<"haydn_mulss64_uhh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uhl : haydn_ternary_intrinsic<"haydn_mulss64_uhl", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uhuh : haydn_ternary_intrinsic<"haydn_mulss64_uhuh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uhul : haydn_ternary_intrinsic<"haydn_mulss64_uhul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ulh : haydn_ternary_intrinsic<"haydn_mulss64_ulh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ull : haydn_ternary_intrinsic<"haydn_mulss64_ull", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_uluh : haydn_ternary_intrinsic<"haydn_mulss64_uluh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulss64_ulul : haydn_ternary_intrinsic<"haydn_mulss64_ulul", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32_hhll : haydn_binary_intrinsic<"haydn_mulzaa32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32_hllh : haydn_binary_intrinsic<"haydn_mulzaa32_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzas32_hhll : haydn_binary_intrinsic<"haydn_mulzas32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzas32_hllh : haydn_binary_intrinsic<"haydn_mulzas32_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzsa32_hhll : haydn_binary_intrinsic<"haydn_mulzsa32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzsa32_hllh : haydn_binary_intrinsic<"haydn_mulzsa32_hllh", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzss32_hhll : haydn_binary_intrinsic<"haydn_mulzss32_hhll", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzss32_hllh : haydn_binary_intrinsic<"haydn_mulzss32_hllh", llvm_i64_ty, llvm_i64_ty>;



//===----------------------------------------------------------------------===//
// D208 Phase 3A A5 gap fixes — 3 builtins that had no backing IR intrinsic.
// The machine instructions existed (HaydnInstrInfo*.td) but no int_haydn_* was
// declared, so auto-map failed and EmitHaydnBuiltinExpr returned nullptr.
//===----------------------------------------------------------------------===//

// ARCTAN rt, rsd, uimm4 — GPR32 dest, DR64 source, 16-bit imm. Custom form
// (cross-bank DR64->GPR32 + immediate). RT = SAT32(arctan(rsd) * uimm4).
def int_haydn_arctan : Haydn_Intrinsic<"haydn_arctan",
    [llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// POPCOUNT32 rt, rs1 — GPR32 dest + GPR32 source (FmtALU32Unary).
def int_haydn_popcount32 : haydn_unary_intrinsic<"haydn_popcount32", llvm_i32_ty, llvm_i32_ty>;

// POPCOUNT64 rt, rsd — GPR32 dest + DR64 source (cross-bank popcount).
def int_haydn_popcount64 : Haydn_Intrinsic<"haydn_popcount64",
    [llvm_i32_ty], [llvm_i64_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===----------------------------------------------------------------------===
// Golden D-ALU + LS coverage (generated)
// Keep inside TargetPrefix = "haydn" (do not close the block above).
//===----------------------------------------------------------------------===

def int_haydn_add64 : haydn_binary_intrinsic<"haydn_add64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64 : haydn_binary_intrinsic<"haydn_sub64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_xor64 : haydn_binary_intrinsic<"haydn_xor64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_add64s_h : haydn_binary_intrinsic<"haydn_add64s_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_add64s_l : haydn_binary_intrinsic<"haydn_add64s_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64s_h : haydn_binary_intrinsic<"haydn_sub64s_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sub64s_l : haydn_binary_intrinsic<"haydn_sub64s_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_move64 : haydn_unary_intrinsic<"haydn_move64", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_transf64_h : haydn_unary_intrinsic<"haydn_transf64_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_transf64_l : haydn_unary_intrinsic<"haydn_transf64_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_sext32t64 : Haydn_Intrinsic<"haydn_sext32t64",
    [llvm_i64_ty], [llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_transf64 : Haydn_Intrinsic<"haydn_transf64",
    [llvm_i64_ty], [llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_transf64f2 : Haydn_Intrinsic<"haydn_transf64f2",
    [llvm_i64_ty], [llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_movei_h : Haydn_Intrinsic<"haydn_movei_h",
    [llvm_i64_ty], [llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_movei_l : Haydn_Intrinsic<"haydn_movei_l",
    [llvm_i64_ty], [llvm_i32_ty],
    [ImmArg<ArgIndex<0>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_zero_dr : Haydn_Intrinsic<"haydn_zero_dr",
    [llvm_i64_ty], [],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_sin_cos : Haydn_Intrinsic<"haydn_sin_cos",
    [llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
// C2.3 / G-MEM-INTRIN: Golden LS ordinary Mem — IntrArgMemOnly+NoCapture so
// AA sees argmem only (co-issue-ready). MMO flags are MOLoad/MOStore (not
// volatile); CB/UA StateMem use MOVolatile in getTgtMemIntrinsic.
//
// IntrHasSideEffects is for hidden AR/CBR/SFR only (UA/CB/BREV above).
// Ordinary POST/PRE/WITH writeback is SSA {data, new_ptr} plus argmem.
// These defs still carry IntrHasSideEffects as a QoR residual: a fully
// dead post-inc load cannot DCE. Dropping the flag on this family is
// the remaining F37 action; do not drop it on UA/CB/BREV.
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_ldw_post_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_ldw_post_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_ldw_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_ldw_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_ldw_with_imm : Haydn_Intrinsic<"haydn_d_ldw_with_imm",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_ldw_with_reg : Haydn_Intrinsic<"haydn_d_ldw_with_reg",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lhw_post_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lhw_post_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lhw_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lhw_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_lhw_with_imm : Haydn_Intrinsic<"haydn_d_lhw_with_imm",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_lhw_with_reg : Haydn_Intrinsic<"haydn_d_lhw_with_reg",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lw_post_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lw_post_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lw_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_d_lw_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i64_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_lw_with_imm : Haydn_Intrinsic<"haydn_d_lw_with_imm",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_lw_with_reg : Haydn_Intrinsic<"haydn_d_lw_with_reg",
    [llvm_i64_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_post_imm : Haydn_Intrinsic<"haydn_d_sdw_post_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_post_reg : Haydn_Intrinsic<"haydn_d_sdw_post_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_pre_imm : Haydn_Intrinsic<"haydn_d_sdw_pre_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_pre_reg : Haydn_Intrinsic<"haydn_d_sdw_pre_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_with_imm : Haydn_Intrinsic<"haydn_d_sdw_with_imm",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sdw_with_reg : Haydn_Intrinsic<"haydn_d_sdw_with_reg",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_post_imm : Haydn_Intrinsic<"haydn_d_shw_post_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_post_reg : Haydn_Intrinsic<"haydn_d_shw_post_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_pre_imm : Haydn_Intrinsic<"haydn_d_shw_pre_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_pre_reg : Haydn_Intrinsic<"haydn_d_shw_pre_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_with_imm : Haydn_Intrinsic<"haydn_d_shw_with_imm",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_shw_with_reg : Haydn_Intrinsic<"haydn_d_shw_with_reg",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_post_imm : Haydn_Intrinsic<"haydn_d_sw_h_post_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_post_reg : Haydn_Intrinsic<"haydn_d_sw_h_post_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_pre_imm : Haydn_Intrinsic<"haydn_d_sw_h_pre_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_pre_reg : Haydn_Intrinsic<"haydn_d_sw_h_pre_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_with_imm : Haydn_Intrinsic<"haydn_d_sw_h_with_imm",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_h_with_reg : Haydn_Intrinsic<"haydn_d_sw_h_with_reg",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_post_imm : Haydn_Intrinsic<"haydn_d_sw_l_post_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_post_reg : Haydn_Intrinsic<"haydn_d_sw_l_post_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_pre_imm : Haydn_Intrinsic<"haydn_d_sw_l_pre_imm",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_pre_reg : Haydn_Intrinsic<"haydn_d_sw_l_pre_reg",
    [llvm_ptr_ty], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_with_imm : Haydn_Intrinsic<"haydn_d_sw_l_with_imm",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_d_sw_l_with_reg : Haydn_Intrinsic<"haydn_d_sw_l_with_reg",
    [], [llvm_i64_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbs_post_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbs_post_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbs_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbs_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lbs_with_imm : Haydn_Intrinsic<"haydn_s_lbs_with_imm",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lbs_with_reg : Haydn_Intrinsic<"haydn_s_lbs_with_reg",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbu_post_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbu_post_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbu_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lbu_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lbu_with_imm : Haydn_Intrinsic<"haydn_s_lbu_with_imm",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lbu_with_reg : Haydn_Intrinsic<"haydn_s_lbu_with_reg",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhws_post_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhws_post_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhws_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhws_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lhws_with_imm : Haydn_Intrinsic<"haydn_s_lhws_with_imm",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lhws_with_reg : Haydn_Intrinsic<"haydn_s_lhws_with_reg",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhwu_post_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhwu_post_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhwu_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lhwu_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lhwu_with_imm : Haydn_Intrinsic<"haydn_s_lhwu_with_imm",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lhwu_with_reg : Haydn_Intrinsic<"haydn_s_lhwu_with_reg",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lw_post_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lw_post_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lw_pre_imm : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn, ImmArg<ArgIndex<1>>]>;
// Multi-result AGU writeback load: {data, new_ptr} = op(base, off).
// No ClangBuiltin auto-map — frexp _pair builtin (EmitHaydnBuiltinExpr).
def int_haydn_s_lw_pre_reg : DefaultAttrsIntrinsic<
    [llvm_i32_ty, llvm_ptr_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lw_with_imm : Haydn_Intrinsic<"haydn_s_lw_with_imm",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<1>>, IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_lw_with_reg : Haydn_Intrinsic<"haydn_s_lw_with_reg",
    [llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
    [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_post_imm : Haydn_Intrinsic<"haydn_s_sb_post_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_post_reg : Haydn_Intrinsic<"haydn_s_sb_post_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_pre_imm : Haydn_Intrinsic<"haydn_s_sb_pre_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_pre_reg : Haydn_Intrinsic<"haydn_s_sb_pre_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_with_imm : Haydn_Intrinsic<"haydn_s_sb_with_imm",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sb_with_reg : Haydn_Intrinsic<"haydn_s_sb_with_reg",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_post_imm : Haydn_Intrinsic<"haydn_s_shw_post_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_post_reg : Haydn_Intrinsic<"haydn_s_shw_post_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_pre_imm : Haydn_Intrinsic<"haydn_s_shw_pre_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_pre_reg : Haydn_Intrinsic<"haydn_s_shw_pre_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_with_imm : Haydn_Intrinsic<"haydn_s_shw_with_imm",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_shw_with_reg : Haydn_Intrinsic<"haydn_s_shw_with_reg",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_post_imm : Haydn_Intrinsic<"haydn_s_sw_post_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_post_reg : Haydn_Intrinsic<"haydn_s_sw_post_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_pre_imm : Haydn_Intrinsic<"haydn_s_sw_pre_imm",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_pre_reg : Haydn_Intrinsic<"haydn_s_sw_pre_reg",
    [llvm_ptr_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_with_imm : Haydn_Intrinsic<"haydn_s_sw_with_imm",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [ImmArg<ArgIndex<2>>, IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;
def int_haydn_s_sw_with_reg : Haydn_Intrinsic<"haydn_s_sw_with_reg",
    [], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn]>;

} // TargetPrefix = "haydn"

//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// 32X16 multiply/MAC family — golden v2_1 (2026-08-18)
//
// Pure forms: binary (a, b) -> i64. Accumulating forms (golden DR write
// alias also read): ternary (acc, a, b) -> i64; logical def ties dest to
// the acc operand per the golden accumulator law (CB-152c).
//===----------------------------------------------------------------------===//

def int_haydn_fmul32x16_h0 : haydn_binary_intrinsic<"haydn_fmul32x16_h0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_h1 : haydn_binary_intrinsic<"haydn_fmul32x16_h1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_h2 : haydn_binary_intrinsic<"haydn_fmul32x16_h2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_h3 : haydn_binary_intrinsic<"haydn_fmul32x16_h3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_l0 : haydn_binary_intrinsic<"haydn_fmul32x16_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_l1 : haydn_binary_intrinsic<"haydn_fmul32x16_l1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_l2 : haydn_binary_intrinsic<"haydn_fmul32x16_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmul32x16_l3 : haydn_binary_intrinsic<"haydn_fmul32x16_l3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32x16_h0_l1 : haydn_binary_intrinsic<"haydn_fmulzaa32x16_h0_l1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32x16_h1_l0 : haydn_binary_intrinsic<"haydn_fmulzaa32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32x16_h2_l3 : haydn_binary_intrinsic<"haydn_fmulzaa32x16_h2_l3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzaa32x16_h3_l2 : haydn_binary_intrinsic<"haydn_fmulzaa32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzas32x16_h1_l0 : haydn_binary_intrinsic<"haydn_fmulzas32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzas32x16_h3_l2 : haydn_binary_intrinsic<"haydn_fmulzas32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzsa32x16_h1_l0 : haydn_binary_intrinsic<"haydn_fmulzsa32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzsa32x16_h3_l2 : haydn_binary_intrinsic<"haydn_fmulzsa32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss32x16_h1_l0 : haydn_binary_intrinsic<"haydn_fmulzss32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_fmulzss32x16_h3_l2 : haydn_binary_intrinsic<"haydn_fmulzss32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_h0 : haydn_binary_intrinsic<"haydn_mul32x16_h0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_h1 : haydn_binary_intrinsic<"haydn_mul32x16_h1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_h2 : haydn_binary_intrinsic<"haydn_mul32x16_h2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_h3 : haydn_binary_intrinsic<"haydn_mul32x16_h3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_l0 : haydn_binary_intrinsic<"haydn_mul32x16_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_l1 : haydn_binary_intrinsic<"haydn_mul32x16_l1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_l2 : haydn_binary_intrinsic<"haydn_mul32x16_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mul32x16_l3 : haydn_binary_intrinsic<"haydn_mul32x16_l3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32x16_h0_l1 : haydn_binary_intrinsic<"haydn_mulzaa32x16_h0_l1", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32x16_h1_l0 : haydn_binary_intrinsic<"haydn_mulzaa32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32x16_h2_l3 : haydn_binary_intrinsic<"haydn_mulzaa32x16_h2_l3", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzaa32x16_h3_l2 : haydn_binary_intrinsic<"haydn_mulzaa32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzas32x16_h1_l0 : haydn_binary_intrinsic<"haydn_mulzas32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzas32x16_h3_l2 : haydn_binary_intrinsic<"haydn_mulzas32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzsa32x16_h1_l0 : haydn_binary_intrinsic<"haydn_mulzsa32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzsa32x16_h3_l2 : haydn_binary_intrinsic<"haydn_mulzsa32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzss32x16_h1_l0 : haydn_binary_intrinsic<"haydn_mulzss32x16_h1_l0", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_mulzss32x16_h3_l2 : haydn_binary_intrinsic<"haydn_mulzss32x16_h3_l2", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2cmul32x16_h : haydn_binary_intrinsic<"haydn_x2cmul32x16_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2cmul32x16_l : haydn_binary_intrinsic<"haydn_x2cmul32x16_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fcmul32x16rs_h : haydn_binary_intrinsic<"haydn_x2fcmul32x16rs_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fcmul32x16rs_l : haydn_binary_intrinsic<"haydn_x2fcmul32x16rs_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16rss_h : haydn_binary_intrinsic<"haydn_x2fmul32x16rss_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16rss_l : haydn_binary_intrinsic<"haydn_x2fmul32x16rss_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16rs_h : haydn_binary_intrinsic<"haydn_x2fmul32x16rs_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16rs_l : haydn_binary_intrinsic<"haydn_x2fmul32x16rs_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16ts_h : haydn_binary_intrinsic<"haydn_x2fmul32x16ts_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2fmul32x16ts_l : haydn_binary_intrinsic<"haydn_x2fmul32x16ts_l", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2mul32x16_h : haydn_binary_intrinsic<"haydn_x2mul32x16_h", llvm_i64_ty, llvm_i64_ty>;
def int_haydn_x2mul32x16_l : haydn_binary_intrinsic<"haydn_x2mul32x16_l", llvm_i64_ty, llvm_i64_ty>;

def int_haydn_fmula32x16_h0 : Haydn_Intrinsic<"haydn_fmula32x16_h0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_h1 : Haydn_Intrinsic<"haydn_fmula32x16_h1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_h2 : Haydn_Intrinsic<"haydn_fmula32x16_h2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_h3 : Haydn_Intrinsic<"haydn_fmula32x16_h3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_l0 : Haydn_Intrinsic<"haydn_fmula32x16_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_l1 : Haydn_Intrinsic<"haydn_fmula32x16_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_l2 : Haydn_Intrinsic<"haydn_fmula32x16_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmula32x16_l3 : Haydn_Intrinsic<"haydn_fmula32x16_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulaa32x16_h0_l1 : Haydn_Intrinsic<"haydn_fmulaa32x16_h0_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulaa32x16_h1_l0 : Haydn_Intrinsic<"haydn_fmulaa32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulaa32x16_h2_l3 : Haydn_Intrinsic<"haydn_fmulaa32x16_h2_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulaa32x16_h3_l2 : Haydn_Intrinsic<"haydn_fmulaa32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulas32x16_h1_l0 : Haydn_Intrinsic<"haydn_fmulas32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulas32x16_h3_l2 : Haydn_Intrinsic<"haydn_fmulas32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_h0 : Haydn_Intrinsic<"haydn_fmuls32x16_h0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_h1 : Haydn_Intrinsic<"haydn_fmuls32x16_h1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_h2 : Haydn_Intrinsic<"haydn_fmuls32x16_h2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_h3 : Haydn_Intrinsic<"haydn_fmuls32x16_h3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_l0 : Haydn_Intrinsic<"haydn_fmuls32x16_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_l1 : Haydn_Intrinsic<"haydn_fmuls32x16_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_l2 : Haydn_Intrinsic<"haydn_fmuls32x16_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmuls32x16_l3 : Haydn_Intrinsic<"haydn_fmuls32x16_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulsa32x16_h1_l0 : Haydn_Intrinsic<"haydn_fmulsa32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulsa32x16_h3_l2 : Haydn_Intrinsic<"haydn_fmulsa32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulss32x16_h1_l0 : Haydn_Intrinsic<"haydn_fmulss32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_fmulss32x16_h3_l2 : Haydn_Intrinsic<"haydn_fmulss32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_h0 : Haydn_Intrinsic<"haydn_mula32x16_h0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_h1 : Haydn_Intrinsic<"haydn_mula32x16_h1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_h2 : Haydn_Intrinsic<"haydn_mula32x16_h2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_h3 : Haydn_Intrinsic<"haydn_mula32x16_h3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_l0 : Haydn_Intrinsic<"haydn_mula32x16_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_l1 : Haydn_Intrinsic<"haydn_mula32x16_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_l2 : Haydn_Intrinsic<"haydn_mula32x16_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mula32x16_l3 : Haydn_Intrinsic<"haydn_mula32x16_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulaa32x16_h0_l1 : Haydn_Intrinsic<"haydn_mulaa32x16_h0_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulaa32x16_h1_l0 : Haydn_Intrinsic<"haydn_mulaa32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulaa32x16_h2_l3 : Haydn_Intrinsic<"haydn_mulaa32x16_h2_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulaa32x16_h3_l2 : Haydn_Intrinsic<"haydn_mulaa32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulas32x16_h1_l0 : Haydn_Intrinsic<"haydn_mulas32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulas32x16_h3_l2 : Haydn_Intrinsic<"haydn_mulas32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_h0 : Haydn_Intrinsic<"haydn_muls32x16_h0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_h1 : Haydn_Intrinsic<"haydn_muls32x16_h1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_h2 : Haydn_Intrinsic<"haydn_muls32x16_h2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_h3 : Haydn_Intrinsic<"haydn_muls32x16_h3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_l0 : Haydn_Intrinsic<"haydn_muls32x16_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_l1 : Haydn_Intrinsic<"haydn_muls32x16_l1", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_l2 : Haydn_Intrinsic<"haydn_muls32x16_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_muls32x16_l3 : Haydn_Intrinsic<"haydn_muls32x16_l3", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulsa32x16_h1_l0 : Haydn_Intrinsic<"haydn_mulsa32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulsa32x16_h3_l2 : Haydn_Intrinsic<"haydn_mulsa32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulss32x16_h1_l0 : Haydn_Intrinsic<"haydn_mulss32x16_h1_l0", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_mulss32x16_h3_l2 : Haydn_Intrinsic<"haydn_mulss32x16_h3_l2", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2cmula32x16_h : Haydn_Intrinsic<"haydn_x2cmula32x16_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2cmula32x16_l : Haydn_Intrinsic<"haydn_x2cmula32x16_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fcmula32x16rs_h : Haydn_Intrinsic<"haydn_x2fcmula32x16rs_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fcmula32x16rs_l : Haydn_Intrinsic<"haydn_x2fcmula32x16rs_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16rss_h : Haydn_Intrinsic<"haydn_x2fmula32x16rss_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16rss_l : Haydn_Intrinsic<"haydn_x2fmula32x16rss_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16rs_h : Haydn_Intrinsic<"haydn_x2fmula32x16rs_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16rs_l : Haydn_Intrinsic<"haydn_x2fmula32x16rs_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16ts_h : Haydn_Intrinsic<"haydn_x2fmula32x16ts_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmula32x16ts_l : Haydn_Intrinsic<"haydn_x2fmula32x16ts_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16rss_h : Haydn_Intrinsic<"haydn_x2fmuls32x16rss_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16rss_l : Haydn_Intrinsic<"haydn_x2fmuls32x16rss_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16rs_h : Haydn_Intrinsic<"haydn_x2fmuls32x16rs_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16rs_l : Haydn_Intrinsic<"haydn_x2fmuls32x16rs_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16ts_h : Haydn_Intrinsic<"haydn_x2fmuls32x16ts_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2fmuls32x16ts_l : Haydn_Intrinsic<"haydn_x2fmuls32x16ts_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2mula32x16_h : Haydn_Intrinsic<"haydn_x2mula32x16_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2mula32x16_l : Haydn_Intrinsic<"haydn_x2mula32x16_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2muls32x16_h : Haydn_Intrinsic<"haydn_x2muls32x16_h", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_haydn_x2muls32x16_l : Haydn_Intrinsic<"haydn_x2muls32x16_l", [llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty], [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
