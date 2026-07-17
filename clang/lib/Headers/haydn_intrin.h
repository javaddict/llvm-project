/*===---- haydn_intrin.h - Haydn DSP Intrinsic Functions ---------*- C++ -*-===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 *
 * This header provides C-callable intrinsics for Haydn DSP instructions.
 * Include this header to access MAC, saturating arithmetic, SIMD, fractional
 * multiply, and other DSP operations from C code.
 *
 * Naming convention:
 *   __haydn_<operation>[_<modifiers>]
 *   e.g., __haydn_mul64_ss_ll = signed*signed multiply, low*low lane select
 *
 * Lane selectors (signed-signed): _ll, _lh, _hl, _hh
 * Lane selectors (signed-unsigned): _lul, _ulh, _uhl, _uhh
 * Lane selectors (unsigned-signed): _luh, _uhuh, _hul, _uhul
 * Lane selectors (unsigned-unsigned): _uluh, _ulul, _ull, _ulh
 *
 * Signedness prefix: _ss (signed*signed), _su (signed*unsigned),
 *                    _us (unsigned*signed), _uu (unsigned*unsigned)
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_INTRIN_H
#define __HAYDN_INTRIN_H

/* Provide fixed-width integer types (int32_t, int64_t, uint32_t, uint64_t,
 * uintptr_t, etc.) and size_t. These are used by the inline wrapper functions
 * and by ported kernels that include this header directly without first
 * pulling in <stdint.h>/<stddef.h>. On a freestanding target the clang
 * resource dir provides both via the freestanding headers, so these includes
 * are always available when this header is in scope. */
#include <stdint.h>
#include <stddef.h>

/// 64-bit data register type for Haydn DSP operations.
typedef long long haydn_dr64_t;

/// Pair of 64-bit data registers — the 2-dest result of the SIMD MAC ops
/// (X2MUL32, X4MUL16, X2CMUL32, …). D400 Path B: these instructions produce
/// TWO i64 results (high pair → `hi`, low pair → `lo`; for complex multiplies
/// `hi` = real, `lo` = imaginary). The clang builtin surface exposes them via
/// the frexp pattern (`_pair` suffix, out-pointer for `lo`); this struct is
/// the C-level view reconstructed by the `__haydn_<op>` inline wrappers below.
typedef struct {
  uint64_t hi;  ///< High-pair result (rtd1); real part for complex ops.
  uint64_t lo;  ///< Low-pair result (rtd2); imaginary part for complex ops.
} haydn_dpair_t;

//===----------------------------------------------------------------------===//
// 64-bit Multiply (MUL64) -- 16 variants
//===----------------------------------------------------------------------===//

// Signed-signed
#define __haydn_mul64_ss_ll __builtin_haydn_mul64_ss_ll
#define __haydn_mul64_ss_lh __builtin_haydn_mul64_ss_lh
#define __haydn_mul64_ss_hl __builtin_haydn_mul64_ss_hl
#define __haydn_mul64_ss_hh __builtin_haydn_mul64_ss_hh

// Signed-unsigned
#define __haydn_mul64_su_lul __builtin_haydn_mul64_su_lul
#define __haydn_mul64_su_ulh __builtin_haydn_mul64_su_ulh
#define __haydn_mul64_su_uhl __builtin_haydn_mul64_su_uhl
#define __haydn_mul64_su_uhh __builtin_haydn_mul64_su_uhh

// Unsigned-signed
#define __haydn_mul64_us_luh  __builtin_haydn_mul64_us_luh
#define __haydn_mul64_us_uhuh __builtin_haydn_mul64_us_uhuh
#define __haydn_mul64_us_hul  __builtin_haydn_mul64_us_hul
#define __haydn_mul64_us_uhul __builtin_haydn_mul64_us_uhul

// Unsigned-unsigned
#define __haydn_mul64_uu_uluh __builtin_haydn_mul64_uu_uluh
#define __haydn_mul64_uu_ulul __builtin_haydn_mul64_uu_ulul
#define __haydn_mul64_uu_ull  __builtin_haydn_mul64_uu_ull
#define __haydn_mul64_uu_ulh  __builtin_haydn_mul64_uu_ulh

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate (MULA64) -- 16 variants
//===----------------------------------------------------------------------===//

// Signed-signed
#define __haydn_mula64_ss_ll __builtin_haydn_mula64_ss_ll
#define __haydn_mula64_ss_lh __builtin_haydn_mula64_ss_lh
#define __haydn_mula64_ss_hl __builtin_haydn_mula64_ss_hl
#define __haydn_mula64_ss_hh __builtin_haydn_mula64_ss_hh

// Signed-unsigned
#define __haydn_mula64_su_lul __builtin_haydn_mula64_su_lul
#define __haydn_mula64_su_ulh __builtin_haydn_mula64_su_ulh
#define __haydn_mula64_su_uhl __builtin_haydn_mula64_su_uhl
#define __haydn_mula64_su_uhh __builtin_haydn_mula64_su_uhh

// Unsigned-signed
#define __haydn_mula64_us_luh  __builtin_haydn_mula64_us_luh
#define __haydn_mula64_us_uhuh __builtin_haydn_mula64_us_uhuh
#define __haydn_mula64_us_hul  __builtin_haydn_mula64_us_hul
#define __haydn_mula64_us_uhul __builtin_haydn_mula64_us_uhul

// Unsigned-unsigned
#define __haydn_mula64_uu_uluh __builtin_haydn_mula64_uu_uluh
#define __haydn_mula64_uu_ulul __builtin_haydn_mula64_uu_ulul
#define __haydn_mula64_uu_ull  __builtin_haydn_mula64_uu_ull
#define __haydn_mula64_uu_ulh  __builtin_haydn_mula64_uu_ulh

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract (MULS64) -- 16 variants
//===----------------------------------------------------------------------===//

// Signed-signed
#define __haydn_muls64_ss_ll __builtin_haydn_muls64_ss_ll
#define __haydn_muls64_ss_lh __builtin_haydn_muls64_ss_lh
#define __haydn_muls64_ss_hl __builtin_haydn_muls64_ss_hl
#define __haydn_muls64_ss_hh __builtin_haydn_muls64_ss_hh

// Signed-unsigned
#define __haydn_muls64_su_lul __builtin_haydn_muls64_su_lul
#define __haydn_muls64_su_ulh __builtin_haydn_muls64_su_ulh
#define __haydn_muls64_su_uhl __builtin_haydn_muls64_su_uhl
#define __haydn_muls64_su_uhh __builtin_haydn_muls64_su_uhh

// Unsigned-signed
#define __haydn_muls64_us_luh  __builtin_haydn_muls64_us_luh
#define __haydn_muls64_us_uhuh __builtin_haydn_muls64_us_uhuh
#define __haydn_muls64_us_hul  __builtin_haydn_muls64_us_hul
#define __haydn_muls64_us_uhul __builtin_haydn_muls64_us_uhul

// Unsigned-unsigned
#define __haydn_muls64_uu_uluh __builtin_haydn_muls64_uu_uluh
#define __haydn_muls64_uu_ulul __builtin_haydn_muls64_uu_ulul
#define __haydn_muls64_uu_ull  __builtin_haydn_muls64_uu_ull
#define __haydn_muls64_uu_ulh  __builtin_haydn_muls64_uu_ulh

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Accumulate-Subtract (MULAS64) -- 16 variants
//===----------------------------------------------------------------------===//

// Signed-signed
#define __haydn_mulas64_ss_ll __builtin_haydn_mulas64_ss_ll
#define __haydn_mulas64_ss_lh __builtin_haydn_mulas64_ss_lh
#define __haydn_mulas64_ss_hl __builtin_haydn_mulas64_ss_hl
#define __haydn_mulas64_ss_hh __builtin_haydn_mulas64_ss_hh

// Signed-unsigned
#define __haydn_mulas64_su_lul __builtin_haydn_mulas64_su_lul
#define __haydn_mulas64_su_ulh __builtin_haydn_mulas64_su_ulh
#define __haydn_mulas64_su_uhl __builtin_haydn_mulas64_su_uhl
#define __haydn_mulas64_su_uhh __builtin_haydn_mulas64_su_uhh

// Unsigned-signed
#define __haydn_mulas64_us_luh  __builtin_haydn_mulas64_us_luh
#define __haydn_mulas64_us_uhuh __builtin_haydn_mulas64_us_uhuh
#define __haydn_mulas64_us_hul  __builtin_haydn_mulas64_us_hul
#define __haydn_mulas64_us_uhul __builtin_haydn_mulas64_us_uhul

// Unsigned-unsigned
#define __haydn_mulas64_uu_uluh __builtin_haydn_mulas64_uu_uluh
#define __haydn_mulas64_uu_ulul __builtin_haydn_mulas64_uu_ulul
#define __haydn_mulas64_uu_ull  __builtin_haydn_mulas64_uu_ull
#define __haydn_mulas64_uu_ulh  __builtin_haydn_mulas64_uu_ulh

//===----------------------------------------------------------------------===//
// 64-bit Multiply-Subtract-Subtract (MULSS64) -- 16 variants
//===----------------------------------------------------------------------===//

