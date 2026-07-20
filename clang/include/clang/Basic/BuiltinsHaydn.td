//==- BuiltinsHaydn.td - Haydn Builtin function database ---*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Haydn DSP-specific builtin function database.
// Every def NAME must exactly match the corresponding LLVM intrinsic suffix
// (after "llvm.haydn.") so that ClangBuiltin<> auto-mapping works.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class HaydnBuiltin<string prototype, string features = ""> : TargetBuiltin {
  let Spellings = ["__builtin_haydn_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

// D208: HiFi-native AE_* builtins. These expose the NatureDSP/HiFi AE_*
// intrinsic names directly as Haydn target builtins, retiring the haydn_dsp.h
// macro compat layer. The spelling is `__builtin_ae_<NAME>` (matching HiFi's
// naming), gated to the Haydn target by the per-target builtin scoping in
// Builtin::Context::InitializeTarget. No collision risk: no other clang
// target defines `__builtin_ae_*` (verified across Builtins*.td).
//
// Each def NAME must match the corresponding LLVM intrinsic suffix so the
// ClangBuiltin<> auto-mapping in CGBuiltin.cpp:6417-6499 handles lowering
// without per-builtin CodeGen. Composed/load-store builtins that need custom
// IR expansion are handled in EmitHaydnBuiltinExpr
// (clang/lib/CodeGen/TargetBuiltins/Haydn.cpp).
class HaydnAeBuiltin<string prototype, string features = ""> : TargetBuiltin {
  let Spellings = ["__builtin_ae_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

// D400 Path B: 2-dest SIMD MAC builtins. These ops produce TWO i64 results
// (golden slot1_mac_instruction_list.json: DR_Write_Port:[rtd1,rtd2]); the
// LLVM intrinsics return `{i64, i64}`. The clang Prototype grammar has no
// struct return and the generic ClangBuiltin<> auto-map in CGBuiltin.cpp
// cannot lower multi-result intrinsics (no extractvalue logic — see
// CGBuiltin.cpp:6434-6505). These builtins therefore use the frexp pattern
// (returns rtd1, writes rtd2 through an out-pointer) and are spelled
// `__builtin_haydn_<NAME>_pair`. They are NOT ClangBuiltin<>-annotated on
// the clang side, so getIntrinsicForClangBuiltin returns not_intrinsic for
// them and control falls through to EmitHaydnBuiltinExpr in Haydn.cpp,
// which emits the 2-result intrinsic call + CreateExtractValue for each
// field. The `_pair` suffix on the spelling ensures that even though the
// underlying LLVM intrinsic (int_haydn_<NAME>) carries a ClangBuiltin<>
// annotation for `__builtin_haydn_<NAME>`, the differently-named clang
// builtin (`__builtin_haydn_<NAME>_pair`) does NOT collide with that map.
class HaydnPairBuiltin<string prototype, string features = ""> : TargetBuiltin {
  let Spellings = ["__builtin_haydn_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

let Attributes = [NoThrow, Const] in {

//===----------------------------------------------------------------------===//
// 64-bit Multiply (MUL64) -- 16 variants
// All: (DR64, DR64) -> DR64
//===----------------------------------------------------------------------===//

// Signed-signed: _ll, _lh, _hl, _hh
def mul64_ss_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ss_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ss_hl  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ss_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Signed-unsigned: _lul, _ulh, _uhl, _uhh
def mul64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Unsigned-signed: _luh, _uhuh, _hul, _uhul
def mul64_us_luh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_us_hul  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Unsigned-unsigned: _uluh, _ulul, _ull, _ulh
def mul64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uu_ull  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uu_ulh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate (MULA64) -- 16 variants
// All: (acc, src1, src2) -> result. Per spec, rtd is both an input (the
// accumulator) and the output. Only the 4 _ss_ variants are called from
// haydn_dsp.h's AE_MULA64_SS_* macros, so they are declared 3-arg here
// (acc, a, b). The other 12 (_su_/_us_/_uu_) stay 2-arg since no wrapper
// currently uses them; they will be promoted alongside their wrappers.
// See D100 for the accumulator-form rationale.
//===----------------------------------------------------------------------===//

// Signed-signed -- 3-arg accumulator form (acc, a, b) -> acc + a*b
def mula64_ss_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ss_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ss_hl  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ss_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Signed-unsigned
def mula64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-signed
def mula64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-unsigned
def mula64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract (MULS64) -- 16 variants
// _ss_ variants are 3-arg accumulator form (acc, a, b) -> acc - a*b (D100).
//===----------------------------------------------------------------------===//

// Signed-signed -- 3-arg accumulator form (acc, a, b) -> acc - a*b
def muls64_ss_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ss_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ss_hl  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ss_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Signed-unsigned
def muls64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-signed
def muls64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-unsigned
def muls64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate-Subtract (MULAS64) -- 16 variants
// _ss_ variants are 3-arg accumulator form (acc, a, b) per D100/D99.
//===----------------------------------------------------------------------===//

// Signed-signed -- 3-arg accumulator form (acc, a, b) -> acc +|+ a*b (saturating)
def mulas64_ss_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ss_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ss_hl  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ss_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Signed-unsigned
def mulas64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-signed
def mulas64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-unsigned
def mulas64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract-Subtract (MULSS64) -- 16 variants
// _ss_ variants are 3-arg accumulator form (acc, a, b) per D100/D99.
//===----------------------------------------------------------------------===//

// Signed-signed -- 3-arg accumulator form (acc, a, b) -> acc -|- a*b (saturating)
def mulss64_ss_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ss_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ss_hl  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ss_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Signed-unsigned
def mulss64_su_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_su_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_su_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_su_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-signed
def mulss64_us_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_us_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_us_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_us_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Unsigned-unsigned
def mulss64_uu_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uu_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uu_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uu_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Saturating Arithmetic (8 builtins -- matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def add32s  : HaydnBuiltin<"int(int, int)">;
def sub32s  : HaydnBuiltin<"int(int, int)">;
def add64s  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def sub64s  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def abs32s  : HaydnBuiltin<"int(int)">;
def neg32s  : HaydnBuiltin<"int(int)">;
def abs64s  : HaydnBuiltin<"int64_t(int64_t)">;
def neg64s  : HaydnBuiltin<"int64_t(int64_t)">;

//===----------------------------------------------------------------------===//
// Bit-Reversed Add (BREV32) -- FFT butterfly addressing
// Semantics: rt = bitreverse(bitreverse(rs1) + rs2)
// Maps 1:1 to NatureDSP AE_ADDBRBA32(idx, stride).
//===----------------------------------------------------------------------===//

def addbrba32 : HaydnBuiltin<"int(int, int)">;

//===----------------------------------------------------------------------===//
// Non-saturating 64-bit absolute/negate (from LLVM intrinsics)
//===----------------------------------------------------------------------===//

def abs64 : HaydnBuiltin<"int64_t(int64_t)">;
def neg64 : HaydnBuiltin<"int64_t(int64_t)">;

//===----------------------------------------------------------------------===//
// Fractional Multiply (9 builtins -- matched to LLVM intrinsics)
// FMULA32S_* / FMULS32S_* are accumulator-form (acc, a, b) per spec (D100):
//   FMULA32S_LL: rtd = SATQ1.63(rtd + SAT(rsd1[31:00] * rsd2[31:00]))
//   FMULS32S_LL: rtd = SATQ1.63(rtd - SAT(rsd1[31:00] * rsd2[31:00]))
// FMUL32S_* are pure 2-input multiplies (no accumulator read).
//===----------------------------------------------------------------------===//

def fmul32s_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul32s_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul32s_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmula32s_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula32s_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula32s_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmuls32s_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmuls32s_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmuls32s_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// FF2 Fractional Multiply with Symmetric Rounding (18 builtins)
// These are the critical DSP kernel intrinsics — FF2 family provides
// fractional 32x32->64 multiply with symmetric rounding + saturation.
//===----------------------------------------------------------------------===//

// Saturating with rounding
// FF2MUL*32RS_*: pure 2-input fractional multiply with round+saturate.
// FF2MULA*32RS_*: accumulator-form (acc, a, b) per spec (D100).
def ff2mul32rs_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mul32rs_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mul32rs_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mula32rs_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2mula32rs_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2mula32rs_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32rs_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32rs_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32rs_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// Non-saturating with rounding
// FF2MUL*32R_*: pure 2-input fractional multiply (no accumulator read).
// FF2MULA*32R_* / FF2MULS*32R_*: accumulator-form (acc, a, b) per spec —
// DB behavior reads rtd (slot-1 DR_Read_Port = [rsd1, rsd2, rtd]); widened
// from 2-arg to 3-arg in D187 to match the saturating siblings above.
def ff2mul32r_ll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mul32r_lh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mul32r_hh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def ff2mula32r_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2mula32r_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2mula32r_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32r_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32r_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def ff2muls32r_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Cross-Width 32x16 Fractional MAC with round + saturate
// Maps to llvm.haydn.mulfp32x16x2ras.{low,high}. Used by NatureDSP IIR/FIR
// kernels for cross-width gain scaling (e.g. AE_MULFP32X16X2RAS_{L,H}).
// Decomposed by the selector since Haydn has no native 32x16 fractional MAC.
//===----------------------------------------------------------------------===//
def mulfp32x16x2ras_low  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulfp32x16x2ras_high : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// 32-bit Multiply (high/low half) -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def mull   : HaydnBuiltin<"int(int, int)">;
def mulssh : HaydnBuiltin<"int(int, int)">;
def mulsuh : HaydnBuiltin<"int(int, int)">;
def muluuh : HaydnBuiltin<"unsigned int(unsigned int, unsigned int)">;

//===----------------------------------------------------------------------===//
// Q-format Multiply/Accumulate -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def mulq31 : HaydnBuiltin<"int(int, int, int)">;
def macq31 : HaydnBuiltin<"int(int, int, int)">;
def mulq63 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mac32  : HaydnBuiltin<"int(int, int, int)">;

//===----------------------------------------------------------------------===//
// SIMD X2 Operations (dual 32-bit) -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def x2add32s    : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sub32s    : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2addsub32s : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// SIMD X4 Operations (quad 16-bit) -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def x4add16s : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4sub16s : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// SIMD MAC -- D400 Path B (2-dest, golden-faithful).
//
// The 6 accumulator ops below (X2MULA32/X2MULS32/X4MULA16/X4MULS16/
// X4MULA16S/X4MULS16S) are TRUE 2-OUTPUT (golden slot1_mac_instruction_list.json:
// DR_Write_Port:[rtd1,rtd2]). Their LLVM intrinsics return [i64, i64]. The
// clang Prototype grammar has no struct return and the generic ClangBuiltin<>
// auto-map in CGBuiltin.cpp cannot lower multi-result intrinsics, so these
// use the frexp pattern: the `_pair` builtin returns rtd1 (high pair) and
// writes rtd2 (low pair) through an out-pointer. They are HaydnAeBuiltin
// (NOT ClangBuiltin<>-annotated), so control falls through to
// EmitHaydnBuiltinExpr in Haydn.cpp, which emits the 2-result intrinsic call
// + CreateExtractValue for each field. The C-level `__haydn_<op>` wrappers
// in haydn_intrin.h reconstruct a `haydn_dpair_t` from the two scalars.
// See D400 and EmitHaydnBuiltinExpr.
//===----------------------------------------------------------------------===//

// D_RRA2: (rtd2_out, acc1, acc2, src1, src2) -> rtd1. acc1/acc2 are the two
// 64-bit accumulators (tied to rtd1/rtd2 in the backend); src1/src2 are the
// multiplier sources.
def x2mula32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x2muls32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x4mula16_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x4muls16_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x4mula16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x4muls16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Transcendental Functions -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def log2  : HaydnBuiltin<"int(int)">;
def exp2  : HaydnBuiltin<"int(int)">;
def recip : HaydnBuiltin<"int(int)">;
def sqrt  : HaydnBuiltin<"int(int)">;

//===----------------------------------------------------------------------===//
// Normalization (NSA) -- matched to LLVM intrinsics
//===----------------------------------------------------------------------===//

def nsa32    : HaydnBuiltin<"int(int)">;
def nsau32   : HaydnBuiltin<"int(int)">;
def nsa64    : HaydnBuiltin<"int(int)">;
def nsa16_l  : HaydnBuiltin<"int(int)">;
def nsa32_l  : HaydnBuiltin<"int(int)">;
def nsaz64   : HaydnBuiltin<"int(int)">;
def nsaz16_l : HaydnBuiltin<"int(int)">;
def nsaz32_l : HaydnBuiltin<"int(int)">;

//===----------------------------------------------------------------------===//
// Dual 32-bit Multiply-Accumulate/Subtract (MULSA32/MULSS32)
// Matched to LLVM intrinsics int_haydn_mulsa32_* / int_haydn_mulss32_*
//===----------------------------------------------------------------------===//

def mulsa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulsa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulss32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulss32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Saturating shift-right and pack-shift-round
// Matched to LLVM intrinsics int_haydn_satsr64 / int_haydn_packsr32
//===----------------------------------------------------------------------===//

def satsr64  : HaydnBuiltin<"int(int64_t, int)">;
def packsr32 : HaydnBuiltin<"int(int64_t, int)">;

//===----------------------------------------------------------------------===//
// PACKSR32 lane family: AE_PKSR32-style dual-accumulator pack with shift+round.
// Matched to LLVM intrinsics int_haydn_packsr32x2_{hh,hl,lh,ll}.
//===----------------------------------------------------------------------===//

def packsr32x2_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int)">;
def packsr32x2_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int)">;
def packsr32x2_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int)">;
def packsr32x2_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int)">;

//===----------------------------------------------------------------------===//
// AE_MAXABS32S: per-lane saturating abs-max of two DR64 SIMD values.
// Matched to LLVM intrinsic int_haydn_maxabs32s.
//===----------------------------------------------------------------------===//

def maxabs32s : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 2: LC3 BASOP 16x16 fractional multiply (matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def fmul16_hs00       : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulaa16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulss16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_HS remaining variants (binary, matched to LLVM intrinsics)
// Q15*Q15->Q31 fractional multiply, high-half signed, lane-select suffixes.
//===----------------------------------------------------------------------===//

def fmul16_hs01 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs03 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs12 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs13 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs23 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_hs33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_LS variants (binary, matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def fmul16_ls00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls01 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls03 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls12 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls13 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls23 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmul16_ls33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 4: FMULS16 HS/LS variants (binary, matched to LLVM intrinsics)
// Q15*Q15->Q31 fractional multiply, both-lanes-select (single output).
//===----------------------------------------------------------------------===//

def fmuls16_hs00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs01 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs03 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs12 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs13 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs23 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_hs33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls01 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls03 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls12 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls13 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls23 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmuls16_ls33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 4: FMULAA16 HS/LS MAC variants (matched to LLVM intrinsics)
// Spec/DB: AA forms accumulate into rtd[63:32]. HS_11_00 and HS_33_22 are
// ternary Acc (selectAccMAC / FmtALU64Acc). Other AA lane pairs remain binary
// selection pending Acc migration (same silicon class).
//===----------------------------------------------------------------------===//

def fmulaa16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulaa16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulaa16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulaa16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulaa16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 4: FMULSS16 HS/LS MSU variants (binary, matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def fmulss16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulss16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulss16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulss16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulss16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 2: IIR biquad fused dual MAC (matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def f2mulaa32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulaa32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulss32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulss32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 2: Shift with rounding (matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def srai64r : HaydnBuiltin<"int64_t(int64_t, int)">;

//===----------------------------------------------------------------------===//
// Wave 2: Complex multiply (matched to LLVM intrinsics)
//===----------------------------------------------------------------------===//

def x4fcmul16rs   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4fcmula16rs  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">; // D211: ternary accumulator
def x4fcmul16rss  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4fcmula16rss : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">; // D211: ternary accumulator

//===----------------------------------------------------------------------===//
// Wave 3: 32-bit complex multiply -- D400 Path B (2-dest, golden-faithful).
// X2CMUL32/X2CMUL32S are TRUE 2-OUTPUT (golden: DR_Write_Port:[rtd1,rtd2]):
//   rtd1 = real = a·c - b·d ; rtd2 = imag = a·d + b·c.
// Frexp-pattern `_pair` builtins (see SIMD MAC block above for the rationale).
//===----------------------------------------------------------------------===//

def x2cmul32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;
def x2cmul32s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208 Phase 1: smula16/smula16s/smuls16/smuls16s 16-bit SIMD MAC family
// (10 missing builtins each, closing the intrinsic-coverage gap.)
// Each auto-maps to llvm.haydn.<name> via ClangBuiltin<>. Binary i64(i64,i64).
//===----------------------------------------------------------------------===//

def smula16_00  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_10  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_11  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_20  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_21  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_22  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_30  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_31  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16_33  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

def smula16s_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smula16s_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

def smuls16_00  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_10  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_11  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_20  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_21  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_22  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_30  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_31  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16_33  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

def smuls16s_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_10 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_11 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_20 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_21 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_30 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_31 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def smuls16s_33 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208 Phase 1: x2sel32 / x4cmul16s / x4cmula16s / x4cjmul16s / x4cjmula16s
// complex-multiply and lane-select families (closing intrinsic-coverage gap).
//===----------------------------------------------------------------------===//

// X4CMUL16S — quad 16-bit complex multiply (unary DR64: i64 -> i64)
def x4cmul16s    : HaydnBuiltin<"int64_t(int64_t)">;
def x4cmul16s_f2 : HaydnBuiltin<"int64_t(int64_t)">;

// X2SEL32 — dual 32-bit lane select (binary DR64: i64, i64 -> i64)
def x2sel32_hh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sel32_hl : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sel32_lh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sel32_ll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4CMUL16S_H/L — per-half complex multiply, saturating (binary DR64)
def x4cmul16s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4cmul16s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4CMULA16S_H/L — per-half complex MAC, saturating (binary DR64)
def x4cmula16s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4cmula16s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4CJMUL16S_H/L — per-half conjugated complex multiply, saturating (binary DR64)
def x4cjmul16s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4cjmul16s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4CJMULA16S_H/L — per-half conjugated complex MAC, saturating (binary DR64)
def x4cjmula16s_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4cjmula16s_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208 Phase 1: scalar 64-bit ALU/shift + f2mul/fmulas dual-MAC families
//===----------------------------------------------------------------------===//

// Scalar 64-bit ALU (binary i64)
def add64_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def add64_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def sub64_h : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def sub64_l : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def max64   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def min64   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Scalar 64-bit unary
def not64 : HaydnBuiltin<"int64_t(int64_t)">;

// 32x32->64 scalar multiply (i32 operands, i64 result)
def mul64_ll : HaydnBuiltin<"int64_t(int, int)">;

// Scalar 64-bit shifts -> 32-bit result
def sll64  : HaydnBuiltin<"int(int, int)">;
def and64  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def or64   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def sra64  : HaydnBuiltin<"int(int64_t, int)">;
def sra64r : HaydnBuiltin<"int(int64_t, int)">;
def srl64  : HaydnBuiltin<"int(int64_t, int)">;

// f2mul dual-MAC (binary i64 — accumulator implicit, IntrinsicsHaydn.td L699-708)
def f2mulas32r_hhll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulas32r_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulas32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulas32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulsa32r_hhll  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulsa32r_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulsa32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulsa32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Fractional add/subtract dual MAC, saturating (ternary i64 — acc explicit)
def fmulas32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulas32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulsa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulsa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208 Phase 1: X2/X4 SIMD families (retyped to vector types in bdd5d253b33f)
//===----------------------------------------------------------------------===//

// --- Retyped to real vector types (_ExtVector<N, T>) ---

// X2 unary (v2i32 -> v2i32)
def x2abs32    : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2abs32s   : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2neg32    : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2neg32s   : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2swap32   : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2mjswap32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;
def x2mjswap32s: HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>)">;

// X2 binary (v2i32, v2i32 -> v2i32)
def x2addsub32: HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)">;
def x2subadd32: HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)">;
def x2max32   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2min32   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2frst32  : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)">;
def x2frsst32 : HaydnBuiltin<"_ExtVector<2, int>(_ExtVector<2, int>, _ExtVector<2, int>)">;

// X2 immediate shift with rounding (v2i32, i32 -> v2i32)
def x2srai32r : HaydnBuiltin<"int64_t(int64_t, int)">;

// X4 unary (v4i16 -> v4i16)
def x4abs16    : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4abs16s   : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4neg16    : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4neg16s   : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4swap16   : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4mjswap16 : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4mjswap16s: HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4conj16   : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;
def x4conj16s  : HaydnBuiltin<"_ExtVector<4, short>(_ExtVector<4, short>)">;

// X4 binary (v4i16, v4i16 -> v4i16)
def x4max16   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4min16   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4 immediate shift with rounding (v4i16, i32 -> v4i16)
def x4srai16r : HaydnBuiltin<"int64_t(int64_t, int)">;

// --- Stay opaque i64 (cross-halfword / MAC / complex / fractional mul / pack) ---

// X2 unary (i64 -> i64): cross-halfword low-lane negate, pass-through high lane
def x2neg32_l  : HaydnBuiltin<"int64_t(int64_t)">;
def x2neg32s_l : HaydnBuiltin<"int64_t(int64_t)">;

// X2 binary (i64, i64 -> i64): cross-halfword HLLH variants + clamp
def x2addsub32_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2addsub32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2subadd32_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2subadd32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2clamp32        : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X2 fractional multiply (binary i64) -- not retyped (fractional mul stays i64)
def x2fmul32rs  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmul32rss : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmul32ts  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmula32rs : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmula32rss: HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmula32ts : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmuls32rs : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmuls32rss: HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fmuls32ts : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// X4 unary (i64 -> i64): complex mul / energy stay opaque (pack semantics)
def x4cmul16    : HaydnBuiltin<"int64_t(int64_t)">;
def x4cmul16_f2 : HaydnBuiltin<"int64_t(int64_t)">;
def x4energy16  : HaydnBuiltin<"int64_t(int64_t)">;

// X4 binary (i64, i64 -> i64): clamp / fractional mul / frac shift stay opaque
def x4clamp16  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4fmul16rs : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4fmul16rss: HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4fmul16ts : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4frst16   : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4frsst16  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

} // Attributes = [NoThrow, Const]

//===----------------------------------------------------------------------===//
// Wave 3: SFR Flag Register Predication
// Compare→SFR→conditional-move pattern for SIMD predication.
// Compare ops: binary DR64, set per-lane SFR flags (return passthrough).
// Move ops: binary DR64, conditionally select per-lane based on SFR.
//
// F19 (consolidated-fix-list §2): these read/write the SFR side-effect
// register. They were previously inside the `Const` block above, which made
// them DCE-able / reorderable. They now carry only `NoThrow`; the
// corresponding LLVM intrinsics carry `IntrHasSideEffects` (see
// IntrinsicsHaydn.td).
//===----------------------------------------------------------------------===//

let Attributes = [NoThrow] in {
def x2seq32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2slt32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sle32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2movf32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2movt32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4seq16  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4slt16  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4sle16  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4movf16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4movt16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Compare -> SFR
//===----------------------------------------------------------------------===//

def slt64 : HaydnBuiltin<"int64_t(int64_t)">;
def sle64 : HaydnBuiltin<"int64_t(int64_t)">;

// D208 Phase 1: SEQ64 — scalar 64-bit equal compare -> SFR (side-effecting; NOT Const)
def seq64 : HaydnBuiltin<"int64_t(int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Conditional Move
//===----------------------------------------------------------------------===//

def movt64 : HaydnBuiltin<"int64_t(int64_t)">;
def movf64 : HaydnBuiltin<"int64_t(int64_t)">;
} // Attributes = [NoThrow]

//===----------------------------------------------------------------------===//
// Clang-only builtins (no LLVM intrinsic -- need custom CodeGen later)
//
// These are kept because they map to Haydn instructions that have no
// corresponding LLVM intrinsic yet. They will require custom emission
// in Clang's CodeGen library when target support is added.
//===----------------------------------------------------------------------===//

let Attributes = [NoThrow, Const] in {

// Population count and bit reversal
def popcount32 : HaydnBuiltin<"int(int)">;
def popcount64 : HaydnBuiltin<"int(int64_t)">;
def brev32     : HaydnBuiltin<"int(int, int)">; // D208: 2-operand GPR32 (BREV32 rt, rs1, rs2) — alias of addbrba32 for NatureDSP AE_BREV32 source compat

// SIMD operations without LLVM intrinsics.
// Note: x4addsub16s/x4subadd16s were retired in D208 Phase 2 — the ops are an
// ISA gap (ISA-36) and the only consumers (AE_ADDANDSUBRNG16RAS_*) now route
// through __builtin_ae_addandsubrng16ras_s*, which composes the interleave
// from x4add16s + x4sub16s + movegpr2sfr + x4movt16 in EmitHaydnBuiltinExpr.
def x2subadd32s : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// SIMD MAC without LLVM intrinsics
def x2dot32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4dot16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

// Complex multiply without LLVM intrinsics
def x2cmula32  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)">;
def x2cmula32s : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)">;
def x2cmuls32  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)">;
def x2cmuls32s : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t, int64_t, int64_t)">;