// Signed-signed
#define __haydn_mulss64_ss_ll __builtin_haydn_mulss64_ss_ll
#define __haydn_mulss64_ss_lh __builtin_haydn_mulss64_ss_lh
#define __haydn_mulss64_ss_hl __builtin_haydn_mulss64_ss_hl
#define __haydn_mulss64_ss_hh __builtin_haydn_mulss64_ss_hh

// Signed-unsigned
#define __haydn_mulss64_su_lul __builtin_haydn_mulss64_su_lul
#define __haydn_mulss64_su_ulh __builtin_haydn_mulss64_su_ulh
#define __haydn_mulss64_su_uhl __builtin_haydn_mulss64_su_uhl
#define __haydn_mulss64_su_uhh __builtin_haydn_mulss64_su_uhh

// Unsigned-signed
#define __haydn_mulss64_us_luh  __builtin_haydn_mulss64_us_luh
#define __haydn_mulss64_us_uhuh __builtin_haydn_mulss64_us_uhuh
#define __haydn_mulss64_us_hul  __builtin_haydn_mulss64_us_hul
#define __haydn_mulss64_us_uhul __builtin_haydn_mulss64_us_uhul

// Unsigned-unsigned
#define __haydn_mulss64_uu_uluh __builtin_haydn_mulss64_uu_uluh
#define __haydn_mulss64_uu_ulul __builtin_haydn_mulss64_uu_ulul
#define __haydn_mulss64_uu_ull  __builtin_haydn_mulss64_uu_ull
#define __haydn_mulss64_uu_ulh  __builtin_haydn_mulss64_uu_ulh

//===----------------------------------------------------------------------===//
// Saturating Arithmetic
//===----------------------------------------------------------------------===//

/// Saturating 32-bit add: clamp to INT32_MAX/INT32_MIN on overflow
#define __haydn_add32s __builtin_haydn_add32s
/// Saturating 32-bit subtract
#define __haydn_sub32s __builtin_haydn_sub32s
/// Saturating 64-bit add
#define __haydn_add64s __builtin_haydn_add64s
/// Saturating 64-bit subtract
#define __haydn_sub64s __builtin_haydn_sub64s
/// Saturating 32-bit absolute value (scalar GPR)
#define __haydn_abs32s __builtin_haydn_abs32s
/// Saturating 32-bit negate (scalar GPR)
#define __haydn_neg32s __builtin_haydn_neg32s
/// Saturating 64-bit absolute value
#define __haydn_abs64s __builtin_haydn_abs64s
/// Saturating 64-bit negate
#define __haydn_neg64s __builtin_haydn_neg64s

//===----------------------------------------------------------------------===//
// Dual-lane (2x32 in DR64) abs / neg
//===----------------------------------------------------------------------===//
// BuiltinsHaydn.td types these as ExtVector<2,int>, while NatureDSP
// ae_int32x2 is an opaque long long (haydn_dr64_t). Wrap with a union so
// call sites can pass/return DR64 without truncation to one lane.
typedef int __haydn_v2i32_t __attribute__((ext_vector_type(2)));

static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x2abs32(long long a) {
  union { long long ll; __haydn_v2i32_t v; } u, r;
  u.ll = a;
  r.v = __builtin_haydn_x2abs32(u.v);
  return r.ll;
}
static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x2abs32s(long long a) {
  union { long long ll; __haydn_v2i32_t v; } u, r;
  u.ll = a;
  r.v = __builtin_haydn_x2abs32s(u.v);
  return r.ll;
}
static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x2neg32(long long a) {
  union { long long ll; __haydn_v2i32_t v; } u, r;
  u.ll = a;
  r.v = __builtin_haydn_x2neg32(u.v);
  return r.ll;
}
static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x2neg32s(long long a) {
  union { long long ll; __haydn_v2i32_t v; } u, r;
  u.ll = a;
  r.v = __builtin_haydn_x2neg32s(u.v);
  return r.ll;
}

//===----------------------------------------------------------------------===//
// Non-saturating 64-bit Absolute/Negate
//===----------------------------------------------------------------------===//

/// 64-bit absolute value (non-saturating)
#define __haydn_abs64 __builtin_haydn_abs64
/// 64-bit negate (non-saturating)
#define __haydn_neg64 __builtin_haydn_neg64

//===----------------------------------------------------------------------===//
// Fractional Multiply
//===----------------------------------------------------------------------===//

/// Fractional 32-bit signed multiply, low-low lane
#define __haydn_fmul32s_ll __builtin_haydn_fmul32s_ll
/// Fractional 32-bit signed multiply, low-high lane
#define __haydn_fmul32s_lh __builtin_haydn_fmul32s_lh
/// Fractional 32-bit signed multiply, high-high lane
#define __haydn_fmul32s_hh __builtin_haydn_fmul32s_hh
/// Fractional 32-bit signed multiply-accumulate, low-low lane
#define __haydn_fmula32s_ll __builtin_haydn_fmula32s_ll
/// Fractional 32-bit signed multiply-accumulate, low-high lane
#define __haydn_fmula32s_lh __builtin_haydn_fmula32s_lh
/// Fractional 32-bit signed multiply-accumulate, high-high lane
#define __haydn_fmula32s_hh __builtin_haydn_fmula32s_hh
/// Fractional 32-bit signed multiply-subtract, low-low lane
#define __haydn_fmuls32s_ll __builtin_haydn_fmuls32s_ll
/// Fractional 32-bit signed multiply-subtract, low-high lane
#define __haydn_fmuls32s_lh __builtin_haydn_fmuls32s_lh
/// Fractional 32-bit signed multiply-subtract, high-high lane
#define __haydn_fmuls32s_hh __builtin_haydn_fmuls32s_hh
/// Dual 32-bit signed multiply-subtract-accumulate, HH+LL lanes
#define __haydn_mulsa32_hhll __builtin_haydn_mulsa32_hhll
/// Dual 32-bit signed multiply-subtract-accumulate, HL+LH cross lanes
#define __haydn_mulsa32_hllh __builtin_haydn_mulsa32_hllh

//===----------------------------------------------------------------------===//
// FF2 Fractional Multiply with Symmetric Rounding
// Critical DSP kernel intrinsics — FF2 family provides fractional 32x32->64
// multiply with symmetric rounding + saturation.
//===----------------------------------------------------------------------===//

/// FF2 saturating fractional multiply with rounding, low-low lane
#define __haydn_ff2mul32rs_ll __builtin_haydn_ff2mul32rs_ll
/// FF2 saturating fractional multiply with rounding, low-high lane
#define __haydn_ff2mul32rs_lh __builtin_haydn_ff2mul32rs_lh
/// FF2 saturating fractional multiply with rounding, high-high lane
#define __haydn_ff2mul32rs_hh __builtin_haydn_ff2mul32rs_hh
/// FF2 saturating fractional multiply-accumulate with rounding, low-low lane
#define __haydn_ff2mula32rs_ll __builtin_haydn_ff2mula32rs_ll
/// FF2 saturating fractional multiply-accumulate with rounding, low-high lane
#define __haydn_ff2mula32rs_lh __builtin_haydn_ff2mula32rs_lh
/// FF2 saturating fractional multiply-accumulate with rounding, high-high lane
#define __haydn_ff2mula32rs_hh __builtin_haydn_ff2mula32rs_hh
/// FF2 saturating fractional multiply-subtract with rounding, low-low lane
#define __haydn_ff2muls32rs_ll __builtin_haydn_ff2muls32rs_ll
/// FF2 saturating fractional multiply-subtract with rounding, low-high lane
#define __haydn_ff2muls32rs_lh __builtin_haydn_ff2muls32rs_lh
/// FF2 saturating fractional multiply-subtract with rounding, high-high lane
#define __haydn_ff2muls32rs_hh __builtin_haydn_ff2muls32rs_hh

/// FF2 non-saturating fractional multiply with rounding, low-low lane
#define __haydn_ff2mul32r_ll __builtin_haydn_ff2mul32r_ll
/// FF2 non-saturating fractional multiply with rounding, low-high lane
#define __haydn_ff2mul32r_lh __builtin_haydn_ff2mul32r_lh
/// FF2 non-saturating fractional multiply with rounding, high-high lane
#define __haydn_ff2mul32r_hh __builtin_haydn_ff2mul32r_hh
/// FF2 non-saturating fractional multiply-accumulate with rounding, low-low lane
#define __haydn_ff2mula32r_ll __builtin_haydn_ff2mula32r_ll
/// FF2 non-saturating fractional multiply-accumulate with rounding, low-high lane
#define __haydn_ff2mula32r_lh __builtin_haydn_ff2mula32r_lh
/// FF2 non-saturating fractional multiply-accumulate with rounding, high-high lane
#define __haydn_ff2mula32r_hh __builtin_haydn_ff2mula32r_hh
/// FF2 non-saturating fractional multiply-subtract with rounding, low-low lane
#define __haydn_ff2muls32r_ll __builtin_haydn_ff2muls32r_ll
/// FF2 non-saturating fractional multiply-subtract with rounding, low-high lane
#define __haydn_ff2muls32r_lh __builtin_haydn_ff2muls32r_lh
/// FF2 non-saturating fractional multiply-subtract with rounding, high-high lane
#define __haydn_ff2muls32r_hh __builtin_haydn_ff2muls32r_hh

//===----------------------------------------------------------------------===//
// Cross-Width 32x16 Fractional MAC with round + saturate
//
// Maps to NatureDSP AE_MULFP32X16X2RAS_{L,H}. Used for cross-width gain
// scaling in IIR (bqriir32x16_df1) and FIR (firinterp16x16) kernels.
//
//   acc     : DR64 with two Q1.31 accumulator lanes [63:32]=hi, [31:0]=lo
//   a32     : DR64 with two Q1.31 multiplier lanes
//   b16     : DR64 with four Q1.15 lanes (lanes 0..3 from low to high)
//
//   low  : acc += sat_q31(a32.lo * b16.lane0) + sat_q31(a32.hi * b16.lane1)
//   high : acc += sat_q31(a32.lo * b16.lane2) + sat_q31(a32.hi * b16.lane3)
//
// Haydn has no native 32x16 fractional MAC; the compiler decomposes this
// into per-lane MACQ31 (fractional 32x32 saturating MAC) sequences. See
// D92-mulfp32x16x2ras-intrinsic.md and ISA-11-mulfp32x16x2ras-gap.md.
//===----------------------------------------------------------------------===//
static __inline__ __attribute__((__always_inline__))
int64_t __haydn_mulfp32x16x2ras_low(int64_t acc, int64_t a32, int64_t b16) {
  return __builtin_haydn_mulfp32x16x2ras_low(acc, a32, b16);
}

static __inline__ __attribute__((__always_inline__))
int64_t __haydn_mulfp32x16x2ras_high(int64_t acc, int64_t a32, int64_t b16) {
  return __builtin_haydn_mulfp32x16x2ras_high(acc, a32, b16);
}

//===----------------------------------------------------------------------===//
// 32-bit Multiply (high/low half)
//===----------------------------------------------------------------------===//

/// Multiply, return lower 32 bits
#define __haydn_mull __builtin_haydn_mull
/// Signed*signed multiply, return upper 32 bits
#define __haydn_mulssh __builtin_haydn_mulssh
/// Signed*unsigned multiply, return upper 32 bits
#define __haydn_mulsuh __builtin_haydn_mulsuh
/// Unsigned*unsigned multiply, return upper 32 bits
#define __haydn_muluuh __builtin_haydn_muluuh

//===----------------------------------------------------------------------===//
// Q-format Multiply/Accumulate
//===----------------------------------------------------------------------===//

/// Fractional 32x32->32 saturating multiply
#define __haydn_mulq31 __builtin_haydn_mulq31
/// Fractional 32x32->32 saturating multiply-accumulate
#define __haydn_macq31 __builtin_haydn_macq31
/// Fractional 64x64->64 saturating multiply
#define __haydn_mulq63 __builtin_haydn_mulq63
/// 32x32->32 multiply-accumulate
#define __haydn_mac32 __builtin_haydn_mac32

//===----------------------------------------------------------------------===//
// SIMD X2 Operations (dual 32-bit)
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD saturating add
#define __haydn_x2add32s __builtin_haydn_x2add32s
/// Dual 32-bit SIMD saturating subtract
#define __haydn_x2sub32s __builtin_haydn_x2sub32s
/// Dual 32-bit SIMD add-subtract with saturation
#define __haydn_x2addsub32s __builtin_haydn_x2addsub32s
/// Dual 32-bit SIMD subtract-add with saturation (Clang-only, no LLVM intrinsic)
#define __haydn_x2subadd32s __builtin_haydn_x2subadd32s

//===----------------------------------------------------------------------===//
// SIMD X4 Operations (quad 16-bit)
//===----------------------------------------------------------------------===//

/// Quad 16-bit SIMD saturating add
#define __haydn_x4add16s __builtin_haydn_x4add16s
/// Quad 16-bit SIMD saturating subtract
#define __haydn_x4sub16s __builtin_haydn_x4sub16s
// Golden X4ABS16 / X4ABS16S as soft DR64 helpers.
// Builtins exist (__builtin_haydn_x4abs16{,s}) and GISel has patterns, but the
// selected X4ABS16S pseudo is not reliably expanded on freestanding paths
// (observed empty/wrong body). Soft lane model matches golden Behavior and
// host_emul / pure haydn_x4abs16s. Prefer soft until HW emit is lit-gated.
static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x4abs16(long long a) {
  /* X4ABS16: wrap at 0x8000 (no sat) */
  unsigned long long u = (unsigned long long)a, o = 0;
  int i;
  for (i = 0; i < 4; i++) {
    short x = (short)((u >> (16 * i)) & 0xffffu);
    short r = (x < 0) ? (short)(-x) : x; /* 0x8000 → 0x8000 wrap */
    o |= ((unsigned long long)(unsigned short)r) << (16 * i);
  }
  return (long long)o;
}
static __inline__ __attribute__((__always_inline__, __nodebug__))
long long __haydn_x4abs16s(long long a) {
  /* X4ABS16S: SAT16(ABS); 0x8000 → 0x7FFF */
  unsigned long long u = (unsigned long long)a, o = 0;
  int i;
  for (i = 0; i < 4; i++) {
    short x = (short)((u >> (16 * i)) & 0xffffu);
    short r;
    if ((unsigned short)x == 0x8000u)
      r = 0x7fff;
    else
      r = (x < 0) ? (short)(-x) : x;
    o |= ((unsigned long long)(unsigned short)r) << (16 * i);
  }
  return (long long)o;
}
// __haydn_x4addsub16s / __haydn_x4subadd16s retired in D208 Phase 2 (ISA-36
// gap; AE_ADDANDSUBRNG16RAS_* now compose via __builtin_ae_addandsubrng16ras_s*
// in EmitHaydnBuiltinExpr — see D210).

//===----------------------------------------------------------------------===//
// SIMD MAC Operations
//===----------------------------------------------------------------------===//
//
// D400 Path B: X2MULA32/X2MULS32/X4MULA16/X4MULS16/X4MULA16S/X4MULS16S are
// 2-dest accumulator ops (D_RRA2): they read TWO 64-bit accumulators and
// write TWO 64-bit results (lanes 3,2 → hi; lanes 1,0 → lo). The clang
// builtins use the frexp pattern (`_pair` suffix: returns rtd1, writes rtd2
// through an out-pointer) because the Prototype grammar has no struct return
// and the generic ClangBuiltin<> auto-map cannot lower multi-result
// intrinsics (see EmitHaydnBuiltinExpr in Haydn.cpp). These inline wrappers
// reconstruct the C-level `haydn_dpair_t` view consumed by haydn_dsp.h.