// Transcendental without LLVM intrinsics
def isqrt  : HaydnBuiltin<"int(int)">;
def arctan : HaydnBuiltin<"int(int64_t, int)">;

} // Attributes = [NoThrow, Const]

//===----------------------------------------------------------------------===//
// Wave 5: SFR Register Transfer (NOT pure — read/write SFR side-effect register)
//===----------------------------------------------------------------------===//

let Attributes = [NoThrow] in {
def movesfr2gpr : HaydnBuiltin<"unsigned int()">;
def movegpr2sfr : HaydnBuiltin<"void(unsigned int)">;
def zero_sfr    : HaydnBuiltin<"void()">;

//===----------------------------------------------------------------------===//
// Cross-RF move: DR64 half-lane -> GPR32 (MOVE32_DR_L / MOVE32_DR_H)
// Pure ALU ops (no memory, no side effect). cbr_sel not involved.
//===----------------------------------------------------------------------===//

def move32_dr_l : HaydnBuiltin<"unsigned int(int64_t)">;
def move32_dr_h : HaydnBuiltin<"unsigned int(int64_t)">;

//===----------------------------------------------------------------------===//
// Circular Buffer Load/Store (CBR — memory operations, NOT pure)
//
// CB loads: load from circular buffer with automatic boundary wrapping.
// Multi-result IR is {data, new_ptr}. Clang Prototype cannot return structs
// for auto-map, so loads use frexp-pattern _pair builtins (HaydnPairBuiltin):
//   int64_t data = __builtin_haydn_ldw_cb_imm_pair(int *new_ptr_out,
//                                                  int ptr, int cbr, int stride);
// CB stores return the updated cursor (i32); auto-map works for single ret.
//
// Model (D208): IR models AGU writeback as SSA (same as BundleSim rs_wb).
// cbr_sel selects CBR set 0 or 1 (Rev 2: 2 sets, no CBR_SIZE).
//===----------------------------------------------------------------------===//

// Load pair: return data, store new_ptr to *arg0 (frexp pattern).
def ldw_cb_imm_pair : HaydnPairBuiltin<"int64_t(int*, int, int, int)">;
def ldw_cb_reg_pair : HaydnPairBuiltin<"int64_t(int*, int, int, int)">;
// Store: return new_ptr (single result — ClangBuiltin auto-map OK).
def sdw_cb_imm : HaydnBuiltin<"int(int64_t, int, int, int)">;
def sdw_cb_reg : HaydnBuiltin<"int(int64_t, int, int, int)">;

//===----------------------------------------------------------------------===//
// Circular Buffer Setup (CBR programming via CSRW — side-effecting CSR write)
//
// Programs a CBR set before any CB load/store. cbr_sel is 0 or 1.
//   setcbr_begin(cbr_sel, value) -> CBR_BEGIN[cbr_sel] = value
//   setcbr_end  (cbr_sel, value) -> CBR_END[cbr_sel]   = value
//===----------------------------------------------------------------------===//

def setcbr_begin : HaydnBuiltin<"void(int, int)">;
def setcbr_end   : HaydnBuiltin<"void(int, int)">;

//===----------------------------------------------------------------------===//
// AR (Aligned Register) unaligned load/store stream (NOT pure)
//
// Golden LS #103-#109. Stateful AR funnel-shift path for unaligned streams.
//   pldwwua(ar_sel, ptr)                         // seed AR
//   d_lqhwua_post(ptr, ar_sel, stride, dir) -> i64
//   d_ltwua_post (ptr, ar_sel, stride, dir) -> i64
//   flar(ar_sel)                                 // flush AR
//   wbarwua(ar_sel, ptr, dir)                    // residual write-back
//   d_sqhwua_post(data, ptr, ar_sel, stride, dir)
//   d_stwua_post (data, ptr, ar_sel, stride, dir)
// ar_sel is 0..3; dir_sel 0=forward (+stride), 1=reverse (-stride).
//===----------------------------------------------------------------------===//

def pldwwua : HaydnBuiltin<"void(int, int)">;
def d_lqhwua_post : HaydnBuiltin<"int64_t(int, int, int, int)">;
def d_ltwua_post : HaydnBuiltin<"int64_t(int, int, int, int)">;
def flar : HaydnBuiltin<"void(int)">;
def wbarwua : HaydnBuiltin<"void(int, int, int)">;
def d_sqhwua_post : HaydnBuiltin<"void(int64_t, int, int, int, int)">;
def d_stwua_post : HaydnBuiltin<"void(int64_t, int, int, int, int)">;

//===----------------------------------------------------------------------===//
// Bit-Reversed Addressing Load/Store (memory operations, NOT pure)
//
// BREV loads: load from bit-reversed address with post-increment.
//   (ptr_base, stride) -> result
// BREV stores: store to bit-reversed address with post-increment.
//   (data, ptr_base, stride) -> result (tied to ptr_base)
//===----------------------------------------------------------------------===//

def ldw_brev_imm : HaydnBuiltin<"int(int, int)">;
def ldw_brev_reg : HaydnBuiltin<"int(int, int)">;
def lw_brev_imm  : HaydnBuiltin<"int(int, int)">;
def lw_brev_reg  : HaydnBuiltin<"int(int, int)">;
def sdw_brev_imm : HaydnBuiltin<"int(int, int, int)">;
def sdw_brev_reg : HaydnBuiltin<"int(int, int, int)">;
def sw_brev_imm  : HaydnBuiltin<"int(int, int)">;
def sw_brev_reg  : HaydnBuiltin<"int(int, int)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD non-saturating arithmetic (binary DR64)
//===----------------------------------------------------------------------===//

def x2add32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sub32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4add16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4sub16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD HLLH cross-lane variants (binary DR64)
//===----------------------------------------------------------------------===//

def x2add32_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2add32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sub32_hllh  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2sub32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD register shifts (binary DR64)
//===----------------------------------------------------------------------===//

def x2sll32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x2sra32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x2srl32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4sll16 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4sra16 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4srl16 : HaydnBuiltin<"int64_t(int64_t, int)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD immediate shifts
//===----------------------------------------------------------------------===//

def x2slli32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x2srai32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x2srli32 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4slli16 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4srai16 : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4srli16 : HaydnBuiltin<"int64_t(int64_t, int)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD shift with rounding (binary DR64)
//===----------------------------------------------------------------------===//

def x2sra32r : HaydnBuiltin<"int64_t(int64_t, int)">;
def x4sra16r : HaydnBuiltin<"int64_t(int64_t, int)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD horizontal reductions (unary DR64)
//===----------------------------------------------------------------------===//

def x2hadd32_h  : HaydnBuiltin<"int64_t(int64_t)">;
def x2hadd32s_h : HaydnBuiltin<"int64_t(int64_t)">;
def x2hadd32_l  : HaydnBuiltin<"int64_t(int64_t)">;
def x2hadd32s_l : HaydnBuiltin<"int64_t(int64_t)">;
def x4hadd16_h  : HaydnBuiltin<"int64_t(int64_t)">;
def x4hadd16_l  : HaydnBuiltin<"int64_t(int64_t)">;
def x2hmax32 : HaydnBuiltin<"int64_t(int64_t)">;
def x2hmin32 : HaydnBuiltin<"int64_t(int64_t)">;
def x4hmax16 : HaydnBuiltin<"int64_t(int64_t)">;
def x4hmin16 : HaydnBuiltin<"int64_t(int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD multiply -- D400 Path B (2-dest, golden-faithful).
// X2MUL32/X4MUL16 are TRUE 2-OUTPUT (golden: DR_Write_Port:[rtd1,rtd2]).
// Frexp-pattern `_pair` builtins (see SIMD MAC block above for the rationale).
//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD multiply (ternary DR64)
//===----------------------------------------------------------------------===//

// D_RR2: (rtd2_out, src1, src2) -> rtd1.
def x2mul32_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;
def x4mul16_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD multiply-pair variants (binary DR64)
//===----------------------------------------------------------------------===//

def x2mulph32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2mulpl32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2mulaph32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2mulapl32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2mulsph32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2mulspl32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 32-bit complex fractional multiply (binary DR64)
//===----------------------------------------------------------------------===//

def x2fcmul32rs  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fcmul32rss : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2fcmula32rs  : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def x2fcmula32rss : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 CMUL F2 variants -- D400 Path B (2-dest).
// Frexp-pattern `_pair` builtins; same 2-dest semantics as X2CMUL32/S.
//===----------------------------------------------------------------------===//

def x2cmul32_f2_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;
def x2cmul32s_f2_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X2 FF2 shift variants (binary DR64)
//===----------------------------------------------------------------------===//

def x2ff2rsst32 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x2ff2rst32  : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X4 FF2MUL variants -- D400 Path B (2-dest).
// X4FF2MUL16S is D_RR2 (non-accum); X4FF2MULA16S/X4FF2MULS16S are D_RRA2
// (accum). Frexp-pattern `_pair` builtins.
//===----------------------------------------------------------------------===//

def x4ff2mul16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t)">;
def x4ff2mula16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;
def x4ff2muls16s_pair : HaydnPairBuiltin<"int64_t(int64_t*, int64_t, int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// Wave 5: X4 pack/sat/sel
//===----------------------------------------------------------------------===//