/// Dual 32-bit SIMD multiply-accumulate (2-dest accumulator form).
/// hi = acc1 + rsd1[63:32] * rsd2[63:32]; lo = acc2 + rsd1[31:0] * rsd2[31:0].
static inline haydn_dpair_t __haydn_x2mula32(uint64_t acc1, uint64_t acc2,
                                             uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2mula32_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

/// Dual 32-bit SIMD multiply-subtract (2-dest accumulator form).
static inline haydn_dpair_t __haydn_x2muls32(uint64_t acc1, uint64_t acc2,
                                             uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2muls32_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

/// Quad 16-bit SIMD multiply-accumulate (2-dest accumulator form).
static inline haydn_dpair_t __haydn_x4mula16(uint64_t acc1, uint64_t acc2,
                                             uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4mula16_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

/// Quad 16-bit SIMD multiply-subtract (2-dest accumulator form).
static inline haydn_dpair_t __haydn_x4muls16(uint64_t acc1, uint64_t acc2,
                                             uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4muls16_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

/// Quad 16-bit SIMD multiply-accumulate with saturation (2-dest accumulator).
static inline haydn_dpair_t __haydn_x4mula16s(uint64_t acc1, uint64_t acc2,
                                              uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4mula16s_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

/// Quad 16-bit SIMD multiply-subtract with saturation (2-dest accumulator).
static inline haydn_dpair_t __haydn_x4muls16s(uint64_t acc1, uint64_t acc2,
                                              uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4muls16s_pair(&r.lo, acc1, acc2, a, b);
  return r;
}
/// Dual 32-bit SIMD dot product (Clang-only, no LLVM intrinsic)
#define __haydn_x2dot32 __builtin_haydn_x2dot32
/// Quad 16-bit SIMD dot product (Clang-only, no LLVM intrinsic)
#define __haydn_x4dot16 __builtin_haydn_x4dot16

//===----------------------------------------------------------------------===//
// Complex Multiply
//===----------------------------------------------------------------------===//

/// Quad 16-bit complex multiply with rounding+saturation
#define __haydn_x4fcmul16rs __builtin_haydn_x4fcmul16rs
/// Quad 16-bit complex multiply-accumulate with rounding+saturation
#define __haydn_x4fcmula16rs __builtin_haydn_x4fcmula16rs
/// Quad 16-bit complex multiply with rounding+saturation variant
#define __haydn_x4fcmul16rss __builtin_haydn_x4fcmul16rss
/// Quad 16-bit complex multiply-accumulate with rounding+saturation variant
#define __haydn_x4fcmula16rss __builtin_haydn_x4fcmula16rss
/// Dual 32-bit complex multiply (2-dest, D400 Path B).
/// Interpreting rsd1 as (a + j·b) and rsd2 as (c + j·d):
///   hi = real = a·c - b·d ; lo = imag = a·d + b·c.
static inline haydn_dpair_t __haydn_x2cmul32(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2cmul32_pair(&r.lo, a, b);
  return r;
}

/// Dual 32-bit complex multiply with saturation (2-dest, D400 Path B).
static inline haydn_dpair_t __haydn_x2cmul32s(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2cmul32s_pair(&r.lo, a, b);
  return r;
}
/// Dual 32-bit complex multiply-accumulate (Clang-only, no LLVM intrinsic)
#define __haydn_x2cmula32 __builtin_haydn_x2cmula32
/// Dual 32-bit complex multiply-accumulate with saturation (Clang-only, no LLVM intrinsic)
#define __haydn_x2cmula32s __builtin_haydn_x2cmula32s
/// Dual 32-bit complex multiply-subtract (Clang-only, no LLVM intrinsic)
#define __haydn_x2cmuls32 __builtin_haydn_x2cmuls32
/// Dual 32-bit complex multiply-subtract with saturation (Clang-only, no LLVM intrinsic)
#define __haydn_x2cmuls32s __builtin_haydn_x2cmuls32s

//===----------------------------------------------------------------------===//
// Transcendental Functions
//===----------------------------------------------------------------------===//

/// Base-2 logarithm approximation
#define __haydn_log2 __builtin_haydn_log2
/// Base-2 exponential approximation
#define __haydn_exp2 __builtin_haydn_exp2
/// Reciprocal approximation
#define __haydn_recip __builtin_haydn_recip
/// Square root approximation
#define __haydn_sqrt __builtin_haydn_sqrt
/// Inverse square root approximation (Clang-only, no LLVM intrinsic)
#define __haydn_isqrt __builtin_haydn_isqrt
/// Arctangent approximation (Clang-only, no LLVM intrinsic)
#define __haydn_arctan __builtin_haydn_arctan

//===----------------------------------------------------------------------===//
// Normalization (NSA)
//===----------------------------------------------------------------------===//

/// Number of sign bits (32-bit)
#define __haydn_nsa32 __builtin_haydn_nsa32
/// Number of sign bits (32-bit, unsigned input)
#define __haydn_nsau32 __builtin_haydn_nsau32
/// Number of sign bits (64-bit)
#define __haydn_nsa64 __builtin_haydn_nsa64
/// Number of sign bits, 16-bit operand, low lane
#define __haydn_nsa16_l __builtin_haydn_nsa16_l
/// Number of sign bits, 32-bit operand, low lane
#define __haydn_nsa32_l __builtin_haydn_nsa32_l
/// Number of sign bits, 64-bit operand, with zero detection
#define __haydn_nsaz64 __builtin_haydn_nsaz64
/// Number of sign bits, 16-bit operand, low lane, with zero detection
#define __haydn_nsaz16_l __builtin_haydn_nsaz16_l
/// Number of sign bits, 32-bit operand, low lane, with zero detection
#define __haydn_nsaz32_l __builtin_haydn_nsaz32_l

//===----------------------------------------------------------------------===//
// Accumulator Shift and Pack
// Convert 64-bit accumulator to 32-bit result with rounding/saturation.
// Used by FIR/IIR kernels for final output conversion.
//===----------------------------------------------------------------------===//

/// Saturating arithmetic right shift 64->32: SAT32(acc >> shift)
#define __haydn_satsr64 __builtin_haydn_satsr64
/// Pack-shift-round: SAT32((acc + rounding) >> shift)
#define __haydn_packsr32 __builtin_haydn_packsr32

/// D215 FIX-B: paired pack-shift-round (2× DR64 → DR64). Avoids the
/// DR64→GPR32→DR64 cross-bank round-trip that scalarized AE_ROUND32X2F48S
/// into ~6 ops per lane. All in DR64, zero GPR32 traffic.
#define __haydn_packsr32x2_hh __builtin_haydn_packsr32x2_hh
#define __haydn_packsr32x2_hl __builtin_haydn_packsr32x2_hl
#define __haydn_packsr32x2_lh __builtin_haydn_packsr32x2_lh
#define __haydn_packsr32x2_ll __builtin_haydn_packsr32x2_ll

//===----------------------------------------------------------------------===//
// PACKSR32 lane family: AE_PKSR32-style dual-accumulator pack with shift+round.
// Packs two 32-bit results from accumulator pair (a, b) with shared per-lane
// arithmetic-right-shift + rounding + saturation. Each variant picks which
// 32-bit lane of each source feeds the high/low lane of the result.
// Used by NatureDSP IIR biquad kernels for the delay-line update + sat pack.
//===----------------------------------------------------------------------===//

/// Pack HH: result.H = SAT32(a.H >> shift + rnd), result.L = SAT32(b.H >> shift + rnd)
#define __haydn_packsr32x2_hh __builtin_haydn_packsr32x2_hh
/// Pack HL: result.H from a.H, result.L from b.L
#define __haydn_packsr32x2_hl __builtin_haydn_packsr32x2_hl
/// Pack LH: result.H from a.L, result.L from b.H
#define __haydn_packsr32x2_lh __builtin_haydn_packsr32x2_lh
/// Pack LL: result.H from a.L, result.L from b.L
#define __haydn_packsr32x2_ll __builtin_haydn_packsr32x2_ll

//===----------------------------------------------------------------------===//
// AE_MAXABS32S: per-lane saturating abs-max of two DR64 SIMD values.
// result = { SAT32(MAX(SAT_ABS(a.H), SAT_ABS(b.H))),
//            SAT32(MAX(SAT_ABS(a.L), SAT_ABS(b.L))) }
// Used 48+ times in NatureDSP FFT (fft_cplx_stages_S2_32x32) for magnitude
// tracking, and in IIR/math kernels for overflow detection.
//===----------------------------------------------------------------------===//

/// Per-lane saturating abs-max of two DR64 SIMD values
#define __haydn_maxabs32s __builtin_haydn_maxabs32s

//===----------------------------------------------------------------------===//
// Population Count and Bit Reversal (Clang-only, no LLVM intrinsics)
//===----------------------------------------------------------------------===//

/// Population count (32-bit)
#define __haydn_popcount32 __builtin_haydn_popcount32
/// Population count (64-bit)
#define __haydn_popcount64 __builtin_haydn_popcount64
/// Bit reversal (32-bit)
#define __haydn_brev32 __builtin_haydn_brev32

//===----------------------------------------------------------------------===//
// Wave 2: LC3 BASOP 16x16 fractional multiply
//===----------------------------------------------------------------------===//

/// Q15*Q15->Q31 fractional multiply (HS00 lane select)
#define __haydn_fmul16_hs00 __builtin_haydn_fmul16_hs00
/// Q15*Q15 MAC into Q31, accumulate-add both lanes (HS_11_00 lane select)
#define __haydn_fmulaa16_hs_11_00 __builtin_haydn_fmulaa16_hs_11_00
/// Q15*Q15 MSU into Q31, subtract both lanes (HS_11_00 lane select)
#define __haydn_fmulss16_hs_11_00 __builtin_haydn_fmulss16_hs_11_00

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_HS remaining lane-select variants (binary DR64)
//===----------------------------------------------------------------------===//

#define __haydn_fmul16_hs01 __builtin_haydn_fmul16_hs01
#define __haydn_fmul16_hs02 __builtin_haydn_fmul16_hs02
#define __haydn_fmul16_hs03 __builtin_haydn_fmul16_hs03
#define __haydn_fmul16_hs11 __builtin_haydn_fmul16_hs11
#define __haydn_fmul16_hs12 __builtin_haydn_fmul16_hs12
#define __haydn_fmul16_hs13 __builtin_haydn_fmul16_hs13
#define __haydn_fmul16_hs22 __builtin_haydn_fmul16_hs22
#define __haydn_fmul16_hs23 __builtin_haydn_fmul16_hs23
#define __haydn_fmul16_hs33 __builtin_haydn_fmul16_hs33

//===----------------------------------------------------------------------===//
// Wave 4: FMUL16_LS variants (binary DR64)
//===----------------------------------------------------------------------===//

#define __haydn_fmul16_ls00 __builtin_haydn_fmul16_ls00
#define __haydn_fmul16_ls01 __builtin_haydn_fmul16_ls01
#define __haydn_fmul16_ls02 __builtin_haydn_fmul16_ls02
#define __haydn_fmul16_ls03 __builtin_haydn_fmul16_ls03
#define __haydn_fmul16_ls11 __builtin_haydn_fmul16_ls11
#define __haydn_fmul16_ls12 __builtin_haydn_fmul16_ls12
#define __haydn_fmul16_ls13 __builtin_haydn_fmul16_ls13
#define __haydn_fmul16_ls22 __builtin_haydn_fmul16_ls22
#define __haydn_fmul16_ls23 __builtin_haydn_fmul16_ls23
#define __haydn_fmul16_ls33 __builtin_haydn_fmul16_ls33

//===----------------------------------------------------------------------===//
// Wave 4: FMULS16 HS/LS variants (binary DR64)
//===----------------------------------------------------------------------===//

#define __haydn_fmuls16_hs00 __builtin_haydn_fmuls16_hs00
#define __haydn_fmuls16_hs01 __builtin_haydn_fmuls16_hs01
#define __haydn_fmuls16_hs02 __builtin_haydn_fmuls16_hs02
#define __haydn_fmuls16_hs03 __builtin_haydn_fmuls16_hs03
#define __haydn_fmuls16_hs11 __builtin_haydn_fmuls16_hs11
#define __haydn_fmuls16_hs12 __builtin_haydn_fmuls16_hs12
#define __haydn_fmuls16_hs13 __builtin_haydn_fmuls16_hs13
#define __haydn_fmuls16_hs22 __builtin_haydn_fmuls16_hs22
#define __haydn_fmuls16_hs23 __builtin_haydn_fmuls16_hs23
#define __haydn_fmuls16_hs33 __builtin_haydn_fmuls16_hs33
#define __haydn_fmuls16_ls00 __builtin_haydn_fmuls16_ls00
#define __haydn_fmuls16_ls01 __builtin_haydn_fmuls16_ls01
#define __haydn_fmuls16_ls02 __builtin_haydn_fmuls16_ls02
#define __haydn_fmuls16_ls03 __builtin_haydn_fmuls16_ls03
#define __haydn_fmuls16_ls11 __builtin_haydn_fmuls16_ls11
#define __haydn_fmuls16_ls12 __builtin_haydn_fmuls16_ls12
#define __haydn_fmuls16_ls13 __builtin_haydn_fmuls16_ls13
#define __haydn_fmuls16_ls22 __builtin_haydn_fmuls16_ls22
#define __haydn_fmuls16_ls23 __builtin_haydn_fmuls16_ls23
#define __haydn_fmuls16_ls33 __builtin_haydn_fmuls16_ls33

//===----------------------------------------------------------------------===//
// Wave 4: FMULAA16 HS/LS MAC variants (binary DR64)
// NOTE: these are 2-input pure-mul ops on Haydn hardware despite the "AA"
// suffix. Only _11_00 is a true 3-arg accumulator MAC.
//===----------------------------------------------------------------------===//

#define __haydn_fmulaa16_hs_13_02 __builtin_haydn_fmulaa16_hs_13_02
#define __haydn_fmulaa16_hs_33_22 __builtin_haydn_fmulaa16_hs_33_22
#define __haydn_fmulaa16_ls_11_00 __builtin_haydn_fmulaa16_ls_11_00
#define __haydn_fmulaa16_ls_13_02 __builtin_haydn_fmulaa16_ls_13_02
#define __haydn_fmulaa16_ls_33_22 __builtin_haydn_fmulaa16_ls_33_22

//===----------------------------------------------------------------------===//
// Wave 4: FMULSS16 HS/LS MSU variants (binary DR64)
//===----------------------------------------------------------------------===//

#define __haydn_fmulss16_hs_13_02 __builtin_haydn_fmulss16_hs_13_02
#define __haydn_fmulss16_hs_33_22 __builtin_haydn_fmulss16_hs_33_22
#define __haydn_fmulss16_ls_11_00 __builtin_haydn_fmulss16_ls_11_00
#define __haydn_fmulss16_ls_13_02 __builtin_haydn_fmulss16_ls_13_02
#define __haydn_fmulss16_ls_33_22 __builtin_haydn_fmulss16_ls_33_22

//===----------------------------------------------------------------------===//
// Wave 2: IIR biquad fused dual MAC
//===----------------------------------------------------------------------===//

/// Fused add-add dual MAC, same-lane, sat+round
#define __haydn_f2mulaa32rs_hhll __builtin_haydn_f2mulaa32rs_hhll
/// Fused add-add dual MAC, cross-lane, sat+round
#define __haydn_f2mulaa32rs_hllh __builtin_haydn_f2mulaa32rs_hllh
/// Fused sub-sub dual MAC, same-lane, sat+round
#define __haydn_f2mulss32rs_hhll __builtin_haydn_f2mulss32rs_hhll
/// Fused sub-sub dual MAC, cross-lane, sat+round
#define __haydn_f2mulss32rs_hllh __builtin_haydn_f2mulss32rs_hllh

/// Zero-accumulator (binary) dual-product MAC, same-lane, sat+round.
/// `rtd = 0 + HH*HH + LL*LL` — no accumulator read (D235). This IS the binary
/// form that ISA-51 wrongly claimed did not exist.
#define __haydn_f2mulzaa32rs_hhll __builtin_haydn_f2mulzaa32rs_hhll
/// Zero-accumulator (binary) dual-product MAC, cross-lane, sat+round.
#define __haydn_f2mulzaa32rs_hllh __builtin_haydn_f2mulzaa32rs_hllh
/// Zero-accumulator (binary) dual-product MAC, same-lane, sat (no round).
#define __haydn_f2mulzaa32r_hhll __builtin_haydn_f2mulzaa32r_hhll
/// Zero-accumulator (binary) dual-product MAC, cross-lane, sat (no round).
#define __haydn_f2mulzaa32r_hllh __builtin_haydn_f2mulzaa32r_hllh

//===----------------------------------------------------------------------===//
// Wave 2: Shift with rounding
//===----------------------------------------------------------------------===//

/// 64-bit shift-right with rounding (immediate shift amount)
#define __haydn_srai64r __builtin_haydn_srai64r

//===----------------------------------------------------------------------===//
// Wave 2: Complex multiply
//===----------------------------------------------------------------------===//

/// Quad 16-bit complex multiply, sat+round
#define __haydn_x4fcmul16rs __builtin_haydn_x4fcmul16rs
/// Quad 16-bit complex MAC, sat+round
#define __haydn_x4fcmula16rs __builtin_haydn_x4fcmula16rs

//===----------------------------------------------------------------------===//
// Wave 3: SFR Flag Register Predication
// Compare->SFR->conditional-move pattern for SIMD predication.
//
// Usage pattern:
//   __haydn_dr64_t cmp = __haydn_x2seq32(a, b);  // set SFR per lane
//   __haydn_dr64_t result = __haydn_x2movt32(fallthrough, cmp_val); // select
//
// X2 variants operate on dual 32-bit lanes in DR64.
// X4 variants operate on quad 16-bit lanes in DR64.
//===----------------------------------------------------------------------===//

/// Dual 32-bit signed equal compare -> SFR flags
#define __haydn_x2seq32 __builtin_haydn_x2seq32
/// Dual 32-bit signed less-than compare -> SFR flags
#define __haydn_x2slt32 __builtin_haydn_x2slt32
/// Dual 32-bit signed less-or-equal compare -> SFR flags
#define __haydn_x2sle32 __builtin_haydn_x2sle32
/// Dual 32-bit conditional move if SFR false: dst = (SFR==0) ? src2 : src1
#define __haydn_x2movf32 __builtin_haydn_x2movf32
/// Dual 32-bit conditional move if SFR true: dst = (SFR==1) ? src2 : src1
#define __haydn_x2movt32 __builtin_haydn_x2movt32
/// D215 FIX-B: native per-lane min/max (avoids the SFR slt+movt path).
/// D217 FIX-F: SIMD rounding arithmetic right shift by immediate.
#define __haydn_x2srai32r __builtin_haydn_x2srai32r
/// D219: native DR64→GPR32 lane extract (1 op, was 20-op scalar i64 shift).
#define __haydn_movad32_h __builtin_haydn_movad32_high
/// D220 Tier 1: wire remaining scalar-C macros to native intrinsics.
#define __haydn_x4srai16r __builtin_haydn_x4srai16r
#define __haydn_sll64 __builtin_haydn_sll64
#define __haydn_srl64 __builtin_haydn_srl64
#define __haydn_and64 __builtin_haydn_and64
#define __haydn_or64  __builtin_haydn_or64
#define __haydn_movad32_l __builtin_haydn_movad32_low
#define __haydn_x2min32 __builtin_haydn_x2min32
#define __haydn_x2max32 __builtin_haydn_x2max32
#define __haydn_x4min16 __builtin_haydn_x4min16
#define __haydn_x4max16 __builtin_haydn_x4max16
/// Quad 16-bit signed equal compare -> SFR flags
#define __haydn_x4seq16 __builtin_haydn_x4seq16
/// Quad 16-bit signed less-than compare -> SFR flags
#define __haydn_x4slt16 __builtin_haydn_x4slt16
/// Quad 16-bit signed less-or-equal compare -> SFR flags
#define __haydn_x4sle16 __builtin_haydn_x4sle16
/// Quad 16-bit conditional move if SFR false: dst = (SFR==0) ? src2 : src1
#define __haydn_x4movf16 __builtin_haydn_x4movf16
/// Quad 16-bit conditional move if SFR true: dst = (SFR==1) ? src2 : src1
#define __haydn_x4movt16 __builtin_haydn_x4movt16

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Compare -> SFR
// Compare a DR64 value and set the SFR register (like SEQ64).
// Usage: __haydn_dr64_t cmp = __haydn_slt64(a);  // SFR = (a < $rd) ? 1 : 0
//===----------------------------------------------------------------------===//

/// 64-bit signed less-than compare -> SFR
#define __haydn_slt64 __builtin_haydn_slt64
/// 64-bit signed less-or-equal compare -> SFR
#define __haydn_sle64 __builtin_haydn_sle64

//===----------------------------------------------------------------------===//
// Wave 5: Scalar 64-bit SFR Conditional Move
// MOVT64: dst = (SFR == 4'b1111) ? src : dst  (move if true)
// MOVF64: dst = (SFR == 4'b0000) ? src : dst  (move if false)
// Usage:
//   __haydn_slt64(a);                   // set SFR
//   result = __haydn_movt64(val);       // conditionally move
//===----------------------------------------------------------------------===//

/// 64-bit conditional move if SFR true: dst = (SFR==1111) ? src : dst
#define __haydn_movt64 __builtin_haydn_movt64
/// 64-bit conditional move if SFR false: dst = (SFR==0000) ? src : dst
#define __haydn_movf64 __builtin_haydn_movf64

//===----------------------------------------------------------------------===//
// Wave 5: SFR Register Transfer
// Read/write the 4-bit Status Flag Register (SFR).
//===----------------------------------------------------------------------===//

/// Read SFR into GPR32 low bits: rt = {28'b0, SFR}
#define __haydn_movesfr2gpr __builtin_haydn_movesfr2gpr
/// Write SFR from GPR32 low bits: SFR = rs[3:0]
#define __haydn_movegpr2sfr __builtin_haydn_movegpr2sfr
/// Clear SFR: SFR = 4'b0000
#define __haydn_zero_sfr __builtin_haydn_zero_sfr

//===----------------------------------------------------------------------===//
// Cross-RF move: DR64 half-lane -> GPR32 (MOVE32_DR_L / MOVE32_DR_H)
//
// The first real DR->GPR cross-bank move in the ISA (Rev 2, 2026-06-18):
//   __haydn_move32_dr_l(rsd) -> rsd[31:00]   (slot 0 ALU)
//   __haydn_move32_dr_h(rsd) -> rsd[63:32]   (slot 0 ALU)
//===----------------------------------------------------------------------===//
#define __haydn_move32_dr_l __builtin_haydn_move32_dr_l
#define __haydn_move32_dr_h __builtin_haydn_move32_dr_h

//===----------------------------------------------------------------------===//
// Circular Buffer Load/Store (CBR)
//
// Haydn has 2 hardware circular-buffer sets (cbr_sel 0/1). Each CBR set is a
// CSR pair CBR_BEGIN/CBR_END (Rev 2: no CBR_SIZE — size = END-BEGIN+1). The
// CB load/store instructions automatically wrap the address pointer within
// the buffer boundaries, and the hardware ALWAYS post-increments the base
// register in place.
//
// D206 two-def API: the pointer is passed BY POINTER (in/out) so the
// compiler surfaces the hardware's updated cursor (Hexagon
// `memd(Rx++#I:circ(Mu))` / AIE G_AIE_POSTINC_LOAD idiom). The builtin:
//   int64_t data = __haydn_ldw_cb_imm(&ptr, 0, 8);  // ptr updated in place
// returns the loaded data AND writes the wrapped cursor back to *ptr.
//
// The cbr_sel parameter (0 or 1) selects which CBR set wraps.
//===----------------------------------------------------------------------===//

/// 64-bit load from circular buffer (data-only; pointer is hardware-managed — D207).
#define __haydn_ldw_cb_imm __builtin_haydn_ldw_cb_imm
/// 64-bit load from circular buffer, register stride.
#define __haydn_ldw_cb_reg __builtin_haydn_ldw_cb_reg
/// 64-bit store to circular buffer.
#define __haydn_sdw_cb_imm __builtin_haydn_sdw_cb_imm
/// 64-bit store to circular buffer, register stride.
#define __haydn_sdw_cb_reg __builtin_haydn_sdw_cb_reg

//===----------------------------------------------------------------------===//
// Circular Buffer Setup (CBR programming via CSRW)
//
// Programs a CBR set before any CB load/store is issued. cbr_sel is 0 or 1
// (Rev 2: 2 sets; there is no CBR_SIZE register — the buffer size is derived
// as CBR_END - CBR_BEGIN + 1). Maps to a CSR write:
//   cbr_sel=0 -> CBR_BEGIN = CSR 0x2C, CBR_END = CSR 0x2D
//   cbr_sel=1 -> CBR_BEGIN = CSR 0x2E, CBR_END = CSR 0x2F
//===----------------------------------------------------------------------===//
#define __haydn_setcbr_begin __builtin_haydn_setcbr_begin
#define __haydn_setcbr_end   __builtin_haydn_setcbr_end

//===----------------------------------------------------------------------===//
// AR (Aligned Register) unaligned load/store stream
//
// Hardware funnel-shift via AR0–AR3 (ar_sel = 0..3).
//
// Pointer contract (do not expose HW AGU writeback to C):
//   - Builtins return data (loads) / void (stores). They do NOT return the
//     post-incremented base.
//   - The program's next pointer is ordinary C arithmetic (ptr + stride).
//     SCEV / loop opts only need that GEP chain — enough for IP streams.
//   - IP helpers below advance *pptr in C after the builtin; that advance
//     is the IR cursor, not a readback of HW writeback.
//
// dir_sel: 0 = forward (+stride), 1 = reverse (-stride).
//===----------------------------------------------------------------------===//

/// Pre-load AR[ar_sel] from mem64[ptr & ~7].
#define __haydn_pldwwua __builtin_haydn_pldwwua
/// Load 4×i16 unaligned via AR (data only; advance ptr in C if needed).
#define __haydn_d_lqhwua_post __builtin_haydn_d_lqhwua_post
/// Load 2×i32 unaligned via AR (data only).
#define __haydn_d_ltwua_post __builtin_haydn_d_ltwua_post
/// Flush AR[ar_sel] to 0.
#define __haydn_flar __builtin_haydn_flar
/// Write-back residual AR bytes to memory at ptr.
#define __haydn_wbarwua __builtin_haydn_wbarwua
/// Store 4×i16 unaligned via AR.
#define __haydn_d_sqhwua_post __builtin_haydn_d_sqhwua_post
/// Store 2×i32 unaligned via AR.
#define __haydn_d_stwua_post __builtin_haydn_d_stwua_post

/// IP helpers: issue the AR op then advance the *C* cursor (SCEV-visible).
static inline int64_t __haydn_d_lqhwua_post_ip(int *pptr, int ar_sel,
                                               int stride, int dir_sel) {
  int64_t v = __haydn_d_lqhwua_post(*pptr, ar_sel, stride, dir_sel);
  *pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);
  return v;
}
static inline int64_t __haydn_d_ltwua_post_ip(int *pptr, int ar_sel,
                                              int stride, int dir_sel) {
  int64_t v = __haydn_d_ltwua_post(*pptr, ar_sel, stride, dir_sel);
  *pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);
  return v;
}
static inline void __haydn_d_sqhwua_post_ip(int64_t data, int *pptr, int ar_sel,
                                            int stride, int dir_sel) {
  __haydn_d_sqhwua_post(data, *pptr, ar_sel, stride, dir_sel);
  *pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);
}
static inline void __haydn_d_stwua_post_ip(int64_t data, int *pptr, int ar_sel,
                                           int stride, int dir_sel) {
  __haydn_d_stwua_post(data, *pptr, ar_sel, stride, dir_sel);
  *pptr = dir_sel ? (*pptr - stride) : (*pptr + stride);
}

//===----------------------------------------------------------------------===//
// Bit-Reversed Addressing Load/Store
//
// These apply REVERSE32() to the base address before memory access,
// then post-increment the original base register. Used for FFT butterfly
// addressing patterns where bit-reversed index ordering is needed.
//
// Usage:
//   int ptr = 0;
//   int val = __haydn_lw_brev_imm(ptr, 4);  // load from REVERSE32(ptr), ptr += 4<<2
//===----------------------------------------------------------------------===//

/// 64-bit load from bit-reversed address, post-inc by immediate
#define __haydn_ldw_brev_imm __builtin_haydn_ldw_brev_imm
/// 64-bit load from bit-reversed address, post-inc by register
#define __haydn_ldw_brev_reg __builtin_haydn_ldw_brev_reg
/// 32-bit load from bit-reversed address, post-inc by immediate
#define __haydn_lw_brev_imm __builtin_haydn_lw_brev_imm
/// 32-bit load from bit-reversed address, post-inc by register
#define __haydn_lw_brev_reg __builtin_haydn_lw_brev_reg
/// 64-bit store to bit-reversed address, post-inc by immediate
#define __haydn_sdw_brev_imm __builtin_haydn_sdw_brev_imm
/// 64-bit store to bit-reversed address, post-inc by register
#define __haydn_sdw_brev_reg __builtin_haydn_sdw_brev_reg
/// 32-bit store to bit-reversed address, post-inc by immediate
#define __haydn_sw_brev_imm __builtin_haydn_sw_brev_imm
/// 32-bit store to bit-reversed address, post-inc by register
#define __haydn_sw_brev_reg __builtin_haydn_sw_brev_reg

//===----------------------------------------------------------------------===//
// Bit-Reversed Add (BREV32) — FFT butterfly addressing
//
// Computes rt = bitreverse(bitreverse(rs1) + rs2). Used as a post-increment
// address modifier when walking an FFT-stage buffer in bit-reversed index
// order with a linear stride. Maps 1:1 to NatureDSP AE_ADDBRBA32(idx, stride).
//===----------------------------------------------------------------------===//

/// Bit-reversed add: result = bitreverse(bitreverse(a) + b)
#define __haydn_addbrba32 __builtin_haydn_addbrba32

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD non-saturating arithmetic
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD add (non-saturating)
#define __haydn_x2add32 __builtin_haydn_x2add32
/// Dual 32-bit SIMD subtract (non-saturating)
#define __haydn_x2sub32 __builtin_haydn_x2sub32
/// Quad 16-bit SIMD add (non-saturating)
#define __haydn_x4add16 __builtin_haydn_x4add16
/// Quad 16-bit SIMD subtract (non-saturating)
#define __haydn_x4sub16 __builtin_haydn_x4sub16

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD HLLH cross-lane variants
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD add, cross-lane HLLH
#define __haydn_x2add32_hllh __builtin_haydn_x2add32_hllh
/// Dual 32-bit SIMD saturating add, cross-lane HLLH
#define __haydn_x2add32s_hllh __builtin_haydn_x2add32s_hllh
/// Dual 32-bit SIMD subtract, cross-lane HLLH
#define __haydn_x2sub32_hllh __builtin_haydn_x2sub32_hllh
/// Dual 32-bit SIMD saturating subtract, cross-lane HLLH
#define __haydn_x2sub32s_hllh __builtin_haydn_x2sub32s_hllh

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD register shifts
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD logical left shift by register
#define __haydn_x2sll32 __builtin_haydn_x2sll32
/// Dual 32-bit SIMD arithmetic right shift by register
#define __haydn_x2sra32 __builtin_haydn_x2sra32
/// Dual 32-bit SIMD logical right shift by register
#define __haydn_x2srl32 __builtin_haydn_x2srl32
/// Quad 16-bit SIMD logical left shift by register
#define __haydn_x4sll16 __builtin_haydn_x4sll16
/// Quad 16-bit SIMD arithmetic right shift by register
#define __haydn_x4sra16 __builtin_haydn_x4sra16
/// Quad 16-bit SIMD logical right shift by register
#define __haydn_x4srl16 __builtin_haydn_x4srl16

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD immediate shifts
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD logical left shift by immediate
#define __haydn_x2slli32 __builtin_haydn_x2slli32
/// Dual 32-bit SIMD arithmetic right shift by immediate
#define __haydn_x2srai32 __builtin_haydn_x2srai32
/// Dual 32-bit SIMD logical right shift by immediate
#define __haydn_x2srli32 __builtin_haydn_x2srli32
/// Quad 16-bit SIMD logical left shift by immediate
#define __haydn_x4slli16 __builtin_haydn_x4slli16
/// Quad 16-bit SIMD arithmetic right shift by immediate
#define __haydn_x4srai16 __builtin_haydn_x4srai16
/// Quad 16-bit SIMD logical right shift by immediate
#define __haydn_x4srli16 __builtin_haydn_x4srli16

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD shift with rounding
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD arithmetic right shift with rounding
#define __haydn_x2sra32r __builtin_haydn_x2sra32r
/// Quad 16-bit SIMD arithmetic right shift with rounding
#define __haydn_x4sra16r __builtin_haydn_x4sra16r

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD horizontal reductions
//===----------------------------------------------------------------------===//

/// Dual 32-bit horizontal add, high lane
#define __haydn_x2hadd32_h __builtin_haydn_x2hadd32_h
/// Dual 32-bit horizontal saturating add, high lane
#define __haydn_x2hadd32s_h __builtin_haydn_x2hadd32s_h
/// Dual 32-bit horizontal add, low lane
#define __haydn_x2hadd32_l __builtin_haydn_x2hadd32_l
/// Dual 32-bit horizontal saturating add, low lane
#define __haydn_x2hadd32s_l __builtin_haydn_x2hadd32s_l
/// Quad 16-bit horizontal add, high half
#define __haydn_x4hadd16_h __builtin_haydn_x4hadd16_h
/// Quad 16-bit horizontal add, low half
#define __haydn_x4hadd16_l __builtin_haydn_x4hadd16_l
/// Dual 32-bit horizontal maximum
#define __haydn_x2hmax32 __builtin_haydn_x2hmax32
/// Dual 32-bit horizontal minimum
#define __haydn_x2hmin32 __builtin_haydn_x2hmin32
/// Quad 16-bit horizontal maximum
#define __haydn_x4hmax16 __builtin_haydn_x4hmax16
/// Quad 16-bit horizontal minimum
#define __haydn_x4hmin16 __builtin_haydn_x4hmin16

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD dot product
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD dot product
#define __haydn_x2dot32 __builtin_haydn_x2dot32
/// Quad 16-bit SIMD dot product
#define __haydn_x4dot16 __builtin_haydn_x4dot16

//===----------------------------------------------------------------------===//
// Wave 5: X2/X4 SIMD multiply (2-dest, D400 Path B)
//===----------------------------------------------------------------------===//
//
// X2MUL32/X4MUL16 are 2-dest non-accumulator ops (D_RR2): they produce TWO
// 64-bit results from two 64-bit sources.
//   X2MUL32: hi = rsd1[63:32]*rsd2[63:32] ; lo = rsd1[31:0]*rsd2[31:0].
//   X4MUL16: hi[63:32]=lane3, hi[31:0]=lane2 ; lo[63:32]=lane1, lo[31:0]=lane0.

/// Dual 32-bit SIMD multiply (2-dest).
static inline haydn_dpair_t __haydn_x2mul32(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2mul32_pair(&r.lo, a, b);
  return r;
}
/// Quad 16-bit SIMD multiply (2-dest).
static inline haydn_dpair_t __haydn_x4mul16(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4mul16_pair(&r.lo, a, b);
  return r;
}

//===----------------------------------------------------------------------===//
// Wave 5: X2 SIMD multiply-pair variants
//===----------------------------------------------------------------------===//

/// Dual 32-bit multiply-pair high
#define __haydn_x2mulph32 __builtin_haydn_x2mulph32
/// Dual 32-bit multiply-pair low
#define __haydn_x2mulpl32 __builtin_haydn_x2mulpl32
/// Dual 32-bit multiply-accumulate pair high
#define __haydn_x2mulaph32 __builtin_haydn_x2mulaph32
/// Dual 32-bit multiply-accumulate pair low
#define __haydn_x2mulapl32 __builtin_haydn_x2mulapl32
/// Dual 32-bit multiply-subtract pair high
#define __haydn_x2mulsph32 __builtin_haydn_x2mulsph32
/// Dual 32-bit multiply-subtract pair low
#define __haydn_x2mulspl32 __builtin_haydn_x2mulspl32

//===----------------------------------------------------------------------===//
// Wave 5: X2 32-bit complex fractional multiply
//===----------------------------------------------------------------------===//

/// Dual 32-bit fractional complex multiply, round+sat
#define __haydn_x2fcmul32rs __builtin_haydn_x2fcmul32rs
/// Dual 32-bit fractional complex multiply, round+sat variant
#define __haydn_x2fcmul32rss __builtin_haydn_x2fcmul32rss
/// Dual 32-bit fractional complex MAC, round+sat
#define __haydn_x2fcmula32rs __builtin_haydn_x2fcmula32rs
/// Dual 32-bit fractional complex MAC, round+sat variant
#define __haydn_x2fcmula32rss __builtin_haydn_x2fcmula32rss

//===----------------------------------------------------------------------===//
// Wave 5: X2 CMUL F2 variants (2-dest, D400 Path B)
//===----------------------------------------------------------------------===//
//
// X2CMUL32_F2/X2CMUL32S_F2 are the Format-2-encoded spellings of the 2-dest
// complex multiply (same semantics as X2CMUL32/X2CMUL32S: hi=real, lo=imag).

/// Dual 32-bit complex multiply, Format-2 encoding (2-dest).
static inline haydn_dpair_t __haydn_x2cmul32_f2(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2cmul32_f2_pair(&r.lo, a, b);
  return r;
}
/// Dual 32-bit complex multiply with saturation, Format-2 encoding (2-dest).
static inline haydn_dpair_t __haydn_x2cmul32s_f2(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x2cmul32s_f2_pair(&r.lo, a, b);
  return r;
}

//===----------------------------------------------------------------------===//
// Wave 5: X2 FF2 shift variants
//===----------------------------------------------------------------------===//

/// Dual 32-bit Format-2 fractional shift, saturate+truncate
#define __haydn_x2ff2rsst32 __builtin_haydn_x2ff2rsst32
/// Dual 32-bit Format-2 fractional shift, round
#define __haydn_x2ff2rst32 __builtin_haydn_x2ff2rst32

//===----------------------------------------------------------------------===//
// Wave 5: X4 FF2MUL variants (2-dest, D400 Path B)
//===----------------------------------------------------------------------===//
//
// X4FF2MUL16S is a 2-dest non-accumulator op (D_RR2); X4FF2MULA16S /
// X4FF2MULS16S are 2-dest accumulator ops (D_RRA2). All produce TWO 64-bit
// results (lanes 3,2 → hi; lanes 1,0 → lo).

/// Quad 16-bit Format-2 fractional multiply, saturating (2-dest).
static inline haydn_dpair_t __haydn_x4ff2mul16s(uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4ff2mul16s_pair(&r.lo, a, b);
  return r;
}
/// Quad 16-bit Format-2 fractional MAC, saturating (2-dest accumulator).
static inline haydn_dpair_t __haydn_x4ff2mula16s(uint64_t acc1, uint64_t acc2,
                                                 uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4ff2mula16s_pair(&r.lo, acc1, acc2, a, b);
  return r;
}
/// Quad 16-bit Format-2 fractional MSU, saturating (2-dest accumulator).
static inline haydn_dpair_t __haydn_x4ff2muls16s(uint64_t acc1, uint64_t acc2,
                                                 uint64_t a, uint64_t b) {
  haydn_dpair_t r;
  r.hi = __builtin_haydn_x4ff2muls16s_pair(&r.lo, acc1, acc2, a, b);
  return r;
}

//===----------------------------------------------------------------------===//
// Wave 5: X4 pack/sat/sel
//===----------------------------------------------------------------------===//

/// Quad 16-bit saturate 32-bit elements to 16-bit range
#define __haydn_x4sat32t16 __builtin_haydn_x4sat32t16
/// Quad 16-bit select with 4-bit immediate
#define __haydn_x4seli16 __builtin_haydn_x4seli16
/// Dual 32-bit lane select (used by AE_SEL32_*)
#define __haydn_x2sel32_hh __builtin_haydn_x2sel32_hh
#define __haydn_x2sel32_hl __builtin_haydn_x2sel32_hl
#define __haydn_x2sel32_lh __builtin_haydn_x2sel32_lh
#define __haydn_x2sel32_ll __builtin_haydn_x2sel32_ll

//===----------------------------------------------------------------------===//
// MOVDA family — GPR32 <-> DR64 lane transfers.
//
// The Haydn ISA has no native cross-bank move (see ISA-10 / D90). Each
// intrinsic lowers to a 4-5 instruction SP-spill sequence generated by the
// MOV_GPR_TO_DR64 / MOV_DR64_TO_GPR pseudo expansion in HaydnInstrInfo.cpp.
// Use sparingly inside hot loops — prefer to keep scalars in GPR32 and SIMD
// values in DR64 throughout a kernel.
//===----------------------------------------------------------------------===//

/// Place i32 in the low lane of a DR64, zero-fill the high lane.
#define __haydn_movda32 __builtin_haydn_movda32
/// Place two i32 inputs into the two DR64 lanes (low, high).
#define __haydn_movda32x2 __builtin_haydn_movda32x2
/// Place i32 in the low lane (16-bit source port — DR64 lanes are i32-wide,
/// so the low 16 bits of the i32 input occupy the lane directly).
#define __haydn_movda16 __builtin_haydn_movda16
/// Extract the low 32-bit lane of a DR64 into a GPR32.
#define __haydn_movad32_low __builtin_haydn_movad32_low
/// Extract the high 32-bit lane of a DR64 into a GPR32.
#define __haydn_movad32_high __builtin_haydn_movad32_high

//===----------------------------------------------------------------------===//
// MULFC32X16RAS -- Complex 32x16 MAC with rounding + saturation.
//
// NatureDSP AE_MULFC32X16RAS_{L,H} compatibility wrappers. The Haydn ISA
// has no single instruction covering this exact operation (see ISA-08
// mulfc32x16ras-gap.md and decision D90). The wrappers perform the
// 16->32 lane widen of the twiddle in C -- Haydn lacks a cross-lane
// 16->32 pack instruction -- and then dispatch to the underlying
// __builtin_haydn_mulfc32x16ras_{low,high} builtin, which the ISel
// selector lowers onto X2FCMULA32RS (the 32x32 complex MAC with round
// and saturation).
//
// Semantics (matching HiFi3 AE_MULFC32X16RAS):
//   acc.re += data.re * tw.re - data.im * tw.im   (with Q1.31 round + sat)
//   acc.im += data.re * tw.im + data.im * tw.re   (with Q1.31 round + sat)
//
//   acc  : DR64 holding two saturated Q1.31 lanes (re in low, im in high)
//   data : DR64 holding 32-bit complex data (re in low lane, im in high lane)
//   tw   : DR64 holding two complex 16-bit Q1.15 twiddles (4x16 layout):
//          bits[15:0]  = low.re, bits[31:16] = low.im,
//          bits[47:32] = high.re, bits[63:48] = high.im.
//          _low selects the low complex pair, _high selects the high pair.
//===----------------------------------------------------------------------===//

static __inline__ __attribute__((always_inline))
long long __haydn_mulfc32x16ras_low(long long acc, long long data,
                                     long long tw) {
  /* D217 FIX-H: widen the low 16-bit complex twiddle to 32-bit using native
   * SIMD shifts (x2slli32 + x2srai32) instead of scalar C trunc/sext/lshr/shl/or
   * which produced 6+ scalar ops + stack spills per call. The low pair
   * {re16@[15:0], im16@[31:16]} is already in the low DR64 lanes — just
   * sign-extend them from 16 to 32 bits via left-shift-then-arithmetic-shift. */
  long long tw_shifted = __builtin_haydn_x2slli32(tw, 16);
  long long tw_widened = __builtin_haydn_x2srai32(tw_shifted, 16);
  return __builtin_haydn_mulfc32x16ras_low(acc, data, tw_widened);
}

static __inline__ __attribute__((always_inline))
long long __haydn_mulfc32x16ras_high(long long acc, long long data,
                                      long long tw) {
  /* D217 FIX-H: widen the high 16-bit complex twiddle {re16@[47:32],
   * im16@[63:48]} to 32-bit using native SIMD shifts. */
  long long tw_low = __builtin_haydn_x2srli32(tw, 32);
  long long tw_shifted = __builtin_haydn_x2slli32(tw_low, 16);
  long long tw_widened = __builtin_haydn_x2srai32(tw_shifted, 16);
  return __builtin_haydn_mulfc32x16ras_high(acc, data, tw_widened);
}

//===----------------------------------------------------------------------===//
// FIR-specific MAC family (see D91-fir-mac-family-decomposition).
//
// These model the HiFi3 dual/quad-output FIR MAC intrinsics used by the
// NatureDSP FIR kernel family (bkfir16x16, convol16x16, blms16x16,
// firdec32x32, etc.). Haydn has no native dual/quad-output FIR MAC op
// (see ISA-09), so each intrinsic is one MAC step that the kernel caller
// issues once per output accumulator lane. The `acc` operand is the in/out
// accumulator lane; init variants ignore its prior value.
//===----------------------------------------------------------------------===//

/// 32x32 fractional MAC, HH lanes: acc += a_h * b_h (saturating).
/// Renamed from __haydn_mulafd32x16x2_fir_hh (D180 A2): the old name claimed
/// 32x16 dual but the backing op (FMULA32S_HH) is 32x32 single-lane. The old
/// name is kept as a compat alias below for NatureDSP source compatibility.
#define __haydn_mulaa32s_fir_hh __builtin_haydn_mulaa32s_fir_hh
/// 32x32 fractional MAC, LH lanes: acc += a_l * b_h (saturating). Renamed
/// from __haydn_mulafd32x16x2_fir_hl (D180 A2).
#define __haydn_mulaa32s_fir_hl __builtin_haydn_mulaa32s_fir_hl
/// Compat alias: old name -> new name (D180 A2). NatureDSP source and the
/// AE_MUL*FD32X16* wrappers in haydn_dsp.h use the old __haydn_ name.
#ifndef __haydn_mulafd32x16x2_fir_hh
#define __haydn_mulafd32x16x2_fir_hh __haydn_mulaa32s_fir_hh
#endif
#ifndef __haydn_mulafd32x16x2_fir_hl
#define __haydn_mulafd32x16x2_fir_hl __haydn_mulaa32s_fir_hl
#endif
/// 16x16 fractional multiply init, lane-pair 0/1: rtd = a_l0 * b_l0.
#define __haydn_mulfq16x2_fir_3 __builtin_haydn_mulfq16x2_fir_3
/// 16x16 fractional multiply init, lane-pair 2/3: rtd = a_l2 * b_l2.
#define __haydn_mulfq16x2_fir_1 __builtin_haydn_mulfq16x2_fir_1
/// 16x16 fractional MAC, lane-pair 1+0: acc += a_l1*b_l1 + a_l0*b_l0.
#define __haydn_mulafq16x2_fir_3 __builtin_haydn_mulafq16x2_fir_3
/// 16x16 fractional MAC, lane-pair 3+2: acc += a_l3*b_l3 + a_l2*b_l2.
#define __haydn_mulafq16x2_fir_1 __builtin_haydn_mulafq16x2_fir_1

#endif /* __HAYDN_INTRIN_H */