def x4sat32t16 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def x4seli16   : HaydnBuiltin<"int64_t(int64_t, int64_t, int)">;

//===----------------------------------------------------------------------===//
// MOVDA family — GPR32 <-> DR64 lane transfers.
// These lower to the existing MOV_GPR_TO_DR64 / MOV_DR64_TO_GPR pseudos
// (no native cross-bank instruction in the Haydn ISA; see ISA-10 / D90).
//===----------------------------------------------------------------------===//

/// Place i32 in low lane of DR64, zero-fill high lane.
def movda32 : HaydnBuiltin<"int64_t(int)">;
/// Place two i32 inputs into the two DR64 lanes.
def movda32x2 : HaydnBuiltin<"int64_t(int, int)">;
/// Place i32 in low lane (16-bit source fidelity port; DR64 lanes are i32-wide).
def movda16 : HaydnBuiltin<"int64_t(int)">;
/// Extract low 32-bit lane of DR64.
def movad32_low : HaydnBuiltin<"int(int64_t)">;
/// Extract high 32-bit lane of DR64.
def movad32_high : HaydnBuiltin<"int(int64_t)">;

//===----------------------------------------------------------------------===//
// MULFC32X16RAS — Complex 32x16 MAC with rounding + saturation.
// NatureDSP AE_MULFC32X16RAS_{L,H} compatibility layer (FFT butterfly core).
// Decomposes onto X2FCMULA32RS after a C-level 16->32 lane widen of the
// twiddle (Haydn lacks a cross-lane 16->32 pack instruction; see ISA-08
// and D90-mulfc32x16ras-intrinsic.md).
//===----------------------------------------------------------------------===//

/// Complex 32x16 MAC, low complex pair (rounding + saturation).
def mulfc32x16ras_low : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// Complex 32x16 MAC, high complex pair (rounding + saturation).
def mulfc32x16ras_high : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// FIR-specific MAC family (decomposed — see D91-fir-mac-family-decomposition).
// NatureDSP AE_MUL{,A}FD32X16X2_FIR_HH/HL and AE_MUL{,A}FQ16X2_FIR_3/1
// compatibility. Each intrinsic is one MAC step; the kernel caller issues
// one per output accumulator lane. See ISA-09-fir-simd-mac-gap.md.
//===----------------------------------------------------------------------===//

/// 32x32 fractional MAC, HH lanes (FIR MAC step).
/// Renamed from mulafd32x16x2_fir_hh (the 32x16 claim was wrong; backing op
/// is FMULA32S_HH, 32x32). See D180 A2.
def mulaa32s_fir_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// 32x32 fractional MAC, LH lanes (FIR MAC step). Renamed from
/// mulafd32x16x2_fir_hl; see D180 A2.
def mulaa32s_fir_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// 16x16 fractional multiply init, lane-pair 0/1 (FIR init step).
def mulfq16x2_fir_3 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// 16x16 fractional multiply init, lane-pair 2/3 (FIR init step).
def mulfq16x2_fir_1 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// 16x16 fractional MAC, lane-pair 1+0 -> Q1.31 (FIR MAC step).
def mulafq16x2_fir_3 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
/// 16x16 fractional MAC, lane-pair 3+2 -> Q1.31 (FIR MAC step).
def mulafq16x2_fir_1 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208: DB-coverage completion — builtins for the 158 newly-modeled intrinsics.
// Each auto-maps to llvm.haydn.<name> via ClangBuiltin<>.
//===----------------------------------------------------------------------===//

def f2mulaa32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulaa32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulss32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulss32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def f2mulzaa32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzaa32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzaa32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzaa32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzas32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzas32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzas32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzas32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzsa32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzsa32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzsa32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzsa32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzss32rs_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzss32rs_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzss32r_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def f2mulzss32r_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmula16_hs00 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs01 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs02 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs03 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs11 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs12 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs13 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs22 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs23 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_hs33 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls00 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls01 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls02 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls03 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls11 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls12 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls13 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls22 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls23 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmula16_ls33 : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulaa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulaa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulss32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulss32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def fmulzaa16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzaa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzas32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzas32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzsa32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzsa32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_hs_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_hs_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_hs_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_ls_11_00 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_ls_13_02 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss16_ls_33_22 : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss32s_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def fmulzss32s_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul16aq : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mul16zaq : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mul64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mula64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mula64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulaa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulaa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulas64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def muls64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_hh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_hl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_huh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_hul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_lh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ll : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_luh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_lul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uhh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uhl : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uhuh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uhul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ulh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ull : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_uluh : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulss64_ulul : HaydnBuiltin<"int64_t(int64_t, int64_t, int64_t)">;
def mulzaa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzaa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzas32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzas32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzsa32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzsa32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzss32_hhll : HaydnBuiltin<"int64_t(int64_t, int64_t)">;
def mulzss32_hllh : HaydnBuiltin<"int64_t(int64_t, int64_t)">;

//===----------------------------------------------------------------------===//
// D208 Phase 2: native HiFi AE_* composed builtins (D210).
//
// These are NOT ClangBuiltin<>-annotated, so getIntrinsicForClangBuiltin() in
// CGBuiltin.cpp returns not_intrinsic and control falls through to
// EmitHaydnBuiltinExpr (clang/lib/CodeGen/TargetBuiltins/Haydn.cpp), which
// emits the 1:N llvm.haydn.* intrinsic sequence. This is the Tier B composed
// CodeGen layer: one builtin -> one or more intrinsics.
//
// All operate on DR64 (ae_int16x4 == int64_t at the C level). The three
// addsubrng16ras slot variants (_s0/_s1/_s2) emit identical IR — the slot tag
// is a HiFi macro convention; Haydn IR has no slot tagging.
//===----------------------------------------------------------------------===//

// Native 1:1 — single correct intrinsic (replaces broken haydn_dsp.h decomps).
def mul16js     : HaydnAeBuiltin<"int64_t(int64_t)">;
def conj16s     : HaydnAeBuiltin<"int64_t(int64_t)">;
def mulafc16ras : HaydnAeBuiltin<"int64_t(int64_t, int64_t, int64_t)">;

// 1:N composed — SFR-based addsub16 (D210/ISA-39). x4addsub16s is an ISA gap
// (ISA-36); the alternating interleave {add,sub,add,sub} cannot be built with
// x4seli16 (ISA-39), so EmitHaydnBuiltinExpr emits:
//   add = x4add16s(a,b); sub = x4sub16s(a,b);
//   movegpr2sfr(0b0101); x4movt16(add, sub)  // clobbers SFR
def addandsubrng16ras_s0 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)">;
def addandsubrng16ras_s1 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)">;
def addandsubrng16ras_s2 : HaydnAeBuiltin<"int64_t(int64_t, int64_t)">;

} // Attributes = [NoThrow, Const]
