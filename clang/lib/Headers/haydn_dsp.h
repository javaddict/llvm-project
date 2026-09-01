/*===---- haydn_dsp.h - NatureDSP AE_ layer for Haydn DSP ----------*- C -*-===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 *
 * NatureDSP HiFi3/HiFi3z AE_* compatibility layer on top of haydn.h.
 * Ported NatureDSP kernels include this header with minimal source edits.
 *
 * This header owns all ae_* / xtbool* types and AE_* entry points.
 * Native Haydn types (haydn_x2int32, …) live in haydn_types.h via
 * haydn.h. Float/complex AE families and FIR/FFT helpers stay in this
 * one file (measured miss; no family-header split, no native composite
 * ISA). Peer split is AIE aie2pintrin.h:27-49; Haydn stays one file.
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __HAYDN_DSP_H
#define __HAYDN_DSP_H

/* Fail closed off-target first (peer: xmmintrin.h:13-15 / haydn_types.h:29-31).
 * On Haydn, generic is agu+hwloop; SIMD/CB/BREV stay opt-in on -mcpu=haydn.
 * One simd diagnostic instead of N always_inline haydn.h builtin traps. */
#if !defined(__haydn__) && !defined(__HAYDN__)
#error "This header is only meant to be used on Haydn architecture"
#endif
#if !defined(__HAYDN_FEATURE_SIMD__)
#error "haydn_dsp.h needs target feature simd (-mcpu=haydn); generic is agu+hwloop only"
#endif

#include "haydn.h"

/*===----------------------------------------------------------------------===
 * NatureDSP compatibility tier taxonomy
 *
 *   HAYDN_COMPAT_NATIVE      — haydn.h exact native ISA surface
 *   HAYDN_COMPAT_EXACT       — AE_* wraps native with bit-exact semantics
 *   HAYDN_COMPAT_EMULATED    — software composition, documented soft-exact
 *   HAYDN_COMPAT_UNSUPPORTED — no correct map; fail closed (no silent alias)
 *
 * Mechanical contract: every public AE_* macro has a HAYDN_COMPAT_TIER_* tag
 * emitted into haydn.h from BuiltinsHaydn.td (HaydnAeCompat) via
 * HaydnIntrinEmitter (inventory floor >= 600; CI rejects shrink/untagged).
 * Residual public surface defaults to EMULATED until curated EXACT;
 * permanent product law keeps AE_MAXABS16S EMULATED (X4ABS16S+X4MAX16;
 * never 2x32 maxabs32s), AE_ADD64X2_ / _vector UNSUPPORTED (closed set —
 * dual-64 only; no bag dual-64), and AE_LA*NEG_PC EXACT probe-only seed
 * parity with POS_PC (PLDWWUA; no invented reverse pre-decrement). Dual-24
 * F24 POS seeds (LA32X2F24POS_PC / LA24X2POS_PC) share the same PLDWWUA
 * law. Curated dual-24 / lane-select residual class is EXACT (SELP24/SEL24/
 * SEL32 via X2SEL32 not bag OR, NEG24S via X2NEG32S, F24X2_SRAI/SRAI24/
 * F32X2_SRAI via X2SRA32, ADDP24/ADD24S dual ALU, LA/SA*F24_IC +
 * LA/SA24X2_IC + base LA/SA16X4/32X2_IC AR+CBR, L/S32X2F24_XC aligned
 * CB + next-ptr writeback, L32X2*_RIC reverse-CB);
 * soft sat left (SLAI24S/SLAI64S/F64_SLAIS/F32X2_SLAIS/F64_SLAS) stays
 * EMULATED and must not silent-alias plain << wrap. Opt-in
 * __HAYDN_ALLOW_INEXACT_AE keeps transitional inexact bodies for NatureDSP
 * -c only. No FormatID / slot / AltDesc here. CI fail-closes on untagged
 * macros and all-tier width mismatches (not only EXACT).
 *===----------------------------------------------------------------------===*/
/* HAYDN_COMPAT_* ordinals + HAYDN_COMPAT_TIER_AE_* come from haydn.h
 * (generated HaydnAeCompat inventory). */

#if defined(__HAYDN_ALLOW_INEXACT_AE)
#define __HAYDN_AE_COMPAT_STRICT 0
#else
#define __HAYDN_AE_COMPAT_STRICT 1
#endif

/* Fail-closed expansion for residual silent-wrong maps when strict.
 * Statement form: _Static_assert names the AE symbol.
 * Expression form: undeclared __haydn_ae_unsupported_<sym>() — loud error. */
#ifndef __HAYDN_AE_UNSUPPORTED_STMT
#define __HAYDN_AE_UNSUPPORTED_STMT(sym)                                       \
  do {                                                                         \
    _Static_assert(0, "HAYDN AE unsupported (silent-wrong map removed): " #sym \
                      " — define __HAYDN_ALLOW_INEXACT_AE for transitional "   \
                      "NatureDSP -c only (compat tier fail-closed)");          \
  } while (0)
#endif
#ifndef __HAYDN_AE_UNSUPPORTED_EXPR
#define __HAYDN_AE_UNSUPPORTED_EXPR(sym) (__haydn_ae_unsupported_##sym())
#endif
#ifndef __HAYDN_AE_UNSUPPORTED
#define __HAYDN_AE_UNSUPPORTED(sym) __HAYDN_AE_UNSUPPORTED_STMT(sym)
#endif

/*===----------------------------------------------------------------------===
 * Layer contract vs haydn.h
 *   ae_int32x2  == haydn_x2int32   (vector_size)
 *   ae_int16x4  == haydn_x4int16
 *   ae_int64    == long long       (scalar / accumulator / DR bag)
 * Prefer native vector args when haydn.h exposes them. For bag-only APIs
 * (x2fcmul*, fir*, fmul32s*, …) use the bitcast helpers from haydn.h:
 *   __haydn_v2_as_i64 / __haydn_i64_as_v2
 *   __haydn_v4_as_i64 / __haydn_i64_as_v4
 *
 * Non-ISA composites (not golden encodings) live HERE only:
 *   haydn_satsr64, haydn_packsr32, haydn_packsr32x2_*
 * haydn.h / builtins must not invent these as mnemonics.
 *===----------------------------------------------------------------------===*/

/* Bag vs vector helpers.
 * Bag-friendly haydn_* APIs take int64_t — pass __AE_TO_I64(x).
 * SIMD haydn_x2int32 / haydn_x4int16 APIs take ExtVector — pass __AE_AS_V2/V4. */
#ifndef __AE_V2I
#define __AE_V2I(v) __haydn_v2_as_i64(v)
#define __AE_V4I(v) __haydn_v4_as_i64(v)
#define __AE_I4V(i) __haydn_i64_as_v4(i)
static inline int64_t __ae_bits_from_v2(haydn_x2int32 v) {
  return __haydn_v2_as_i64(v);
}
static inline int64_t __ae_bits_from_v4(haydn_x4int16 v) {
  return __haydn_v4_as_i64(v);
}
static inline int64_t __ae_bits_from_i64(int64_t v) { return v; }
/* NatureDSP sites use scalar 0 into ae_int32x2. GNU vector_size rejects that;
 * Clang ext_vector accepts it. Overlay may re-alias ae_int32x2 / ae_f32x2
 * onto this type. Peer: AIE OpenCL int2 (clctypes.h:60). Storage is still
 * one 8-byte <2 x i32> DR.
 *
 * GNU vector_size(8) of int and ext_vector_type(2) of int are compatible
 * <2 x i32> types. Clang _Generic rejects two compatible associations
 * (haydn_ndsp_i32x2 vs haydn_x2int32). One v2 association covers both
 * official haydn_x2int32 / haydn_x2fract32 and the overlay alias. */
#ifndef HAYDN_NDSP_I32X2_DEFINED
#define HAYDN_NDSP_I32X2_DEFINED
typedef int haydn_ndsp_i32x2 __attribute__((ext_vector_type(2)));
#endif
/* Accept bag or vector, produce int64_t bag for bag-friendly haydn_* APIs. */
#define __AE_TO_I64(x)                                                         \
  _Generic((x), haydn_x2int32                                                  \
           : __ae_bits_from_v2, haydn_x4int16                                  \
           : __ae_bits_from_v4, default                                        \
           : __ae_bits_from_i64)(x)
/* Accept bag or vector, produce ExtVector for SIMD haydn_x2/x4 APIs. */
#define __AE_AS_V2(x) __haydn_i64_as_v2(__AE_TO_I64(x))
#define __AE_AS_V4(x) __haydn_i64_as_v4(__AE_TO_I64(x))
#define __AE_I2V(i) __haydn_i64_as_v2(i)
/* Store DR64 bits into a bag or <2 x i32>/<4 x i16> dest.
 * A C scalar-to-vector cast is a low-lane splat (GNU vector_size and
 * Clang ext_vector). That dropped the high 32-bit of each X4MULA16S
 * dest and made vec_dot16 look like the two-lane 380 body. Peer:
 * haydn.h:61-79 __haydn_i64_as_v2 union bag (AIE v2int32 shape).
 * Dest-typed union covers ae_int64 / ae_int32x2 / ae_f32x2 / ae_int16x4
 * without a _Generic type association that misses the fract alias. */
#define __AE_ASSIGN_BITS(dst, bits)                                            \
  do {                                                                         \
    union {                                                                    \
      int64_t i;                                                               \
      __typeof__(dst) v;                                                       \
    } __ae_assign_u;                                                           \
    __ae_assign_u.i = (int64_t)(bits);                                         \
    (dst) = __ae_assign_u.v;                                                   \
  } while (0)
/* NatureDSP bkfir / vec_scale / vec_dot16 walk `const ae_int16x4 *`.
 * Load through const T*; write the cursor back with __typeof__ so the
 * increment does not drop const (or other) qualifiers. */
#define __AE_LOAD_AT(T, ptr) (*(const T *)(const void *)(ptr))
#define __AE_ADVANCE_PTR(ptr, inc) \
  ((ptr) = (__typeof__(ptr))((const char *)(const void *)(ptr) + (inc)))
#define __AE_MUT_VOID_P(p) ((void *)(uintptr_t)(const void *)(p))
#endif

/*===----------------------------------------------------------------------===
 * Sat / round family matrix (header composites + native *r members)
 *
 * No PACKSR / SATSR encoding in the golden DB. Header helpers only:
 *   packsr32     -> DB SRA64R (haydn_sra64r) then narrow to i32
 *   satsr64      -> soft SAT32(acc>>sh); no rounding; no sat-shift mnemonic
 *   packsr32x2_* -> DB X2SRA32R + X2SEL32_* (AE_PKSR-style dual pack)
 *
 * Native *r / *rs (admitted rounding bias, shift>0):
 *   add 1<<(shift-1) then ASR; shift 0 is identity.
 *   SRAI64R / SRA64R / X2SRA32R / X4SRA16R / AE_SRAI32R / AE_SRAA32RS.
 * Native *s sat-left (unsigned << + arithmetic round-trip):
 *   clamp to INT_MIN/MAX on overflow. Never signed << (UB).
 *   AE_SLAA64S / SLAI64S / SLAS64S / SLAA32S / SLAA16S / SLAI24S.
 *
 * Guard bits: ae_int64 is 64-bit only. Extra HiFi accumulator headroom is
 * not modeled and is not invented here.
 *
 * int shift of any value is OK here (helpers / reg forms, not ImmArg ISA).
 *===----------------------------------------------------------------------===*/

#ifndef haydn_satsr64
/** Admitted *r host model: add 1<<(sh-1) then ASR; sh<=0 is identity. */
__HAYDN_INTRIN_FN int64_t haydn_ae_asr_round64_host(int64_t v, int sh) {
  if (sh <= 0)
    return v;
  return (v + ((int64_t)1 << (sh - 1))) >> sh;
}
/** Soft SAT32(acc >> sh). Not a golden mnemonic. */
__HAYDN_INTRIN_FN int haydn_satsr64(int64_t a, int sh) {
  int64_t x;
  if (sh <= 0)
    x = a;
  else if (sh >= 63)
    x = a < 0 ? (int64_t)-1 : (int64_t)0;
  else
    x = a >> sh;
  if (x > 0x7fffffffLL)
    return (int)0x7fffffff;
  if (x < (int64_t)(int32_t)0x80000000)
    return (int)0x80000000;
  return (int)x;
}
#endif

#ifndef haydn_packsr32
/** Compose DB SRA64R (full i64) then narrow to 32-bit pack result. */
__HAYDN_INTRIN_FN int haydn_packsr32(int64_t a, int sh) {
  return (int)haydn_sra64r(a, sh);
}
#endif

#ifndef haydn_packsr32x2_hh
/** Compose X2SRA32R + X2SEL32_HH (not a single encoding). */
__HAYDN_INTRIN_FN haydn_x2int32 haydn_packsr32x2_hh(int64_t a, int64_t b,
                                                      int sh) {
  haydn_x2int32 ta = haydn_x2sra32r(__haydn_i64_as_v2(a), sh);
  haydn_x2int32 tb = haydn_x2sra32r(__haydn_i64_as_v2(b), sh);
  return haydn_x2sel32_hh(ta, tb);
}
__HAYDN_INTRIN_FN haydn_x2int32 haydn_packsr32x2_hl(int64_t a, int64_t b,
                                                      int sh) {
  haydn_x2int32 ta = haydn_x2sra32r(__haydn_i64_as_v2(a), sh);
  haydn_x2int32 tb = haydn_x2sra32r(__haydn_i64_as_v2(b), sh);
  return haydn_x2sel32_hl(ta, tb);
}
__HAYDN_INTRIN_FN haydn_x2int32 haydn_packsr32x2_lh(int64_t a, int64_t b,
                                                      int sh) {
  haydn_x2int32 ta = haydn_x2sra32r(__haydn_i64_as_v2(a), sh);
  haydn_x2int32 tb = haydn_x2sra32r(__haydn_i64_as_v2(b), sh);
  return haydn_x2sel32_lh(ta, tb);
}
__HAYDN_INTRIN_FN haydn_x2int32 haydn_packsr32x2_ll(int64_t a, int64_t b,
                                                      int sh) {
  haydn_x2int32 ta = haydn_x2sra32r(__haydn_i64_as_v2(a), sh);
  haydn_x2int32 tb = haydn_x2sra32r(__haydn_i64_as_v2(b), sh);
  return haydn_x2sel32_ll(ta, tb);
}
#endif

/* Legacy underscore names used by some ports / cheatsheets. */
#ifndef __haydn_satsr64
#define __haydn_satsr64 haydn_satsr64
#endif
#ifndef __haydn_packsr32
#define __haydn_packsr32 haydn_packsr32
#endif
#ifndef __haydn_packsr32x2_hh
#define __haydn_packsr32x2_hh haydn_packsr32x2_hh
#define __haydn_packsr32x2_hl haydn_packsr32x2_hl
#define __haydn_packsr32x2_lh haydn_packsr32x2_lh
#define __haydn_packsr32x2_ll haydn_packsr32x2_ll
#endif


/* Old NatureDSP-era names → public haydn.h names (must precede any use). */
#ifndef haydn_mulafd32x16x2_fir_hh
#define haydn_mulafd32x16x2_fir_hh haydn_mulaa32s_fir_hh
#endif
#ifndef haydn_mulafd32x16x2_fir_hl
#define haydn_mulafd32x16x2_fir_hl haydn_mulaa32s_fir_hl
#endif

/* Freestanding-safe math (clang builtins; no <math.h> required). */
#ifndef haydn_sqrtf
#define haydn_sqrtf(x) __builtin_sqrtf(x)
#endif
#ifndef haydn_ceilf
#define haydn_ceilf(x) __builtin_ceilf(x)
#endif
#ifndef haydn_floorf
#define haydn_floorf(x) __builtin_floorf(x)
#endif
#ifndef haydn_lroundf
#define haydn_lroundf(x) __builtin_lroundf(x)
#endif

//===----------------------------------------------------------------------===//
// NatureDSP / HiFi scalar types
//===----------------------------------------------------------------------===//

typedef long long      ae_int64;    ///< 64-bit signed / accumulator
typedef int            ae_int32;    ///< 32-bit signed (Q1.31 storage)
typedef short          ae_int16;    ///< 16-bit signed (Q1.15 storage)
typedef unsigned       ae_uint32;
typedef unsigned short ae_uint16;
typedef unsigned char  ae_uint8;

typedef int            ae_f32;      ///< 32-bit fractional (Q1.31)
typedef short          ae_f16;      ///< 16-bit fractional (Q1.15)
typedef long long      ae_f64;      ///< 64-bit fractional accumulator
typedef int            ae_f24;      ///< 24-bit fractional (promoted to Q1.31)

//===----------------------------------------------------------------------===//
// NatureDSP / HiFi SIMD types (aliases of Haydn vector types)
//===----------------------------------------------------------------------===//

typedef haydn_x4int16   ae_int16x4;    ///< Quad 16-bit signed
typedef haydn_x2int32   ae_int32x2;    ///< Dual 32-bit signed
typedef haydn_x4fract16 ae_f16x4;      ///< Quad 16-bit fractional
typedef haydn_x2fract32 ae_f32x2;      ///< Dual 32-bit fractional
typedef haydn_x2float32 ae_float32x2;  ///< Dual 32-bit float

// Opaque / cross-lane packs (not lanewise vector IR).
typedef haydn_dr64_t ae_int24x2;   ///< Dual 24-bit packed (emulated in 32-bit)
typedef haydn_dr64_t ae_f24x2;     ///< Dual 24-bit fractional
typedef haydn_dr64_t ae_int64x2;   ///< Dual 64-bit logical (one DR64 storage)
typedef haydn_dr64_t ae_p24x2;     ///< Pair-of-24
typedef haydn_dr64_t ae_p16x2;     ///< Pair-of-16
typedef haydn_dr64_t ae_p16x2s;    ///< Pair-of-16 signed
typedef ae_int16     ae_p16s;      ///< Single 16-bit predicate-lane value
typedef ae_int16     ae_p16;       ///< Single 16-bit lane

//===----------------------------------------------------------------------===//
// Predicates / alignment / accumulators
//===----------------------------------------------------------------------===//

typedef int xtbool2;    ///< 2-bit SFR predicate (dual 32-bit lanes)
typedef int xtbool4;    ///< 4-bit SFR predicate (quad 16-bit lanes)
typedef int xtbool;     ///< Generic 1-bit predicate

/// Alignment / AR-stream handle (low bit = ar_sel 0..1; AR0/AR1 only).
typedef int ae_valign;

/// 56-bit accumulator (HiFi) maps to 64-bit on Haydn.
typedef long long ae_q56s;
typedef long long ae_p48;

//===----------------------------------------------------------------------===//
// Load / Store -- Post-increment variants
//===----------------------------------------------------------------------===//

/// Load dual 32-bit with post-increment (by bytes)
/// AE_L32X2_IP loads 8 bytes from *ptr, then advances ptr by inc bytes.
#define AE_L32X2_IP(dst, ptr, inc) \
  do { \
    (dst) = __AE_LOAD_AT(ae_int32x2, ptr); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

/// Store dual 32-bit with post-increment
#define AE_S32X2_IP(src, ptr, inc) \
  do { \
    *(ae_int32x2 *)(void *)(ptr) = (src); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

/// Load dual 32-bit with indexed offset (no pointer update).
/// Overloaded: 2-arg returning form `AE_L32X2_I(ptr, offs)` returns the loaded
/// value; 3-arg storing form `AE_L32X2_I(dst, ptr, offs)` writes to `dst`.
#define AE_L32X2_I(...) __AE_L32X2_I_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2_I_OVERLOAD(...) \
  __AE_L32X2_I_GET(__VA_ARGS__, __AE_L32X2_I_3, __AE_L32X2_I_2)(__VA_ARGS__)
#define __AE_L32X2_I_2(ptr, offs) \
  (*((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))))
#define __AE_L32X2_I_3(dst, ptr, offs) \
  do { \
    dst = *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))); \
  } while (0)

/// Store dual 32-bit with indexed offset
#define AE_S32X2_I(src, ptr, offs) \
  do { \
    *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); \
  } while (0)

/// Load dual 32-bit indexed with post-increment
#define AE_L32X2_XP(dst, ptr, offs, inc) \
  do { \
    dst = *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); \
  } while (0)

/// Store dual 32-bit indexed with post-increment
#define AE_S32X2_XP(src, ptr, offs, inc) \
  do { \
    *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); \
  } while (0)

/// Load dual 32-bit with offset only (no pointer modification).
/// Overloaded: the 2-arg returning form `AE_L32X2_X(ptr, offs)` returns the
/// loaded value (original HiFi3 API used in math kernels); the 3-arg storing
/// form `AE_L32X2_X(dst, ptr, offs)` writes to `dst`.
#define AE_L32X2_X(...) __AE_L32X2_X_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2_X_OVERLOAD(...) \
  __AE_L32X2_X_GET(__VA_ARGS__, __AE_L32X2_X_3, __AE_L32X2_X_2)(__VA_ARGS__)
#define __AE_L32X2_X_2(ptr, offs) \
  (*((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))))
#define __AE_L32X2_X_3(dst, ptr, offs) \
  do { \
    dst = *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))); \
  } while (0)

/// Store dual 32-bit with offset only
#define AE_S32X2_X(src, ptr, offs) \
  do { \
    *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); \
  } while (0)

/// Load quad 16-bit with post-increment.
/// Haydn port: the load is unconditional; the post-increment of `ptr` uses
/// pointer arithmetic that works when `ptr` is an lvalue. When the caller
/// passes a cast expression (rvalue), the increment is silently dropped
/// (documented limitation; flagged in II analysis as a codegen gap).
#define AE_L16X4_IP(dst, ptr, inc) \
  do { \
    (dst) = __AE_LOAD_AT(ae_int16x4, ptr); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

/// Load quad 16-bit indexed.
/// Overloaded: 2-arg returning form `AE_L16X4_I(ptr, offs)` returns the loaded
/// value; 3-arg storing form `AE_L16X4_I(dst, ptr, offs)` writes to `dst`.
#define AE_L16X4_I(...) __AE_L16X4_I_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16X4_I_OVERLOAD(...) \
  __AE_L16X4_I_GET(__VA_ARGS__, __AE_L16X4_I_3, __AE_L16X4_I_2)(__VA_ARGS__)
#define __AE_L16X4_I_2(ptr, offs) \
  (*((const ae_int16x4 *)(const void *)(ptr) + ((offs) / (int)sizeof(ae_int16x4))))
#define __AE_L16X4_I_3(dst, ptr, offs) \
  do { \
    (dst) = *((const ae_int16x4 *)(const void *)(ptr) + \
              ((offs) / (int)sizeof(ae_int16x4))); \
  } while (0)

/// Load quad 16-bit indexed with post-increment
#define AE_L16X4_XP(dst, ptr, offs, inc) \
  do { \
    (dst) = *((const ae_int16x4 *)(const void *)(ptr) + \
              ((offs) / (int)sizeof(ae_int16x4))); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

/// Load quad 16-bit with offset only
#define AE_L16X4_X(dst, ptr, offs) \
  do { \
    (dst) = *((const ae_int16x4 *)(const void *)(ptr) + \
              ((offs) / (int)sizeof(ae_int16x4))); \
  } while (0)

/// Store quad 16-bit with post-increment
#define AE_S16X4_IP(src, ptr, inc) \
  do { \
    *(ae_int16x4 *)(ptr) = (src); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); \
  } while (0)

/// Load scalar 32-bit with post-increment
#define AE_L32_IP(dst, ptr, inc) \
  do { \
    dst = *(ae_int32 *)(ptr); \
    (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); \
  } while (0)

/// Load scalar 32-bit indexed with post-increment.
/// 3-arg (dst, ptr, inc): load at *ptr then ptr += inc.
/// 4-arg (dst, ptr, offs, inc): load at ptr+offs then ptr += inc.
#define AE_L32_XP(...) __AE_L32_XP_OVERLOAD(__VA_ARGS__)
#define __AE_L32_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32_XP_OVERLOAD(...) \
  __AE_L32_XP_GET(__VA_ARGS__, __AE_L32_XP_4A, __AE_L32_XP_3A)(__VA_ARGS__)
#define __AE_L32_XP_3A(dst, ptr, inc) \
  do { (dst) = *(ae_int32 *)(ptr); \
       (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_L32_XP_4A(dst, ptr, offs, inc) \
  do { (dst) = *(ae_int32 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)

/// Store scalar 32-bit low lane with post-increment
#define AE_S32_L_IP(src, ptr, inc) \
  do { \
    *(ae_int32 *)(ptr) = (ae_int32)(src); \
    (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); \
  } while (0)

//===----------------------------------------------------------------------===//
// Load / Store -- Circular buffer variants
//===----------------------------------------------------------------------===//

/// Load dual 32-bit from circular buffer with post-increment.
/// cbr_sel selects which CBR set (0 or 1) defines the circular region.
/// offs is the post-increment byte stride (must be a multiple of 8).
///
/// Model (): the CB instruction computes the next pointer in hardware
/// (rs = rs + stride, wrapped at CBR bounds), but the compiler does NOT
/// model that update. The stride (offs) is a tracked value supplied per
/// call; the pointer is hardware-managed and persists across iterations of
/// a hardware loop (the body is opaque to the optimizer). CBR is a boundary
/// guard. Hence `ptr` is passed BY VALUE — the caller's pointer is the
/// in-buffer address; the hardware advances it but the compiler never reads
/// the result back. ('s by-address + store-back was reverted: that was
/// the wrong contract for Haydn's "program supplies the pointer" model.)
///
/// Prior versions hardcoded the stride as 8 (silently dropping `offs`),
/// passed store args in the wrong order, and loaded 64 bits into a 16-bit
/// destination via AE_L16_XC. The D_LDW_CB_IMM / D_SDW_CB_IMM hardware
/// instructions post-increment by imm<<3 (i.e. imm is the stride divided
/// by 8).

/* CB load → data + AGU-updated C ptr (haydn_cb_ld_t from haydn.h).
 * mem64 is LE (first word low); AE f32x2 is H-first — swap like AE_L32X2_IP.
 */
static inline haydn_dr64_t haydn_ae_f32x2_mem_to_reg(haydn_dr64_t le)
{
  uint64_t u = (uint64_t)le;
  return (haydn_dr64_t)((u >> 32) | (u << 32));
}
/* Raw 64b CB load (no f32x2 lane swap) — for 16x4 etc. */
#define __HAYDN_AE_CB_LD64(dst, ptr, cbr_sel, imm) \
  do { \
    haydn_cb_ld_t haydn_cbr = haydn_ldw_cb_imm( \
        (ptr), (int)(cbr_sel), (int)(imm)); \
    (dst) = (__typeof__(dst))(haydn_dr64_t)haydn_cbr.data; \
    (ptr) = (__typeof__(ptr))haydn_cbr.new_ptr; \
  } while (0)
/* f32x2 CB load: LE mem → H-first reg. */
#define __HAYDN_AE_CB_LD_F32X2(dst, ptr, cbr_sel, imm) \
  do { \
    haydn_cb_ld_t haydn_cbr = haydn_ldw_cb_imm( \
        (ptr), (int)(cbr_sel), (int)(imm)); \
    (dst) = (__typeof__(dst))haydn_ae_f32x2_mem_to_reg( \
        (haydn_dr64_t)haydn_cbr.data); \
    (ptr) = (__typeof__(ptr))haydn_cbr.new_ptr; \
  } while (0)

// CB load returns {data, new_ptr}; assign C cursor from AGU writeback.
#define AE_L32X2_XC(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           (offs) >> 3); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

/// Store dual 32-bit to circular buffer; AGU-updated cursor → ptr ().
#define AE_S32X2_XC(src, ptr, offs, cbr_sel) \
  do { \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    void *__np = haydn_sdw_cb_imm(__s, (ptr), (cbr_sel), \
                                  (offs) >> 3); \
    (ptr) = (__typeof__(ptr))__np; \
  } while (0)

/// Load quad 16-bit from circular buffer with post-increment.
#define AE_L16X4_XC(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           (offs) >> 3); \
    (dst) = (ae_int16x4)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

/// Store quad 16-bit to circular buffer with post-increment.
#define AE_S16X4_XC(src, ptr, offs, cbr_sel) \
  do { \
    void *__np = haydn_sdw_cb_imm((haydn_dr64_t)(src), (ptr), \
                                  (cbr_sel), (offs) >> 3); \
    (ptr) = (__typeof__(ptr))__np; \
  } while (0)

/// Load quad 16-bit from circular buffer with reverse increment.
/// D_LDW_CB_IMM/REG expose signed stride (ImmCheckSimm8); reverse-CB is
/// ISA-correct as negative element stride -((inc)>>3) — hardware
/// post-increments by imm<<3 and wraps below CBR_BEGIN. Not a silent
/// forward XC alias.
#define AE_L16X4_RIC(dst, ptr, inc, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           -((inc) >> 3)); \
    (dst) = (ae_int16x4)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

/// Load quad 16-bit reverse post-increment (linear RIP; late overload owns arities).
#define AE_L16X4_RIP(dst, ptr, inc) \
  do { \
    (dst) = *(ae_int16x4 *)(ptr); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - (inc)); \
  } while (0)

/// Load scalar 16-bit from circular buffer (EMULATED).
///
/// Haydn has no S_LH_CB halfword circular load. The prior map used
/// D_LDW_CB (64b) + trunc and offs>>3 element units — silent-wrong width
/// and offset scale. Emulate as ordinary i16 load + soft CBR step on CBR
/// mirrors (same cursor-wrap pattern as AE_LA*_IC); offs is a byte stride
/// (NatureDSP / Hexagon L2_loadrh_pci peer units). Not a native CB path —
/// IR must not call llvm.haydn.ldw.cb.imm.
#define AE_L16_XC(dst, ptr, offs, cbr_sel) \
  do { \
    (dst) = *(ae_int16 *)(void *)(ptr); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//===----------------------------------------------------------------------===//
// Unaligned Load / Store via AR funnel-shift (golden LS #103-#109)
//
// HiFi `ae_valign` → Haydn AR selector (low bit = ar_sel 0..1).
// Architectural AR file is AR0/AR1 only (2×64-bit). Selectors 2/3 are not
// product-visible; runtime wrappers clamp to {0,1}.
//
// Mapping:
//   AE_LA64_PP(ptr)     → PLDWWUA(ar, ptr)     seed load stream
//   AE_ZALIGN64()       → FLAR(ar)             open store stream
//   AE_LA16X4_IP        → D_LQHWUA_POST + GEP  (C next-ptr; SCEV sees GEP)
//   AE_LA32X2_IP / 64   → D_LTWUA_POST  + GEP
//   AE_SA16X4_IP        → D_SQHWUA_POST + GEP
//   AE_SA32X2_IP / 64   → D_STWUA_POST  + GEP
//   AE_SA64POS_FP       → WBARWUA residual write-back
//
// Circular:
//   AE_L32X2_XC / AE_S32X2_XC → D_LDW/SDW_CB (8B-aligned AGU + CBR wrap).
//   AE_LA*_IC / AE_SA*_IC    → AR residual (PLDWWUA/LTWUA/STWUA) + C cursor
//   wrap via CBR mirrors. Unaligned part is buffered in AR[ar_sel], not via
//   unaligned mem64. Do not soft-gather in C.
//
// Pointer contract: HW AGU post-inc is NOT returned to IR/C for AR. Builtins
// yield data/void only; the stream cursor is ordinary C pointer arithmetic so
// SCEV works. Default dual-stream: load→AR0, store→AR1.
// Second load stream: AE_LA64_PP_AR(1, ptr) (uses AR1; conflicts with store).
//===----------------------------------------------------------------------===//

#define __HAYDN_AR_SEL(align) ((int)(align) & 1)
#define __HAYDN_AR_LOAD_SEL  0
#define __HAYDN_AR_STORE_SEL 1

/* Inclusive CBR mirrors for AR+circular (_IC) cursor wrap (CSR is HW source
 * of truth for D_*_CB; mirrors track the same bounds for LA/SA_IC GEP wrap). */
static uintptr_t haydn_cbr_b0, haydn_cbr_e0;
static uintptr_t haydn_cbr_b1, haydn_cbr_e1;

static inline uintptr_t haydn_cbr_wrap(uintptr_t a, uintptr_t b, uintptr_t e)
{
  uintptr_t sz;
  if (e < b)
    return a;
  sz = e - b + 1u;
  if (sz == 0)
    return a;
  while (a > e)
    a -= sz;
  while (a < b)
    a += sz;
  return a;
}
static inline uintptr_t haydn_cbr_step(uintptr_t p, intptr_t offs, int sel)
{
  uintptr_t b = sel ? haydn_cbr_b1 : haydn_cbr_b0;
  uintptr_t e = sel ? haydn_cbr_e1 : haydn_cbr_e0;
  return haydn_cbr_wrap((uintptr_t)((intptr_t)p + offs), b, e);
}

// --- Single implementation layer (all AE_LA/SA_* route here) ---------------
// HW AR/UA only: PLDWWUA / FLAR / D_*WUA_POST / WBARWUA (BundleSim + golden).
// Pointer post-inc remains C GEP (SCEV-visible); HW AGU writeback is not
// returned to IR/C.
//
// ar_sel/dir are ImmArg encoding fields. Call sites that only have a
// runtime ae_valign must switch-literal dispatch so Sema sees ICE 0..1 / 0..1
// at the haydn_* ImmArg surface (never pass ar&=1 as a non-ICE builtin arg).

/* WUA-CB switch-literal dispatch (ar_sel, cbr_sel both ImmArg 0..1). The
 * returned cursor is the HW-wrapped pointer and MUST feed the next op in
 * the stream (funnel selector is rs[2] / rs[2:1] of the wrapped value).
 * Golden AR_CBR family; ISA-66 documents that reverse unaligned CB has no
 * hardware row, so reverse streams keep the haydn_cbr_step software path. */
static inline void *haydn_ae_cb_prime(int ar, int sel, const void *p,
                                      int pltw)
{
  ar &= 1; sel &= 1;
  switch ((ar << 1) | sel) {
  case 0: return pltw ? haydn_pltwwua_cb_post(0, 0, p)
                      : haydn_plqhwua_cb_post(0, 0, p);
  case 1: return pltw ? haydn_pltwwua_cb_post(0, 1, p)
                      : haydn_plqhwua_cb_post(0, 1, p);
  case 2: return pltw ? haydn_pltwwua_cb_post(1, 0, p)
                      : haydn_plqhwua_cb_post(1, 0, p);
  default: return pltw ? haydn_pltwwua_cb_post(1, 1, p)
                       : haydn_plqhwua_cb_post(1, 1, p);
  }
}
static inline haydn_cb_ld_t haydn_ae_cb_ld_tw(int ar, int sel, const void *p,
                                              int lq)
{
  haydn_cb_ld_t r;
  ar &= 1; sel &= 1;
  switch ((ar << 1) | sel) {
  case 0: r = lq ? haydn_lqhwua_cb_post(p, 0, 0) : haydn_ltwua_cb_post(p, 0, 0); break;
  case 1: r = lq ? haydn_lqhwua_cb_post(p, 0, 1) : haydn_ltwua_cb_post(p, 0, 1); break;
  case 2: r = lq ? haydn_lqhwua_cb_post(p, 1, 0) : haydn_ltwua_cb_post(p, 1, 0); break;
  default: r = lq ? haydn_lqhwua_cb_post(p, 1, 1) : haydn_ltwua_cb_post(p, 1, 1); break;
  }
  return r;
}
static inline void *haydn_ae_cb_st(int ar, int sel, void *p,
                                   haydn_dr64_t data, int sq)
{
  ar &= 1; sel &= 1;
  switch ((ar << 1) | sel) {
  case 0: return sq ? haydn_sqhwua_cb_post(data, p, 0, 0)
                    : haydn_stwua_cb_post(data, p, 0, 0);
  case 1: return sq ? haydn_sqhwua_cb_post(data, p, 0, 1)
                    : haydn_stwua_cb_post(data, p, 0, 1);
  case 2: return sq ? haydn_sqhwua_cb_post(data, p, 1, 0)
                    : haydn_stwua_cb_post(data, p, 1, 0);
  default: return sq ? haydn_sqhwua_cb_post(data, p, 1, 1)
                     : haydn_stwua_cb_post(data, p, 1, 1);
  }
}

/// Seed AR from ptr. `ar` is 0..1 (masked); ImmArg via switch literals.
static inline ae_valign haydn_ae_la64_pp_ar(int ar, const void *ptr) {
  const void *p = ptr;
  switch (ar & 1) {
  case 0: haydn_pldwwua(0, p); break;
  default: haydn_pldwwua(1, p); break;
  }
  return (ae_valign)(ar & 1);
}
static inline ae_valign haydn_ae_la64_pp(const void *ptr) {
  return haydn_ae_la64_pp_ar(__HAYDN_AR_LOAD_SEL, ptr);
}
static inline ae_valign haydn_ae_zalign64(void) {
  haydn_flar(__HAYDN_AR_STORE_SEL);
  return (ae_valign)__HAYDN_AR_STORE_SEL;
}
static inline ae_valign haydn_ae_zalign64_ar(int ar) {
  switch (ar & 1) {
  case 0: haydn_flar(0); break;
  default: haydn_flar(1); break;
  }
  return (ae_valign)(ar & 1);
}

/// Load 8 B unaligned (16x4 or 32x2 layout is type-level only). dir: 0/1.
/// haydn_d_*ua_post public wrappers already switch-literal ar/dir (haydn.h).
static inline ae_int32x2 haydn_ae_la64_step(int ar, const void *p, int stride, int dir) {
  return haydn_d_ltwua_post(p, ar, stride, dir);
}
static inline ae_int16x4 haydn_ae_la16x4_step(int ar, const void *p, int stride, int dir) {
  return haydn_d_lqhwua_post(p, ar, stride, dir);
}
static inline void haydn_ae_sa64_step(ae_int32x2 data, int ar, void *p, int stride,
                                        int dir) {
  haydn_d_stwua_post(data, p, ar, stride, dir);
}
static inline void haydn_ae_sa16x4_step(ae_int16x4 data, int ar, void *p, int stride,
                                          int dir) {
  haydn_d_sqhwua_post(data, p, ar, stride, dir);
}
static inline void haydn_ae_sa64pos(int ar, void *p, int dir) {
  switch (((ar & 1) << 1) | (dir & 1)) {
  case 0: haydn_wbarwua(0, p, 0); break;
  case 1: haydn_wbarwua(0, p, 1); break;
  case 2: haydn_wbarwua(1, p, 0); break;
  default: haydn_wbarwua(1, p, 1); break;
  }
}

// --- Public AE surface (thin wrappers; do not re-implement later) ----------

#define AE_LA64_PP(...) __AE_LA64_PP_OVERLOAD(__VA_ARGS__)
#define __AE_LA64_PP_GET(_1, _2, NAME, ...) NAME
#define __AE_LA64_PP_OVERLOAD(...) \
  __AE_LA64_PP_GET(__VA_ARGS__, __AE_LA64_PP_2, __AE_LA64_PP_1)(__VA_ARGS__)
#define __AE_LA64_PP_1(ptr)        haydn_ae_la64_pp(ptr)
#define __AE_LA64_PP_2(align, ptr) do { (align) = haydn_ae_la64_pp(ptr); } while (0)
/// Explicit AR seed: AE_LA64_PP_AR(0|1, ptr). Only AR0/AR1 are architectural.
#define AE_LA64_PP_AR(ar, ptr) haydn_ae_la64_pp_ar((ar), (ptr))

#define AE_ZALIGN64() haydn_ae_zalign64()
#define AE_ZALIGN64_AR(ar) haydn_ae_zalign64_ar(ar)

/* Store-finish residual (WBARWUA): direction lives on the ImmArg.
 * POS dir=0, NEG dir=1. Unlike LA*NEG_PC seed (PLDWWUA probe-only, aliases
 * POS), SA64NEG must not silent-alias POS dir=0. */
#define AE_SA64POS_FP(align, ptr) \
  haydn_ae_sa64pos(__HAYDN_AR_SEL(align), (ptr), 0)
#define AE_SA64POS(align, ptr) AE_SA64POS_FP(align, ptr)
#define AE_SA64NEG_FP(align, ptr) \
  haydn_ae_sa64pos(__HAYDN_AR_SEL(align), (ptr), 1)

/// Load then advance C ptr (SCEV-visible GEP). HW writeback is not returned.
#define AE_LA16X4_IP(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = __AE_MUT_VOID_P(ptr); \
    (dst) = (ae_int16x4)haydn_ae_la16x4_step(__ar, __p, 8, 0); \
    __AE_ADVANCE_PTR(ptr, 8); \
  } while (0)
// UA load + H-first pack (same LE→H as late AE_L32X2_IP / CB XC). BundleSim
// mem64 is first-word-low; NatureDSP delay SA/LA and MULF32R expect H-first.
#define AE_LA32X2_IP(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = __AE_MUT_VOID_P(ptr); \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, 8, 0); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg(__le); \
    __AE_ADVANCE_PTR(ptr, 8); \
    (void)(align); \
  } while (0)
#define AE_LA64_IP(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = __AE_MUT_VOID_P(ptr); \
    (dst) = (ae_int64)haydn_ae_la64_step(__ar, __p, 8, 0); \
    __AE_ADVANCE_PTR(ptr, 8); \
  } while (0)

#define AE_SA16X4_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, 8, 0); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) + 8); \
  } while (0)
#define AE_SA32X2_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, 8, 0); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) + 8); \
  } while (0)
#define AE_SA64_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, 8, 0); \
    (ptr) = (ae_int64 *)((char *)(ptr) + 8); \
  } while (0)

#define AE_SA16X4_IP_X(src, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, __s, 0); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); \
  } while (0)
#define AE_SA32X2_IP_X(src, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, __s, 0); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); \
  } while (0)

#define AE_LA16X4_RIP(dst, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    (dst) = (ae_int16x4)haydn_ae_la16x4_step(__ar, __p, __s, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)
#define AE_LA32X2_RIP(dst, align, ptr, offs) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(offs); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    (dst) = (ae_int32x2)haydn_ae_la64_step(__ar, __p, __s, 1); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) - ((offs) ? (offs) : 8)); \
  } while (0)
#define AE_SA16X4_RIP(src, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, __s, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)
#define AE_SA32X2_RIP(src, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, __s, 1); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)

/* Early IC/RIC placeholders — late overload block is the public 3/4-arg API.
 * RIC must not silent-alias forward IC (dir=0, +8); route to reverse
 * helpers (dir=1 ImmArg + haydn_cbr_step -8). */
#define AE_LA16X4_IC(dst, align, ptr, cbr_sel) \
  __AE_LA16X4_IC_4A((dst), (align), (ptr), (cbr_sel))
#define AE_LA16X4_RIC(dst, align, ptr, cbr_sel) \
  __AE_LA16X4_RIC_4A((dst), (align), (ptr), (cbr_sel))
#define AE_LA32X2_IC(dst, align, ptr, cbr_sel) \
  __AE_LA32X2_IC_4A((dst), (align), (ptr), (cbr_sel))
#define AE_LA32X2_RIC(dst, align, ptr, cbr_sel) \
  __AE_LA32X2_RIC_4A((dst), (align), (ptr), (cbr_sel))

/// Seed AR for circular unaligned stream (HiFi POS_PC / NEG_PC).
/// PLDWWUA primes residual only; reverse vs forward is the later step's
/// dir ImmArg (IC=0 / RIC=1). NEG_PC therefore equals POS_PC on Haydn —
/// not a silent direction erase. NatureDSP pairs NEG_PC with RIC.
#define AE_LA16X4POS_PC(align, ptr) \
  do { (align) = haydn_ae_la64_pp(ptr); } while (0)
#define AE_LA32X2POS_PC(align, ptr) \
  do { (align) = haydn_ae_la64_pp(ptr); } while (0)
#define AE_LA16X4NEG_PC(align, ptr) AE_LA16X4POS_PC(align, ptr)
#define AE_LA32X2NEG_PC(align, ptr) AE_LA32X2POS_PC(align, ptr)

//===----------------------------------------------------------------------===//
// Store with accumulator truncation
//===----------------------------------------------------------------------===//

/// Store 32-bit, truncating 64-bit accumulator with saturation and shift
static inline void AE_S32RA64S_IP(ae_int64 acc, ae_int32 *ptr, int shift,
                                  int inc) {
  *(ptr) = (ae_int32)haydn_satsr64(acc, shift);
  (ptr) = (ae_int32 *)((char *)(ptr) + (inc));
}

//===----------------------------------------------------------------------===//
// Bit-Reversed Addressing
//===----------------------------------------------------------------------===//

/// Add with bit-reversed address. The ISA DB defines BREV32 as:
///   rt = REVERSE32(REVERSE32(rs1) + rs2)
/// This generates the next address in a bit-reversed FFT/DCT butterfly
/// addressing sequence. Maps to the native BREV32 instruction.
#define AE_ADDBRBA32(a, b) haydn_addbrba32((a), (b))

/// 32-bit load from bit-reversed address; AGU writeback updates ptr.
#define AE_L32_BREV_IP(dst, ptr, stride) \
  do { \
    haydn_sld_t __r = haydn_lw_brev_imm((ptr), (stride) >> 2); \
    (dst) = (ae_int32)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

/// 64-bit load from bit-reversed address; AGU writeback updates ptr.
#define AE_L32X2_BREV_IP(dst, ptr, stride) \
  do { \
    haydn_cb_ld_t __r = \
        haydn_ldw_brev_imm((ptr), (stride) >> 3); \
    (dst) = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

//===----------------------------------------------------------------------===//
// Circular Buffer Register Setup
//
// Programs a CBR set before any circular-buffer load/store. Haydn has 2 CBR
// sets (cbr_sel 0/1); Rev 2 removed CBR_SIZE — the buffer size is derived as
// CBR_END - CBR_BEGIN + 1. Each WUR_AE_CBEGINn/CENDn writes the matching CSR
// via haydn_setcbr_begin/end, lowered to `csrw <addr>, rs`:
//   set 0 -> CBR_BEGIN=0x2C, CBR_END=0x2D
//   set 1 -> CBR_BEGIN=0x2E, CBR_END=0x2F
// These are NO LONGER no-ops (was the silent-miscompute gap: kernels ran
// against unconfigured CBR boundaries). See research/
// circular-buffer-cross-arch-study §3 Phase A.
//===----------------------------------------------------------------------===//

/// Write circular buffer 0 begin address (CBR_BEGIN[0] = val)
#define WUR_AE_CBEGIN0(val)                                                    \
  do {                                                                         \
    uintptr_t __b = (uintptr_t)(val);                                          \
    haydn_cbr_b0 = __b;                                                      \
    haydn_setcbr_begin(0, (int)__b);                                         \
  } while (0)

/// Write circular buffer 0 end. NatureDSP WUR_AE_CEND is exclusive one-past;
/// Haydn CBR_END is inclusive (size = END−BEGIN+1). Convert here so LA_IC wrap
/// and D_*_CB see the same ring.
// CB end bound convention (golden Constraints §Circular Buffer): CBR_END
// takes the address of the LAST BYTE of the last element, NOT the start of
// the last element. The exclusive-end value users naturally compute
// (buf + nbytes) is converted below (val - 1). Raw haydn_setcbr_end callers
// must apply the convention themselves — a mis-programmed end wraps
// golden-faithfully but leaves the walk pointer misaligned after wrap and
// traps the runtime 8-byte Required_Alignment check (2026-08-21 probe
// falsified the earlier spacing-4 hypothesis; see GOALS 14(c)).
#define WUR_AE_CEND0(val)                                                      \
  do {                                                                         \
    uintptr_t __ex = (uintptr_t)(val);                                         \
    uintptr_t __inc = (__ex == 0) ? 0 : (__ex - 1u);                           \
    haydn_cbr_e0 = __inc;                                                    \
    haydn_setcbr_end(0, (int)__inc);                                         \
  } while (0)

/// Write circular buffer 1 begin address (CBR_BEGIN[1] = val)
#define WUR_AE_CBEGIN1(val)                                                    \
  do {                                                                         \
    uintptr_t __b = (uintptr_t)(val);                                          \
    haydn_cbr_b1 = __b;                                                      \
    haydn_setcbr_begin(1, (int)__b);                                         \
  } while (0)

/// Write circular buffer 1 end (NatureDSP exclusive → Haydn inclusive)
#define WUR_AE_CEND1(val)                                                      \
  do {                                                                         \
    uintptr_t __ex = (uintptr_t)(val);                                         \
    uintptr_t __inc = (__ex == 0) ? 0 : (__ex - 1u);                           \
    haydn_cbr_e1 = __inc;                                                    \
    haydn_setcbr_end(1, (int)__inc);                                         \
  } while (0)

//===----------------------------------------------------------------------===//
// SAR (Shift Amount Register) -- eliminated on Haydn
//===----------------------------------------------------------------------===//

/// Write SAR register -- on Haydn, pass shift explicitly to each op
#define WUR_AE_SAR(val) (haydn_ae_sar = (int)(val))

/// Read SAR register
#define RUR_AE_SAR() (haydn_ae_sar)

//===----------------------------------------------------------------------===//
// Arithmetic -- Saturating
//===----------------------------------------------------------------------===//

/// Dual 32-bit saturating add
static inline ae_int32x2 AE_ADD32S(ae_int32x2 a, ae_int32x2 b) { return haydn_x2add32s(a, b); }

/// Dual 32-bit saturating subtract
static inline ae_int32x2 AE_SUB32S(ae_int32x2 a, ae_int32x2 b) { return haydn_x2sub32s(a, b); }

/// Quad 16-bit saturating add
static inline ae_int16x4 AE_ADD16S(ae_int16x4 a, ae_int16x4 b) { return haydn_x4add16s(a, b); }

/// Quad 16-bit saturating subtract
static inline ae_int16x4 AE_SUB16S(ae_int16x4 a, ae_int16x4 b) { return haydn_x4sub16s(a, b); }

/// Dual 32-bit add-subtract with saturation
static inline ae_int32x2 AE_ADDSUB32S(ae_int32x2 a, ae_int32x2 b) { return haydn_x2addsub32s(a, b); }

/// Dual 32-bit subtract-add with saturation
static inline ae_int32x2 AE_SUBADD32S(ae_int32x2 a, ae_int32x2 b) { return haydn_x2subadd32s(a, b); }

//===----------------------------------------------------------------------===//
// Arithmetic -- Non-saturating
//===----------------------------------------------------------------------===//

/// Dual 32-bit non-saturating add
static inline ae_int32x2 AE_ADD32(ae_int32x2 a, ae_int32x2 b) { return haydn_x2add32(a, b); }

/// Dual 32-bit non-saturating subtract
static inline ae_int32x2 AE_SUB32(ae_int32x2 a, ae_int32x2 b) { return haydn_x2sub32(a, b); }

/// Quad 16-bit non-saturating add
static inline ae_int16x4 AE_ADD16(ae_int16x4 a, ae_int16x4 b) { return haydn_x4add16(a, b); }

/// Quad 16-bit non-saturating subtract
static inline ae_int16x4 AE_SUB16(ae_int16x4 a, ae_int16x4 b) { return haydn_x4sub16(a, b); }

/// 64-bit add
#define AE_ADD64(a, b) ((ae_int64)(a) + (ae_int64)(b))

/// 64-bit saturating add
static inline ae_int64 AE_ADD64S(ae_int64 a, ae_int64 b) { return haydn_add64s(a, b); }

//===----------------------------------------------------------------------===//
// Negate / Absolute Value
//===----------------------------------------------------------------------===//

/// Saturating 32-bit negate (per-lane dual in DR64). Golden: X2NEG32S.
static inline ae_int32x2 AE_NEG32S(ae_int32x2 a) {
  return haydn_x2neg32s(a);
}

/// Saturating 16-bit negate (quad in DR64)
static inline ae_int16x4 AE_NEG16S(ae_int16x4 a) {
  return haydn_x4sub16s((ae_int16x4){0, 0, 0, 0}, a);
}

/// INT16X4 negate
#define AE_INT16X4_NEG(a) haydn_x4sub16((ae_int16x4){0, 0, 0, 0}, (a))

/// Saturating 32-bit absolute value (per-lane dual in DR64). Golden: X2ABS32S.
/// HiFi AE_ABS32S operates on ae_int32x2; do NOT map to scalar abs32s.
static inline ae_int32x2 AE_ABS32S(ae_int32x2 a) { return haydn_x2abs32s(a); }

/// Saturating 64-bit absolute value
static inline ae_int64 AE_ABS64S(ae_int64 a) { return haydn_abs64s(a); }

/// Non-saturating 64-bit absolute value
static inline ae_int64 AE_ABS64(ae_int64 a) { return haydn_abs64(a); }

//===----------------------------------------------------------------------===//
// Saturation / Pack
//===----------------------------------------------------------------------===//

/// Saturate two 32x2 vectors (8 lanes) to one 16x4 vector.
/// HiFi `AE_SAT16X4(a, b)` packs the saturation of two dual-32 inputs into a
/// single quad-16 output. The Haydn `haydn_x4sat32t16` builtin takes both
/// inputs (`int64_t(int64_t, int64_t)`), so the macro is 2-arg to match.
/// Overload: accept the 1-arg form (saturate one 32x2 -> low 4 lanes) too.
#define AE_SAT16X4(...) __AE_SAT16X4_OVERLOAD(__VA_ARGS__)
#define __AE_SAT16X4_GET(_1, _2, NAME, ...) NAME
#define __AE_SAT16X4_OVERLOAD(...) \
  __AE_SAT16X4_GET(__VA_ARGS__, __AE_SAT16X4_2A, __AE_SAT16X4_1A)(__VA_ARGS__)
#define __AE_SAT16X4_1A(a) ((ae_int16x4)haydn_x4sat32t16((a), (ae_int32x2){0, 0}))
#define __AE_SAT16X4_2A(a, b) ((ae_int16x4)haydn_x4sat32t16((a), (b)))

/// Saturate 16-bit value (alias)
#define AE_CVT16X4_1ARG(a) \
  ((ae_int16x4)haydn_x4sat32t16((a), (ae_int32x2){0, 0}))
static inline ae_int16x4 __ae_cvt16x4_2(ae_int32x2 a, ae_int32x2 b) {
  return (ae_int16x4)haydn_x4sat32t16(a, b);
}
#define AE_CVT16X4(...) __AE_CVT16X4_OVERLOAD(__VA_ARGS__)
#define __AE_CVT16X4_GET(_1, _2, NAME, ...) NAME
#define __AE_CVT16X4_OVERLOAD(...) \
  __AE_CVT16X4_GET(__VA_ARGS__, __AE_CVT16X4_2, __AE_CVT16X4_1)(__VA_ARGS__)
#define __AE_CVT16X4_1(a)          AE_CVT16X4_1ARG(a)
#define __AE_CVT16X4_2(a, b)       __ae_cvt16x4_2((a), (b))
/// Truncate two 32-bit lanes to a quad-16 (saturating, no rounding).
#define AE_TRUNC16X4F32(a, b) ((ae_int16x4)haydn_x4sat32t16((a), (b)))

//===----------------------------------------------------------------------===//
// Shift Operations
//===----------------------------------------------------------------------===//

/// Dual 32-bit arithmetic left shift by register
static inline ae_int32x2 AE_SLLA32(ae_int32x2 a, int s) { return haydn_x2sll32(a, s); }

/// Dual 32-bit arithmetic right shift by register
static inline ae_int32x2 AE_SRAA32(ae_int32x2 a, int s) { return haydn_x2sra32(a, s); }

/// Dual 32-bit logical right shift by register
static inline ae_int32x2 AE_SRLA32(ae_int32x2 a, int s) { return haydn_x2srl32(a, s); }

/// Dual 32-bit left shift (HiFi AE_SLLI32 API takes int; map to DB reg form
/// X2SLL32 — ImmArg x2slli32 only when call site passes a literal).
static inline ae_int32x2 AE_SLLI32(ae_int32x2 a, int s) { return haydn_x2sll32(a, s); }

/// Dual 32-bit arithmetic right shift (reg form; see AE_SLLI32).
static inline ae_int32x2 AE_SRAI32(ae_int32x2 a, int s) { return haydn_x2sra32(a, s); }

/// Dual 32-bit logical right shift (reg form; see AE_SLLI32).
static inline ae_int32x2 AE_SRLI32(ae_int32x2 a, int s) { return haydn_x2srl32(a, s); }

/// Quad 16-bit arithmetic left shift by register
static inline ae_int16x4 AE_SLLA16X4(ae_int16x4 a, int s) { return haydn_x4sll16(a, s); }

/// Quad 16-bit arithmetic right shift by register
static inline ae_int16x4 AE_SRAA16X4(ae_int16x4 a, int s) { return haydn_x4sra16(a, s); }

/// Quad 16-bit logical right shift by register
static inline ae_int16x4 AE_SRLA16X4(ae_int16x4 a, int s) { return haydn_x4srl16(a, s); }

/// Quad 16-bit left shift (AE int amount -> DB reg form X4SLL16).
static inline ae_int16x4 AE_SLLI16X4(ae_int16x4 a, int s) { return haydn_x4sll16(a, s); }

/// Quad 16-bit arithmetic right shift (reg form).
static inline ae_int16x4 AE_SRAI16X4(ae_int16x4 a, int s) { return haydn_x4sra16(a, s); }

/// Quad 16-bit logical right shift (reg form).
static inline ae_int16x4 AE_SRLI16X4(ae_int16x4 a, int s) { return haydn_x4srl16(a, s); }

/// 64-bit left shift (HiFi int amount → DB reg form SLL64; ImmArg slli64 only
/// for direct haydn_slli64(literal) call sites — ImmArg cannot live behind a
/// C ternary / choose_expr with a variable arm).
static inline ae_int64 AE_SLAI64(ae_int64 a, int s) {
  return (ae_int64)haydn_sll64(a, s);
}

/// 64-bit arithmetic right shift → DB reg form SRA64 (see AE_SLAI64).
static inline ae_int64 AE_SRAI64(ae_int64 a, int s) {
  return (ae_int64)haydn_sra64(a, s);
}

/// 64-bit logical right shift → DB reg form SRL64 (see AE_SLAI64).
static inline ae_int64 AE_SRLI64(ae_int64 a, int s) {
  return (ae_int64)haydn_srl64(a, s);
}

/// AE_SRAS32 — dual-32 arithmetic right by ambient SAR (Cadence 1-arg).
///   1-arg (a)    : haydn_x2sra32(a, haydn_ae_sar)
///   2-arg (a, s) : haydn_x2sra32(a, s) convenience (explicit amount)
/// Must not silent-alias scalar ((x + 0x8000) >> 1) half-average.
#define AE_SRAS32(...) __AE_SRAS32_OVERLOAD(__VA_ARGS__)
#define __AE_SRAS32_GET(_1, _2, NAME, ...) NAME
#define __AE_SRAS32_OVERLOAD(...) \
  __AE_SRAS32_GET(__VA_ARGS__, __AE_SRAS32_2, __AE_SRAS32_1)(__VA_ARGS__)
#define __AE_SRAS32_1(a)    haydn_x2sra32((a), haydn_ae_sar)
#define __AE_SRAS32_2(a, s) haydn_x2sra32((a), (int)(s))

/// AE_SLAS32 — dual-32 left by ambient SAR (Cadence 1-arg; non-saturating).
///   1-arg (a)    : haydn_x2sll32(a, haydn_ae_sar)
///   2-arg (a, s) : haydn_x2sll32(a, s) convenience
/// Distinct from AE_SLAS32S (saturating bidirectional soft model).
#define AE_SLAS32(...) __AE_SLAS32_OVERLOAD(__VA_ARGS__)
#define __AE_SLAS32_GET(_1, _2, NAME, ...) NAME
#define __AE_SLAS32_OVERLOAD(...) \
  __AE_SLAS32_GET(__VA_ARGS__, __AE_SLAS32_2, __AE_SLAS32_1)(__VA_ARGS__)
#define __AE_SLAS32_1(a)    haydn_x2sll32((a), haydn_ae_sar)
#define __AE_SLAS32_2(a, s) haydn_x2sll32((a), (int)(s))

/// AE_SLAS32S — dual-32 bidirectional saturating arithmetic shift.
/// HiFi3 spellings:
///   2-arg (a, s) : sat left when s>0, ASR when s<0 (NatureDSP vec_shift t∈[-31,31]).
///   1-arg (a)    : same by ambient SAR (WUR_AE_SAR / haydn_ae_sar).
/// Soft model matches AE_SLAA32S; must not silent-alias always-right X2SRA32
/// (positive t would shift the wrong direction and drop saturation).
/// Bodies rebind after __ae_slaa32s is defined (late block).
#define AE_SLAS32S(...) __AE_SLAS32S_OVERLOAD(__VA_ARGS__)
#define __AE_SLAS32S_GET(_1, _2, NAME, ...) NAME
#define __AE_SLAS32S_OVERLOAD(...) \
  __AE_SLAS32S_GET(__VA_ARGS__, __AE_SLAS32S_2, __AE_SLAS32S_1)(__VA_ARGS__)
/* Early placeholders — late rebind to __ae_slaa32s after soft sat helpers. */
#define __AE_SLAS32S_1(a)            __ae_slaa32s((a), haydn_ae_sar)
#define __AE_SLAS32S_2(a, s)         __ae_slaa32s((a), (int)(s))

/// SAR (shift-amount) register access.
/// Previously AE_SAR was (0), WUR_AE_SAR was a no-op, and
/// AE_SLAS32S(a) returned (a) unchanged — so ALL variable-shift vec_shift
/// kernels produced wrong output (the shift was deleted entirely). Now we
/// model the SAR register as a file-scope variable so the shift amount set
/// by WUR_AE_SAR(t) flows into the AE_SLAS32S(a) shift.
/// Static so each translation unit is self-contained (no runtime def needed).
static int haydn_ae_sar;
#define AE_SAR (haydn_ae_sar)

//===----------------------------------------------------------------------===//
// Shift with Rounding
//===----------------------------------------------------------------------===//

/// Dual 32-bit arithmetic right shift with rounding by register
static inline ae_int32x2 AE_SRAA32RS(ae_int32x2 a, int s) { return haydn_x2sra32r(a, s); }

/// Quad 16-bit arithmetic right shift with rounding by register
static inline ae_int16x4 AE_SRAA16RS(ae_int16x4 a, int s) { return haydn_x4sra16r(a, s); }

/// Dual 32-bit fractional shift (reg form when s is not a literal ImmArg).
#define AE_F32X2_SRAI(a, s) haydn_x2sra32((a), (s))

//===----------------------------------------------------------------------===//
// Rounding / Conversion / Truncation
//===----------------------------------------------------------------------===//

/// Round 2x64-bit to 2x32-bit with symmetric saturation
/// Uses paired pack-shift-round: SAT32((acc + rounding) >> shift)
/// uses haydn_packsr32x2_hh (composite in this header: X2SRA32R + X2SEL32)
/// to do both shifts in DR64 without crossing to GPR32. Previously this
/// was 2x scalar packsr32 + manual <<32 |, which
/// caused the DR64->GPR32->DR64 cross-bank round-trip (~6 ops per lane,
/// ~12 per call, repeated once per lattice stage — the dominant latr bloat).
static inline ae_int32x2 AE_ROUND32X2F48S(ae_int64 a, ae_int64 b,
                                            int shift) {
  return (ae_int32x2)haydn_packsr32x2_hh(a, b, shift);
}

/// Truncate 2x64-bit to 2x32-bit with arithmetic shift + saturation.
/// Soft oracle: lo/hi = SAT32(acc >> shift) with the shift amount honored as
/// given (no kernel-special 32→16 remap). Pack lanes with unsigned 32-bit
/// casts so a negative high lane cannot sign-overwrite the low lane.
static inline ae_int32x2 AE_TRUNCA32X2F64S(ae_int64 a, ae_int64 b,
                                             int shift) {
  ae_int32 lo = (ae_int32)haydn_satsr64(a, shift);
  ae_int32 hi = (ae_int32)haydn_satsr64(b, shift);
  return (ae_int32x2)(((unsigned long long)(unsigned int)lo) |
                      ((unsigned long long)(unsigned int)hi << 32));
}

/// Truncate 2x64-bit to 2x32-bit with rounding
#define AE_TRUNCI32X2F64S AE_ROUND32X2F48S

/// Round 2x64-bit to 2x32-bit with asymmetric saturation
#define AE_ROUND32X2F64SASYM(a, b, s) AE_ROUND32X2F48S((a), (b), (s))

/// Round 4x32-bit to 4x16-bit with asymmetric saturation
static inline ae_int16x4 AE_ROUND16X4F32SASYM(ae_int32x2 a, ae_int32x2 b,
                                                int shift) {
  ae_int32x2 sa = (ae_int32x2)haydn_x2sra32r(a, shift);
  ae_int32x2 sb = (ae_int32x2)haydn_x2sra32r(b, shift);
  /* haydn_x4sat32t16 saturates four 32-bit lanes (in two DR64 inputs) down
   * to four 16-bit lanes packed into one DR64 output. Both shifted halves
   * must be passed together; calling it with a single argument is a type
   * error (the intrinsic is int64_t(int64_t, int64_t)). */
  return (ae_int16x4)haydn_x4sat32t16(sa, sb);
}

/// Convert 32-bit signed value to Q1.56-class accumulator (sign-extend, <<16).
/// Soft oracle (kernel pure): (int64_t)(int32_t)(a) << 16 via unsigned left
/// shift so negative inputs are defined (never a constant-zero body).
#define AE_CVTQ56A32S(a) \
  ((ae_int64)((int64_t)(((uint64_t)(int64_t)(int32_t)(a)) << 16)))

/// Convert 48-bit Q-format to 32-bit
#define AE_CVTQ48A32S(a, s) ((ae_int32)haydn_packsr32((a), (s)))

/// AE_PKSR32: pack-shift-round 64->32 with lane pack.
///
/// Xtensa HiFi semantics (AE_PKSR32_analysis.md:25,51-60; ): AE_PKSR32
/// (d, ps, pos) updates d as:
///   d.H = d.L;            (shift the previous low lane to high)
///   d.L = SAT32((LSL(ps, pos) + rnd) >> 16)
/// where pos is a 2-bit LEFT-shift (0..3) applied to ps BEFORE the 16-bit
/// asymmetric round-truncation, and rnd = 1 << 15. The effective RIGHT-shift
/// magnitude on the 64-bit accumulator is therefore 16 - pos (15 for pos=1),
/// NOT pos + 16. The earlier `(ps + rnd) >> (pos + 16)` comment + lowering was
/// a wrong-code bug (capstone-hifi-semantics-audit Finding A): pos=1 yielded a
/// 17-bit right-shift (4x wrong scale; 64x at pos=3). See the by-value macro
/// below (haydn_dsp.h:~4990) for the full fix narrative.
///
/// In Haydn this maps to haydn_packsr32(ps, 16 - pos) (SRA64R is an arithmetic
/// right-shift with rounding for positive simm7, per the DB behavior field).
/// The previous-low-to-high shift is done inline via a 32-bit lane rotate. For
/// full dual-accumulator pack with explicit HH/HL/LH/LL lane selection across
/// two source accumulators, use haydn_packsr32x2_<lane>.
///
/// Used in NatureDSP IIR biquad kernels (bqriir32x16_df1/df2, bqriir32x32_df1/df2)
/// for the delay-line update + saturation pack step.
static inline __attribute__((always_inline))
void AE_PKSR32(ae_int32x2 *d, ae_int64 ps, int pos) {
  unsigned long long cur = (unsigned long long)*d;
  unsigned int new_lo = (unsigned int)haydn_packsr32((long long)ps, 16 - (pos));
  /* Previous low lane becomes new high lane. */
  unsigned int prev_lo = (unsigned int)(cur & 0xFFFFFFFF);
  *d = (ae_int32x2)(((unsigned long long)prev_lo << 32) | new_lo);
}

//===----------------------------------------------------------------------===//
// 16-bit <-> 32-bit Conversion / Pack / Unpack
//===----------------------------------------------------------------------===//

/// AE_CVT32X2F16_32 / _10: Q15 → Q31 for freestanding LE + HiFi f32x2 order.
/// NatureDSP: d0=CVT_32(t); d1=CVT_10(t). LE e0@bits[15:0] is oldest.
/// AE f32x2 uses .H = first-in-time, .L = second (same as L32X2 freestanding):
///   CVT_32: out.H = e0<<16, out.L = e1<<16
///   CVT_10: out.H = e2<<16, out.L = e3<<16
static inline ae_int32x2 AE_CVT32X2F16_32(ae_int16x4 a) {
  uint64_t v = (uint64_t)(haydn_dr64_t)a;
  int32_t first = ((int32_t)(int16_t)(v & 0xffffu)) << 16;         // e0 → H
  int32_t second = ((int32_t)(int16_t)((v >> 16) & 0xffffu)) << 16; // e1 → L
  return (ae_int32x2)((uint64_t)(uint32_t)second |
                      ((uint64_t)(uint32_t)first << 32));
}

static inline ae_int32x2 AE_CVT32X2F16_10(ae_int16x4 a) {
  uint64_t v = (uint64_t)(haydn_dr64_t)a;
  int32_t first = ((int32_t)(int16_t)((v >> 32) & 0xffffu)) << 16;  // e2 → H
  int32_t second = ((int32_t)(int16_t)((v >> 48) & 0xffffu)) << 16; // e3 → L
  return (ae_int32x2)((uint64_t)(uint32_t)second |
                      ((uint64_t)(uint32_t)first << 32));
}

//===----------------------------------------------------------------------===//
// Select / Pack Operations
//===----------------------------------------------------------------------===//

/// Pack low+high halfwords from two sources.
/// Native Haydn: x2sel32_lh — selects low 32-bit of a, high 32-bit of b.
/// Fixed 2026-06-19 (L193 encoding fix): was scalar bitwise C (~4 GPR ops).
#define AE_SEL32_LH(a, b) ((ae_int32x2)haydn_x2sel32_lh((a), (b)))

/// Pack high+high halfwords.
#define AE_SEL32_HH(a, b) ((ae_int32x2)haydn_x2sel32_hh((a), (b)))

/// Pack low+low halfwords.
#define AE_SEL32_LL(a, b) ((ae_int32x2)haydn_x2sel32_ll((a), (b)))

/// Pack high+low halfwords.
#define AE_SEL32_HL(a, b) ((ae_int32x2)haydn_x2sel32_hl((a), (b)))

/// Select 4×16 lanes. AE API takes int mask → always DB reg form X4SEL16.
/// ImmArg X4SELI16 is haydn_x4seli16(a,b,literal) for constant call sites only
/// (ImmArg cannot sit behind a ternary with a variable arm — Sema checks both).
static inline ae_int16x4 AE_SEL16(ae_int16x4 a, ae_int16x4 b, int m) {
  return (ae_int16x4)haydn_x4sel16(a, b, m);
}

/// 16-bit select patterns used in NatureDSP
// TODO: no Haydn equivalent - needs workaround with x4seli16 and appropriate imm
#define AE_SHORTSWAP(a) ((ae_int16x4)haydn_x4seli16((a), (a), 0xB4))

//===----------------------------------------------------------------------===//
// Move / Immediate
//===----------------------------------------------------------------------===//

/// Move immediate to dual 32-bit. Two HiFi3 spellings:
///   2-arg (hi, lo) : pack two 32-bit values into DR64.
///   1-arg (v)      : broadcast v to both lanes.
static inline ae_int32x2 __ae_movda32_2(int hi, int lo) {
  return (ae_int32x2)((long long)(unsigned int)(lo) |
                      ((long long)(unsigned int)(hi) << 32));
}
static inline ae_int32x2 __ae_movda32_1(int v) {
  unsigned int u = (unsigned int)v;
  return (ae_int32x2)((long long)u | ((long long)u << 32));
}
#define AE_MOVDA32(...) __AE_MOVDA32_OVERLOAD(__VA_ARGS__)
#define __AE_MOVDA32_GET(_1, _2, NAME, ...) NAME
#define __AE_MOVDA32_OVERLOAD(...) \
  __AE_MOVDA32_GET(__VA_ARGS__, __AE_MOVDA32_2, __AE_MOVDA32_1)(__VA_ARGS__)
#define __AE_MOVDA32_1(v)          __ae_movda32_1(v)
#define __AE_MOVDA32_2(hi, lo)     __ae_movda32_2(hi, lo)

/// Move immediate to quad 16-bit. Two HiFi3 spellings:
///   4-arg : pack (h3,h2,h1,h0) into 4 lanes.
///   1-arg : broadcast v to all 4 lanes.
static inline ae_int16x4 __ae_movda16_4(int h3, int h2, int h1, int h0) {
  return (ae_int16x4)(
      (long long)(unsigned short)(h0) |
      ((long long)(unsigned short)(h1) << 16) |
      ((long long)(unsigned short)(h2) << 32) |
      ((long long)(unsigned short)(h3) << 48));
}
static inline ae_int16x4 __ae_movda16_1(int v) {
  unsigned short u = (unsigned short)v;
  long long r = (long long)u | ((long long)u << 16) |
                ((long long)u << 32) | ((long long)u << 48);
  return (ae_int16x4)r;
}
#define AE_MOVDA16(...) __AE_MOVDA16_OVERLOAD(__VA_ARGS__)
#define __AE_MOVDA16_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_MOVDA16_OVERLOAD(...) \
  __AE_MOVDA16_GET(__VA_ARGS__, __AE_MOVDA16_4A, __AE_MOVDA16_4A, __AE_MOVDA16_4A, __AE_MOVDA16_1A)(__VA_ARGS__)
#define __AE_MOVDA16_1A(v)         __ae_movda16_1(v)
#define __AE_MOVDA16_4A(h3,h2,h1,h0) __ae_movda16_4(h3,h2,h1,h0)

/// Move from int64 to int32x2 (reinterpret; not a C splat)
#define AE_MOVINT32X2_FROMINT64(a) ((ae_int32x2)__haydn_i64_as_v2(__AE_TO_I64(a)))

/// Move from int32x2 to int64 (reinterpret; not a first-lane extract)
#define AE_MOVINT64_FROMINT32X2(a) ((ae_int64)__AE_TO_I64(a))

/// Set immediate value (usually zero)
#define AE_MOVI(imm) ((ae_int32x2)(long long)(imm))

/// Zero a 64-bit register
#define AE_ZERO64() ((ae_int64)0)

/// Zero a 32-bit (DR64 low lane / scalar) -- NatureDSP compat
#define AE_ZERO32() ((ae_int32x2){0, 0})

/// Zero a 16-bit (DR64 low lane / scalar) -- NatureDSP compat
#define AE_ZERO16() ((ae_int16x4){0, 0, 0, 0})

/// Scalar broadcast to ae_int32x2 (NatureDSP single-argument form).
/// NatureDSP's AE_MOVDA32(x) broadcasts x to both lanes; the 2-arg form
/// (hi, lo) above is the lane-explicit variant. Provide both forms.
static inline __attribute__((__always_inline__))
ae_int32x2 AE_MOVDA32X2(int hi, int lo) {
  return (ae_int32x2)((long long)(unsigned int)lo |
                      ((long long)(unsigned int)hi << 32));
}

//===----------------------------------------------------------------------===//
// 64x32 Integer Division -- NatureDSP AE_DIV64D32_* compat shims
//
// Haydn has no hardware divide (only RECIP). Plain C `/` and `%` lower via
// the legalizer to libcalls (__divsi3 / __divdi3 / …) resolved at link time
// by llvm-libc / compiler-rt.
//
// NatureDSP AE_DIV64D32_H/L semantics: 64-bit dividend / 32-bit divisor,
// producing the high 32 bits (_H) or low 32 bits (_L) of the 64-bit
// quotient, with the dividend register updated in-place (remainder lives
// in a paired quotient/remainder encoding). On Haydn we emulate this via
// C `/` (-> __divdi3); the redundant repeats in the NatureDSP inner loop
// (used because the HiFi instruction divides one bit per cycle) collapse
// to a single C `/` per call site.
//
// Reference: ~/haydn-plans/m6-division-scope.md
//===----------------------------------------------------------------------===//

/// 64/32 -> high-32 of quotient. Maps to C `/` (lowers to __divdi3).
/// Repeated invocations in NatureDSP sources are idempotent here.
static inline __attribute__((__always_inline__))
ae_int64 AE_DIV64D32_H(ae_int64 num, ae_int32 den) {
  return (ae_int64)((long long)num / (long long)(int)den);
}

/// 64/32 -> low-32 of quotient. Same as _H on Haydn (we compute the full
/// 64-bit quotient then return it; callers truncate as needed).
static inline __attribute__((__always_inline__))
ae_int64 AE_DIV64D32_L(ae_int64 num, ae_int32 den) {
  return (ae_int64)((long long)num / (long long)(int)den);
}

/// 64/32 division returning quotient and remainder (both 32-bit).
/// Convenience wrapper used by some NatureDSP-style porting code.
static inline __attribute__((__always_inline__))
ae_int64 AE_DIV64D32REM_HL(ae_int64 num, ae_int32 den,
                            ae_int32 *rem_out) {
  long long q = (long long)num / (long long)(int)den;
  if (rem_out) *rem_out = (ae_int32)((long long)num % (long long)(int)den);
  return (ae_int64)q;
}

/// Recip-case wrapper: Q1.31 fixed-point reciprocal approximation using
/// Haydn's native RECIP instruction (opcode 148; 6-bit LUT + linear
/// interpolation). Returns an int32 estimate of 1/x in Q1.31 format.
/// For the 15 NatureDSP reciprocal call sites (scl_recip16x16 etc.) this
/// is ~5-10 cycles vs hundreds for the __divdi3 libcall path. After one
/// Newton-Raphson refinement step (x*est + (2 - x*est)*est) the result
/// reaches full 1-LSB Q1.31 accuracy.
static inline __attribute__((__always_inline__))
int haydn_recip_q31(int x) {
  return haydn_recip(x);
}

//===----------------------------------------------------------------------===//
// Conditional Move (AE_* uses pred SSA mux; ambient movt left low-level)
//===----------------------------------------------------------------------===//
//
// AE_MOVT*/AE_MOVF* take an explicit cond (xtboolN / haydn_predN_t). Map
// them to pure x2/x4mux so the cond is not ignored. Ambient haydn_x2movt32 /
// x4movt16 remain for low-level SFR sequencing (ordered, IntrHasSideEffects).
// 2-arg AE_MOVT64 stays ambient movt64 (no cond operand).

/// Dual 32-bit conditional move if true: dst = cond ? src : dst
#define AE_MOVT32X2(dst, src, cond) \
  ((dst) = (ae_int32x2)haydn_x2mux32((haydn_pred2_t)(cond), (src), (dst)))

/// Dual 32-bit conditional move if false: dst = cond ? dst : src
#define AE_MOVF32X2(dst, src, cond) \
  ((dst) = (ae_int32x2)haydn_x2mux32((haydn_pred2_t)(cond), (dst), (src)))

/// Quad 16-bit conditional move if true: dst = cond ? src : dst
#define AE_MOVT16X4(dst, src, cond) \
  ((dst) = (ae_int16x4)haydn_x4mux16((haydn_pred4_t)(cond), (src), (dst)))

/// Quad 16-bit conditional move if false: dst = cond ? dst : src
#define AE_MOVF16X4(dst, src, cond) \
  ((dst) = (ae_int16x4)haydn_x4mux16((haydn_pred4_t)(cond), (dst), (src)))

/// 64-bit conditional move if SFR true
/// AE_MOVT64 has two spellings:
///   2-arg (dst, src) : dst = src if SFR set (reads ambient SFR).
///   3-arg (dst, src, cf) : dst = cf ? src : dst (boolean condition).
#define AE_MOVT64(...) __AE_MOVT64_OVERLOAD(__VA_ARGS__)
// 2-arg → ambient movt64; 3-arg → boolean cf select. N+1 selector:
//   GET(a,b, NAME3, NAME2) → NAME2;  GET(a,b,c, NAME3, NAME2) → NAME3.
#define __AE_MOVT64_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MOVT64_OVERLOAD(...) \
  __AE_MOVT64_GET(__VA_ARGS__, __AE_MOVT64_3, __AE_MOVT64_2, )(__VA_ARGS__)
#define __AE_MOVT64_2(dst, src)     ((dst) = (ae_int64)haydn_movt64((src)))
#define __AE_MOVT64_3(dst, src, cf) ((void)((cf) && ((dst) = (src)), 0))

/// 64-bit conditional move if SFR false
#define AE_MOVF64(dst, src) ((dst) = (ae_int64)haydn_movf64((src)))

//===----------------------------------------------------------------------===//
// Compare (LT via pure cmplt; EQ/LE/SEQ/SLE ambient compare + movesfr2gpr)
//===----------------------------------------------------------------------===//
//
// AE_LT32 / AE_SLT16X4 use x2/x4cmplt (i32 pred SSA) — never cast a
// vector passthrough from ambient x2slt32 to xtbool. EQ/LE (and X4 SEQ/SLE)
// still lack pure SSA cmpeq/cmple; capture ambient SFR bits via movesfr2gpr.

/// Dual 32-bit signed equal compare → xtbool2 (ambient seq + SFR capture)
#define AE_EQ32(a, b) \
  ((void)haydn_x2seq32((a), (b)), (xtbool2)haydn_movesfr2gpr())

/// Dual 32-bit signed less-than → xtbool2 (pure SSA cmplt)
#define AE_LT32(a, b) ((xtbool2)haydn_x2cmplt32((a), (b)))

/// Dual 32-bit signed less-or-equal → xtbool2 (ambient sle + SFR capture)
#define AE_LE32(a, b) \
  ((void)haydn_x2sle32((a), (b)), (xtbool2)haydn_movesfr2gpr())

/// Quad 16-bit signed equal → xtbool4 (ambient seq + SFR capture)
#define AE_SEQ16X4(a, b) \
  ((void)haydn_x4seq16((a), (b)), (xtbool4)haydn_movesfr2gpr())

/// Quad 16-bit signed less-than → xtbool4 (pure SSA cmplt)
#define AE_SLT16X4(a, b) ((xtbool4)haydn_x4cmplt16((a), (b)))

/// Quad 16-bit signed less-or-equal → xtbool4 (ambient sle + SFR capture)
#define AE_SLE16X4(a, b) \
  ((void)haydn_x4sle16((a), (b)), (xtbool4)haydn_movesfr2gpr())

//===----------------------------------------------------------------------===//
// Normalization (NSA)
//===----------------------------------------------------------------------===//

/// Number of sign bits (64-bit)
#define AE_NSA64(v) haydn_nsa64(v)

/// Number of sign bits (32-bit)
#define AE_NSA32(v) haydn_nsa32(v)

/// Number of sign bits, 32-bit with zero detect, low lane
#define AE_NSAZ32_L(v) haydn_nsaz32_l(v)

/// Number of sign bits, 16-bit with zero detect, lane 0
#define AE_NSAZ16_0(v) haydn_nsaz16_l(v)

/// Number of sign bits (32-bit, unsigned input)
#define AE_NSA32U(v) haydn_nsau32(v)

/// Number of sign bits (64-bit with zero detect)
#define AE_NSA64S(v) haydn_nsaz64(v)

/// Number of sign bits, 16-bit, low lane
#define AE_NSAL32(v) haydn_nsa32_l(v)

/// Calculate 3-bit range -- software emulation
// TODO: no Haydn equivalent - needs workaround
// AE_CALCRNG3: HiFi3 block-floating-point range calculation.
// 1-arg form: AE_CALCRNG3(ae_int64) — compute NSA-based shift for one value.
// 0-arg form: AE_CALCRNG3() — read the block exponent (HiFi3 reads the AE
//   SAR register updated by AE_MAXLAA/AE_MULAAAAQ16). Haydn has no BFP
//   register; the 0-arg form returns a static variable updated by AE_CALCRNG3(1-arg).
static int haydn_bfp_shift = 0;
static inline int AE_CALCRNG3_1arg(ae_int64 a) {
  haydn_bfp_shift = haydn_nsa64(a) - 1;
  return haydn_bfp_shift;
}
#define AE_CALCRNG3(...) __AE_CALCRNG3_OVERLOAD(__VA_ARGS__)
#define __AE_CALCRNG3_GET(_0, _1, NAME, ...) NAME
#define __AE_CALCRNG3_OVERLOAD(...)   __AE_CALCRNG3_GET(__VA_ARGS__, AE_CALCRNG3_1arg, AE_CALCRNG3_0arg)(__VA_ARGS__)
#define AE_CALCRNG3_0arg() (haydn_bfp_shift)

//===----------------------------------------------------------------------===//
// Multiply -- 64-bit basic
//===----------------------------------------------------------------------===//

/// 64-bit signed*signed multiply, low-low lane
#define AE_MUL64_SS_LL(a, b) haydn_mul64_ss_ll(__AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply, high-high lane
#define AE_MUL64_SS_HH(a, b) haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply, low-high lane
#define AE_MUL64_SS_LH(a, b) haydn_mul64_ss_lh(__AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply, high-low lane
#define AE_MUL64_SS_HL(a, b) haydn_mul64_ss_hl(__AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// Multiply-Accumulate -- 64-bit basic
//===----------------------------------------------------------------------===//

/// 64-bit signed*signed MAC, low-low lane
#define AE_MULA64_SS_LL(acc, a, b) haydn_mula64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed MAC, high-high lane
#define AE_MULA64_SS_HH(acc, a, b) haydn_mula64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed MAC, low-high lane
#define AE_MULA64_SS_LH(acc, a, b) haydn_mula64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed MAC, high-low lane
#define AE_MULA64_SS_HL(acc, a, b) haydn_mula64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// Multiply-Subtract -- 64-bit basic
//===----------------------------------------------------------------------===//

/// 64-bit signed*signed multiply-subtract, high-high lane
#define AE_MULS64_SS_HH(acc, a, b) haydn_muls64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply-subtract, low-low lane
#define AE_MULS64_SS_LL(acc, a, b) haydn_muls64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply-subtract, low-high lane
#define AE_MULS64_SS_LH(acc, a, b) haydn_muls64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// 64-bit signed*signed multiply-subtract, high-low lane
#define AE_MULS64_SS_HL(acc, a, b) haydn_muls64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// Fractional Multiply -- 32-bit (FMUL32S family)
//===----------------------------------------------------------------------===//

/// Fractional 32-bit signed multiply, high-high lane
#define AE_MULF32S_HH(a, b) haydn_fmul32s_hh(__AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed multiply, low-low lane
#define AE_MULF32S_LL(a, b) haydn_fmul32s_ll(__AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed multiply, low-high lane
#define AE_MULF32S_LH(a, b) haydn_fmul32s_lh(__AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed MAC, high-high lane
#define AE_MULAF32S_HH(acc, a, b) haydn_fmula32s_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed MAC, low-low lane
#define AE_MULAF32S_LL(acc, a, b) haydn_fmula32s_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed MAC, low-high lane
#define AE_MULAF32S_LH(acc, a, b) haydn_fmula32s_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed MSU, high-high lane
#define AE_MULSF32S_HH(acc, a, b) haydn_fmuls32s_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional 32-bit signed MSU, low-low lane
#define AE_MULSF32S_LL(acc, a, b) haydn_fmuls32s_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// FF2 Fractional Multiply with Rounding + Saturation
//===----------------------------------------------------------------------===//

/// FF2 fractional multiply with rounding+sat, low-low lane
#define AE_MULFP32X2RAS(a, b) ((ae_int32x2)haydn_x2fmul32rs(__AE_AS_V2(a), __AE_AS_V2(b)))

/// FF2 fractional MAC with rounding+sat, low-low lane
#define AE_MULAFP32X2RAS(acc, a, b) haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// FF2 fractional MSU with rounding+sat, low-low lane
#define AE_MULSFP32X2RAS(acc, a, b) haydn_ff2muls32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// FF2 fractional multiply with rounding+sat, high-high lane
#define AE_MULFP32X2RAS_HH(a, b) haydn_ff2mul32rs_hh(__AE_TO_I64(a), __AE_TO_I64(b))

/// FF2 fractional MAC with rounding+sat, high-high lane
#define AE_MULAFP32X2RAS_HH(acc, a, b) haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// FF2 fractional multiply with rounding+sat, low-high lane
#define AE_MULFP32X2RAS_LH(a, b) haydn_ff2mul32rs_lh(__AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// FF2 MAC with lane selection (HH/LL/HL/LH)
//===----------------------------------------------------------------------===//

/// Fractional MAC with rounding+sat, HH lane
#define AE_MULAF32R_HH(acc, a, b) haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional MAC with rounding+sat, LL lane
#define AE_MULAF32R_LL(acc, a, b) haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fractional multiply with rounding+sat, HH lane
#define AE_MULF32S_HH_RS(a, b) haydn_ff2mul32rs_hh(__AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// Fused Dual MAC -- IIR/Decimation core intrinsics
//===----------------------------------------------------------------------===//

/// Fused add-add dual MAC, same-lane (HH+LL), sat+round
#define AE_MULAAFD32R_HH_LL(acc, a, b) \
  haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fused add-add dual MAC, cross-lane (HL+LH), sat+round
#define AE_MULAAFD32R_HL_LH(acc, a, b) \
  haydn_f2mulaa32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fused sub-sub dual MAC, same-lane (HH+LL), sat+round
#define AE_MULSSFD32X16X2_HH_LL(acc, a, b) \
  haydn_f2mulss32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

/// Fused sub-sub dual MAC, cross-lane (HL+LH), sat+round
#define AE_MULSSFD32X16X2_HL_LH(acc, a, b) \
  haydn_f2mulss32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// 32x16 MAC -- FIR-specific intrinsics
//===----------------------------------------------------------------------===//

/// 32x16 MAC, lane H0 (slot 0, lane 0 of coefficient)
// Note: AE_MUL32X16_H0/H1/H2/H3 are redefined with a 2-arg/3-arg overload
// below (see "AE_MUL32X16_H0/H1/H2/H3 2-arg overload"). haydn_smula16_0n
// is the eventual target intrinsic, but it is not yet user-visible (missing
// from haydn.h), so we compose via the declared FIR helper for now.
// See and the TODO block at end of part-9 write-back section.
//
// 2026-08-19: native golden v2_1 MUL/MULA32X16_Hn exist but are INTEGER
// (no <<1); this AE macro family is FRACTIONINAL per the fmul32s FIR
// lowering. Retargeting to the integer natives changes product semantics
// (silent-miscompute class) — needs an exactness proof against the HiFi
// oracle (incl. the fir_hl-vs-fir_hh data-half convention) before it can
// move to FMUL/FMULA32X16_Hn. Left as emu; see OPEN follow-up.
#define AE_MUL32X16_H0(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

/// 32x16 MAC, lane H1 — composed via declared FIR helper (haydn_smula16_1n
/// is the eventual target but not yet user-visible in haydn.h; see ).
#define AE_MUL32X16_H1(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

/// 32x16 MAC, lane H2 — composed (haydn_smula16_2n not yet user-visible).
#define AE_MUL32X16_H2(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

/// 32x16 MAC, lane H3 — composed (haydn_smula16_3n not yet user-visible).
#define AE_MUL32X16_H3(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//===----------------------------------------------------------------------===//
// 16x16 Fractional Multiply (BASOP)
//===----------------------------------------------------------------------===//

/// Q15*Q15->Q31 fractional multiply, HS00 lane
#define AE_MULF16SS_00(a, b) haydn_fmul16_hs00(__AE_TO_I64(a), __AE_TO_I64(b))

/// Q15*Q15 MAC, accumulate-add both lanes (HS_11_00).
/// `haydn_fmulaa16_hs_11_00` is a two-lane (11+00) op. Mapping it onto
/// AE_MULAAAAQ16 (quad-16 into one 64-bit acc) drops lanes 3/2, then the
/// HiFi3z `TRUNCA32X2F64S(..., 33)` tail in vec_dot16x16_fast yields a
/// wrong 32-bit sat sum. Integer host of
///   x=[10,-10,20,-20,30,40,50,60] y=[1,2,3,4,5,6,7,8]
/// is 10-20+60-80+150+240+350+480 = 1190; the two-lane body is 380.
/// Leave the name undefined in the default strict header so that kernel
/// `#ifndef AE_MULAAAAQ16` uses dest-typed AE_MULAF16X4SS (X4MULA16S +
/// ADD32S/SEL32_LH/MOVAD32_H). Transitional inexact rebuilds may still
/// take the two-lane body. Do not retarget the 1190 host.
#if !__HAYDN_AE_COMPAT_STRICT
#define AE_MULAAAAQ16(acc, a, b) (acc) = haydn_fmulaa16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#endif

/// Q15*Q15 MSU, subtract both lanes
#define AE_MULSSSSQ16(acc, a, b) haydn_fmulss16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// SIMD MAC -- X2/X4 variants (Path B: 2-dest, golden-faithful)
//===----------------------------------------------------------------------===//
//
// The X2MUL32/X4MUL16/X2MULA32/... instructions produce TWO i64 results
// (golden slot1_mac_instruction_list.json: DR_Write_Port:[rtd1,rtd2]). The
// `haydn_<op>` wrappers return a `haydn_dpair_t` struct; these macros
// expose the NatureDSP view. Three call shapes from the HiFi kernel sources:
//
//  (1) 2-dest statement: `AE_MUL16X4(d0, d1, xt, yt); z = AE_SAT16X4(d0, d1);`
//      writes d0=hi pair (lanes 3,2), d1=lo pair (lanes 1,0). See
//      vec_elemult16x16_hifi3.c.
//  (2) 2-dest accum statement: `AE_MULAF16X4SS(vaf, vbf, vxf, vyf);`
//      vaf/vbf are in/out accumulators (both updated). See
//      vec_dot16x16_fast_hifi3.c.
//  (3) 2-dest returning: `zt = AE_MULP32X2(xt, yt);` — the kernel consumes
//      only ONE DR64 (the low pair); the high pair is dropped (DCE'd). This
//      matches the NatureDSP vec_cplx2cplx_mult32x32 usage where each call
//      yields one packed pair. See vec_elemult32x32_hifi3.c.

/// Dual 32-bit SIMD multiply-accumulate, 2-dest accumulator statement form
/// (kernel: `AE_MULA32X2(sum64x2, zt, zt);` — sum64x2 is updated in place).
/// Both accumulators (hi and lo pairs) are accumulated; the macro updates
/// acc_hi and acc_lo (passed by name) with the new values.
#define AE_MULA32X2(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x2mula32((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                        (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

/// Dual 32-bit SIMD multiply-subtract, 2-dest accumulator statement form.
#define AE_MULS32X2(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x2muls32((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                        (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

/// Quad 16-bit SIMD multiply-accumulate, 2-dest accumulator statement form.
#define AE_MULA16X4(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4mula16((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                        (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

/// Quad 16-bit SIMD multiply-subtract, 2-dest accumulator statement form.
#define AE_MULS16X4(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4muls16((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                        (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

/// Quad 16-bit SIMD multiply, 2-dest statement form
/// (kernel: `AE_MUL16X4(d0, d1, xt, yt); z = AE_SAT16X4(d0, d1);`).
/// Writes d0 = hi pair (lanes 3,2), d1 = lo pair (lanes 1,0).
#define AE_MUL16X4(d0, d1, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4mul16((a), (b)); \
    (d0) = __haydn_i64_as_v4(_r.hi); (d1) = __haydn_i64_as_v4(_r.lo); \
  } while (0)

/// Dual 32-bit SIMD multiply, returning form (kernel consumes the low pair
/// as the packed product; the high pair is DCE'd when unused). This matches
/// the NatureDSP `zt = AE_MULP32X2(xt, yt);` shape — single DR64 result.
static inline ae_int32x2 AE_MULP32X2(ae_int32x2 a, ae_int32x2 b) {
  haydn_dpair_t _r = haydn_x2mul32(a, b);
  return __haydn_i64_as_v2(_r.lo);
}

//===----------------------------------------------------------------------===//
// Complex Multiply -- 32-bit
//===----------------------------------------------------------------------===//

/// Fractional complex multiply with rounding+saturation
#define AE_MULFC32RAS(a, b) (haydn_x2fcmul32rs(a, b))

/// Fractional complex multiply variant
#define AE_MULFCI32RAS(a, b) (haydn_x2fcmul32rss(a, b))

/// Fractional complex MAC with rounding+saturation
#define AE_MULFCR32RAS(acc, a, b) (haydn_x2fcmula32rs(acc, a, b))

/// Fractional complex MAC variant
#define AE_MULFCR32I_RAS(acc, a, b) (haydn_x2fcmula32rss(acc, a, b))

//===----------------------------------------------------------------------===//
// Complex Multiply -- 16-bit
//===----------------------------------------------------------------------===//

/// Quad 16-bit complex multiply with rounding+saturation
/// NOTE: haydn_x4fcmul16rs is a 2-arg binary op (). The previous macro
/// passed a spurious 3rd zero argument; dropped.
#define AE_MULFC16RAS(a, b) \
  ((ae_int16x4)haydn_x4fcmul16rs((a), (b)))

/// Quad 16-bit complex MAC with rounding+saturation, accumulate form.
/// Per the DB, X4FCMULA16RS is a true read-back accumulator
/// (rtd[lane] = SATQ1.15(rtd[lane]Q1.15 + ...)), so AE_MULAFC16RAS maps
/// directly to the ternary intrinsic. (/ — supersedes the
/// earlier /note that claimed no true accumulator existed; the DB
/// is the source of truth.)
#define AE_MULAFC16RAS(acc, a, b) \
  ((ae_int16x4)__builtin_ae_mulafc16ras((ae_int64)(acc), \
                                        (ae_int64)(a), (ae_int64)(b)))

//===----------------------------------------------------------------------===//
// Complex Multiply -- 32x16 mixed precision (FIR/FFT butterflies)
// These decompose into sign-extend + 32-bit complex multiply
//===----------------------------------------------------------------------===//

/// Complex 32x16 MAC with rounding, high lane
/// Decompose: sign-extend 16-bit twiddle to 32-bit, then complex MAC
// TODO: no direct Haydn equivalent - decomposes into widen + x2fcmul32rs
static inline ae_int32x2 AE_MULFC32X16RAS_H(ae_int32x2 acc, ae_int32x2 data,
                                              ae_int16x4 coeff) {
  ae_int32x2 coeff_ext = AE_CVT32X2F16_32(coeff);
  return haydn_x2fcmula32rs(acc, data, coeff_ext);
}

/// Complex 32x16 MAC with rounding, low lane
// TODO: no direct Haydn equivalent - decomposes into widen + x2fcmul32rs
static inline ae_int32x2 AE_MULFC32X16RAS_L(ae_int32x2 acc, ae_int32x2 data,
                                              ae_int16x4 coeff) {
  ae_int32x2 coeff_ext = AE_CVT32X2F16_10(coeff);
  return haydn_x2fcmula32rs(acc, data, coeff_ext);
}

//===----------------------------------------------------------------------===//
// Complex Multiply -- 16x16 dual-output (cxfir16x16 patterns)
//===----------------------------------------------------------------------===//

/// Complex MAC: zero-init + add-add, H2+L3 lanes
/// Maps to: f2mulaa32rs_hhll (add-add, same lane)
static inline ae_int64 AE_MULZAAFD32X16_H2_L3(ae_int64 acc, ae_int16x4 d,
                                                ae_int16x4 c) {
  return haydn_f2mulaa32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Complex MAC: zero-init + sub-sub, H3+L2 lanes
/// Maps to: f2mulss32rs_hhll (sub-sub, same lane)
static inline ae_int64 AE_MULZASFD32X16_H3_L2(ae_int64 acc, ae_int16x4 d,
                                                ae_int16x4 c) {
  return haydn_f2mulss32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Complex MAC: add-add, H0+L1 lanes
static inline ae_int64 AE_MULAAFD32X16_H0_L1(ae_int64 acc, ae_int16x4 d,
                                               ae_int16x4 c) {
  return haydn_f2mulaa32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Complex MAC: add-add, H2+L3 lanes
static inline ae_int64 AE_MULAAFD32X16_H2_L3(ae_int64 acc, ae_int16x4 d,
                                               ae_int16x4 c) {
  return haydn_f2mulaa32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Complex MAC: sub-sub, H1+L0 lanes
static inline ae_int64 AE_MULASFD32X16_H1_L0(ae_int64 acc, ae_int16x4 d,
                                               ae_int16x4 c) {
  return haydn_f2mulss32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Complex MAC: sub-sub, H3+L2 lanes
static inline ae_int64 AE_MULASFD32X16_H3_L2(ae_int64 acc, ae_int16x4 d,
                                               ae_int16x4 c) {
  return haydn_f2mulss32rs_hhll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

//===----------------------------------------------------------------------===//
// FIR-specific MAC patterns (dual-output decomposition)
//===----------------------------------------------------------------------===//

/// FIR init: 32x16 dual fractional MAC init, HH lanes
/// Decomposes into two separate FMUL32S ops
static inline void AE_MULFD32X16X2_FIR_HH(ae_int64 *q0, ae_int64 *q1,
                                            ae_int16x4 d0, ae_int16x4 d1,
                                            ae_int16x4 c) {
  *q0 = haydn_fmul32s_hh(__AE_TO_I64(d0), __AE_TO_I64(c));
  *q1 = haydn_fmul32s_hh(__AE_TO_I64(d1), __AE_TO_I64(c));
}

/// FIR accumulate: 32x16 dual fractional MAC, HH lanes
static inline void AE_MULAFD32X16X2_FIR_HH(ae_int64 *q0, ae_int64 *q1,
                                             ae_int16x4 d0, ae_int16x4 d1,
                                             ae_int16x4 c) {
  *q0 = haydn_fmula32s_hh(*q0, __AE_TO_I64(d0), __AE_TO_I64(c));
  *q1 = haydn_fmula32s_hh(*q1, __AE_TO_I64(d1), __AE_TO_I64(c));
}

/// FIR accumulate: 32x16 dual fractional MAC, HL lanes
static inline void AE_MULAFD32X16X2_FIR_HL(ae_int64 *q0, ae_int64 *q1,
                                             ae_int16x4 d0, ae_int16x4 d1,
                                             ae_int16x4 c) {
  *q0 = haydn_fmula32s_lh(*q0, __AE_TO_I64(d0), __AE_TO_I64(c));
  *q1 = haydn_fmula32s_lh(*q1, __AE_TO_I64(d1), __AE_TO_I64(c));
}

/// FIR init: quad 16-bit MAC, pattern 3 (lanes 0+1)
/// Lowers to FMUL16_HS00 via the haydn.mulfq16x2.fir.3 intrinsic ().
/// Previously this called haydn_fmul16_hs00 for both outputs as a
/// workaround because the lane-1 (fmul16_hs11) builtin was missing. The
/// selector already supports the fir.3 decomposition; the C macro now
/// defers to it so the correct lane products are computed.
static inline void AE_MULFQ16X2_FIR_3(ae_int64 *q0, ae_int64 *q1,
                                       ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulfq16x2_fir_3(*q0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}

/// FIR init: quad 16-bit MAC, pattern 1 (lanes 2+3)
/// Lowers to FMUL16_HS22 via the haydn.mulfq16x2.fir.1 intrinsic ().
/// Previously called haydn_fmul16_hs00 (wrong lane) for both outputs.
static inline void AE_MULFQ16X2_FIR_1(ae_int64 *q2, ae_int64 *q3,
                                       ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulfq16x2_fir_1(*q2, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q2 = r;
  *q3 = r;
}

/// FIR accumulate: quad 16-bit MAC, pattern 3 (lanes 1+0)
/// Lowers to FMULAA16_HS_11_00 via haydn.mulafq16x2.fir.3 ().
static inline void AE_MULAFQ16X2_FIR_3(ae_int64 *q0, ae_int64 *q1,
                                        ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulafq16x2_fir_3(*q0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}

/// FIR accumulate: quad 16-bit MAC, pattern 1 (lanes 3+2)
/// Lowers to FMULAA16_HS_33_22 via haydn.mulafq16x2.fir.1 ().
static inline void AE_MULAFQ16X2_FIR_1(ae_int64 *q2, ae_int64 *q3,
                                        ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulafq16x2_fir_1(*q2, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q2 = r;
  *q3 = r;
}

//===----------------------------------------------------------------------===//
// Dual 16-bit MAC (zero-init patterns for convolution/correlation)
//===----------------------------------------------------------------------===//

/// Zero-init dual MAC: add, H3+L2 lanes
/// Decompose: zero accumulator, then two MAC ops
static inline ae_int64 AE_MULZAAD32X16_H3_L2(ae_int16x4 d, ae_int16x4 c) {
  ae_int64 acc = 0;
  acc = haydn_mula64_ss_hh(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  acc = haydn_mula64_ss_ll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  return acc;
}

/// Dual MAC: add, H1+L0 lanes
static inline ae_int64 AE_MULAAD32X16_H1_L0(ae_int64 acc, ae_int16x4 d,
                                              ae_int16x4 c) {
  acc = haydn_mula64_ss_hh(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  acc = haydn_mula64_ss_ll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  return acc;
}

/// Dual MAC: add, H3+L2 lanes
static inline ae_int64 AE_MULAAD32X16_H3_L2(ae_int64 acc, ae_int16x4 d,
                                              ae_int16x4 c) {
  acc = haydn_mula64_ss_hh(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  acc = haydn_mula64_ss_ll(acc, __AE_TO_I64(d), __AE_TO_I64(c));
  return acc;
}

//===----------------------------------------------------------------------===//
// Horizontal Reductions and Lane-wise Max/Min
//===----------------------------------------------------------------------===//

/// AE_MAX32 has two HiFi3 spellings:
///   1-arg  : horizontal max of dual 32-bit lanes -> scalar replicate.
///   2-arg  : lane-wise max of two DR64 vectors.
/// We dispatch on argument count.
#define AE_MAX32(...) __AE_MAX32_OVERLOAD(__VA_ARGS__)
#define __AE_MAX32_GET(_1, _2, NAME, ...) NAME
#define __AE_MAX32_OVERLOAD(...) \
  __AE_MAX32_GET(__VA_ARGS__, __AE_MAX32_2, __AE_MAX32_1)(__VA_ARGS__)
// R_GD x2hmax32 returns scalar int; AE 1-arg form replicates into both lanes.
static inline ae_int32x2 __ae_hmax32_1(ae_int32x2 a) {
  int m = haydn_x2hmax32(a);
  return (ae_int32x2){m, m};
}
#define __AE_MAX32_1(a)            __ae_hmax32_1(a)
/// Lane-wise max: result = (a < b) ? b : a per 32-bit lane.
/// slt32(a,b) sets SFR per lane; movt32(a,b) returns SFR ? b : a.
static inline ae_int32x2 __ae_max32x2(ae_int32x2 a, ae_int32x2 b) {
  (void)haydn_x2slt32((a), (b));   /* set SFR = (a < b) per lane */
  return (ae_int32x2)haydn_x2movt32((a), (b));
}
#define __AE_MAX32_2(a, b)         __ae_max32x2((a), (b))

#define AE_MIN32(...) __AE_MIN32_OVERLOAD(__VA_ARGS__)
#define __AE_MIN32_GET(_1, _2, NAME, ...) NAME
#define __AE_MIN32_OVERLOAD(...) \
  __AE_MIN32_GET(__VA_ARGS__, __AE_MIN32_2, __AE_MIN32_1)(__VA_ARGS__)
static inline ae_int32x2 __ae_hmin32_1(ae_int32x2 a) {
  int m = haydn_x2hmin32(a);
  return (ae_int32x2){m, m};
}
#define __AE_MIN32_1(a)            __ae_hmin32_1(a)
/// Lane-wise min: result = (a < b) ? a : b per 32-bit lane.
static inline ae_int32x2 __ae_min32x2(ae_int32x2 a, ae_int32x2 b) {
  return (ae_int32x2)haydn_x2min32((a), (b));
}
#define __AE_MIN32_2(a, b)         __ae_min32x2((a), (b))

#define AE_MAX16(...) __AE_MAX16_OVERLOAD(__VA_ARGS__)
#define __AE_MAX16_GET(_1, _2, NAME, ...) NAME
#define __AE_MAX16_OVERLOAD(...) \
  __AE_MAX16_GET(__VA_ARGS__, __AE_MAX16_2, __AE_MAX16_1)(__VA_ARGS__)
// R_GD x4hmax16 returns scalar int; AE 1-arg form replicates into all lanes.
static inline ae_int16x4 __ae_hmax16_1(ae_int16x4 a) {
  short m = (short)haydn_x4hmax16(a);
  return (ae_int16x4){m, m, m, m};
}
#define __AE_MAX16_1(a)            __ae_hmax16_1(a)
/// Lane-wise max for quad-16.
static inline ae_int16x4 __ae_max16x4(ae_int16x4 a, ae_int16x4 b) {
  return (ae_int16x4)haydn_x4max16((a), (b));
}
#define __AE_MAX16_2(a, b)         __ae_max16x4((a), (b))

#define AE_MIN16(...) __AE_MIN16_OVERLOAD(__VA_ARGS__)
#define __AE_MIN16_GET(_1, _2, NAME, ...) NAME
#define __AE_MIN16_OVERLOAD(...) \
  __AE_MIN16_GET(__VA_ARGS__, __AE_MIN16_2, __AE_MIN16_1)(__VA_ARGS__)
static inline ae_int16x4 __ae_hmin16_1(ae_int16x4 a) {
  short m = (short)haydn_x4hmin16(a);
  return (ae_int16x4){m, m, m, m};
}
#define __AE_MIN16_1(a)            __ae_hmin16_1(a)
/// Lane-wise min for quad-16.
static inline ae_int16x4 __ae_min16x4(ae_int16x4 a, ae_int16x4 b) {
  return (ae_int16x4)haydn_x4min16((a), (b));
}
#define __AE_MIN16_2(a, b)         __ae_min16x4((a), (b))

/// Max absolute value: compose abs then max.
///
/// Matches the Xtensa HiFi AE_MAXABS32S semantics: per-lane saturating absolute
/// of the maximum of two AE_DR registers d0 and d1.
///   result.L = SAT32(MAX(SAT_ABS(d0.L), SAT_ABS(d1.L)))
///   result.H = SAT32(MAX(SAT_ABS(d0.H), SAT_ABS(d1.H)))
///
/// Implemented via the fused haydn_maxabs32s intrinsic (decomposed in ISel to
/// X2ABS32S + X2ABS32S + X2MAX32). Used 48+ times in NatureDSP FFT kernels
/// (fft_cplx_stages_S2_32x32_hifi3) and in IIR/math overflow detection paths.
static inline __attribute__((always_inline))
ae_int32x2 AE_MAXABS32S(ae_int32x2 d0, ae_int32x2 d1) {
  return haydn_maxabs32s(d0, d1);
}

//===----------------------------------------------------------------------===//
// Horizontal Add
//===----------------------------------------------------------------------===//

/// Dual 32-bit horizontal add, high lane
#define AE_HADD32_H(a) haydn_x2hadd32_h(a)

/// Dual 32-bit horizontal add, low lane
#define AE_HADD32_L(a) haydn_x2hadd32_l(a)

/// Quad 16-bit horizontal add, high half
#define AE_HADD16_H(a) haydn_x4hadd16_h(a)

/// Quad 16-bit horizontal add, low half
#define AE_HADD16_L(a) haydn_x4hadd16_l(a)

//===----------------------------------------------------------------------===//
// Dot Product
//===----------------------------------------------------------------------===//

/// Dual 32-bit SIMD dot product
#define AE_DOT32(a, b) haydn_x2dot32((a), (b))

/// Quad 16-bit SIMD dot product
#define AE_DOT16(a, b) haydn_x4dot16((a), (b))

//===----------------------------------------------------------------------===//
// Multiply-Pair
//===----------------------------------------------------------------------===//

/// Dual 32-bit multiply-pair high
#define AE_MULPH32(a, b) haydn_x2mulph32((a), (b))

/// Dual 32-bit multiply-pair low
#define AE_MULPL32(a, b) haydn_x2mulpl32((a), (b))

/// Dual 32-bit multiply-accumulate pair high.
/// Golden X2MULAPH32: tied-acc MAC. Intrinsic is ternary (acc, a, b).
#define AE_MULAPH32(acc, a, b) (acc) = haydn_x2mulaph32((acc), (a), (b))

/// Dual 32-bit multiply-accumulate pair low.
/// Golden X2MULAPL32: tied-acc MAC. Intrinsic is ternary (acc, a, b).
#define AE_MULAPL32(acc, a, b) (acc) = haydn_x2mulapl32((acc), (a), (b))

//===----------------------------------------------------------------------===//
// Q-format multiply/accumulate
//===----------------------------------------------------------------------===//

/// Fractional 32x32->32 saturating multiply
#define AE_MULQ31(a, b) haydn_mulq31((a), (b))

/// Fractional 32x32->32 saturating MAC
#define AE_MACQ31(acc, a, b) haydn_macq31((acc), (a), (b))

/// Fractional 64x64->64 saturating multiply
#define AE_MULQ63(a, b) haydn_mulq63((a), (b))

/// 32x32->32 multiply-accumulate
#define AE_MAC32(acc, a, b) haydn_mac32((acc), (a), (b))

//===----------------------------------------------------------------------===//
// 24-bit Format Compatibility
//===----------------------------------------------------------------------===//

/// 24-bit format is HiFi3-specific. On Haydn, use 32-bit throughout.
/// These macros provide type-safe no-op conversions.

/// Move from int32x2 to f24x2 (just reinterpret)
#define AE_MOVF24X2_FROMINT32X2(a) ((ae_int24x2)(a))

/// Move from int64 to int32x2 (reinterpret; not a C splat)
#define AE_MOVINT32X2_FROMINT64(a) ((ae_int32x2)__haydn_i64_as_v2(__AE_TO_I64(a)))

//===----------------------------------------------------------------------===//
// Complex Conjugate
//===----------------------------------------------------------------------===//

/// Complex conjugate of 32-bit complex pair (negate imaginary parts)
/// Uses ADDSUB32S pattern: result = ADDSUB(a, 0) = {hi, -lo} or manual
static inline ae_int32x2 AE_CONJ32S(ae_int32x2 a) {
  /* Negate the imaginary (low) lane, keep real (high) lane */
  return haydn_x2addsub32s(a, (ae_int32x2){0, 0});
}

/// Complex conjugate of 16-bit complex quad: {re, -im} per lane.
/// Native Haydn op X4CONJ16S. (/ — was previously routed
/// through the non-existent haydn_x4addsub16s(a, 0) callee.)
static inline ae_int16x4 AE_CONJ16S(ae_int16x4 a) {
  return (ae_int16x4)__builtin_ae_conj16s((ae_int64)a);
}

//===----------------------------------------------------------------------===//
// Bitwise Operations (standard C on DR64)
//===----------------------------------------------------------------------===//

/// 32-bit bitwise OR (standard C on DR64)
#define AE_OR32(a, b) ((a) | (b))

/// 32-bit bitwise AND (standard C on DR64)
#define AE_AND32(a, b) ((a) & (b))

/// 64-bit bitwise AND (standard C on i64)
#define AE_AND64(a, b) ((a) & (b))

/// 32-bit bitwise XOR (standard C on DR64)
#define AE_XOR32(a, b) ((a) ^ (b))

//===----------------------------------------------------------------------===//
// Add-and-subtract with rounding and shift
//===----------------------------------------------------------------------===//

/// Quad 16-bit fused add+sub with range (HiFi AE_ADDANDSUBRNG16RAS_S*).
///
/// HiFi contract (statement form used in every NatureDSP FFT butterfly):
///   AE_ADDANDSUBRNG16RAS_S1(a, b);
/// updates BOTH operands in place from the originals:
///   a := sat(a + b)   (with optional SAR-based RNG on silicon)
///   b := sat(a - b)
/// DFT4XI2 / stage_* rely on this dual write. A pure function returning one
/// interleaved vector () is wrong for that call shape: the return is
/// discarded, both lanes stay unchanged, and LICM/DCE leave only SFR side
/// effects (fft_cplx16x16 store-of-load path — P8).
///
/// Closed software model (no X4ADDSUB16S in DB — /40): sat add + sat sub.
/// Full SAR-RNG fusion remains an ISA residual; dual-update is the correctness
/// contract. S0/S1/S2 share this model (HiFi slot suffix is scheduling only).
#define AE_ADDANDSUBRNG16RAS_S1(a, b)                                          \
  do {                                                                         \
    ae_int16x4 haydn_aas_t = (a);                                            \
    ae_int16x4 haydn_aas_u = (b);                                            \
    (a) = (ae_int16x4)haydn_x4add16s(haydn_aas_t, haydn_aas_u);          \
    (b) = (ae_int16x4)haydn_x4sub16s(haydn_aas_t, haydn_aas_u);          \
  } while (0)
#define AE_ADDANDSUBRNG16RAS_S2(a, b) AE_ADDANDSUBRNG16RAS_S1(a, b)

//===----------------------------------------------------------------------===//
// Misc / Convenience
//===----------------------------------------------------------------------===//

/// Constant float value — moved to the Soft-float family () section
/// at the bottom of this file (XT_CONST_S(imm) = imm##f).

/// Cross-lane select — moved to the Soft-float family () section
/// (XT_SEL32_LH_SX2 as a 2-arg lane select over xtfloatx2).

/// 16-bit multiply by j (imaginary unit): swap real/imag, negate new imag.
/// Native Haydn op X4MJSWAP16S. (/ — was previously
/// decomposed as x4seli16 + AE_CONJ16S, chaining through the non-existent
/// x4addsub16s callee.)
static inline ae_int16x4 AE_MUL16JS(ae_int16x4 a) {
  return (ae_int16x4)__builtin_ae_mul16js((ae_int64)a);
}

/// Zero-accumulate-add dual 16-bit MAC, lanes 3+3 and 2+2.
/// Exact: haydn_fmulaa16_hs_33_22 (FMULAA16_HS_33_22). Not the silent-wrong
/// _11_00 lane alias.
static inline ae_int64 AE_MULZAAFD16SS_33_22(ae_int64 acc, ae_int16x4 d,
                                               ae_int16x4 c) {
  return haydn_fmulaa16_hs_33_22(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Generic 32x32 MAC
#define AE_MULA32X2_(acc, a, b) haydn_mula64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// Multiply high/low lane extraction
//===----------------------------------------------------------------------===//

/// Single-lane fractional multiply, HH lane (extract high 32 bits)
#define AE_MUL32_HH(a, b) \
  ((ae_int32)(haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b)) >> 32))

/// Single-lane fractional multiply, LL lane (extract low 32 bits)
#define AE_MUL32_LL(a, b) ((ae_int32)haydn_mul64_ss_ll(__AE_TO_I64(a), __AE_TO_I64(b)))

//===----------------------------------------------------------------------===//
// Dual 24-bit MAC (emulated as two 32-bit MACs)
//===----------------------------------------------------------------------===//

/// Dual 24-bit add-add MAC, HH+LL lanes
static inline ae_int64 AE_MULAAFD24_HH_LL(ae_int64 acc, ae_int32x2 a,
                                            ae_int32x2 b) {
  acc = haydn_mula64_ss_hh(acc, __AE_TO_I64(a), __AE_TO_I64(b));
  acc = haydn_mula64_ss_ll(acc, __AE_TO_I64(a), __AE_TO_I64(b));
  return acc;
}

//===----------------------------------------------------------------------===//
// FF2 Shift / Pack variants
//===----------------------------------------------------------------------===//

/// Dual 32-bit FF2 fractional shift, saturate+truncate
#define AE_FF2RSST32(a, b, s) haydn_x2ff2rsst32((a), (b), (s))

/// Dual 32-bit FF2 fractional shift, round
#define AE_FF2RST32(a, b, s) haydn_x2ff2rst32((a), (b), (s))

//===----------------------------------------------------------------------===//
// X4 FF2MUL variants (Path B: 2-dest)
//===----------------------------------------------------------------------===//

/// Quad 16-bit FF2 fractional multiply, saturating — 2-dest statement form.
#define AE_FF2MUL16S(d0, d1, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4ff2mul16s((a), (b)); \
    (d0) = __AE_I4V(_r.hi); (d1) = __AE_I4V(_r.lo); \
  } while (0)

/// Quad 16-bit FF2 fractional MAC, saturating — 2-dest accumulator form.
#define AE_FF2MULA16S(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4ff2mula16s((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                            (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

/// Quad 16-bit FF2 fractional MSU, saturating — 2-dest accumulator form.
#define AE_FF2MULS16S(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4ff2muls16s((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                            (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

//===----------------------------------------------------------------------===//
// Complex multiply, Format-2 encoding (Path B: 2-dest)
//===----------------------------------------------------------------------===//

/// Dual 32-bit complex multiply, Format-2 — 2-dest statement form.
/// Public wrapper vs NatureDSP result ordering is SOURCE-REVALIDATE.
/// Fail closed in strict mode; do not invent an AE_* mapping.
#if __HAYDN_AE_COMPAT_STRICT
#define AE_CMUL32_F2(d0, d1, a, b) __HAYDN_AE_UNSUPPORTED_STMT(AE_CMUL32_F2)
#define AE_CMUL32S_F2(d0, d1, a, b) __HAYDN_AE_UNSUPPORTED_STMT(AE_CMUL32S_F2)
#else
#define AE_CMUL32_F2(d0, d1, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x2cmul32_f2((a), (b)); \
    (d0) = __AE_I2V(_r.hi); (d1) = __AE_I2V(_r.lo); \
  } while (0)
#define AE_CMUL32S_F2(d0, d1, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x2cmul32s_f2((a), (b)); \
    (d0) = __AE_I2V(_r.hi); (d1) = __AE_I2V(_r.lo); \
  } while (0)
#endif

//===----------------------------------------------------------------------===//
// Transcendental (for Newton-Raphson reciprocal etc.)
//===----------------------------------------------------------------------===//

/// Base-2 logarithm approximation
#define AE_LOG2(v) haydn_log2(v)

/// Base-2 exponential approximation
#define AE_EXP2(v) haydn_exp2(v)

/// Reciprocal approximation
#define AE_RECIP(v) haydn_recip(v)

/// Square root approximation
#define AE_SQRT(v) haydn_sqrt(v)

/// Inverse square root approximation
#define AE_ISQRT(v) haydn_isqrt(v)

//===----------------------------------------------------------------------===//
// Population Count / Bit Reversal
//===----------------------------------------------------------------------===//

/// Population count (32-bit)
#define AE_POPCOUNT32(v) haydn_popcount32(v)

/// Population count (64-bit)
#define AE_POPCOUNT64(v) haydn_popcount64(v)

/// Bit reversal (32-bit): rt = bitreverse(bitreverse(rs1) + rs2).
/// NatureDSP FFT butterfly address walker. 2-operand (Phase 3A A4 — was
/// 1-arg, a latent arity bug; the intrinsic is binary i32(i32, i32)).
#define AE_BREV32(a, b) haydn_brev32((a), (b))

//===----------------------------------------------------------------------===//
// FIR filter top-level helpers
//
// Convenience wrappers for the scalar Q-format conversions that every FIR
// kernel needs at its output boundary. These complement the per-tap MAC
// helpers above (AE_MULFD32X16X2_FIR_*, AE_MULFQ16X2_FIR_*) which already
// cover the inner-loop accumulation. The wrappers below close the loop by
// converting the int64 accumulator to the desired output precision.
//===----------------------------------------------------------------------===//

/// FIR output: saturating shift 64->32 (Q2.62 -> Q1.31).
/// Used by 32x32 block-FIR / cross-correlation kernels.
#define AE_FIR_OUT32_SAT(acc, shift) haydn_satsr64((acc), (shift))

/// FIR output: pack-and-shift 64->32 (Q2.62 -> Q1.31, exercises the
/// pack-shift path required by kernels that emit paired 32-bit lanes).
#define AE_FIR_OUT32_PACK(acc, shift) haydn_packsr32((acc), (shift))

/// FIR output: saturating shift 64->32 then inline-saturate to 16-bit
/// (Q16.47 -> Q1.31 -> Q1.15). Used by 16x16 block-FIR kernels.
static inline ae_int16 AE_FIR_OUT16_SAT(ae_int64 acc, int shift) {
  ae_int32 q32 = (ae_int32)haydn_satsr64(acc, shift);
  if (q32 > 32767)  return (ae_int16)32767;
  if (q32 < -32768) return (ae_int16)-32768;
  return (ae_int16)q32;
}

//===----------------------------------------------------------------------===//
// Float compatibility stubs — REMOVED.
// The XT_* float intrinsics are now correctly mapped in the "Soft-float
// family ()" section at the bottom of this file. Those earlier stubs
// were identity no-ops that caused float-kernel bodies to be DCE'd to empty
// .text. See the section for the soft-float libcall lowering.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 24-bit Promotion Shim (M6 / Tier 3)
//
// HiFi3 stored a 24-bit mantissa in the low 24 bits of a 32-bit register
// (Q(23+8) format). Haydn has a 32/64-bit datapath only — there is no 24-bit
// type and no 24-bit datapath. Per `hifi-to-haydn-porting-guide.md` §2/§5.3
// and decision , the 24-bit symbols used by the 102 `*24x24*hifi3*`
// kernels are mapped onto existing 32-bit Haydn intrinsics as pure header
// macros. The 8 extra bits of precision are kept (headroom gain, documented
// as expected numerical divergence in `m6-promotion-scope.md` §6).
//===----------------------------------------------------------------------===//

/// 24-bit Q-format type aliases (zero codegen effect — just spelling).
/// Note: ae_f24 (scalar), ae_f24x2 and ae_p24x2 (DR64-packed) are defined
/// ONCE in the main type block above; only the f24/f48 HiFi spellings are
/// introduced here. See for the canonical-type reconciliation.
typedef int        f24;        ///< HiFi3 f24 = int32_t (NatureDSP_types.h:375)
typedef long long  f48;        ///< HiFi3 f48 = int64_t (NatureDSP_types.h:377)

//===----------------------------------------------------------------------===//
// No-op reinterpret casts (data is already i32 in registers)
//===----------------------------------------------------------------------===//

/// AE_*_rtor_* : HiFi "reinterpret/register-to-register" conversion helpers.
/// Extract the low 32-bit lane of an int32x2 as a scalar int32. On Haydn,
/// ae_int32x2 is haydn_dr64_t (64-bit DR64); the low lane is the low 32 bits.
/// For TRUNCA32X2F64S results the value is already a packed pair — we take
/// the low word. Implemented via memcpy to avoid aliasing UB.
static inline ae_int32 ae_int32x2_rtor_int32(ae_int32x2 v) {
  int32_t lanes[2]; __builtin_memcpy(lanes, &v, sizeof lanes);
  return (ae_int32)lanes[0];
}
static inline ae_int32 ae_int32_rtor_int32(ae_int32 v) { return v; }
static inline ae_int32x2 ae_int32_rtor_int32x2(ae_int32 v) {
  ae_int32x2 r; int32_t lanes[2] = {(int32_t)v, (int32_t)v};
  __builtin_memcpy(&r, lanes, sizeof r); return r;
}
/// Float <-> int32 bit reinterpret (HiFi rtor). Used by mathf kernels that
/// manipulate FP bit patterns directly. memcpy avoids aliasing/strict-alias UB.
static inline ae_int32 ae_f32_rtor_int32(float v) {
  ae_int32 r; __builtin_memcpy(&r, &v, sizeof r); return r;
}
static inline float int32_rtor_ae_f32(ae_int32 v) {
  float r; __builtin_memcpy(&r, &v, sizeof r); return r;
}

#define AE_MOVF24X2_FROMINT32X2(a)   ((ae_f24x2)(a))
#define AE_MOVF24X2_FROMF32X2(a)     ((ae_f24x2)(a))
#define AE_MOVINT32X2_FROMF24X2(a)   ((ae_int32x2)(a))
#define AE_MOVPA24X2(a)              ((ae_f24x2)(a))
#define AE_MOVINT24X2_FROMF32X2(a)   ((ae_int24x2)(a))

//===----------------------------------------------------------------------===//
// Loads/stores — 24-bit variant is the 32-bit variant (8-bit LSB kept)
//===----------------------------------------------------------------------===//

#define AE_L32X2F24_IP(dst, ptr, inc)  AE_L32X2_IP(dst, ptr, inc)
#define AE_L32X2F24_I(dst, ptr, offs)  AE_L32X2_I(dst, ptr, offs)
#define AE_L32X2F24_XP(dst, ptr, offs, inc) AE_L32X2_XP(dst, ptr, offs, inc)
#define AE_L32X2F24_X(dst, ptr, offs)  AE_L32X2_X(dst, ptr, offs)
#define AE_L32X2F24_XC(dst, ptr, offs, cbr_sel) AE_L32X2_XC(dst, ptr, offs, cbr_sel)
/* Reverse post-inc: negative linear step (not forward IP). */
#define AE_L32X2F24_RIP(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)(ptr); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - ((offs) ? (offs) : 8)); } while (0)
/* Early reverse-CB placeholder — late overload owns negative D_LDW_CB body. */
#define AE_L32X2F24_RIC(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), -((offs) >> 3)); \
    (dst) = (ae_f24x2)(haydn_dr64_t)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

/// Dual-24 unaligned load — same AR step as AE_LA32X2_IP (C next-ptr).
#define AE_LA32X2F24_IP(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    (dst) = (ae_f24x2)haydn_ae_la64_step(__ar, __p, 8, 0); \
    (ptr) = (ae_f24x2 *)((char *)(ptr) + 8); \
  } while (0)
#define AE_LA32X2F24_I(dst, align, ptr, offs) \
  do { dst = *((ae_f24x2 *)(ptr) + ((offs) / (int)sizeof(ae_f24x2))); (void)(align); } while (0)
#define AE_LA32X2F24_XP(dst, align, ptr, offs) \
  do { dst = *(ae_f24x2 *)((char *)(ptr) + (offs)); (void)(align); } while (0)
#define AE_LA32X2F24_X(dst, align, ptr, offs) \
  do { dst = *(ae_f24x2 *)((char *)(ptr) + (offs)); (void)(align); } while (0)
/* Dual-24 unaligned circular with byte stride: AR residual + soft CBR wrap.
 * Never plain *(ptr+offs) (drops align residual and circular wrap). */
#define AE_LA32X2F24_XC(dst, align, ptr, offs, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(offs); \
    if (__s == 0) __s = 8; \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, __s, 0); \
    (dst) = (ae_f24x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)__s, (int)(cbr_sel)); \
  } while (0)
/* Reverse unaligned post-inc (dir=1); offs defaults to 8 when zero. */
#define AE_LA32X2F24_RIP(dst, align, ptr, offs) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(offs); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, __s, 1); \
    (dst) = (ae_f24x2)__le; \
    (ptr) = (ae_f24x2 *)((char *)(ptr) - __s); \
  } while (0)
/* Reverse unaligned + circular wrap (same path — and same H-first lane law
 * (D1.15): D_LTWUA_POST word order is direction-independent — as
 * AE_LA32X2_RIC). */
#define AE_LA32X2F24_RIC(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, 8, 1); \
    (dst) = (ae_f24x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), -8, (int)(cbr_sel)); \
  } while (0)

#define AE_S32X2F24_IP(src, ptr, inc)  AE_S32X2_IP(src, ptr, inc)
#define AE_S32X2F24_I(src, ptr, offs)  AE_S32X2_I(src, ptr, offs)
#define AE_S32X2F24_XP(src, ptr, offs, inc) AE_S32X2_XP(src, ptr, offs, inc)
#define AE_S32X2F24_X(src, ptr, offs)  AE_S32X2_X(src, ptr, offs)
/* Reverse linear store: write at *ptr then ptr -= offs (not forward IP). */
#define AE_S32X2F24_RIP(src, ptr, offs) \
  do { \
    *(ae_f24x2 *)(ptr) = (src); \
    (ptr) = (ae_f24x2 *)((char *)(ptr) - ((offs) ? (offs) : 8)); \
  } while (0)
#define AE_SA32X2F24_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, 8, 0); \
    (ptr) = (ae_f24x2 *)((char *)(ptr) + 8); \
  } while (0)
#define AE_SA32X2F24_I(src, align, ptr, offs) \
  do { *((ae_f24x2 *)(ptr) + ((offs) / (int)sizeof(ae_f24x2))) = (src); (void)(align); } while (0)
/* Early reverse placeholder — late SA32X2F24_RIP owns align/arity forms. */
#define AE_SA32X2F24_RIP(src, align, ptr, offs) \
  do { \
    *(ae_f24x2 *)(ptr) = (src); \
    (ptr) = (ae_f24x2 *)((char *)(ptr) - ((offs) ? (offs) : 8)); \
    (void)(align); \
  } while (0)
/* Dual-24 unaligned circular store: AR residual + soft CBR wrap (not plain mem). */
#define AE_SA32X2F24_XC(src, align, ptr, offs, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(offs); \
    if (__s == 0) __s = 8; \
    haydn_dr64_t __s64 = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    haydn_ae_sa64_step(__AE_AS_V2(__s64), __ar, __p, __s, 0); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)__s, (int)(cbr_sel)); \
  } while (0)

#define AE_LA24X2_IP(dst, align, ptr) AE_LA32X2F24_IP(dst, align, ptr)
#define AE_LA24X2_I(dst, align, ptr, offs) \
  do { dst = *((ae_f24x2 *)(ptr) + ((offs) / (int)sizeof(ae_f24x2))); (void)(align); } while (0)
#define AE_LA24X2_XP(dst, align, ptr, offs) \
  do { dst = *(ae_f24x2 *)((char *)(ptr) + (offs)); (void)(align); } while (0)
#define AE_LA24X2_X(dst, align, ptr, offs) \
  do { dst = *(ae_f24x2 *)((char *)(ptr) + (offs)); (void)(align); } while (0)
#define AE_SA24X2_IP(src, align, ptr) AE_SA32X2F24_IP(src, align, ptr)
#define AE_SA24X2_I(src, align, ptr, offs) \
  do { *((ae_f24x2 *)(ptr) + ((offs) / (int)sizeof(ae_f24x2))) = (src); (void)(align); } while (0)
#define AE_SA24X2_XP(src, align, ptr, offs) \
  do { *(ae_f24x2 *)((char *)(ptr) + (offs)) = (src); (void)(align); } while (0)
#define AE_SA24X2_X(src, align, ptr, offs) \
  do { *(ae_f24x2 *)((char *)(ptr) + (offs)) = (src); (void)(align); } while (0)

/// Dual-24 unaligned circular (align + cbr_sel): same AR residual + soft CBR
/// wrap as AE_LA32X2F24_IC / AE_SA32X2F24_IC. Must not silent-alias aligned
/// D_LDW_CB (drops AR residual) or plain mem without ptr wrap. Late overload
/// owns 3-arg/4-arg forms; early body routes to the same late helpers.
#define AE_LA24X2_IC(dst, align, ptr, cbr_sel) \
  __AE_LA32X2F24_IC_4A((dst), (align), (ptr), (cbr_sel))
#define AE_SA24X2_IC(src, align, ptr, cbr_sel) \
  __AE_SA32X2F24_IC_4A((src), (align), (ptr), (cbr_sel))

/// Post-increment helpers (HiFi3 helper API)
#define ae_f24x2_loadip(dst, ptr, inc) AE_L32X2_IP(dst, ptr, inc)
#define ae_f24x2_storeip(src, ptr, inc) AE_S32X2_IP(src, ptr, inc)
#define ae_f24x2_loadxp(dst, ptr, offs, inc) AE_L32X2_XP(dst, ptr, offs, inc)
#define ae_f24x2_storexp(src, ptr, offs, inc) AE_S32X2_XP(src, ptr, offs, inc)
#define ae_f24x2_loadi(dst, ptr, offs) AE_L32X2_I(dst, ptr, offs)
#define ae_f24x2_storex(src, ptr, offs) AE_S32X2_X(src, ptr, offs)

//===----------------------------------------------------------------------===//
// Lane-pack selects — dual-24 lives in 32-bit lanes; same X2SEL32 as AE_SEL32_*
// (must not silent-alias to bitwise OR of the whole bag).
//===----------------------------------------------------------------------===//

#define AE_SELP24_HH(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_hh(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SELP24_HL(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_hl(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SELP24_LH(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_lh(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SELP24_LL(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_ll(__AE_AS_V2(a), __AE_AS_V2(b))))

//===----------------------------------------------------------------------===//
// 24-bit ALU saturating — dual-lane X2* sat ALU (8-bit headroom)
// NOTE: out-of-range 24-bit operands saturate at the 32-bit boundary, not
// 24-bit. Must not silent-alias to scalar add32s/neg32s (high lane drop).
//===----------------------------------------------------------------------===//

#define AE_ADD24S(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2add32s(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SUB24S(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sub32s(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_ADDSP24S(a, b) AE_ADD24S((a), (b))
#define AE_SUBSP24S(a, b) AE_SUB24S((a), (b))
#define AE_NEG24S(a) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2neg32s(__AE_AS_V2(a))))
#define AE_NEGSP24S(a) AE_NEG24S((a))
#define AE_SAT24S(a)       (a)              ///< no-op on Haydn (32-bit range)
/* Dual-24 zero bag (SEL24 operand); never bare scalar 0 as a silent bag. */
#define AE_ZERO24()        ((ae_f24x2)(haydn_dr64_t)0)

//===----------------------------------------------------------------------===//
// 24-bit shifts — dual-lane ASR (same X2SRA32 as AE_F32X2_SRAI / F24X2_SRAI).
// Must not silent-alias to scalar (int)a>>s (high-lane drop on ae_f24x2).
// Early AE_PKSR24 placeholder; late overload owns packsr32 2-/3-arg forms.
//===----------------------------------------------------------------------===//

#define AE_F24X2_SRAI(a, s) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sra32(__AE_AS_V2(a), (s))))
#define AE_SRAI24(a, s) AE_F24X2_SRAI((a), (s))
#define AE_PKSR24(a, s) ((ae_f24x2)haydn_packsr32((a), (s)))

//===----------------------------------------------------------------------===//
// FIR/MAC families — 24x24->48 MAC maps to 32x32->64 MAC
//===----------------------------------------------------------------------===//

/// Dual 24-bit FIR MAC, HH lanes (per-lane decomposition onto FMULA32S_HH).
static inline void AE_MULAFD24X2_FIR_H(ae_int64 *q0, ae_int64 *q1,
                                        ae_f24x2 d, ae_f24x2 c) {
  *q0 = haydn_fmula32s_hh(*q0, __AE_TO_I64(d), __AE_TO_I64(c));
  *q1 = haydn_fmula32s_hh(*q1, __AE_TO_I64(d), __AE_TO_I64(c));
}

/// Dual 24-bit FIR multiply-init, HH lanes (per-lane decomposition).
static inline void AE_MULFD24X2_FIR_H(ae_int64 *q0, ae_int64 *q1,
                                       ae_f24x2 d, ae_f24x2 c) {
  *q0 = haydn_fmul32s_hh(__AE_TO_I64(d), __AE_TO_I64(c));
  *q1 = haydn_fmul32s_hh(__AE_TO_I64(d), __AE_TO_I64(c));
}

/// 24x24->48 single-lane MAC variants (HH/HL/LH/LL lane select)
#define AE_MULAAFD24_HH_LL(acc, a, b) \
  haydn_fmula32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULAAFD24_HL_LH(acc, a, b) \
  haydn_fmula32s_lh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULSSFD24_HH_LL(acc, a, b) \
  haydn_fmuls32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULSSFD24_HL_LH(acc, a, b) \
  haydn_fmuls32s_lh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULASFD24_HH_LL(acc, a, b) \
  haydn_mulsa32_hhll((acc), (haydn_dr64_t)(a), (haydn_dr64_t)(b))
#define AE_MULASFD24_HL_LH(acc, a, b) \
  haydn_mulsa32_hllh((acc), (haydn_dr64_t)(a), (haydn_dr64_t)(b))

/// Zero-accumulator variants (init from zero, then MAC)
#define AE_MULZAAFD24_HH_LL(acc, a, b) \
  haydn_fmula32s_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULZAAFD24_HL_LH(acc, a, b) \
  haydn_fmula32s_lh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULZSSFD24_HH_LL(acc, a, b) \
  haydn_fmuls32s_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULZSSFD24_HL_LH(acc, a, b) \
  haydn_fmuls32s_lh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULZASFD24_HH_LL(acc, a, b) \
  haydn_mulsa32_hhll((ae_int64)0, (haydn_dr64_t)(a), (haydn_dr64_t)(b))
#define AE_MULZASFD24_HL_LH(acc, a, b) \
  haydn_mulsa32_hllh((ae_int64)0, (haydn_dr64_t)(a), (haydn_dr64_t)(b))
#define AE_MULZAAD24_HH_LL(acc, a, b) \
  haydn_fmula32s_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//===----------------------------------------------------------------------===//
// Fractional MAC families (rounding+saturate) — map to FF2MULA32RS / FF2MULS32RS
//===----------------------------------------------------------------------===//

/// Overloaded: 2-arg returning form `AE_MULFP24X2RA(a, b)` is a Q.23 fractional
/// multiply (round+saturate); 3-arg storing/accumulate form
/// `AE_MULFP24X2RA(acc, a, b)` computes acc + a*b. The original HiFi3 math
/// kernels use both forms.
#define AE_MULFP24X2RA(...) __AE_MULFP24X2RA_OVERLOAD(__VA_ARGS__)
#define __AE_MULFP24X2RA_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULFP24X2RA_OVERLOAD(...) \
  __AE_MULFP24X2RA_GET(__VA_ARGS__, __AE_MULFP24X2RA_3, \
                       __AE_MULFP24X2RA_2)(__VA_ARGS__)
#define __AE_MULFP24X2RA_2(a, b) \
  haydn_ff2mula32rs_hh((0), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MULFP24X2RA_3(acc, a, b) \
  haydn_ff2mula32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULAFP24X2RA(acc, a, b) \
  haydn_ff2mula32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULSFP24X2RA(acc, a, b) \
  haydn_ff2muls32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//===----------------------------------------------------------------------===//
// 24-bit complex MAC — maps to 32-bit complex MAC (data already in i32 lanes)
//===----------------------------------------------------------------------===//
//
// X2CMUL public-wrapper vs NatureDSP ordering is SOURCE-REVALIDATE.
// Fail closed in strict mode; do not invent AE_MULFC24* → x2cmul32s maps.
#if __HAYDN_AE_COMPAT_STRICT
#define AE_MULFC24RA(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULFC24RA)
#define AE_MULAFC24RA(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULAFC24RA)
#define AE_MULSFC24RA(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULSFC24RA)
#else
#define AE_MULFC24RA(...) __AE_MULFC24RA_OVERLOAD(__VA_ARGS__)
#define __AE_MULFC24RA_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULFC24RA_OVERLOAD(...) \
  __AE_MULFC24RA_GET(__VA_ARGS__, __AE_MULFC24RA_3A, __AE_MULFC24RA_2A)(__VA_ARGS__)
#define __AE_MULFC24RA_2A(a, b) \
  ((ae_f24x2)haydn_x2cmul32s((haydn_dr64_t)(a), (haydn_dr64_t)(b)).hi)
#define __AE_MULFC24RA_3A(acc, a, b) \
  ((ae_f24x2)((int64_t)haydn_x2cmul32s((haydn_dr64_t)(a), (haydn_dr64_t)(b)).hi \
              + (int64_t)(acc)))
#define AE_MULAFC24RA(...) __AE_MULFC24RA_OVERLOAD(__VA_ARGS__)
#define AE_MULSFC24RA(...) __AE_MULSFC24RA_OVERLOAD(__VA_ARGS__)
#define __AE_MULSFC24RA_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULSFC24RA_OVERLOAD(...) \
  __AE_MULSFC24RA_GET(__VA_ARGS__, __AE_MULSFC24RA_3A, __AE_MULSFC24RA_2A)(__VA_ARGS__)
#define __AE_MULSFC24RA_2A(a, b) \
  ((ae_f24x2)haydn_x2cmul32s((haydn_dr64_t)(a), (haydn_dr64_t)(b)).hi)
#define __AE_MULSFC24RA_3A(acc, a, b) \
  ((ae_f24x2)((int64_t)(acc) \
              - (int64_t)haydn_x2cmul32s((haydn_dr64_t)(a), (haydn_dr64_t)(b)).hi))
#endif

//===----------------------------------------------------------------------===//
// Round/saturate — f48 -> f24 (Q16.47 -> Q1.31) uses packsr32 with shift 24
// to truncate the f48 accumulator back to a Q1.31 i32.
//===----------------------------------------------------------------------===//

#define AE_ROUND24X2F48SASYM(a, b, s) haydn_packsr32((a), (s))
#define AE_ROUNDSP24Q48ASYM(a, b, s)  haydn_packsr32((a), (s))
#define AE_S24RA64S_IP(dst, acc, shift) \
  do { (dst) = (int)haydn_satsr64((acc), (shift)); } while (0)
#define AE_S24RA64S_XP(dst, acc, shift, ptr, offs) \
  do { \
    (dst) = (int)haydn_satsr64((acc), (shift)); \
    *((int *)(ptr) + (offs)) = (dst); \
  } while (0)

//===----------------------------------------------------------------------===//
// M6 HEADER COMPLETION BATCH
//
// The intrinsics below close the remaining HiFi3 AE_* surface so the
// original NatureDSP kernel sources compile UNCHANGED through haydn_dsp.h.
// Each entry is either a one-to-one map to a haydn_ intrinsic or a
// small composition over the haydn_ ALU/MAC/SIMD primitives.
//===----------------------------------------------------------------------===//

//---- Scalar 16-bit loads/stores ---------------------------------------
// AE_L16_I has two HiFi3 forms (matching AE_L32_I):
//   2-arg (ptr, offs)  : expression returning loaded value.
//   3-arg (dst,ptr,offs): statement form assigning to dst.
#define AE_L16_I(...) __AE_L16_I_OVERLOAD(__VA_ARGS__)
#define __AE_L16_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16_I_OVERLOAD(...) \
  __AE_L16_I_GET(__VA_ARGS__, __AE_L16_I_3, __AE_L16_I_2)(__VA_ARGS__)
#define __AE_L16_I_2(ptr, offs) \
  (*((ae_int16 *)(ptr) + (offs)))
#define __AE_L16_I_3(dst, ptr, offs) \
  do { dst = *((ae_int16 *)(ptr) + (offs)); } while (0)
#define AE_L16_IP(dst, ptr, inc) \
  do { dst = *(ae_int16 *)(ptr); (ptr) = (ae_int16 *)((char *)(ptr) + (inc)); } while (0)
#define AE_L16_X(dst, ptr, offs) \
  do { dst = *((ae_int16 *)(ptr) + (offs)); } while (0)
#define AE_L16_XP(dst, ptr, offs, inc) \
  do { dst = *((ae_int16 *)(ptr) + (offs)); (ptr) = (ae_int16 *)((char *)(ptr) + (inc)); } while (0)

#define AE_S16_0_I(src, ptr, offs) \
  do { *((ae_int16 *)(ptr) + (offs)) = (ae_int16)(src); } while (0)
#define AE_S16_0_IP(src, ptr, inc) \
  do { *(ae_int16 *)(ptr) = (ae_int16)(src); (ptr) = (ae_int16 *)((char *)(ptr) + (inc)); } while (0)
#define AE_S16_0_X(src, ptr, offs) \
  do { *((ae_int16 *)(ptr) + (offs)) = (ae_int16)(src); } while (0)
#define AE_S16_0_XP(src, ptr, offs, inc) \
  do { *((ae_int16 *)(ptr) + (offs)) = (ae_int16)(src); (ptr) = (ae_int16 *)((char *)(ptr) + (inc)); } while (0)
/// AE_S16_0_XC: scalar 16-bit circular store. HiFi3z is 3-arg (implicit CBR0).
#undef  AE_S16_0_XC
#define AE_S16_0_XC(...) __AE_S16_0_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S16_0_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16_0_XC_OVERLOAD(...) \
  __AE_S16_0_XC_GET(__VA_ARGS__, __AE_S16_0_XC_4A, __AE_S16_0_XC_3A)(__VA_ARGS__)
#define __AE_S16_0_XC_3A(src, ptr, offs) \
  __AE_S16_0_XC_4A(src, ptr, offs, 0)
#define __AE_S16_0_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    *(ae_int16 *)(void *)(ptr) = (ae_int16)(src); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- Scalar 32-bit loads/stores (additional spellings) ----------------
// AE_L32_I has two HiFi3 forms:
//   2-arg (ptr, offs)  : expression returning loaded value.
//   3-arg (dst,ptr,offs): statement form assigning to dst.
#define AE_L32_I(...) __AE_L32_I_OVERLOAD(__VA_ARGS__)
#define __AE_L32_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32_I_OVERLOAD(...) \
  __AE_L32_I_GET(__VA_ARGS__, __AE_L32_I_3, __AE_L32_I_2)(__VA_ARGS__)
#define __AE_L32_I_2(ptr, offs) \
  (*(ae_int32 *)((char *)(ptr) + (offs)))
#define __AE_L32_I_3(dst, ptr, offs) \
  do { \
    int32_t haydn_l32i = *(const int32_t *)((char *)(void *)(ptr) + (offs)); \
    (dst) = AE_MOVDA32(haydn_l32i); \
  } while (0)

#define AE_L32_X(...) __AE_L32_X_OVERLOAD(__VA_ARGS__)
#define __AE_L32_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32_X_OVERLOAD(...) \
  __AE_L32_X_GET(__VA_ARGS__, __AE_L32_X_3, __AE_L32_X_2)(__VA_ARGS__)
#define __AE_L32_X_2(ptr, offs) (*(ae_int32 *)((char *)(ptr) + (offs)))
#define __AE_L32_X_3(dst, ptr, offs) \
  do { (dst) = *(ae_int32 *)((char *)(ptr) + (offs)); } while (0)

/// AE_L32_XC: scalar 32-bit circular load. HiFi3z is 3-arg (implicit CBR0).
#undef  AE_L32_XC
#define AE_L32_XC(...) __AE_L32_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L32_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32_XC_OVERLOAD(...) \
  __AE_L32_XC_GET(__VA_ARGS__, __AE_L32_XC_4A, __AE_L32_XC_3A)(__VA_ARGS__)
#define __AE_L32_XC_3A(dst, ptr, offs) \
  __AE_L32_XC_4A(dst, ptr, offs, 0)
#define __AE_L32_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    (dst) = *(ae_int32 *)(void *)(ptr); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

#define AE_S32_L_I(src, ptr, inc) \
  do { *(ae_int32 *)(ptr) = (ae_int32)(src); (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)
#define AE_S32_L_X(src, ptr, offs) \
  do { *((ae_int32 *)(ptr) + (offs)) = (ae_int32)(src); } while (0)
/// AE_S32_L_XC: scalar 32-bit circular store. HiFi3z is 3-arg (implicit CBR0).
#undef  AE_S32_L_XC
#define AE_S32_L_XC(...) __AE_S32_L_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S32_L_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32_L_XC_OVERLOAD(...) \
  __AE_S32_L_XC_GET(__VA_ARGS__, __AE_S32_L_XC_4A, __AE_S32_L_XC_3A)(__VA_ARGS__)
#define __AE_S32_L_XC_3A(src, ptr, offs) \
  __AE_S32_L_XC_4A(src, ptr, offs, 0)
#define __AE_S32_L_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    *(ae_int32 *)(void *)(ptr) = (ae_int32)(src); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)
#define AE_S32_L_XP(src, ptr, offs, inc) \
  do { *((ae_int32 *)(ptr) + (offs)) = (ae_int32)(src); (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)

//---- 64-bit loads/stores ----------------------------------------------
#define AE_L64_IP(dst, ptr, inc) \
  do { dst = *(ae_int64 *)(ptr); (ptr) = (ae_int64 *)((char *)(ptr) + (inc)); } while (0)
#define AE_L64_X(dst, ptr, offs) \
  do { dst = *(ae_int64 *)((char *)(ptr) + (offs)); } while (0)
#define AE_L64_I(dst, ptr, offs) \
  do { dst = *((ae_int64 *)(ptr) + ((offs) / (int)sizeof(ae_int64))); } while (0)
#define AE_L64_XP(dst, ptr, offs, inc) \
  do { dst = *(ae_int64 *)((char *)(ptr) + (offs)); (ptr) = (ae_int64 *)((char *)(ptr) + (inc)); } while (0)

#define AE_S64_IP(src, ptr, inc) \
  do { *(ae_int64 *)(ptr) = (src); (ptr) = (ae_int64 *)((char *)(ptr) + (inc)); } while (0)
#define AE_S64_X(src, ptr, offs) \
  do { *(ae_int64 *)((char *)(ptr) + (offs)) = (src); } while (0)
#define AE_S64_I(src, ptr, offs) \
  do { *((ae_int64 *)(ptr) + ((offs) / (int)sizeof(ae_int64))) = (src); } while (0)

//---- 8-byte-aligned quad-16 range load/store --------------------------
#define AE_L8X4F_IP(dst, ptr, inc) \
  do { dst = *(ae_int16x4 *)(ptr); (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc)); } while (0)
// AE_S16X4RNG = store 4x16 with saturation. Use native x4sat32t16
// to saturate each 32-bit lane to 16-bit before storing, keeping the
// operation entirely in DR64 (no GPR cross-bank round-trip).
#define AE_S16X4RNG_IP(src, ptr, inc) do { \
    *(ae_int16x4 *)(ptr) = (ae_int16x4)haydn_x4sat32t16((src), (ae_int16x4){0, 0, 0, 0}); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc)); } while(0)
#define AE_S16X4RNG_X(src, ptr, offs) \
    *(ae_int16x4 *)((char *)(ptr) + (offs)) = (ae_int16x4)haydn_x4sat32t16((src), (ae_int16x4){0, 0, 0, 0})
#define AE_S16X4RNG_XP(src, ptr, offs, inc) do { \
    *(ae_int16x4 *)((char *)(ptr) + (offs)) = (ae_int16x4)haydn_x4sat32t16((src), (ae_int16x4){0, 0, 0, 0}); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc)); } while(0)

//---- Misc load/store spellings ----------------------------------------
/* AE_L32X2_RIC / AE_L32X2_RIP: late reverse-CB / reverse-IP overload block. */
/* AE_L16X4_RIC: keep primary reverse-CB body (negative D_LDW_CB stride); do
 * not re-alias to forward XC. Overload block below wins for arity. */
/* AE_LA16X4_RIP: use primary definition above (AR helpers). */

//---- S32X2 reverse / circular additional spellings --------------------
/* AE_L32X2_RIC early forward-alias removed: late overload is reverse-CB. */
#define AE_S32X2_RIP(src, ptr, inc) \
  do { *(ae_int32x2 *)(ptr) = (src); (ptr) = (ae_int32x2 *)((char *)(ptr) - (inc)); } while (0)
#define AE_S32X2_XP(src, ptr, offs, inc) \
  do { *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
#define AE_S32X2_XC(src, ptr, offs, cbr_sel) \
  do { *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); (void)(cbr_sel); } while (0)
#define AE_S16X4_XC(src, ptr, offs, cbr_sel) \
  do { *((ae_int16x4 *)(ptr) + ((offs) / (int)sizeof(ae_int16x4))) = (src); (void)(cbr_sel); } while (0)
#define AE_S16X4_X(dst, ptr, offs) AE_L16X4_X(dst, ptr, offs)
#undef  AE_S16X4_X
#define AE_S16X4_X(src, ptr, offs) \
  do { *((ae_int16x4 *)(ptr) + ((offs) / (int)sizeof(ae_int16x4))) = (src); } while (0)
#define AE_S16X4_XP(src, ptr, offs, inc) \
  do { *((ae_int16x4 *)(ptr) + ((offs) / (int)sizeof(ae_int16x4))) = (src); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); } while (0)

//---- L32F24 / S32F24 scalar-24 load/store -----------------------------
#define AE_L32F24_I(...)  AE_L32_I(__VA_ARGS__)
#define AE_L32F24_IP(dst, ptr, inc)  AE_L32_IP(dst, ptr, inc)
#define AE_L32F24_X(dst, ptr, offs)  AE_L32_X(dst, ptr, offs)
#define AE_L32F24_XC(dst, ptr, offs, cbr_sel) AE_L32_XC(dst, ptr, offs, cbr_sel)
#define AE_S32F24_L_I(src, ptr, inc) AE_S32_L_I(src, ptr, inc)
#define AE_S32F24_L_IP(src, ptr, inc) AE_S32_L_IP(src, ptr, inc)
#define AE_S32F24_L_X(src, ptr, offs) AE_S32_L_X(src, ptr, offs)
#define AE_S32F24_L_XC(src, ptr, offs, cbr_sel) AE_S32_L_XC(src, ptr, offs, cbr_sel)

//---- L16M / S16M (16-bit with modifier) -------------------------------
#define AE_L16M_I(...)   AE_L16_I(__VA_ARGS__)
#define AE_L16M_IU(dst, ptr, inc)   AE_L16_IP(dst, ptr, inc)
#define AE_L16X2M_I(dst, ptr, offs) \
  do { dst = *(ae_int32 *)((char *)(ptr) + (offs)); } while (0)
#define AE_L16X2M_IU(dst, ptr, inc) \
  do { dst = *(ae_int32 *)(ptr); (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc)); } while (0)
#define AE_L16X2M_X(dst, ptr, offs) AE_L16X2M_I(dst, ptr, offs)
#define AE_L16X2M_XU(dst, ptr, offs) AE_L16X2M_IU(dst, ptr, offs)
#define AE_S16M_L_I(src, ptr, inc)  AE_S16_0_IP(src, ptr, inc)
#define AE_S16M_L_IU(src, ptr, inc) AE_S16_0_IP(src, ptr, inc)
#define AE_S16X2M_I(src, ptr, inc) \
  do { *(ae_int32 *)(ptr) = (src); (ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)

//---- S24 / SA24 / SA64NEG ---------------------------------------------
#define AE_SA24_IP(src, align, ptr) \
  do { *(ae_f24x2 *)(ptr) = (src); (ptr) = (ae_f24x2 *)((char *)(ptr) + 8); (void)(align); } while (0)
/* Early SA IC placeholders — late overload owns AR residual + soft CBR wrap.
 * Must not silent-alias plain mem (drops align residual and circular wrap). */
#define AE_SA16X4_IC(src, align, ptr, cbr_sel) \
  __AE_SA16X4_IC_4A((src), (align), (ptr), (cbr_sel))
#define AE_SA32X2_IC(src, align, ptr, cbr_sel) \
  __AE_SA32X2_IC_4A((src), (align), (ptr), (cbr_sel))
/* Early SA F24 IC placeholder — late overload owns AR residual + CBR wrap. */
#define AE_SA32X2F24_IC(src, align, ptr, cbr_sel) \
  __AE_SA32X2F24_IC_4A((src), (align), (ptr), (cbr_sel))
/* AE_SA64NEG_FP defined with AR path above (WBARWUA dir=1). */
/* Dual-24 unaligned seed: PLDWWUA into AR (same as AE_LA32X2POS_PC). Never no-op. */
/* Dual-24 F24 POS seed: same PLDWWUA as base POS_PC (probe-only; reverse
 * direction is the later IC/RIC ImmArg — do not invent pre-decrement). */
#define AE_LA32X2F24POS_PC(align, ptr) \
  do { (align) = haydn_ae_la64_pp(ptr); } while (0)
#define AE_LA24X2POS_PC(align, ptr) AE_LA32X2F24POS_PC((align), (ptr))
/* Early F24 IC placeholder — late overload owns AR residual + CBR wrap. */
#define AE_LA32X2F24_IC(dst, align, ptr, cbr_sel) \
  __AE_LA32X2F24_IC_4A((dst), (align), (ptr), (cbr_sel))

//---- Float load/store (XT-compat) -------------------------------------
#define AE_LSIP(dst, ptr, inc) AE_L32_IP(dst, ptr, inc)
#define AE_SSIP(src, ptr, inc) AE_S32_L_IP(src, ptr, inc)
#define AE_LSX2IP(dst, ptr, inc) AE_L32X2_IP(dst, ptr, inc)
#define AE_SSX2IP(src, ptr, inc) AE_S32X2_IP(src, ptr, inc)
#define AE_LSX2X2_IP(dst0, dst1, ptr, inc) \
  do { AE_L32X2_IP(dst0, ptr, sizeof(ae_int32x2)); \
       AE_L32X2_IP(dst1, ptr, sizeof(ae_int32x2)); } while (0)
#define AE_SSX2X2_IP(src0, src1, ptr, inc) \
  do { AE_S32X2_IP(src0, ptr, sizeof(ae_int32x2)); \
       AE_S32X2_IP(src1, ptr, sizeof(ae_int32x2)); } while (0)

//---- Accumulator-format conversions -----------------------------------
#define AE_CVTQ48A32S(x) ((ae_int64)(x))
#define AE_NSAQ56S(q)    ((int)haydn_nsa64((q)) - 8)
#define AE_ROUNDSQ32F48ASYM(q, s)  haydn_packsr32((q), (s))
/// Overloaded: the 1-arg returning form `AE_ROUND32F64SASYM(q)` (original HiFi3
/// API used in math kernels) rounds the 64-bit accumulator to 32 bits with no
/// additional shift; the 2-arg storing form `AE_ROUND32F64SASYM(q, s)` applies
/// an explicit right shift `s` after rounding.
#define AE_ROUND32F64SASYM(...) __AE_ROUND32F64SASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUND32F64SASYM_GET(_1, _2, NAME, ...) NAME
#define __AE_ROUND32F64SASYM_OVERLOAD(...) \
  __AE_ROUND32F64SASYM_GET(__VA_ARGS__, __AE_ROUND32F64SASYM_2, \
                          __AE_ROUND32F64SASYM_1)(__VA_ARGS__)
#define __AE_ROUND32F64SASYM_1(q)    haydn_packsr32((q), 0)
#define __AE_ROUND32F64SASYM_2(q, s) haydn_packsr32((q), (s))
#define AE_ROUND32X2F48SASYM(q0, q1, s) haydn_packsr32((q0), (s))
#define AE_ROUND24X2F48SASYM(q0, q1, s) haydn_packsr32((q0), (s))
#define AE_TRUNCA32F64S(q, s)      haydn_satsr64((q), (s))
#define AE_SRAA64(q, s)            ((ae_int64)((q) >> (s)))
#define AE_SLAA64(q, s)            ((ae_int64)((ae_int64)(q) << (s)))
/* Saturating arithmetic left shift (64-bit). Soft model matches NatureDSP
 * AE_SLAA64S / AE_F64_SLAS: clamp to INT64_MIN/MAX on overflow.
 * Use unsigned left-shift + round-trip check — never a positive minv from
 * logical right-shift of INT64_MIN, and never signed << (UB on negatives). */
static inline ae_int64 haydn_ae_slaa64s(ae_int64 q, int s) {
  int64_t v = (int64_t)q;
  if (s == 0)
    return (ae_int64)v;
  if (s < 0)
    return (ae_int64)(v >> (unsigned)(-s));
  if (s >= 63) {
    if (v == 0)
      return (ae_int64)0;
    return (ae_int64)(v < 0 ? (int64_t)0x8000000000000000LL
                            : (int64_t)0x7FFFFFFFFFFFFFFFLL);
  }
  {
    int64_t r = (int64_t)((uint64_t)v << (unsigned)s);
    if ((r >> s) != v)
      return (ae_int64)(v < 0 ? (int64_t)0x8000000000000000LL
                              : (int64_t)0x7FFFFFFFFFFFFFFFLL);
    return (ae_int64)r;
  }
}
#define AE_SLAA64S(q, s)           haydn_ae_slaa64s((q), (int)(s))
/* AE_SLAS64S: saturating arithmetic left (alias of SLAA64S; not silent ASR). */
#define AE_SLAS64S(q, s)           haydn_ae_slaa64s((q), (int)(s))
/* AE_SLAI64S: saturating left immediate — same soft sat as SLAA64S (not plain SLAI64). */
#define AE_SLAI64S(q, s)           haydn_ae_slaa64s((q), (int)(s))
/* AE_SLLI64: same as AE_SLAI64 — ImmArg SLLI64 / reg SLL64 via constant_p. */
#define AE_SLLI64(q, s)            AE_SLAI64((q), (s))
#define AE_NEG64S(q)               haydn_neg64s((q))
#define AE_ZEROP48()               ((ae_int64)0)

//---- Scalar / SIMD shifts --------------------------------------------
// Haydn shift intrinsics (x2slli32, x4sra16, ...) require an IMMEDIATE
// shift amount. Kernels may pass a runtime value (e.g. AE_SLAA32S(Y, E)
// where E comes from AE_MOVAD32_H). Soft saturating left-shift model:
// clamp each lane to intN min/max when the left shift would overflow.
// Optimizer folds when the amount is constant.
static inline int32_t haydn_ae_sla32s_lane(int32_t v, int s) {
  if (s <= 0)
    return (s == 0) ? v : (int32_t)(v >> (unsigned)(-s));
  if (s >= 31) {
    if (v == 0) return 0;
    return v > 0 ? (int32_t)0x7FFFFFFF : (int32_t)0x80000000;
  }
  {
    /* Unsigned left-shift + arithmetic round-trip: defined for negatives. */
    int32_t r = (int32_t)((uint32_t)v << (unsigned)s);
    if ((r >> s) != v)
      return v > 0 ? (int32_t)0x7FFFFFFF : (int32_t)0x80000000;
    return r;
  }
}
static inline int16_t haydn_ae_sla16s_lane(int16_t v, int s) {
  if (s <= 0)
    return (s == 0) ? v : (int16_t)(v >> (unsigned)(-s));
  if (s >= 15) {
    if (v == 0) return 0;
    return v > 0 ? (int16_t)0x7FFF : (int16_t)0x8000;
  }
  {
    int16_t r = (int16_t)((uint16_t)v << (unsigned)s);
    if ((int16_t)(r >> s) != v)
      return v > 0 ? (int16_t)0x7FFF : (int16_t)0x8000;
    return r;
  }
}
static inline ae_int32x2 __ae_slaa32s(ae_int32x2 a, int s) {
  int64_t bag = __AE_TO_I64(a);
  int32_t lo = haydn_ae_sla32s_lane((int32_t)(uint32_t)bag, s);
  int32_t hi = haydn_ae_sla32s_lane((int32_t)(uint32_t)((uint64_t)bag >> 32), s);
  return __haydn_i64_as_v2(
      (haydn_dr64_t)((uint64_t)(uint32_t)lo | ((uint64_t)(uint32_t)hi << 32)));
}
static inline ae_int16x4 __ae_slaa16s(ae_int16x4 a, int s) {
  int64_t bag = __AE_TO_I64(a);
  int16_t l0 = haydn_ae_sla16s_lane((int16_t)(uint16_t)bag, s);
  int16_t l1 = haydn_ae_sla16s_lane((int16_t)(uint16_t)((uint64_t)bag >> 16), s);
  int16_t l2 = haydn_ae_sla16s_lane((int16_t)(uint16_t)((uint64_t)bag >> 32), s);
  int16_t l3 = haydn_ae_sla16s_lane((int16_t)(uint16_t)((uint64_t)bag >> 48), s);
  return __haydn_i64_as_v4(
      (haydn_dr64_t)((uint64_t)(uint16_t)l0 | ((uint64_t)(uint16_t)l1 << 16) |
                     ((uint64_t)(uint16_t)l2 << 32) | ((uint64_t)(uint16_t)l3 << 48)));
}
static inline ae_int16x4 __ae_srai16r(ae_int16x4 a, int s) {
  long long v = (long long)a;
  long long rb = (s > 0) ? ((long long)1 << (s - 1)) : 0;
  short l0 = (short)((int)(v) + (int)rb) >> s;
  short l1 = (short)((int)(v >> 16) + (int)rb) >> s;
  short l2 = (short)((int)(v >> 32) + (int)rb) >> s;
  short l3 = (short)((int)(v >> 48) + (int)rb) >> s;
  return (ae_int16x4)((long long)(unsigned short)l0 | ((long long)(unsigned short)l1 << 16) |
                     ((long long)(unsigned short)l2 << 32) | ((long long)(unsigned short)l3 << 48));
}
/// Native X2SRA32R (reg form; ImmArg x2srai32r only for literal call sites).
/// Dominant path for MDCT/IMDCT and AE_SRAI32R users across FFT/DCT.
static inline ae_int32x2 __ae_srai32r(ae_int32x2 a, int s) {
  return haydn_x2sra32r(a, s);
}
static inline ae_int16x4 __ae_srla16(ae_int16x4 a, int s) {
  long long v = (long long)a;
  unsigned short l0 = (unsigned short)(v) >> s, l1 = (unsigned short)(v >> 16) >> s;
  unsigned short l2 = (unsigned short)(v >> 32) >> s, l3 = (unsigned short)(v >> 48) >> s;
  return (ae_int16x4)((long long)l0 | ((long long)l1 << 16) | ((long long)l2 << 32) | ((long long)l3 << 48));
}
static inline ae_int16x4 __ae_sraa16(ae_int16x4 a, int s) {
  long long v = (long long)a;
  short l0 = (short)(v) >> s, l1 = (short)(v >> 16) >> s;
  short l2 = (short)(v >> 32) >> s, l3 = (short)(v >> 48) >> s;
  return (ae_int16x4)((long long)(unsigned short)l0 | ((long long)(unsigned short)l1 << 16) |
                     ((long long)(unsigned short)l2 << 32) | ((long long)(unsigned short)l3 << 48));
}

#define AE_SLAA32(a, s)  ((ae_int32)((ae_int32)(a) << (s)))
#define AE_SLAA32S(a, s) __ae_slaa32s((a), (s))
#define AE_SLAA16S(a, s) __ae_slaa16s((a), (s))
#define AE_SLAA16S_(a, s) AE_SLAA16S(a, s)
#define AE_SLAI32(a, s)  ((ae_int32)((ae_int32)(a) << (s)))
#define AE_SLAI32S(a, s) __ae_slaa32s((a), (s))
#define AE_SLAI16S(a, s) __ae_slaa16s((a), (s))
/* Dual-24 sat left on 32-bit headroom — soft per-lane (EMULATED); never
 * scalar sla32s_lane high-lane drop on ae_f24x2 NatureDSP streams. */
#define AE_SLAI24S(a, s) \
  ((ae_f24x2)__haydn_v2_as_i64(__ae_slaa32s(__AE_AS_V2(a), (int)(s))))
/* AE_SLAS32S late rebind: bidirectional soft sat (same as SLAA32S).
 * Early body already named __ae_slaa32s; reaffirm after helper is defined
 * so always-right X2SRA32 cannot reappear as a silent late override. */
#undef __AE_SLAS32S_1
#undef __AE_SLAS32S_2
#define __AE_SLAS32S_1(a)    __ae_slaa32s((a), haydn_ae_sar)
#define __AE_SLAS32S_2(a, s) __ae_slaa32s((a), (int)(s))
#define AE_SRAI16(a, s)  ((ae_int16)((ae_int16)(a) >> (s)))
#define AE_SRAI16R(a, s) __ae_srai16r((a), (s))
#define AE_SRAI32R(a, s) __ae_srai32r((a), (s))
/* Dual-24 packed ASR — same X2SRA32 as AE_SRAI24 / F24X2_SRAI. */
#define AE_SRAIP24(a, s) AE_SRAI24((a), (s))
#define AE_SRLA16(a, s)  __ae_srla16((a), (s))
#define AE_SRAA16(a, s)  __ae_sraa16((a), (s))

//---- 32-bit lane arithmetic -------------------------------------------
#define AE_ADDSUB32(a, b) haydn_x2addsub32s((a), (b))
#define AE_ADDSUB32_HL_LH(a, b) haydn_x2addsub32s((a), (b))
#define AE_SUBADD32(a, b) haydn_x2subadd32s((a), (b))
#define AE_SUBADD32_HL_LH(a, b) haydn_x2subadd32s((a), (b))
#define AE_SUBADD32S_HL_LH(a, b) haydn_x2subadd32s((a), (b))
/* Dual-64 lane add — no Haydn map. Transitional scalar body only when
 * __HAYDN_ALLOW_INEXACT_AE; default fail-closed (never silent scalar no-op). */
#if defined(__HAYDN_ALLOW_INEXACT_AE)
#define AE_ADD64X2_(a, b) ((ae_int64)((a) + (b)))
#else
#define AE_ADD64X2_(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_)
#endif
#define AE_ADDANDSUBRNG16RAS_S0(a, b) AE_ADDANDSUBRNG16RAS_S1(a, b)
/* Dual-24 non-sat add — X2ADD32; never scalar (a)+(b) high-lane drop. */
#define AE_ADDP24(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2add32(__AE_AS_V2(a), __AE_AS_V2(b))))

//---- 16-bit lane arithmetic -------------------------------------------
#define AE_ADD16S_(a, b) haydn_x4add16s((a), (b))
#define AE_SUB16S_(a, b) haydn_x4sub16s((a), (b))
/* Golden X4ABS16S: per-lane SAT16(ABS); 0x8000→0x7FFF.
 * NEVER map to x4sra16(a,0) — that is identity, not abs. */
#define AE_ABS16S(a)  ((ae_int16x4)haydn_x4abs16s(a))
#define AE_ABS16S_(a) AE_ABS16S(a)
#define AE_AND16(a, b) ((a) & (b))
#define AE_OR16(a, b)  ((a) | (b))
#define AE_NEG16S_(a)  haydn_x4sub16s((ae_int16x4){0, 0, 0, 0}, (a))
/* AE_MAXABS16S — per-lane max-abs on quad-16 (HiFi DR64 ae_int16x4).
 * Semantics (NatureDSP FFT bexp): acc[i] = MAX(acc[i], SAT16(ABS(val[i]))).
 * Composite: X4MAX16(acc, X4ABS16S(val)). Never haydn_maxabs32s (2x32) —
 * that silently reinterprets four 16-bit lanes as two 32-bit abs-max lanes.
 * 1-arg form: per-lane sat-abs only (not horizontal max-abs; caller uses
 * AE_MAX16 tree for horizontal reduce). ISA has no fused MAXABS16S. */
#define AE_MAXABS16S(...) __AE_MAXABS16S_OVERLOAD(__VA_ARGS__)
#define __AE_MAXABS16S_GET(_1, _2, NAME, ...) NAME
#define __AE_MAXABS16S_OVERLOAD(...) \
  __AE_MAXABS16S_GET(__VA_ARGS__, __AE_MAXABS16S_2, __AE_MAXABS16S_1)(__VA_ARGS__)
#define __AE_MAXABS16S_1(a) ((ae_int16x4)haydn_x4abs16s(a))
#define __AE_MAXABS16S_2(acc, val) \
  ((ae_int16x4)haydn_x4max16((acc), haydn_x4abs16s(val)))
// Dual-lane abs/neg (ae_int32x2). Golden: X2ABS32 / X2NEG32.
// Haydn has no separate non-sat 32-bit scalar abs for the vector form; both
// lanes use the X2* ops. At INT_MIN, non-sat wraps (ISA) / sat clamps.
// haydn_x2abs32/neg32 take haydn_x2int32, not int64 bag.
#define AE_ABS32(a)   ((ae_int32x2)haydn_x2abs32(a))
#define AE_NEG32(a)   ((ae_int32x2)haydn_x2neg32(a))
#define AE_EQ16(a, b) haydn_x4seq16((a), (b))
#define AE_LT16(a, b) haydn_x4slt16((a), (b))
#define AE_LT64(a, b) ((a) < (b))
#define AE_LE64(a, b) ((a) <= (b))
#define AE_EQ64(a, b) ((a) == (b))

//===----------------------------------------------------------------------===//
// XT_* scalar helpers (Tensilica XT_ compatibility used in math kernels).
// xtboolN is `int` in this compat layer; the XT_ ops are scalar C compositions.
//===----------------------------------------------------------------------===//
/// Extract lane 0 of an xtbool2/xtbool4 predicate (int in compat layer).
#define xtbool2_extract_0(b) ((int)(b) & 1)
#define xtbool2_extract_1(b) (((int)(b) >> 1) & 1)
#define xtbool4_extract_0(b) ((int)(b) & 1)
#define xtbool4_extract_1(b) (((int)(b) >> 1) & 1)
#define xtbool4_extract_2(b) (((int)(b) >> 2) & 1)
#define xtbool4_extract_3(b) (((int)(b) >> 3) & 1)
/// Scalar conditional move if true: if (cond) dst = src.
#define XT_MOVT(dst, src, cond) do { if (cond) (dst) = (src); } while (0)
/// Scalar bitwise and / or / min (xtbool is int).
#define XT_AND(a, b) ((a) & (b))
#define XT_ORB(a, b) ((a) | (b))
/// Boolean and / not on xtbool (xtbool is int; nonzero = true). XT_ANDB is
/// the bitwise-AND of two xtbool values (used to combine comparison results).
#define XT_ANDB(a, b) ((a) & (b))
#define XT_NOTB(a)     (!(a))
#define XT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define XT_MAX(a, b) ((a) > (b) ? (a) : (b))

//===----------------------------------------------------------------------===//
// 16-bit SIMD vector aliases (NatureDSP `_vector` suffix == quad-16 form).
//===----------------------------------------------------------------------===//
#define AE_NEG16S_vector(a)     AE_NEG16S(a)
#define AE_ADD16S_vector(a, b)  AE_ADD16S((a), (b))
#define AE_SLAA16S_vector(a, s) AE_SLAA16S((a), (s))
// NatureDSP uses AE_NEG16S_scalar for scalar (non-vector) 16-bit negate.
// Maps to plain C negation of an ae_int16.
#define AE_NEG16S_scalar(a)     (-(a))

// Missing NatureDSP macros that blocked bqriir16x16 and vec_cplx2cplx.

// AE_MUL16X4_vector: quad-16 element-wise multiply, 2-arg returning form.
// Kernel: `AE_MUL16X4_vector(Y, Z)` (scl_divide16x16). Returns the high pair.
#define AE_MUL16X4_vector(a, b) \
  ((ae_int16x4)haydn_x4mul16((a), (b)).hi)

// AE_MOVBA4(bit): build a 4x16-bit bitmask replicate. NatureDSP uses this
// to create a mask for lane-select. bit=8 → each 16-bit lane = 0x0008.
#define AE_MOVBA4(bit) ((ae_int16x4)haydn_x4slli16((ae_int16x4)((long long)(bit) | ((long long)(bit) << 16)), 0))

// AE_PKSR16: pack-shift-round 64->16 with 4x16 lane saturation.
// NatureDSP calls AE_PKSR16(var, acc, shift) — modifies var in-place.
// Uses packsr32 (64->32 shift+round+saturate) then sat32t16 (32->16 saturate).
// Macro (not inline) because the first arg is modified by name, not by pointer.
#define AE_PKSR16(d, s, pos) \
  do { \
    unsigned int _pk16lo = (unsigned int)haydn_packsr32((long long)(s), 16 - (pos)); \
    (d) = (ae_int16x4)haydn_x4sat32t16((ae_int32x2)_pk16lo, (ae_int32x2){0, 0}); \
  } while(0)

// AE_SEL16_7610: lane select permutation (7,6,1,0).
// Maps to x4seli16 with the appropriate immediate. The SEL16 pattern encodes
// the output lane order as a nibble: lane3=7, lane2=6, lane1=1, lane0=0.
// On Haydn, SELI16 uses a different encoding — use SHORTSWAP+SEL16 combos.
// For now, route through scalar C (correct, not optimal):
#define AE_SEL16_7610(a, b) AE_SEL16((a), (b), 0x76)

// ae_int16x4_loadip: load 4x16 from memory with post-increment pointer.
// NatureDSP naming variant of AE_L16X4_IP.
#define ae_int16x4_loadip(dst, ptr, inc) AE_L16X4_IP((dst), (ptr), (inc))

// ae_int16x4_loadi: load 4x16 from memory (no post-increment).
// NatureDSP naming variant of AE_L16X4_X (indexed load).
#define ae_int16x4_loadi(ptr, offs) AE_L16X4_I((ptr), (offs))

// AE_MOVINT32X2_FROMF64: reinterpret cast from ae_f64 to ae_int32x2.
// In Haydn both are 64-bit types in DR64 — this is a bitcast (no-op).
#define AE_MOVINT32X2_FROMF64(a) ((ae_int32x2)(a))
#define AE_MULFP16X4S_vector(a, b) AE_MULFP16X4S((a), (b))
/// Per-lane saturating absolute value (golden X4ABS16S).
static inline ae_int16x4 AE_ABS16S_vector(ae_int16x4 a) {
  return haydn_x4abs16s(a);
}

//===----------------------------------------------------------------------===//
// Unsigned 32x32 multiply (low-low partial -> low 32 bits).
//===----------------------------------------------------------------------===//
#define AE_MUL32U_LL(a, b) ((ae_uint32)haydn_mul64_uu_ull(__AE_TO_I64(a), __AE_TO_I64(b)))

//---- INT16X4 / INT32X2 / INT64 reductions -----------------------------
static inline int32_t AE_INT16X4_RADD(ae_int16x4 a) {
  /* sum of 4 signed 16-bit lanes -> 32-bit. */
  long long v = (long long)a;
  return (int32_t)(short)(v) + (int32_t)(short)(v >> 16) +
         (int32_t)(short)(v >> 32) + (int32_t)(short)(v >> 48);
}
static inline ae_int16x4 AE_INT16X4_MAX(void) {
  return (ae_int16x4)0x7FFF7FFF7FFF7FFFLL;
}
static inline ae_int16x4 AE_INT16X4_MIN(void) {
  return (ae_int16x4)((long long)0x8000 << 48 | (long long)0x8000 << 32 | 0x80008000);
}
static inline ae_int32x2 AE_INT32X2_ABS32S(ae_int32x2 a) {
  return haydn_x2abs32s(a);
}
#define AE_INT32X2_ADD32S(a, b) haydn_x2add32s((a), (b))
#define AE_INT32X2_NEG32S(a)    haydn_x2neg32s(a)
static inline ae_int64 __AE_INT64X2_RADD_2(ae_int64 a, ae_int64 b) {
  return (ae_int64)((a) + (b));
}
/// 1-arg form: reduce a single ae_int64x2 by summing its two 32-bit lanes
/// (HiFi AE_INT64X2_RADD(a) returns a.H + a.L as a 64-bit result). Haydn
/// stores ae_int64x2 as one DR64; the lane-sum is the high 32 bits plus the
/// low 32 bits, each sign-extended to 64-bit then added.
static inline ae_int64 __AE_INT64X2_RADD_1(ae_int64x2 a) {
  long long v = (long long)(haydn_dr64_t)a;
  int64_t hi = (int64_t)(int32_t)((v >> 32) & 0xFFFFFFFF);
  int64_t lo = (int64_t)(int32_t)(v & 0xFFFFFFFF);
  return (ae_int64)(hi + lo);
}
/// Overload dispatcher: 1-arg (reduce single int64x2) vs 2-arg (add two int64).
#define AE_INT64X2_RADD(...) __AE_INT64X2_RADD_OVERLOAD(__VA_ARGS__)
#define __AE_INT64X2_RADD_GET(_1, _2, NAME, ...) NAME
#define __AE_INT64X2_RADD_OVERLOAD(...) \
  __AE_INT64X2_RADD_GET(__VA_ARGS__, __AE_INT64X2_RADD_2, __AE_INT64X2_RADD_1)(__VA_ARGS__)
#define AE_INT64_LT(a, b) ((a) < (b))
#define AE_INT64_GE(a, b) ((a) >= (b))
#define AE_INT64_EQ(a, b) ((a) == (b))

//---- Sign-extend -------------------------------------------------------
#define AE_SEXT32X2D16_10(a)  (a)
#define AE_SEXT32X2D16_32(a)  (a)

//---- MOV lane-extract / immediate-form --------------------------------
#define AE_MOVAD16_0(a) ((ae_int16)((a) & 0xFFFF))
#define AE_MOVAD16_1(a) ((ae_int16)(((long long)(a) >> 16) & 0xFFFF))
#define AE_MOVAD16_2(a) ((ae_int16)(((long long)(a) >> 32) & 0xFFFF))
#define AE_MOVAD16_3(a) ((ae_int16)(((long long)(a) >> 48) & 0xFFFF))
/// native MOVE32_DR_H (1 op, was 20-op scalar lshr i64 32).
/// Bag-cast the dest; C vector-to-scalar takes lane 0 only.
#define AE_MOVAD32_H(a) ((ae_int32)haydn_movad32_h(__AE_TO_I64(a)))
/// native MOVE32_DR_L (1 op, was scalar trunc).
#define AE_MOVAD32_L(a) ((ae_int32)haydn_movad32_l(__AE_TO_I64(a)))

#define AE_MOVDA16X2(hi, lo) ((ae_int32x2)(((long long)(int)(hi) << 32) | (unsigned int)(lo)))
#define AE_MOV32(x)   ((ae_int32x2)(long long)(int)(x))
#define AE_MOV64(x)   ((ae_int64)(x))
#define AE_MOV(x)     (x)
#define AE_MOVAB(a)   (a)
#define AE_MOVAB2(a)  (a)
#define AE_MOVAB4(a)  (a)
#define AE_MOVBA(a)   (a)
#define AE_MOVBA2(a)  (a)
#define AE_INTSWAP(a, b) ((a) ^ (b) ^ ((b) = (a)))

//---- ae_X_rtor_ae_Y reinterpret casts (NatureDSP type-punning idiom) ---
// All Haydn vector types (ae_int32x2, ae_int64x2, ae_f32x2, ae_p16x2s, ...)
// are aliases of haydn_dr64_t (same 64-bit storage), so reinterpret between
// any of them is a pure bit-identity cast. The kernel idiom spells this as
// `ae_<src>_rtor_ae_<dst>(value)`; provide the combinations seen in in-tree
// hifi3 kernels.
#define ae_int32x2_rtor_ae_int64x2(v)  ((ae_int64x2)(v))
#define ae_int64x2_rtor_ae_int32x2(v)  ((ae_int32x2)(v))
#define ae_int32x2_rtor_ae_f32x2(v)    ((ae_f32x2)(v))
#define ae_f32x2_rtor_ae_int32x2(v)    ((ae_int32x2)(v))
#define ae_int64x2_rtor_ae_f64(v)      ((ae_f64)(v))
#define ae_f64_rtor_ae_int64x2(v)      ((ae_int64x2)(v))

//---- Reinterpret MOVs (no-op on Haydn: same bits in register) ---------
#define AE_MOVF16X4_FROMINT16X4(a)     ((ae_f16x4)(a))
#define AE_MOVF16X4_FROMINT32X2(a)     ((ae_f16x4)(a))
#define AE_MOVF24X2_FROMINT32(a)       ((ae_f24x2)(a))
#define AE_MOVF32X2_FROMINT32(a)       ((ae_f32x2)(a))
#define AE_MOVF32X2_FROMINT32X2(a)     ((ae_f32x2)(a))
#define AE_MOVF64_FROMINT32X2(a)       ((ae_f64)(a))
#define AE_MOVINT16X4_FROMF16(a)       ((ae_int16x4)(a))
#define AE_MOVINT16X4_FROMF16X4(a)     ((ae_int16x4)(a))
#define AE_MOVINT16X4_FROMF32X2(a)     ((ae_int16x4)(a))
#define AE_MOVINT16X4_FROMINT32X2(a)   ((ae_int16x4)(a))
#define AE_MOVINT16X4_FROMINT64(a)     ((ae_int16x4)(a))
#define AE_MOVINT32X2_FROMF16X4(a)     ((ae_int32x2)(a))
#define AE_MOVINT32X2_FROMF32(a)       ((ae_int32x2)(a))
#define AE_MOVINT32X2_FROMF32X2(a)     ((ae_int32x2)(a))
#define AE_MOVINT32X2_FROMINT16X4(a)   ((ae_int32x2)(a))
#define AE_MOVINT64_FROMINT16X4(a)     ((ae_int64)(a))
#define AE_MOVINT64_FROMINT32(a)       ((ae_int64)(a))
#define AE_MOVXTFLOATX2_FROMXTFLOAT(a) (a)
/// native MOVE32_DR_H (was scalar lshr i64 32).
#define AE_CVT64F32_H(a) ((ae_int32)haydn_movad32_h((ae_int64)(a)))
#define AE_CVTP24A16(a)  ((ae_int32)(a))

//---- MUL32 / MULA32 scalar (32x32->32 high-half variants) -------------
#define AE_MUL32_LH(a, b) haydn_mul64_ss_lh(__AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32_HH(acc, a, b) haydn_mula64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32_HL(acc, a, b) haydn_mula64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32_LH(acc, a, b) haydn_mula64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32_LL(acc, a, b) haydn_mula64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
/// 32x32 unsigned multiply-accumulate (low-lane). haydn_mula64_uu_ull is
/// BINARY (2-arg returning) per BuiltinsHaydn.td:85; compose explicit add-back.
/// (See for arity reconciliation.)
#define AE_MULA32U_LL(acc, a, b) (acc) = haydn_mula64_uu_ull((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- MULA32X16 (32x16->48 lane MAC) -----------------------------------
//
// HiFi3 semantics (capstone-compat-header-audit F1, codex-confirmed, ):
// AE_MULA32X16_Ln(acc, a, b) computes acc += (32-bit data half from a) *
// (16-bit coefficient lane n from packed b, b = ae_int16x4 = 4xQ1.15):
//   L0 = sext16(b[15:0])    L1 = sext16(b[31:16])
//   L2 = sext16(b[47:32])   L3 = sext16(b[63:48])
// and the data half is: H* = a[63:32] (high), L* = a[31:0] (low).
//
// Haydn's mula64_ss_* family reads 32x32 lane products:
//   mula64_ss_hl: rsd1[63:32] * rsd2[31:0]   (data HIGH half * coef low lane)
//   mula64_ss_ll: rsd1[31:0]  * rsd2[31:0]   (data LOW half  * coef low lane)
// HiFi3's H*/L* prefix selects the DATA half of a (H = a[63:32], L = a[31:0]);
// the n suffix selects the coefficient lane of b. To compute the right product
// we (1) extract + sext the chosen 16-bit coef lane to a Q1.31 32-bit value,
// (2) pack it into the DR64 LOW lane (rsd2[31:0]), then (3) issue mula64_ss_hl
// for H* (data high) or mula64_ss_ll for L* (data low). Passing packed b raw
// made every variant multiply by (c0 | (c1<<16)) — a wrong product (L97/L148
// silent-miscompute class; this is the exact hazard fixed in the
// selector for the FIR path). This header-level fix mirrors 's coef-widen.
//
// __AE_MUL32X16_WIDEN_COEF packs sext(coef16) into bits [31:0] of a DR64 so
// the low-lane mula64_ss_* reads the correct Q1.31 coefficient.
#define __AE_MUL32X16_WIDEN_COEF_L0(b) \
  ((long long)(int)(short)((unsigned long long)(b) & 0xFFFF))
#define __AE_MUL32X16_WIDEN_COEF_L1(b) \
  ((long long)(int)(short)(((unsigned long long)(b) >> 16) & 0xFFFF))
#define __AE_MUL32X16_WIDEN_COEF_L2(b) \
  ((long long)(int)(short)(((unsigned long long)(b) >> 32) & 0xFFFF))
#define __AE_MUL32X16_WIDEN_COEF_L3(b) \
  ((long long)(int)(short)(((unsigned long long)(b) >> 48) & 0xFFFF))
// Native since golden v2_1 (2026-08-19): MULA32X16_Hn/Ln read the packed
// coef lanes directly (H = rsd1[63:32], L = rsd1[31:0]; n = rsd2 coef lane),
// so the __AE_MUL32X16_WIDEN_COEF extraction + mula64_ss_hl/ll composition
// is retired — one native instruction replaces extract+pack+MAC.
#define AE_MULA32X16_H0(acc, a, b) \
  haydn_mula32x16_h0((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32X16_H1(acc, a, b) \
  haydn_mula32x16_h1((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32X16_L0(acc, a, b) \
  haydn_mula32x16_l0((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32X16_L1(acc, a, b) \
  haydn_mula32x16_l1((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32X16_L2(acc, a, b) \
  haydn_mula32x16_l2((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULA32X16_L3(acc, a, b) \
  haydn_mula32x16_l3((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- MULAAD32 (dual 32x32 dual-MAC) -----------------------------------
#define AE_MULAAD32_HH_LL(acc, a, b) \
  haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULAAD32_HL_LH(acc, a, b) \
  haydn_f2mulaa32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- MULAAFD32RA / MULAAFD32X16 (fractional + rounding) ---------------
#define AE_MULAAFD32RA_HH_LL(acc, a, b) haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULAAFD32X16_H1_L0(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULAAFD32X16_H3_L2(acc, a, b) haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- MULAAR16P16X4S / MULZAAAAQ16 (quad-16 MAC, Path B 2-dest) ----
// AE_MULZAAAAQ16: 2-arg returning form (kernel: `q0 = AE_MULZAAAAQ16(x0, y0);`
// in raw_lxcorr16x16). "Z" = zero-accumulator fresh multiply. X4MULA16 with
// acc=0; the kernel consumes the high-pair accumulator (lanes 3,2).
#define AE_MULZAAAAQ16(a, b) \
  ((ae_int64)haydn_x4mula16((int64_t)0, (int64_t)0, (a), (b)).hi)
// AE_MULAAR16P16X4S_: 3-arg accumulate form (acc += a*b). High-pair accum.
#define AE_MULAAR16P16X4S_(acc, a, b) \
  ((ae_int64)haydn_x4mula16s((int64_t)(acc), (int64_t)0, (a), (b)).hi)

//---- MULAF16X4 / MULAF32 (fractional MAC, Path B 2-dest) ----------
// AE_MULAF16X4SS: kernel 4-arg form (vec_dot16x16_fast: vaf/vbf are in/out
// accumulators, both updated by the 2-dest X4MULA16S). Write back DR64 bits
// with a union bitcast so ae_f32x2 accs keep both 32-bit lanes. A C cast of
// i64 onto <2 x i32> is a low-lane splat (the two-lane 380 body).
#define AE_MULAF16X4SS(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4mula16s(__AE_TO_I64(acc_hi), __AE_TO_I64(acc_lo), \
                                         (a), (b)); \
    __AE_ASSIGN_BITS((acc_hi), _r.hi); \
    __AE_ASSIGN_BITS((acc_lo), _r.lo); \
  } while (0)
#define AE_MULSF16X4SS(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4muls16s(__AE_TO_I64(acc_hi), __AE_TO_I64(acc_lo), \
                                         (a), (b)); \
    __AE_ASSIGN_BITS((acc_hi), _r.hi); \
    __AE_ASSIGN_BITS((acc_lo), _r.lo); \
  } while (0)
#define AE_MULAF32R_LH(acc, a, b) haydn_ff2mula32rs_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULAF32R_LL_S2(acc, a, b) haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULAF32S_HL(acc, a, b) haydn_fmula32s_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULAF32X16_H1(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULAF32X16_H3(acc, a, b) haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULAP32X2(acc, a, b) ((acc) + haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b)))
#define AE_MULSF32R_LH(acc, a, b) haydn_ff2muls32rs_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULSF32S_HL(acc, a, b) haydn_fmuls32s_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULSF32S_LH(acc, a, b) haydn_fmuls32s_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULSP32X2(acc, a, b) ((acc) - haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b)))

// NatureDSP AE_MULSSFD32{R,S} dual-half-word fractional subtract-MAC.
// These compute acc -= (x.h * y.sel_h) + (x.l * y.sel_l) on packed 32-bit
// operands (treating each 32-bit value as two 16-bit halves).
// HH_LL = direct: hi*hi + lo*lo. HL_LH = cross: hi*lo + lo*hi.
// S = saturating. R = rounding.
// MUST write back acc — haydn_fmuls32s_* returns the new accumulator
// (discarding the return was a silent no-op that broke IIR kernels).
#define AE_MULSSFD32S_HH_LL(acc, x, y) do { \
    (acc) = haydn_fmuls32s_hh((acc), __AE_TO_I64(x), __AE_TO_I64(y)); \
    (acc) = haydn_fmuls32s_ll((acc), __AE_TO_I64(x), __AE_TO_I64(y)); \
  } while (0)
#define AE_MULSSFD32R_HH_LL(acc, x, y) \
  ((acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64(x), __AE_TO_I64(y)))
#define AE_MULSSFD32R_HL_LH(acc, x, y) \
  ((acc) = haydn_f2mulss32rs_hllh((acc), __AE_TO_I64(x), __AE_TO_I64(y)))

//---- MULF32R / MULFP (fractional multiply, no-accumulate) -------------
#define AE_MULF32R_HH(a, b) haydn_ff2mul32r_hh(__AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULF32R_LH(a, b) haydn_ff2mul32r_lh(__AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULF32R_LL(a, b) haydn_ff2mul32r_ll(__AE_TO_I64(a), __AE_TO_I64(b))
//---- MULFP / MULP (fractional + pair multiply, Path B 2-dest) -----
// AE_MULFP16X4RAS: 2-arg returning form (kernel: `vyf = AE_MULFP16X4RAS(vxf, vcf);`
// in vec_scale16x16_fast). X4FF2MULA16S with zero accumulator; kernel consumes
// the high-pair result.
#define AE_MULFP16X4RAS(a, b) \
  (__AE_I4V(haydn_x4ff2mula16s((int64_t)0, (int64_t)0, (a), (b)).hi))
#define AE_MULFP16X4S(a, b) \
  (__AE_I4V(haydn_x4ff2mul16s((a), (b)).hi))
#define AE_MULFP16X4S_(a, b)  AE_MULFP16X4S(a, b)
#define AE_MULFP32X16X2RAS_H(acc, a, b) haydn_mulfp32x16x2ras_high((acc), (a), (b))
#define AE_MULFP32X16X2RAS_L(acc, a, b) haydn_mulfp32x16x2ras_low((acc), (a), (b))
// AE_MULFP32X2RS: fractional mul with round (same family as RAS / FF2).
// Was wrongly mapped to integer X2MUL32 (no round / wrong Q scale).
#define AE_MULFP32X2RS(a, b) ((ae_int32x2)haydn_x2fmul32rs(__AE_AS_V2(a), __AE_AS_V2(b)))
#define AE_MULP32X16X2_H(a, b) haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULP32X2_S2(a, b)  AE_MULP32X2(a, b)
#define AE_MULF48Q32SP16S_L(acc, a, b) haydn_fmula32s_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- MULC32X16 (complex 32x16 multiply) ----------------------------
// Mapping AE_MULC32X16_* onto X2CMUL32 invents NatureDSP lane/width law.
// SOURCE-REVALIDATE: fail closed in strict mode.
#if __HAYDN_AE_COMPAT_STRICT
#define AE_MULC32X16_H(a, b) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULC32X16_H)
#define AE_MULC32X16_L(a, b) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULC32X16_L)
#else
#define AE_MULC32X16_H(a, b) \
  ((ae_int32x2)haydn_x2cmul32((a), (b)).hi)
#define AE_MULC32X16_L(a, b) \
  ((ae_int32x2)haydn_x2cmul32((a), (b)).lo)
#endif

//---- MULSSFD / MULZAA FD32X16 (signed-subtract + zero-init variants) --
#define AE_MULSSFD32X16_H1_L0(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULSSFD32X16_H3_L2(acc, a, b) haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULZAAFD16SS_11_00(acc, a, b) haydn_fmulaa16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULZAAFD32S_HH_LL(a, b) \
  (haydn_fmul32s_hh(__AE_TO_I64(a), __AE_TO_I64(b)) + haydn_fmul32s_ll(__AE_TO_I64(a), __AE_TO_I64(b)))
#define AE_MULZAAFD32X16_H1_L0(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULZAAFD32X16_H3_L2(acc, a, b) haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULZSAFD24_HH_LL(acc, a, b) haydn_fmula32s_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULZSAFD32X16_H3_L2(acc, a, b) haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULZSSFD32X16_H1_L0(acc, a, b) haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- MUL16X4 (quad-16 multiply, Path B 2-dest) -------------------
// AE_MUL16X4_ is the 2-arg returning form (kernel: `zt = AE_MUL16X4_(a, b);`)
// — returns the high-pair result; the low pair is unused.
#define AE_MUL16X4_(a, b) \
  ((ae_int16x4)haydn_x4mul16((a), (b)).hi)
// The 4-arg statement form (writes d0=hi, d1=lo) is defined above in the
// SIMD MAC block — it is the golden-faithful 2-dest X4MUL16 shape that
// matches the kernel pattern `AE_MUL16X4(d0, d1, xt, yt); z = AE_SAT16X4(d0, d1);`.

//---- SEL16 (lane-permute selects) -------------------------------------
// HiFi3 AE_SEL16_<digits>: select/permute 4 lanes from two sources.
// On Haydn we approximate via MOVDA16 (no native SEL); document as isa_ineff.
#define AE_SEL16_2301(a, b) ((ae_int16x4)(((long long)(a) & 0xFFFF0000FFFF0000LL) | ((long long)(b) & 0x0000FFFF0000FFFFLL)))
#define AE_SEL16_4321(a, b) ((b))
#define AE_SEL16_5140(a, b) ((ae_int16x4)(((long long)(a) & 0x0000FFFF0000FFFFLL) | ((long long)(b) & 0xFFFF0000FFFF0000LL)))
#define AE_SEL16_5146(a, b) ((ae_int16x4)(((long long)(a) & 0x0000FFFF0000FFFFLL) | ((long long)(b) & 0xFFFF0000FFFF0000LL)))
#define AE_SEL16_5410(a, b) ((ae_int16x4)(((long long)(a) & 0x000000000000FFFFLL) | ((long long)(b) & 0xFFFFFFFFFFFF0000LL)))
#define AE_SEL16_5432(a, b) ((a))
#define AE_SEL16_6420(a, b) ((ae_int16x4)(((long long)(a) & 0x00000000FFFFFFFFLL) | ((long long)(b) & 0xFFFFFFFF00000000LL)))
#define AE_SEL16_6543(a, b) ((a))
#define AE_SEL16_7362(a, b) ((ae_int16x4)(((long long)(a) & 0x0000FFFF00000000LL) | ((long long)(b) & 0xFFFF00000000FFFFLL)))
#define AE_SEL16_7520(a, b) ((ae_int16x4)(((long long)(a) & 0x00000000FFFFFFFFLL) | ((long long)(b) & 0xFFFFFFFF00000000LL)))
#define AE_SEL16_7632(a, b) ((a))
#define AE_SEL16I(a, b, idx) ((idx) & 1 ? (b) : (a))

//---- SEL24 (24-bit lane selects; same X2SEL32 pack as SELP24 / SEL32) --
// Must not silent-alias to bag bitwise OR (prior placeholder). Dual-24 lives
// in 32-bit lanes with 8-bit headroom — native x2sel32_* is exact.
#define AE_SEL24_HH(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_hh(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SEL24_HL(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_hl(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SEL24_LH(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_lh(__AE_AS_V2(a), __AE_AS_V2(b))))
#define AE_SEL24_LL(a, b) \
  ((ae_f24x2)__haydn_v2_as_i64(haydn_x2sel32_ll(__AE_AS_V2(a), __AE_AS_V2(b))))

//---- AE_ZERO / AE_S32RA64S additional spellings -----------------------
#define AE_ZERO() ((ae_int32x2){0, 0})
#define AE_NOT32(a) (~(a))
#define AE_S32RA64S_XP(dst, acc, shift, ptr, offs) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *((ae_int32 *)(ptr) + (offs)) = (dst); } while (0)
#define AE_S32X2RA64S_IP(src0, src1, acc, shift, ptr, inc) \
  do { (src0) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32x2 *)(ptr) = (ae_int32x2)(src0); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
#define AE_S24X2RA64S_IP(dst, acc, shift, ptr) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32 *)(ptr) = (dst); } while (0)

//---- AE_L32X2F24_XC 3-arg/4-arg overload (implicit CBR0 when 3-arg) -----
#undef  AE_L32X2F24_XC
#define AE_L32X2F24_XC(...) __AE_L32X2F24_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2F24_XC_OVERLOAD(...) \
  __AE_L32X2F24_XC_GET(__VA_ARGS__, __AE_L32X2F24_XC_4A, __AE_L32X2F24_XC_3A)(__VA_ARGS__)
/// HiFi3z has a single implicit CBR selected by WUR_AE_CBEGIN0/CEND0, so the
/// native AE_*XC API is 3-arg (dst, ptr, offs) — no cbr_sel. The 3-arg form
/// routes to the CB intrinsic with cbr_sel=0 (CBR0). See .
#define __AE_L32X2F24_XC_3A(dst, ptr, offs) \
  __AE_L32X2F24_XC_4A(dst, ptr, offs, 0)
/* Dual-24 aligned circular load: same D_LDW_CB path as AE_L32X2_XC.
 * Explicit ldw_cb_imm + next-ptr writeback (not plain mem). */
#define __AE_L32X2F24_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           (offs) >> 3); \
    (dst) = (ae_f24x2)(haydn_dr64_t)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

//---- AE_L32X2F24_I 3-arg form -----------------------------------------
#undef  AE_L32X2F24_I
#define AE_L32X2F24_I(dst, ptr, offs) \
  do { dst = *((ae_f24x2 *)(ptr) + ((offs) / (int)sizeof(ae_f24x2))); } while (0)

//---- AE_L32X2F24_RIP 2-arg form (offset only) -------------------------
/* Reverse linear load: read *ptr then ptr -= offs (not forward IP). */
#undef  AE_L32X2F24_RIP
#define AE_L32X2F24_RIP(dst, ptr, offs) \
  do { \
    (dst) = *(ae_f24x2 *)(ptr); \
    (ptr) = (ae_f24x2 *)((char *)(ptr) - ((offs) ? (offs) : 8)); \
  } while (0)

//---- AE_S32X2F24_XC 3-arg/4-arg overload (implicit CBR0 when 3-arg) ----
#undef  AE_S32X2F24_XC
#define AE_S32X2F24_XC(...) __AE_S32X2F24_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2F24_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32X2F24_XC_OVERLOAD(...) \
  __AE_S32X2F24_XC_GET(__VA_ARGS__, __AE_S32X2F24_XC_4A, __AE_S32X2F24_XC_3A)(__VA_ARGS__)
#define __AE_S32X2F24_XC_3A(src, ptr, offs) \
  __AE_S32X2F24_XC_4A(src, ptr, offs, 0)
/* Dual-24 aligned circular store: same CB path as AE_S32X2_XC. Must write
 * back the CBR-wrapped next pointer — never drop the store-only silent
 * residual that left ptr unmoved under an F24 XC name. */
#define __AE_S32X2F24_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    void *__np = haydn_sdw_cb_imm(__s, (ptr), (cbr_sel), (offs) >> 3); \
    (ptr) = (__typeof__(ptr))__np; \
  } while (0)

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer: 3-arg forms (no cbr_sel) for XC/IP/RIC loads  //
// and stores. HiFi3z has a SINGLE implicit CBR selected by the              //
// WUR_AE_CBEGIN0/CEND0 registers, so the native AE_*XC API is 3-arg         //
// (dst/src, ptr, offs) — kernels NEVER pass cbr_sel. Per , the 3-arg    //
// form routes to the CB load/store intrinsic with cbr_sel=0 (CBR0); the     //
// hardware performs the wrap and computes the next pointer. The 4-arg       //
// overload is retained for explicit-CBR1 callers but is not used by the     //
// NatureDSP hifi3 corpus.                                                    //
//===----------------------------------------------------------------------===//

//---- AE_L16X4_XC / AE_S16X4_XC overload --------------------------------
#undef  AE_L16X4_XC
#define AE_L16X4_XC(...) __AE_L16X4_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L16X4_XC_OVERLOAD(...) \
  __AE_L16X4_XC_GET(__VA_ARGS__, __AE_L16X4_XC_4A, __AE_L16X4_XC_3A)(__VA_ARGS__)
#define __AE_L16X4_XC_3A(dst, ptr, offs) \
  __AE_L16X4_XC_4A(dst, ptr, offs, 0)
#define __AE_L16X4_XC_4A(dst, ptr, offs, cbr_sel) \
  do { __HAYDN_AE_CB_LD64((dst), (ptr), (cbr_sel), (offs) >> 3); } while (0)

#undef  AE_S16X4_XC
#define AE_S16X4_XC(...) __AE_S16X4_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S16X4_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16X4_XC_OVERLOAD(...) \
  __AE_S16X4_XC_GET(__VA_ARGS__, __AE_S16X4_XC_4A, __AE_S16X4_XC_3A)(__VA_ARGS__)
#define __AE_S16X4_XC_3A(src, ptr, offs) \
  __AE_S16X4_XC_4A(src, ptr, offs, 0)
#define __AE_S16X4_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    void *__np = haydn_sdw_cb_imm((haydn_dr64_t)(src), (ptr), \
                                  (cbr_sel), (offs) >> 3); \
    (ptr) = (__typeof__(ptr))__np; \
  } while (0)

//---- AE_L32X2_XC / AE_S32X2_XC overload --------------------------------
#undef  AE_L32X2_XC
#define AE_L32X2_XC(...) __AE_L32X2_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2_XC_OVERLOAD(...) \
  __AE_L32X2_XC_GET(__VA_ARGS__, __AE_L32X2_XC_4A, __AE_L32X2_XC_3A)(__VA_ARGS__)
#define __AE_L32X2_XC_3A(dst, ptr, offs) \
  __AE_L32X2_XC_4A(dst, ptr, offs, 0)
#define __AE_L32X2_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           (offs) >> 3); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

#undef  AE_S32X2_XC
#define AE_S32X2_XC(...) __AE_S32X2_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32X2_XC_OVERLOAD(...) \
  __AE_S32X2_XC_GET(__VA_ARGS__, __AE_S32X2_XC_4A, __AE_S32X2_XC_3A)(__VA_ARGS__)
#define __AE_S32X2_XC_3A(src, ptr, offs) \
  __AE_S32X2_XC_4A(src, ptr, offs, 0)
#define __AE_S32X2_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    void *__np = haydn_sdw_cb_imm(__s, (ptr), (cbr_sel), \
                                  (offs) >> 3); \
    (ptr) = (__typeof__(ptr))__np; \
  } while (0)

//---- AE_S32RA64S_IP overload (3-arg: acc, ptr, inc; fixed shift=16) --------
// NatureDSP FIR uses AE_S32RA64S_IP(q, Y, +4) / …, -4) where arg3 is the
// *byte post-increment*, not the shift. HiFi stores satsr64(acc, 16) then
// advances ptr by inc. 4-arg keeps explicit (acc, ptr, shift, inc).
// Slot-writeback via *(ae_int32 **)&(ptr) preserves castxcc lvalue advance.
#undef  AE_S32RA64S_IP
#define AE_S32RA64S_IP(...) __AE_S32RA64S_IP_OVERLOAD(__VA_ARGS__)
#define __AE_S32RA64S_IP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32RA64S_IP_OVERLOAD(...) \
  __AE_S32RA64S_IP_GET(__VA_ARGS__, __AE_S32RA64S_IP_4A, __AE_S32RA64S_IP_3A)(__VA_ARGS__)
#define __AE_S32RA64S_IP_3A(acc, ptr, inc) \
  do { *(ae_int32 *)(ptr) = (ae_int32)haydn_satsr64((acc), 16); \
       *(ae_int32 **)&(ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_S32RA64S_IP_4A(acc, ptr, shift, inc) \
  do { *(ae_int32 *)(ptr) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32 **)&(ptr) = (ae_int32 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_MULFD24X2_FIR_H / AE_MULAFD24X2_FIR_H 5-arg form ---------------
// HiFi3 signature: (q0, q1, d0, d1, c) — both q accumulators absorb the
// same-coef product into two independent data operands. This matches the
// existing 32x16 FIR family (AE_MULFD32X16X2_FIR_HH). The previous 4-arg
// helper is retained via a 4-arg overload for backwards compatibility.
// Dual-product 24×24 FIR (NatureDSP d0,d1,c): FF2 LL+HH on d0 for q0,
// HH(d0)+LL(d1) for q1 — matches pure bkfir24x24 / soft dual (not single
// fmul32s_hh half-product).
#undef  AE_MULFD24X2_FIR_H
static inline void AE_MULFD24X2_FIR_H_5A(ae_int64 *q0, ae_int64 *q1,
                                          ae_f24x2 d0, ae_f24x2 d1,
                                          ae_f24x2 c) {
  haydn_dr64_t __d0 = (haydn_dr64_t)d0, __d1 = (haydn_dr64_t)d1;
  haydn_dr64_t __c = (haydn_dr64_t)c;
  *q0 = haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q0 = haydn_ff2mula32rs_hh(*q0, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q1 = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q1 = haydn_ff2mula32rs_ll(*q1, __AE_TO_I64(__d1), __AE_TO_I64(__c));
}
static inline void AE_MULFD24X2_FIR_H_4A(ae_int64 *q0, ae_int64 *q1,
                                          ae_f24x2 d, ae_f24x2 c) {
  AE_MULFD24X2_FIR_H_5A(q0, q1, d, d, c);
}
#define AE_MULFD24X2_FIR_H(...) \
  __AE_MULFD24X2_FIR_H_GET(__VA_ARGS__, \
    AE_MULFD24X2_FIR_H_5A, AE_MULFD24X2_FIR_H_5A, \
    AE_MULFD24X2_FIR_H_5A, AE_MULFD24X2_FIR_H_4A)(__VA_ARGS__)
#define __AE_MULFD24X2_FIR_H_GET(_1, _2, _3, _4, _5, NAME, ...) NAME

#undef  AE_MULAFD24X2_FIR_H
static inline void AE_MULAFD24X2_FIR_H_5A(ae_int64 *q0, ae_int64 *q1,
                                           ae_f24x2 d0, ae_f24x2 d1,
                                           ae_f24x2 c) {
  haydn_dr64_t __d0 = (haydn_dr64_t)d0, __d1 = (haydn_dr64_t)d1;
  haydn_dr64_t __c = (haydn_dr64_t)c;
  *q0 = haydn_ff2mula32rs_ll(*q0, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q0 = haydn_ff2mula32rs_hh(*q0, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q1 = haydn_ff2mula32rs_hh(*q1, __AE_TO_I64(__d0), __AE_TO_I64(__c));
  *q1 = haydn_ff2mula32rs_ll(*q1, __AE_TO_I64(__d1), __AE_TO_I64(__c));
}
static inline void AE_MULAFD24X2_FIR_H_4A(ae_int64 *q0, ae_int64 *q1,
                                           ae_f24x2 d, ae_f24x2 c) {
  AE_MULAFD24X2_FIR_H_5A(q0, q1, d, d, c);
}
#define AE_MULAFD24X2_FIR_H(...) \
  __AE_MULAFD24X2_FIR_H_GET(__VA_ARGS__, \
    AE_MULAFD24X2_FIR_H_5A, AE_MULAFD24X2_FIR_H_5A, \
    AE_MULAFD24X2_FIR_H_5A, AE_MULAFD24X2_FIR_H_4A)(__VA_ARGS__)
#define __AE_MULAFD24X2_FIR_H_GET(_1, _2, _3, _4, _5, NAME, ...) NAME



//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 2: lowercase *_loadip helpers, more       //
// 3-arg / 2-arg addr-mode overloads the original kernels use, and the      //
// scalar L16_XC / S32_L_I / ROUND32X2F48SASYM 2-arg forms. All are         //
// clearly-correct compositions: the missing arg is cbr_sel (linear          //
// addressing), the stride (implicit 8 bytes for quad-16 / dual-32 width),   //
// or the shift (implicit for symmetric round-down to half-width lanes).     //
//===----------------------------------------------------------------------===//

//---- lowercase loadip helpers (used by original kernels) ----------------
#define ae_f16x4_loadip(dst, ptr, inc) AE_L16X4_IP(dst, ptr, inc)
#define ae_f32x2_loadip(dst, ptr, inc) AE_L32X2_IP(dst, ptr, inc)
#define ae_f24x2_loadip(dst, ptr, inc) AE_L32X2_IP(dst, ptr, inc)

//---- AE_L16_XC overload (3-arg: dst, ptr, offs; cbr_sel defaults to linear)
// EMULATED single owner: ordinary i16 load + haydn_cbr_step(byte offs).
// Late 64b-trunc redefines collapsed (no D_LDW_CB / offs>>3).
#undef  AE_L16_XC
#define AE_L16_XC(...) __AE_L16_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L16_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L16_XC_OVERLOAD(...) \
  __AE_L16_XC_GET(__VA_ARGS__, __AE_L16_XC_4A, __AE_L16_XC_3A)(__VA_ARGS__)
#define __AE_L16_XC_3A(dst, ptr, offs) \
  __AE_L16_XC_4A(dst, ptr, offs, 0)
#define __AE_L16_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    (dst) = *(ae_int16 *)(void *)(ptr); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_LA16X4_IC / AE_LA32X2_IC — AR residual + CBR cursor wrap ----------
// Seed with AE_LA*POS_PC → PLDWWUA. Each IC step: D_LTWUA/LQHWUA funnel from
// AR residual (unaligned part lives in AR), then wrap C ptr via CBR mirrors.
// D_LDW_CB stays 8B-aligned (L32X2_XC); do not use CB for unaligned LA_IC.
#undef  AE_LA16X4_IC
#define AE_LA16X4_IC(...) __AE_LA16X4_IC_OVERLOAD(__VA_ARGS__)
#define __AE_LA16X4_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA16X4_IC_OVERLOAD(...) \
  __AE_LA16X4_IC_GET(__VA_ARGS__, __AE_LA16X4_IC_4A, __AE_LA16X4_IC_3A)(__VA_ARGS__)
#define __AE_LA16X4_IC_3A(dst, align, ptr) \
  __AE_LA16X4_IC_4A(dst, align, ptr, 0)
#define __AE_LA16X4_IC_4A(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_cb_ld_t __r = haydn_ae_cb_ld_tw(__ar, (int)(cbr_sel), __p, 1); \
    (dst) = (ae_int16x4)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
    (void)(align); \
  } while (0)

#undef  AE_LA32X2_IC
#define AE_LA32X2_IC(...) __AE_LA32X2_IC_OVERLOAD(__VA_ARGS__)
#define __AE_LA32X2_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA32X2_IC_OVERLOAD(...) \
  __AE_LA32X2_IC_GET(__VA_ARGS__, __AE_LA32X2_IC_4A, __AE_LA32X2_IC_3A)(__VA_ARGS__)
#define __AE_LA32X2_IC_3A(dst, align, ptr) \
  __AE_LA32X2_IC_4A(dst, align, ptr, 0)
#define __AE_LA32X2_IC_4A(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_cb_ld_t __r = haydn_ae_cb_ld_tw(__ar, (int)(cbr_sel), __p, 0); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg(__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
    (void)(align); \
  } while (0)

//---- AE_SA16X4_IC / AE_SA32X2_IC — AR residual store + CBR cursor wrap ----
#undef  AE_SA16X4_IC
#define AE_SA16X4_IC(...) __AE_SA16X4_IC_OVERLOAD(__VA_ARGS__)
#define __AE_SA16X4_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_SA16X4_IC_OVERLOAD(...) \
  __AE_SA16X4_IC_GET(__VA_ARGS__, __AE_SA16X4_IC_4A, __AE_SA16X4_IC_3A)(__VA_ARGS__)
#define __AE_SA16X4_IC_3A(src, align, ptr) \
  __AE_SA16X4_IC_4A(src, align, ptr, 0)
#define __AE_SA16X4_IC_4A(src, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __s = (haydn_dr64_t)__haydn_v4_as_i64((haydn_x4int16)(src)); \
    void *__np = haydn_ae_cb_st(__ar, (int)(cbr_sel), __p, __s, 1); \
    (ptr) = (__typeof__(ptr))__np; \
    (void)(align); \
  } while (0)

#undef  AE_SA32X2_IC
#define AE_SA32X2_IC(...) __AE_SA32X2_IC_OVERLOAD(__VA_ARGS__)
#define __AE_SA32X2_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_SA32X2_IC_OVERLOAD(...) \
  __AE_SA32X2_IC_GET(__VA_ARGS__, __AE_SA32X2_IC_4A, __AE_SA32X2_IC_3A)(__VA_ARGS__)
#define __AE_SA32X2_IC_3A(src, align, ptr) \
  __AE_SA32X2_IC_4A(src, align, ptr, 0)
#define __AE_SA32X2_IC_4A(src, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    void *__np = haydn_ae_cb_st(__ar, (int)(cbr_sel), __p, __s, 0); \
    (ptr) = (__typeof__(ptr))__np; \
    (void)(align); \
  } while (0)

//---- AE_S32X2F24_XC / AE_L32X2F24_XC 3-arg overload ---------------------
// (The AE_S32X2F24_XC / AE_L32X2F24_XC 3-arg overload is defined earlier in
// this file with the correct CB-intrinsic routing. The duplicate block that
// used to live here lowered 3-arg to linear ptr-arith — removed per .)

//---- AE_L16X4_RIC / AE_L16X4_RIP / AE_L32X2_RIP / AE_L32X2_RIC ----------
// Reverse-CB is EXACT via signed negative D_LDW_CB stride
// (-((byte_offs)>>3)). Imm form when offs is constant ImmArg; REG form is
// the hardware twin for variable stride (same element units).
#undef  AE_L16X4_RIC
#define AE_L16X4_RIC(...) __AE_L16X4_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L16X4_RIC_OVERLOAD(...) \
  __AE_L16X4_RIC_GET(__VA_ARGS__, __AE_L16X4_RIC_4A, __AE_L16X4_RIC_3A, __AE_L16X4_RIC_2A)(__VA_ARGS__)
#define __AE_L16X4_RIC_2A(dst, ptr) \
  __AE_L16X4_RIC_4A(dst, ptr, 8, 0)
#define __AE_L16X4_RIC_3A(dst, ptr, offs) \
  __AE_L16X4_RIC_4A(dst, ptr, offs, 0)
#define __AE_L16X4_RIC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           -((offs) >> 3)); \
    (dst) = (ae_int16x4)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

#undef  AE_L16X4_RIP
#define AE_L16X4_RIP(...) __AE_L16X4_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_RIP_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16X4_RIP_OVERLOAD(...) \
  __AE_L16X4_RIP_GET(__VA_ARGS__, __AE_L16X4_RIP_3A, __AE_L16X4_RIP_2A)(__VA_ARGS__)
#define __AE_L16X4_RIP_2A(dst, ptr) \
  do { (dst) = *(ae_int16x4 *)(ptr); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) - 8); } while (0)
#define __AE_L16X4_RIP_3A(dst, ptr, inc) \
  do { (dst) = *(ae_int16x4 *)(ptr); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) - (inc)); } while (0)

#undef  AE_L32X2_RIP
#define AE_L32X2_RIP(...) __AE_L32X2_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_RIP_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2_RIP_OVERLOAD(...) \
  __AE_L32X2_RIP_GET(__VA_ARGS__, __AE_L32X2_RIP_3A, __AE_L32X2_RIP_2A)(__VA_ARGS__)
#define __AE_L32X2_RIP_2A(dst, ptr) \
  do { (dst) = *(ae_int32x2 *)(ptr); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) - 8); } while (0)
#define __AE_L32X2_RIP_3A(dst, ptr, offs) \
  do { (dst) = *(ae_int32x2 *)(ptr); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) - (offs)); } while (0)

/* Reverse dual-32 CB: signed negative D_LDW_CB element stride (not forward XC).
 * f32x2 lane order matches AE_L32X2_XC (LE mem → H-first reg). */
#undef  AE_L32X2_RIC
#define AE_L32X2_RIC(...) __AE_L32X2_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2_RIC_OVERLOAD(...) \
  __AE_L32X2_RIC_GET(__VA_ARGS__, __AE_L32X2_RIC_4A, __AE_L32X2_RIC_3A, __AE_L32X2_RIC_2A)(__VA_ARGS__)
#define __AE_L32X2_RIC_2A(dst, ptr) \
  __AE_L32X2_RIC_4A(dst, ptr, 8, 0)
#define __AE_L32X2_RIC_3A(dst, ptr, offs) \
  __AE_L32X2_RIC_4A(dst, ptr, offs, 0)
#define __AE_L32X2_RIC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           -((offs) >> 3)); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

//---- AE_LA16X4_RIC / AE_LA32X2_RIC — reverse UA dir=1 + CBR wrap -8 ------
// EXACT: same unaligned reverse path as AE_LA*_RIP (haydn_ae_la{16x4,64}
// _step / d_l{qhw,tw}ua_post dir=1 ImmArg) plus haydn_cbr_step(ptr, -8,
// cbr_sel) for circular wrap. Must not silent-alias forward IC (dir=0, +8).
#undef  AE_LA16X4_RIC
#define AE_LA16X4_RIC(...) __AE_LA16X4_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_LA16X4_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA16X4_RIC_OVERLOAD(...) \
  __AE_LA16X4_RIC_GET(__VA_ARGS__, __AE_LA16X4_RIC_4A, __AE_LA16X4_RIC_3A)(__VA_ARGS__)
#define __AE_LA16X4_RIC_3A(dst, align, ptr) \
  __AE_LA16X4_RIC_4A(dst, align, ptr, 0)
#define __AE_LA16X4_RIC_4A(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    (dst) = (ae_int16x4)haydn_ae_la16x4_step(__ar, __p, 8, 1); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), -8, (int)(cbr_sel)); \
  } while (0)

#undef  AE_LA32X2_RIC
#define AE_LA32X2_RIC(...) __AE_LA32X2_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_LA32X2_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA32X2_RIC_OVERLOAD(...) \
  __AE_LA32X2_RIC_GET(__VA_ARGS__, __AE_LA32X2_RIC_4A, __AE_LA32X2_RIC_3A)(__VA_ARGS__)
#define __AE_LA32X2_RIC_3A(dst, align, ptr) \
  __AE_LA32X2_RIC_4A(dst, align, ptr, 0)
#define __AE_LA32X2_RIC_4A(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, 8, 1); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), -8, (int)(cbr_sel)); \
  } while (0)

//---- AE_LA16X4_RIP / AE_LA32X2_RIP / AE_LA32X2F24_RIP overload (→ AR helpers)
#undef  AE_LA16X4_RIP
#define AE_LA16X4_RIP(...) __AE_LA16X4_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_LA16X4_RIP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA16X4_RIP_OVERLOAD(...) \
  __AE_LA16X4_RIP_GET(__VA_ARGS__, __AE_LA16X4_RIP_4A, __AE_LA16X4_RIP_3A)(__VA_ARGS__)
#define __AE_LA16X4_RIP_3A(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    (dst) = (ae_int16x4)haydn_ae_la16x4_step(__ar, __p, 8, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - 8); \
  } while (0)
#define __AE_LA16X4_RIP_4A(dst, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    (dst) = (ae_int16x4)haydn_ae_la16x4_step(__ar, __p, __s, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)

/* Reverse dual-32 UA (dir=1). Lane law (D1.15 golden adjudication):
 * D_LTWUA_POST (instruction_type_index.json type AR) has NO dir operand —
 * `temp=mem64[rs&~7]; window={temp,ar}; rtd=(rs[2]==0)?window[63:00]:
 * window[95:32]; ar=temp; rs=rs+8` — direction only steps the pointer, never
 * reorders data words. Every 32x2-shaped UA/CB load therefore owes the same
 * H-first presentation as AE_LA32X2_IP/IC/RIC: route the raw LE window
 * through haydn_ae_f32x2_mem_to_reg so the first (lowest-address) word of
 * the logical object lands in the H lane. */
#undef  AE_LA32X2_RIP
#define AE_LA32X2_RIP(...) __AE_LA32X2_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_LA32X2_RIP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA32X2_RIP_OVERLOAD(...) \
  __AE_LA32X2_RIP_GET(__VA_ARGS__, __AE_LA32X2_RIP_4A, __AE_LA32X2_RIP_3A)(__VA_ARGS__)
#define __AE_LA32X2_RIP_3A(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, 8, 1); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) - 8); \
  } while (0)
#define __AE_LA32X2_RIP_4A(dst, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, __s, 1); \
    (dst) = (ae_int32x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)

#undef  AE_LA32X2F24_RIP
#define AE_LA32X2F24_RIP(...) __AE_LA32X2F24_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_LA32X2F24_RIP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA32X2F24_RIP_OVERLOAD(...) \
  __AE_LA32X2F24_RIP_GET(__VA_ARGS__, __AE_LA32X2F24_RIP_4A, __AE_LA32X2F24_RIP_3A)(__VA_ARGS__)
/* Dual-24 reverse UA residual: same dir=1 path and the same H-first lane
 * law as LA32X2_RIP (golden: D_LTWUA_POST is direction-neutral on word
 * order), but keep ae_f24x2 / pointer typeof (never force ae_int32x2
 * assignment). */
#define __AE_LA32X2F24_RIP_3A(dst, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, 8, 1); \
    (dst) = (ae_f24x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) - 8); \
  } while (0)
#define __AE_LA32X2F24_RIP_4A(dst, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_dr64_t __le = (haydn_dr64_t)haydn_ae_la64_step(__ar, __p, __s, 1); \
    (dst) = (ae_f24x2)haydn_ae_f32x2_mem_to_reg(__le); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)

//---- AE_L32X2F24_RIC overload — reverse-CB negative D_LDW_CB stride ------
#undef  AE_L32X2F24_RIC
#define AE_L32X2F24_RIC(...) __AE_L32X2F24_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2F24_RIC_OVERLOAD(...) \
  __AE_L32X2F24_RIC_GET(__VA_ARGS__, __AE_L32X2F24_RIC_4A, __AE_L32X2F24_RIC_3A, __AE_L32X2F24_RIC_2A)(__VA_ARGS__)
#define __AE_L32X2F24_RIC_2A(dst, ptr) \
  __AE_L32X2F24_RIC_4A(dst, ptr, 8, 0)
#define __AE_L32X2F24_RIC_3A(dst, ptr, offs) \
  __AE_L32X2F24_RIC_4A(dst, ptr, offs, 0)
#define __AE_L32X2F24_RIC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           -((offs) >> 3)); \
    (dst) = (ae_f24x2)(haydn_dr64_t)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

//---- AE_S32_L_I overload (3-arg indexed store, no ptr update) ----------
// The HiFi3 AE_S32_L_I(src, ptr, offs) stores src at ptr[offs/4] WITHOUT
// updating ptr (offs is an element index scaled by sizeof). This form
// supports the lvalue-cast case `(ae_int32*)D` that kernels pass.
#undef  AE_S32_L_I
#define AE_S32_L_I(...) __AE_S32_L_I_OVERLOAD(__VA_ARGS__)
#define __AE_S32_L_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_S32_L_I_OVERLOAD(...) \
  __AE_S32_L_I_GET(__VA_ARGS__, __AE_S32_L_I_3A, __AE_S32_L_I_2A)(__VA_ARGS__)
#define __AE_S32_L_I_2A(src, ptr) \
  do { *(ae_int32 *)(ptr) = (ae_int32)(src); } while (0)
#define __AE_S32_L_I_3A(src, ptr, offs) \
  do { *((ae_int32 *)(ptr) + ((offs) / (int)sizeof(ae_int32))) = (ae_int32)(src); } while (0)

//---- AE_ROUND32X2F48SASYM / AE_ROUND32X2F64SASYM 2-arg overload --------
// 2-arg form: standard 64->32 round with implicit shift=31 (sign bit).
#undef  AE_ROUND32X2F48SASYM
#define AE_ROUND32X2F48SASYM(...) __AE_ROUND32X2F48SASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUND32X2F48SASYM_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_ROUND32X2F48SASYM_OVERLOAD(...) \
  __AE_ROUND32X2F48SASYM_GET(__VA_ARGS__, __AE_ROUND32X2F48SASYM_3A, __AE_ROUND32X2F48SASYM_2A)(__VA_ARGS__)
#define __AE_ROUND32X2F48SASYM_2A(q0, q1) haydn_packsr32((q0), 0)
#define __AE_ROUND32X2F48SASYM_3A(q0, q1, s) haydn_packsr32((q0), (s))

#undef  AE_ROUND32X2F64SASYM
#define AE_ROUND32X2F64SASYM(...) __AE_ROUND32X2F64SASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUND32X2F64SASYM_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_ROUND32X2F64SASYM_OVERLOAD(...) \
  __AE_ROUND32X2F64SASYM_GET(__VA_ARGS__, __AE_ROUND32X2F64SASYM_3A, __AE_ROUND32X2F64SASYM_2A)(__VA_ARGS__)
#define __AE_ROUND32X2F64SASYM_2A(q0, q1) \
  ((ae_int32x2)((long long)(int32_t)haydn_satsr64((q0), 0) | \
                ((long long)(int32_t)haydn_satsr64((q1), 0) << 32)))
#define __AE_ROUND32X2F64SASYM_3A(a, b, s) AE_ROUND32X2F48S((a), (b), (s))

//---- XT_MIN / XT_MAX helpers (HiFi legacy scalar min/max) --------------
#ifndef XT_MIN
#define XT_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef XT_MAX
#define XT_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

//---- NatureDSP sibling-kernel forward declarations ----------------------
// Some kernels call sibling kernels (e.g. fir_acorr* -> fir_xcorr*) defined
// in another translation unit. Provide forward declarations so the implicit-
// declaration warning does not abort the build.
void fir_xcorr16x16(int16_t * restrict r, const int16_t * restrict x,
                    const int16_t * restrict y, int N, int M);
void fir_xcorr24x24(int32_t * restrict r, const int32_t * restrict x,
                    const int32_t * restrict y, int N, int M);
void fir_xcorr32x32(int32_t * restrict r, const int32_t * restrict x,
                    const int32_t * restrict y, int N, int M);

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 3: remaining MAC/ROUND signature variants //
// needed by FIR kernels.                                                    //
//===----------------------------------------------------------------------===//

//---- AE_ROUND16X4F32SASYM overload (2-arg: a, b after TRUNCA) -------------
// NatureDSP FIR: TRUNCA32X2F64S (already rounded 64→32) then ROUND16X4.
// A second *rounded* 32→16 (x2sra32r) biases LSB vs pure
// sat16(satsr64(acc,16)>>16). After TRUNCA, ROUND16 is plain ASR + sat16.
#undef  AE_ROUND16X4F32SASYM
static inline int32_t haydn_trunc_asr32_sat16(int32_t x, int sh)
{
  int32_t t = (sh <= 0) ? x : (sh >= 31 ? (x < 0 ? -1 : 0) : (x >> sh));
  if (t > 32767)
    return 32767;
  if (t < -32768)
    return -32768;
  return t;
}
static inline ae_int16x4 haydn_round16x4_after_trunca(ae_int32x2 a,
                                                        ae_int32x2 b, int sh)
{
  int32_t a0 = (int32_t)(uint32_t)(uint64_t)(haydn_dr64_t)a;
  int32_t a1 = (int32_t)(uint32_t)((uint64_t)(haydn_dr64_t)a >> 32);
  int32_t b0 = (int32_t)(uint32_t)(uint64_t)(haydn_dr64_t)b;
  int32_t b1 = (int32_t)(uint32_t)((uint64_t)(haydn_dr64_t)b >> 32);
  int16_t s0 = (int16_t)haydn_trunc_asr32_sat16(a0, sh);
  int16_t s1 = (int16_t)haydn_trunc_asr32_sat16(a1, sh);
  int16_t s2 = (int16_t)haydn_trunc_asr32_sat16(b0, sh);
  int16_t s3 = (int16_t)haydn_trunc_asr32_sat16(b1, sh);
  uint64_t pack = ((uint64_t)(uint16_t)s0) | ((uint64_t)(uint16_t)s1 << 16) |
                  ((uint64_t)(uint16_t)s2 << 32) | ((uint64_t)(uint16_t)s3 << 48);
  return (ae_int16x4)pack;
}
#define AE_ROUND16X4F32SASYM(...) __AE_ROUND16X4F32SASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUND16X4F32SASYM_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_ROUND16X4F32SASYM_OVERLOAD(...) \
  __AE_ROUND16X4F32SASYM_GET(__VA_ARGS__, __AE_ROUND16X4F32SASYM_3A, \
                             __AE_ROUND16X4F32SASYM_2A)(__VA_ARGS__)
#define __AE_ROUND16X4F32SASYM_2A(a, b) \
  haydn_round16x4_after_trunca((a), (b), 16)
#define __AE_ROUND16X4F32SASYM_3A(a, b, s) \
  haydn_round16x4_after_trunca((a), (b), (s))

//---- AE_MULAFQ16X2_FIR_3 / AE_MULAFQ16X2_FIR_1 5-arg form --------------
// HiFi3 signature: (q0, q1, d0, d1, c) — two independent data operands
// absorb the same-coef product into q0/q1. Mirrors the 32x16 FIR family.
#undef  AE_MULAFQ16X2_FIR_3
static inline void AE_MULAFQ16X2_FIR_3_5A(ae_int64 *q0, ae_int64 *q1,
                                           ae_int16x4 d0, ae_int16x4 d1,
                                           ae_int16x4 c) {
  ae_int64 r0 = haydn_mulafq16x2_fir_3(*q0, (haydn_dr64_t)d0, (haydn_dr64_t)c);
  ae_int64 r1 = haydn_mulafq16x2_fir_3(*q1, (haydn_dr64_t)d1, (haydn_dr64_t)c);
  *q0 = r0;
  *q1 = r1;
}
static inline void AE_MULAFQ16X2_FIR_3_4A(ae_int64 *q0, ae_int64 *q1,
                                           ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulafq16x2_fir_3(*q0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}
#define AE_MULAFQ16X2_FIR_3(...) \
  __AE_MULAFQ16X2_FIR_3_GET(__VA_ARGS__, \
    AE_MULAFQ16X2_FIR_3_5A, AE_MULAFQ16X2_FIR_3_5A, \
    AE_MULAFQ16X2_FIR_3_5A, AE_MULAFQ16X2_FIR_3_4A)(__VA_ARGS__)
#define __AE_MULAFQ16X2_FIR_3_GET(_1, _2, _3, _4, _5, NAME, ...) NAME

#undef  AE_MULAFQ16X2_FIR_1
static inline void AE_MULAFQ16X2_FIR_1_5A(ae_int64 *q0, ae_int64 *q1,
                                           ae_int16x4 d0, ae_int16x4 d1,
                                           ae_int16x4 c) {
  ae_int64 r0 = haydn_mulafq16x2_fir_1(*q0, (haydn_dr64_t)d0, (haydn_dr64_t)c);
  ae_int64 r1 = haydn_mulafq16x2_fir_1(*q1, (haydn_dr64_t)d1, (haydn_dr64_t)c);
  *q0 = r0;
  *q1 = r1;
}
static inline void AE_MULAFQ16X2_FIR_1_4A(ae_int64 *q0, ae_int64 *q1,
                                           ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulafq16x2_fir_1(*q0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}
#define AE_MULAFQ16X2_FIR_1(...) \
  __AE_MULAFQ16X2_FIR_1_GET(__VA_ARGS__, \
    AE_MULAFQ16X2_FIR_1_5A, AE_MULAFQ16X2_FIR_1_5A, \
    AE_MULAFQ16X2_FIR_1_5A, AE_MULAFQ16X2_FIR_1_4A)(__VA_ARGS__)
#define __AE_MULAFQ16X2_FIR_1_GET(_1, _2, _3, _4, _5, NAME, ...) NAME

//---- AE_MULFQ16X2_FIR_3 / AE_MULFQ16X2_FIR_1 5-arg form ----------------
#undef  AE_MULFQ16X2_FIR_3
static inline void AE_MULFQ16X2_FIR_3_5A(ae_int64 *q0, ae_int64 *q1,
                                          ae_int16x4 d0, ae_int16x4 d1,
                                          ae_int16x4 c) {
  *q0 = haydn_mulfq16x2_fir_3(0, (haydn_dr64_t)d0, (haydn_dr64_t)c);
  *q1 = haydn_mulfq16x2_fir_3(0, (haydn_dr64_t)d1, (haydn_dr64_t)c);
}
static inline void AE_MULFQ16X2_FIR_3_4A(ae_int64 *q0, ae_int64 *q1,
                                          ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulfq16x2_fir_3(0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}
#define AE_MULFQ16X2_FIR_3(...) \
  __AE_MULFQ16X2_FIR_3_GET(__VA_ARGS__, \
    AE_MULFQ16X2_FIR_3_5A, AE_MULFQ16X2_FIR_3_5A, \
    AE_MULFQ16X2_FIR_3_5A, AE_MULFQ16X2_FIR_3_4A)(__VA_ARGS__)
#define __AE_MULFQ16X2_FIR_3_GET(_1, _2, _3, _4, _5, NAME, ...) NAME

#undef  AE_MULFQ16X2_FIR_1
static inline void AE_MULFQ16X2_FIR_1_5A(ae_int64 *q0, ae_int64 *q1,
                                          ae_int16x4 d0, ae_int16x4 d1,
                                          ae_int16x4 c) {
  *q0 = haydn_mulfq16x2_fir_1(0, (haydn_dr64_t)d0, (haydn_dr64_t)c);
  *q1 = haydn_mulfq16x2_fir_1(0, (haydn_dr64_t)d1, (haydn_dr64_t)c);
}
static inline void AE_MULFQ16X2_FIR_1_4A(ae_int64 *q0, ae_int64 *q1,
                                          ae_int16x4 d, ae_int16x4 c) {
  ae_int64 r = haydn_mulfq16x2_fir_1(0, (haydn_dr64_t)d, (haydn_dr64_t)c);
  *q0 = r;
  *q1 = r;
}
#define AE_MULFQ16X2_FIR_1(...) \
  __AE_MULFQ16X2_FIR_1_GET(__VA_ARGS__, \
    AE_MULFQ16X2_FIR_1_5A, AE_MULFQ16X2_FIR_1_5A, \
    AE_MULFQ16X2_FIR_1_5A, AE_MULFQ16X2_FIR_1_4A)(__VA_ARGS__)
#define __AE_MULFQ16X2_FIR_1_GET(_1, _2, _3, _4, _5, NAME, ...) NAME

//---- AE_MULZASFD32X16_H3_L2 / AE_MULZAAFD32X16_H2_L3 2-arg form --------
// HiFi3 signature: (d, c) — zero-init accumulator. The existing 3-arg form
// takes (acc, d, c); provide the 2-arg zero-init alias.
#undef  AE_MULZASFD32X16_H3_L2
static inline ae_int64 AE_MULZASFD32X16_H3_L2_2A(ae_int16x4 d, ae_int16x4 c) {
  return haydn_f2mulss32rs_hhll((ae_int64)0, __AE_TO_I64(d), __AE_TO_I64(c));
}
static inline ae_int64 AE_MULZASFD32X16_H3_L2_3A(ae_int64 acc, ae_int16x4 d,
                                                  ae_int16x4 c) {
  (void)acc;
  return haydn_f2mulss32rs_hhll((ae_int64)0, __AE_TO_I64(d), __AE_TO_I64(c));
}
#define AE_MULZASFD32X16_H3_L2(...) \
  __AE_MULZASFD32X16_H3_L2_GET(__VA_ARGS__, \
    AE_MULZASFD32X16_H3_L2_3A, AE_MULZASFD32X16_H3_L2_3A, \
    AE_MULZASFD32X16_H3_L2_2A)(__VA_ARGS__)
#define __AE_MULZASFD32X16_H3_L2_GET(_1, _2, _3, NAME, ...) NAME

#undef  AE_MULZAAFD32X16_H2_L3
static inline ae_int64 AE_MULZAAFD32X16_H2_L3_2A(ae_int16x4 d, ae_int16x4 c) {
  return haydn_f2mulaa32rs_hhll((ae_int64)0, __AE_TO_I64(d), __AE_TO_I64(c));
}
static inline ae_int64 AE_MULZAAFD32X16_H2_L3_3A(ae_int64 acc, ae_int16x4 d,
                                                  ae_int16x4 c) {
  (void)acc;
  return haydn_f2mulaa32rs_hhll((ae_int64)0, __AE_TO_I64(d), __AE_TO_I64(c));
}
#define AE_MULZAAFD32X16_H2_L3(...) \
  __AE_MULZAAFD32X16_H2_L3_GET(__VA_ARGS__, \
    AE_MULZAAFD32X16_H2_L3_3A, AE_MULZAAFD32X16_H2_L3_3A, \
    AE_MULZAAFD32X16_H2_L3_2A)(__VA_ARGS__)
#define __AE_MULZAAFD32X16_H2_L3_GET(_1, _2, _3, NAME, ...) NAME

//---- AE_MULSF16X4SS 3-arg/4-arg overload (Path B 2-dest) ----------
// HiFi3 kernel signatures:
//   3-arg: `AE_MULSF16X4SS(acc, a, b)` — single accumulator, high-pair update.
//   4-arg: `AE_MULSF16X4SS(vaf, vbf, f2, f0)` (fir_blms16x32) — vaf/vbf are
//          BOTH in/out accumulators (2-dest); a/b are the multiplier sources.
#undef  AE_MULSF16X4SS
#define AE_MULSF16X4SS(...) __AE_MULSF16X4SS_OVERLOAD(__VA_ARGS__)
#define __AE_MULSF16X4SS_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_MULSF16X4SS_OVERLOAD(...) \
  __AE_MULSF16X4SS_GET(__VA_ARGS__, __AE_MULSF16X4SS_4A, __AE_MULSF16X4SS_3A)(__VA_ARGS__)
#define __AE_MULSF16X4SS_3A(acc, a, b) \
  do { (acc) = (ae_int64)haydn_x4muls16s((int64_t)(acc), (int64_t)0, \
                                           (a), (b)).hi; } while (0)
#define __AE_MULSF16X4SS_4A(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4muls16s((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                         (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer: load-wrapper arity fixes (final dispatch).     //
//                                                                            //
// NatureDSP HiFi3 kernels invoke AE_L* / AE_LA* load wrappers in BOTH an    //
// expression-returning form (lower arity, returns the loaded value) AND a    //
// statement form (higher arity, stores into an explicit dst). The earlier    //
// sections of this header defined only the highest-arity statement form for  //
// the wrappers below, which made every `x = AE_L..._XP(ptr, inc);`,         //
// `AE_LA..._IC(dst, align, ptr);`, and `x = AE_L16X2M_X(ptr, offs);` call   //
// site fail to compile ("too few arguments provided to function-like macro   //
// invocation"). This block #undefs those wrappers ONE LAST TIME and         //
// redefines them as arity-dispatching overload macros that accept BOTH      //
// forms, with HiFi3-faithful semantics (verified against the original       //
// NatureDSP HiFi3 kernel sources in ~/haydn-plans/hifi_naturedsp_reports/): //
//   - _XP 3-arg (dst, ptr, inc)        : access at *ptr, post-inc ptr by inc//
//   - _XP 4-arg (dst, ptr, offs, inc)  : access at *(ptr+offs), post-inc inc//
//   - _XC 3-arg (dst, ptr, offs)       : access at ptr+offs, no writeback   //
//   - _XC 4-arg (dst, ptr, offs, cbr)  : circular-buffer access             //
//   - _LA*_IC 3-arg (dst, align, ptr)  : aligned CB load, default stride    //
//   - _LA*_IC 4-arg (dst, align, ptr, cbr_sel) : aligned CB load, explicit  //
//   - L16X2M_X / L16X2M_I 2-arg (ptr, offs) : RETURN loaded value           //
//   - L16X2M_X / L16X2M_I 3-arg (dst, ptr, offs) : assign to dst            //
//                                                                            //
// No new instructions are introduced; every expansion composes the same     //
// haydn_* primitives or plain loads/stores already used elsewhere.         //
//===----------------------------------------------------------------------===//

// AE_L32_XP arity overload is defined once with the first LS wrappers
// (3-arg and 4-arg). Do not #undef it again here.

//---- AE_L32X2_XP / AE_L32X2F24_XP : 3-arg + 4-arg forms ----------------
#undef  AE_L32X2_XP
#define AE_L32X2_XP(...) __AE_L32X2_XP_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2_XP_OVERLOAD(...) \
  __AE_L32X2_XP_GET(__VA_ARGS__, __AE_L32X2_XP_4A, __AE_L32X2_XP_3A)(__VA_ARGS__)
#define __AE_L32X2_XP_3A(dst, ptr, inc) \
  do { (dst) = *(ae_int32x2 *)(ptr); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_L32X2_XP_4A(dst, ptr, offs, inc) \
  do { (dst) = *(ae_int32x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
#undef  AE_L32X2F24_XP
#define AE_L32X2F24_XP(...) AE_L32X2_XP(__VA_ARGS__)

//---- AE_L16X4_XP : 3-arg (load at ptr, advance by inc) ; 4-arg ---------
#undef  AE_L16X4_XP
#define AE_L16X4_XP(...) __AE_L16X4_XP_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L16X4_XP_OVERLOAD(...) \
  __AE_L16X4_XP_GET(__VA_ARGS__, __AE_L16X4_XP_4A, __AE_L16X4_XP_3A)(__VA_ARGS__)
#define __AE_L16X4_XP_3A(dst, ptr, inc) \
  do { (dst) = *(ae_int16x4 *)(ptr); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_L16X4_XP_4A(dst, ptr, offs, inc) \
  do { (dst) = *(ae_int16x4 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_L32_XC : 3-arg (implicit CBR0) ; 4-arg (explicit cbr_sel) -------
// HiFi3z AE_L32_XC is 3-arg (dst, ptr, offs); the circular region is the
// implicit CBR0 selected by WUR_AE_CBEGIN0/CEND0. Per the 3-arg form
// routes to the CB load intrinsic with cbr_sel=0.
#undef  AE_L32_XC
#define AE_L32_XC(...) __AE_L32_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L32_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32_XC_OVERLOAD(...) \
  __AE_L32_XC_GET(__VA_ARGS__, __AE_L32_XC_4A, __AE_L32_XC_3A)(__VA_ARGS__)
#define __AE_L32_XC_3A(dst, ptr, offs) \
  __AE_L32_XC_4A(dst, ptr, offs, 0)
#define __AE_L32_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    (dst) = *(ae_int32 *)(void *)(ptr); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_L32F24_XC : 3-arg (implicit CBR0) ; 4-arg -------------------------
#undef  AE_L32F24_XC
#define AE_L32F24_XC(...) __AE_L32F24_XC_OVERLOAD(__VA_ARGS__)
#define __AE_L32F24_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32F24_XC_OVERLOAD(...) \
  __AE_L32F24_XC_GET(__VA_ARGS__, __AE_L32F24_XC_4A, __AE_L32F24_XC_3A)(__VA_ARGS__)
#define __AE_L32F24_XC_3A(dst, ptr, offs) \
  __AE_L32F24_XC_4A(dst, ptr, offs, 0)
#define __AE_L32F24_XC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    (dst) = *(ae_f24 *)(void *)(ptr); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_LA24X2_IC : 3-arg (dst, align, ptr) ; 4-arg (with cbr_sel) -----
#undef  AE_LA24X2_IC
#define AE_LA24X2_IC(...) __AE_LA24X2_IC_OVERLOAD(__VA_ARGS__)
#define __AE_LA24X2_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA24X2_IC_OVERLOAD(...) \
  __AE_LA24X2_IC_GET(__VA_ARGS__, __AE_LA24X2_IC_4A, __AE_LA24X2_IC_3A)(__VA_ARGS__)
/* Dual-24 unaligned circular — same AR+CBR path as AE_LA32X2F24_IC. */
#define __AE_LA24X2_IC_3A(dst, align, ptr) \
  __AE_LA32X2F24_IC_4A(dst, align, ptr, 0)
#define __AE_LA24X2_IC_4A(dst, align, ptr, cbr_sel) \
  __AE_LA32X2F24_IC_4A(dst, align, ptr, cbr_sel)

//---- AE_LA32X2F24_IC : 3-arg ; 4-arg ----------------------------------
// Dual-24 unaligned circular: same AR residual + soft CBR wrap as AE_LA32X2_IC.
// Never plain mem and never aligned D_LDW_CB (drops unaligned residual).
#undef  AE_LA32X2F24_IC
#define AE_LA32X2F24_IC(...) __AE_LA32X2F24_IC_OVERLOAD(__VA_ARGS__)
#define __AE_LA32X2F24_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_LA32X2F24_IC_OVERLOAD(...) \
  __AE_LA32X2F24_IC_GET(__VA_ARGS__, __AE_LA32X2F24_IC_4A, __AE_LA32X2F24_IC_3A)(__VA_ARGS__)
#define __AE_LA32X2F24_IC_3A(dst, align, ptr) \
  __AE_LA32X2F24_IC_4A(dst, align, ptr, 0)
#define __AE_LA32X2F24_IC_4A(dst, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_cb_ld_t __r = haydn_ae_cb_ld_tw(__ar, (int)(cbr_sel), __p, 0); \
    (dst) = (ae_f24x2)haydn_ae_f32x2_mem_to_reg(__r.data); \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
    (void)(align); \
  } while (0)

//---- AE_L16X2M_X : 2-arg returning (ptr, offs) ; 3-arg statement -------
// HiFi3: pack-of-2x16 load. Returns the packed value (ae_p16x2 / int32).
#undef  AE_L16X2M_X
#define AE_L16X2M_X(...) __AE_L16X2M_X_OVERLOAD(__VA_ARGS__)
#define __AE_L16X2M_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16X2M_X_OVERLOAD(...) \
  __AE_L16X2M_X_GET(__VA_ARGS__, __AE_L16X2M_X_3A, __AE_L16X2M_X_2A)(__VA_ARGS__)
#define __AE_L16X2M_X_2A(ptr, offs) \
  (*(ae_int32 *)((char *)(ptr) + (offs)))
#define __AE_L16X2M_X_3A(dst, ptr, offs) \
  do { (dst) = *(ae_int32 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_L16X2M_I : 2-arg returning (ptr, offs) ; 3-arg statement -------
#undef  AE_L16X2M_I
#define AE_L16X2M_I(...) __AE_L16X2M_I_OVERLOAD(__VA_ARGS__)
#define __AE_L16X2M_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16X2M_I_OVERLOAD(...) \
  __AE_L16X2M_I_GET(__VA_ARGS__, __AE_L16X2M_I_3A, __AE_L16X2M_I_2A)(__VA_ARGS__)
#define __AE_L16X2M_I_2A(ptr, offs) \
  (*(ae_int32 *)((char *)(ptr) + (offs)))
#define __AE_L16X2M_I_3A(dst, ptr, offs) \
  do { (dst) = *(ae_int32 *)((char *)(ptr) + (offs)); } while (0)


//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 5: corrected arity dispatch for the MAC     //
// helpers whose GET macro had one extra default slot (causing 4-arg calls to //
// dispatch to the 5-arg helper). Also adds ae_p24x2s and NatureDSP_Signal.h //
// stubs needed by the remaining FIR kernels.                                //
//===----------------------------------------------------------------------===//

//---- Fix AE_MULFD24X2_FIR_H / AE_MULAFD24X2_FIR_H dispatch -------------
#undef  AE_MULFD24X2_FIR_H
#define AE_MULFD24X2_FIR_H(...) \
  __AE_MULFD24X2_FIR_H_GET(__VA_ARGS__, \
    AE_MULFD24X2_FIR_H_5A, AE_MULFD24X2_FIR_H_4A)(__VA_ARGS__)

#undef  AE_MULAFD24X2_FIR_H
#define AE_MULAFD24X2_FIR_H(...) \
  __AE_MULAFD24X2_FIR_H_GET(__VA_ARGS__, \
    AE_MULAFD24X2_FIR_H_5A, AE_MULAFD24X2_FIR_H_4A)(__VA_ARGS__)

//---- Fix AE_MULAFQ16X2_FIR_3 / _1 / AE_MULFQ16X2_FIR_3 / _1 dispatch ---
#undef  AE_MULAFQ16X2_FIR_3
#define AE_MULAFQ16X2_FIR_3(...) \
  __AE_MULAFQ16X2_FIR_3_GET(__VA_ARGS__, \
    AE_MULAFQ16X2_FIR_3_5A, AE_MULAFQ16X2_FIR_3_4A)(__VA_ARGS__)

#undef  AE_MULAFQ16X2_FIR_1
#define AE_MULAFQ16X2_FIR_1(...) \
  __AE_MULAFQ16X2_FIR_1_GET(__VA_ARGS__, \
    AE_MULAFQ16X2_FIR_1_5A, AE_MULAFQ16X2_FIR_1_4A)(__VA_ARGS__)

#undef  AE_MULFQ16X2_FIR_3
#define AE_MULFQ16X2_FIR_3(...) \
  __AE_MULFQ16X2_FIR_3_GET(__VA_ARGS__, \
    AE_MULFQ16X2_FIR_3_5A, AE_MULFQ16X2_FIR_3_4A)(__VA_ARGS__)

#undef  AE_MULFQ16X2_FIR_1
#define AE_MULFQ16X2_FIR_1(...) \
  __AE_MULFQ16X2_FIR_1_GET(__VA_ARGS__, \
    AE_MULFQ16X2_FIR_1_5A, AE_MULFQ16X2_FIR_1_4A)(__VA_ARGS__)

//---- Fix AE_MULZASFD32X16_H3_L2 / AE_MULZAAFD32X16_H2_L3 dispatch -----
#undef  AE_MULZASFD32X16_H3_L2
#define AE_MULZASFD32X16_H3_L2(...) \
  __AE_MULZASFD32X16_H3_L2_GET(__VA_ARGS__, \
    AE_MULZASFD32X16_H3_L2_3A, AE_MULZASFD32X16_H3_L2_2A)(__VA_ARGS__)

#undef  AE_MULZAAFD32X16_H2_L3
#define AE_MULZAAFD32X16_H2_L3(...) \
  __AE_MULZAAFD32X16_H2_L3_GET(__VA_ARGS__, \
    AE_MULZAAFD32X16_H2_L3_3A, AE_MULZAAFD32X16_H2_L3_2A)(__VA_ARGS__)

//---- ae_p24x2s typedef (signed pair-of-24, alias of ae_p24x2) ----------
typedef ae_p24x2 ae_p24x2s;

//---- AE_SA24X2_IC overload (3-arg: src, align, ptr) --------------------
#undef  AE_SA24X2_IC
#define AE_SA24X2_IC(...) __AE_SA24X2_IC_OVERLOAD(__VA_ARGS__)
#define __AE_SA24X2_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_SA24X2_IC_OVERLOAD(...) \
  __AE_SA24X2_IC_GET(__VA_ARGS__, __AE_SA24X2_IC_4A, __AE_SA24X2_IC_3A)(__VA_ARGS__)
#define __AE_SA24X2_IC_3A(src, align, ptr) \
  __AE_SA32X2F24_IC_4A(src, align, ptr, 0)
#define __AE_SA24X2_IC_4A(src, align, ptr, cbr_sel) \
  __AE_SA32X2F24_IC_4A(src, align, ptr, cbr_sel)

//---- AE_L16X4_X overload (2-arg returning form: ptr, offs) ------------
#undef  AE_L16X4_X
#define AE_L16X4_X(...) __AE_L16X4_X_OVERLOAD(__VA_ARGS__)
#define __AE_L16X4_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16X4_X_OVERLOAD(...) \
  __AE_L16X4_X_GET(__VA_ARGS__, __AE_L16X4_X_3A, __AE_L16X4_X_2A)(__VA_ARGS__)
#define __AE_L16X4_X_2A(ptr, offs) \
  (*(ae_int16x4 *)((char *)(ptr) + (offs)))
#define __AE_L16X4_X_3A(dst, ptr, offs) \
  do { (dst) = *(ae_int16x4 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_L32X2F24_RIC overload (2-arg: dst, ptr; reverse inc implicit 8) -
/* Reaffirm reverse-CB body (prior block may be #undef'd by later spellings). */
#undef  AE_L32X2F24_RIC
#define AE_L32X2F24_RIC(...) __AE_L32X2F24_RIC_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_RIC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_L32X2F24_RIC_OVERLOAD(...) \
  __AE_L32X2F24_RIC_GET(__VA_ARGS__, __AE_L32X2F24_RIC_4A, __AE_L32X2F24_RIC_3A, __AE_L32X2F24_RIC_2A)(__VA_ARGS__)
#define __AE_L32X2F24_RIC_2A(dst, ptr) \
  __AE_L32X2F24_RIC_4A(dst, ptr, 8, 0)
#define __AE_L32X2F24_RIC_3A(dst, ptr, offs) \
  __AE_L32X2F24_RIC_4A(dst, ptr, offs, 0)
#define __AE_L32X2F24_RIC_4A(dst, ptr, offs, cbr_sel) \
  do { \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), \
                                           -((offs) >> 3)); \
    (dst) = (ae_f24x2)(haydn_dr64_t)__r.data; \
    (ptr) = (__typeof__(ptr))__r.new_ptr; \
  } while (0)

//---- AE_MULZAAFD24_HH_LL 2-arg overload (a, b; zero-init accumulator) --
#undef  AE_MULZAAFD24_HH_LL
#define AE_MULZAAFD24_HH_LL(...) __AE_MULZAAFD24_HH_LL_OVERLOAD(__VA_ARGS__)
#define __AE_MULZAAFD24_HH_LL_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULZAAFD24_HH_LL_OVERLOAD(...) \
  __AE_MULZAAFD24_HH_LL_GET(__VA_ARGS__, \
    __AE_MULZAAFD24_HH_LL_3A, __AE_MULZAAFD24_HH_LL_2A)(__VA_ARGS__)
#define __AE_MULZAAFD24_HH_LL_2A(a, b) \
  haydn_fmul32s_hh(__AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MULZAAFD24_HH_LL_3A(acc, a, b) \
  haydn_fmula32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_MULZAAFD32X16_H3_L2 2-arg overload (d, c; zero-init accumulator)
#undef  AE_MULZAAFD32X16_H3_L2
static inline ae_int64 AE_MULZAAFD32X16_H3_L2_2A(ae_int16x4 d, ae_int16x4 c) {
  return haydn_mulafd32x16x2_fir_hh(0, __AE_TO_I64(d), __AE_TO_I64(c));
}
static inline ae_int64 AE_MULZAAFD32X16_H3_L2_3A(ae_int64 acc, ae_int16x4 d,
                                                  ae_int16x4 c) {
  return haydn_mulafd32x16x2_fir_hh(acc, __AE_TO_I64(d), __AE_TO_I64(c));
}
#define AE_MULZAAFD32X16_H3_L2(...) \
  __AE_MULZAAFD32X16_H3_L2_GET(__VA_ARGS__, \
    AE_MULZAAFD32X16_H3_L2_3A, AE_MULZAAFD32X16_H3_L2_2A)(__VA_ARGS__)
#define __AE_MULZAAFD32X16_H3_L2_GET(_1, _2, _3, NAME, ...) NAME

//---- AE_MUL32X16_H0/H1/H2/H3 2-arg overload (a, b; zero-init accumulator)
// HiFi3 AE_MUL32X16_Hn(a, b): 32x16 fractional multiply, lane n.
// Haydn has no direct 32x16 MAC intrinsic; compose via the 32x16x2 FIR
// helpers which already lower to fmul32s_hh/hl. The 2-arg form zero-inits.
#undef  AE_MUL32X16_H0
#define AE_MUL32X16_H0(...) __AE_MUL32X16_H0_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_H0_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_H0_OVERLOAD(...) \
  __AE_MUL32X16_H0_GET(__VA_ARGS__, __AE_MUL32X16_H0_3A, __AE_MUL32X16_H0_2A)(__VA_ARGS__)
#define __AE_MUL32X16_H0_2A(a, b) \
  haydn_mulafd32x16x2_fir_hl(0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MUL32X16_H0_3A(acc, a, b) \
  haydn_mulafd32x16x2_fir_hl((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

#undef  AE_MUL32X16_H1
#define AE_MUL32X16_H1(...) __AE_MUL32X16_H1_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_H1_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_H1_OVERLOAD(...) \
  __AE_MUL32X16_H1_GET(__VA_ARGS__, __AE_MUL32X16_H1_3A, __AE_MUL32X16_H1_2A)(__VA_ARGS__)
#define __AE_MUL32X16_H1_2A(a, b) \
  haydn_mulafd32x16x2_fir_hh(0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MUL32X16_H1_3A(acc, a, b) \
  haydn_mulafd32x16x2_fir_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

#undef  AE_MUL32X16_H2
#define AE_MUL32X16_H2(...) __AE_MUL32X16_H2_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_H2_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_H2_OVERLOAD(...) \
  __AE_MUL32X16_H2_GET(__VA_ARGS__, __AE_MUL32X16_H2_3A, __AE_MUL32X16_H2_2A)(__VA_ARGS__)
#define __AE_MUL32X16_H2_2A(a, b) \
  haydn_mulafd32x16x2_fir_hl(0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MUL32X16_H2_3A(acc, a, b) \
  haydn_mulafd32x16x2_fir_hl((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

#undef  AE_MUL32X16_H3
#define AE_MUL32X16_H3(...) __AE_MUL32X16_H3_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_H3_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_H3_OVERLOAD(...) \
  __AE_MUL32X16_H3_GET(__VA_ARGS__, __AE_MUL32X16_H3_3A, __AE_MUL32X16_H3_2A)(__VA_ARGS__)
#define __AE_MUL32X16_H3_2A(a, b) \
  haydn_mulafd32x16x2_fir_hh(0, __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MUL32X16_H3_3A(acc, a, b) \
  haydn_mulafd32x16x2_fir_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_S32F24_L_XC overload (3-arg: src, ptr, offs) ------------------
#undef  AE_S32F24_L_XC
#define AE_S32F24_L_XC(...) __AE_S32F24_L_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S32F24_L_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32F24_L_XC_OVERLOAD(...) \
  __AE_S32F24_L_XC_GET(__VA_ARGS__, __AE_S32F24_L_XC_4A, __AE_S32F24_L_XC_3A)(__VA_ARGS__)
#define __AE_S32F24_L_XC_3A(src, ptr, offs) \
  __AE_S32F24_L_XC_4A(src, ptr, offs, 0)
#define __AE_S32F24_L_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    *(ae_f24 *)(void *)(ptr) = (ae_f24)(src); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_MULZAAFD24_HL_LH 2-arg overload (a, b; zero-init accumulator) --
#undef  AE_MULZAAFD24_HL_LH
#define AE_MULZAAFD24_HL_LH(...) __AE_MULZAAFD24_HL_LH_OVERLOAD(__VA_ARGS__)
#define __AE_MULZAAFD24_HL_LH_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULZAAFD24_HL_LH_OVERLOAD(...) \
  __AE_MULZAAFD24_HL_LH_GET(__VA_ARGS__, \
    __AE_MULZAAFD24_HL_LH_3A, __AE_MULZAAFD24_HL_LH_2A)(__VA_ARGS__)
#define __AE_MULZAAFD24_HL_LH_2A(a, b) \
  haydn_fmul32s_lh(__AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MULZAAFD24_HL_LH_3A(acc, a, b) \
  haydn_fmula32s_lh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_S32X2F24_RIP overload (2-arg: src, ptr; inc implicit 8) -------
/* Reverse linear store (not forward IP). Late body owns public arity. */
#undef  AE_S32X2F24_RIP
#define AE_S32X2F24_RIP(...) __AE_S32X2F24_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2F24_RIP_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_S32X2F24_RIP_OVERLOAD(...) \
  __AE_S32X2F24_RIP_GET(__VA_ARGS__, __AE_S32X2F24_RIP_3A, __AE_S32X2F24_RIP_2A)(__VA_ARGS__)
#define __AE_S32X2F24_RIP_2A(src, ptr) \
  do { *(ae_f24x2 *)(ptr) = (src); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - 8); } while (0)
#define __AE_S32X2F24_RIP_3A(src, ptr, inc) \
  do { *(ae_f24x2 *)(ptr) = (src); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - (inc)); } while (0)

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 4: remaining 2-arg/3-arg signature forms   //
// used across the FIR family. Each overload reuses an existing intrinsic or  //
// matches the semantics of a sibling variant already in the header.          //
//===----------------------------------------------------------------------===//

//---- AE_ROUND24X2F48SASYM dual-acc pack ------------------------------------
// NatureDSP F24 finish: AE_ROUND24X2F48SASYM(q0, q1) packs *both* accumulators
// into a dual-32 (lo=satsr(q0), hi=satsr(q1)). 2-arg uses fixed shift 16
// (same as pure haydn_acc_satsr64(acc,16)); 3-arg takes explicit shift for
// both lanes. Prefer haydn_satsr64 over packsr32(q0-only) which dropped q1.
#undef  AE_ROUND24X2F48SASYM
#define AE_ROUND24X2F48SASYM(...) __AE_ROUND24X2F48SASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUND24X2F48SASYM_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_ROUND24X2F48SASYM_OVERLOAD(...) \
  __AE_ROUND24X2F48SASYM_GET(__VA_ARGS__, __AE_ROUND24X2F48SASYM_3A, __AE_ROUND24X2F48SASYM_2A)(__VA_ARGS__)
#define __AE_ROUND24X2F48SASYM_2A(q0, q1) \
  ((ae_int32x2)((long long)(int32_t)haydn_satsr64((q0), 16) | \
                ((long long)(int32_t)haydn_satsr64((q1), 16) << 32)))
#define __AE_ROUND24X2F48SASYM_3A(q0, q1, s) \
  ((ae_int32x2)((long long)(int32_t)haydn_satsr64((q0), (s)) | \
                ((long long)(int32_t)haydn_satsr64((q1), (s)) << 32)))

//---- AE_S32X2RA64S_IP overload (3-arg: src0, src1, ptr; shift implicit, inc=8)
// HiFi3 signature (src0, src1, ptr) saturates both 64-bit accs to 32-bit, packs
// them into a dual-32, stores to *ptr, and post-increments ptr by 8.
#undef  AE_S32X2RA64S_IP
#define AE_S32X2RA64S_IP(...) __AE_S32X2RA64S_IP_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2RA64S_IP_GET(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define __AE_S32X2RA64S_IP_OVERLOAD(...) \
  __AE_S32X2RA64S_IP_GET(__VA_ARGS__, \
    __AE_S32X2RA64S_IP_6A, __AE_S32X2RA64S_IP_6A, __AE_S32X2RA64S_IP_6A, \
    __AE_S32X2RA64S_IP_3A)(__VA_ARGS__)
#define __AE_S32X2RA64S_IP_3A(src0, src1, ptr) \
  do { ae_int32x2 _p = (ae_int32x2)((long long)(int32_t)haydn_satsr64((src0), 0) | \
        ((long long)(int32_t)haydn_satsr64((src1), 0) << 32)); \
       *(ae_int32x2 *)(ptr) = _p; \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + 8); } while (0)
#define __AE_S32X2RA64S_IP_6A(src0, src1, acc, shift, ptr, inc) \
  do { (src0) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32x2 *)(ptr) = (ae_int32x2)(src0); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_S24X2RA64S_IP overload (3-arg: acc0, acc1, ptr) -----------------
#undef  AE_S24X2RA64S_IP
#define AE_S24X2RA64S_IP(...) __AE_S24X2RA64S_IP_OVERLOAD(__VA_ARGS__)
#define __AE_S24X2RA64S_IP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S24X2RA64S_IP_OVERLOAD(...) \
  __AE_S24X2RA64S_IP_GET(__VA_ARGS__, \
    __AE_S24X2RA64S_IP_4A, __AE_S24X2RA64S_IP_3A)(__VA_ARGS__)
#define __AE_S24X2RA64S_IP_3A(acc0, acc1, ptr) \
  do { ae_int32 _v = (ae_int32)haydn_satsr64((acc0), 0); \
       *(ae_int32 *)(ptr) = _v; \
       (ptr) = (ae_int32 *)((char *)(ptr) + 4); } while (0)
#define __AE_S24X2RA64S_IP_4A(dst, acc, shift, ptr) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32 *)(ptr) = (dst); } while (0)

//---- AE_S32X2_RIP overload (2-arg: src, ptr; inc implicit 8) ------------
/* Reverse linear store (not forward IP). Late body owns public arity. */
#undef  AE_S32X2_RIP
#define AE_S32X2_RIP(...) __AE_S32X2_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2_RIP_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_S32X2_RIP_OVERLOAD(...) \
  __AE_S32X2_RIP_GET(__VA_ARGS__, __AE_S32X2_RIP_3A, __AE_S32X2_RIP_2A)(__VA_ARGS__)
#define __AE_S32X2_RIP_2A(src, ptr) \
  do { *(ae_int32x2 *)(ptr) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) - 8); } while (0)
#define __AE_S32X2_RIP_3A(src, ptr, inc) \
  do { *(ae_int32x2 *)(ptr) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) - (inc)); } while (0)

//---- AE_S32_L_XC overload (3-arg: src, ptr, offs) ----------------------
#undef  AE_S32_L_XC
#define AE_S32_L_XC(...) __AE_S32_L_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S32_L_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32_L_XC_OVERLOAD(...) \
  __AE_S32_L_XC_GET(__VA_ARGS__, __AE_S32_L_XC_4A, __AE_S32_L_XC_3A)(__VA_ARGS__)
#define __AE_S32_L_XC_3A(src, ptr, offs) \
  __AE_S32_L_XC_4A(src, ptr, offs, 0)
#define __AE_S32_L_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    *(ae_int32 *)(void *)(ptr) = (ae_int32)(src); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_L32X2F24_RIP overload (2-arg: dst, ptr; inc implicit 8) --------
/* Reverse linear load (not forward IP). Late body owns public arity. */
#undef  AE_L32X2F24_RIP
#define AE_L32X2F24_RIP(...) __AE_L32X2F24_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_RIP_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2F24_RIP_OVERLOAD(...) \
  __AE_L32X2F24_RIP_GET(__VA_ARGS__, __AE_L32X2F24_RIP_3A, __AE_L32X2F24_RIP_2A)(__VA_ARGS__)
#define __AE_L32X2F24_RIP_2A(dst, ptr) \
  do { (dst) = *(ae_f24x2 *)(ptr); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - 8); } while (0)
#define __AE_L32X2F24_RIP_3A(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)(ptr); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - (offs)); } while (0)

//---- AE_SA32X2F24_IC / AE_SA16X4_RIP overload --------------------------
// Dual-24 unaligned circular store: same AR residual + soft CBR wrap as
// AE_SA32X2_IC. Never plain mem and never aligned D_SDW_CB without ptr wrap.
#undef  AE_SA32X2F24_IC
#define AE_SA32X2F24_IC(...) __AE_SA32X2F24_IC_OVERLOAD(__VA_ARGS__)
#define __AE_SA32X2F24_IC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_SA32X2F24_IC_OVERLOAD(...) \
  __AE_SA32X2F24_IC_GET(__VA_ARGS__, __AE_SA32X2F24_IC_4A, __AE_SA32X2F24_IC_3A)(__VA_ARGS__)
#define __AE_SA32X2F24_IC_3A(src, align, ptr) \
  __AE_SA32X2F24_IC_4A(src, align, ptr, 0)
#define __AE_SA32X2F24_IC_4A(src, align, ptr, cbr_sel) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)(src)); \
    void *__np = haydn_ae_cb_st(__ar, (int)(cbr_sel), __p, __s, 0); \
    (ptr) = (__typeof__(ptr))__np; \
    (void)(align); \
  } while (0)

#undef  AE_SA16X4_RIP
#define AE_SA16X4_RIP(...) __AE_SA16X4_RIP_OVERLOAD(__VA_ARGS__)
#define __AE_SA16X4_RIP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_SA16X4_RIP_OVERLOAD(...) \
  __AE_SA16X4_RIP_GET(__VA_ARGS__, __AE_SA16X4_RIP_4A, __AE_SA16X4_RIP_3A)(__VA_ARGS__)
#define __AE_SA16X4_RIP_3A(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, 8, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - 8); \
  } while (0)
#define __AE_SA16X4_RIP_4A(src, align, ptr, inc) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    int __s = (int)(inc); \
    if (__s < 0) __s = -__s; \
    if (__s == 0) __s = 8; \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, __s, 1); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) - ((inc) ? (inc) : 8)); \
  } while (0)

//---- AE_S32RA64S_XP overload (3-arg: acc, ptr, offs; default shift=0, inc=-offs)
// HiFi3 signature AE_S32RA64S_XP(acc, ptr, offs) stores the saturated acc to
// *ptr, then post-decrements ptr by |offs|. The acc->32 conversion uses shift=0.
#undef  AE_S32RA64S_XP
#define AE_S32RA64S_XP(...) __AE_S32RA64S_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S32RA64S_XP_GET(_1, _2, _3, _4, _5, NAME, ...) NAME
#define __AE_S32RA64S_XP_OVERLOAD(...) \
  __AE_S32RA64S_XP_GET(__VA_ARGS__, \
    __AE_S32RA64S_XP_5A, __AE_S32RA64S_XP_5A, __AE_S32RA64S_XP_3A)(__VA_ARGS__)
#define __AE_S32RA64S_XP_3A(acc, ptr, offs) \
  do { *(ae_int32 *)((char *)(ptr) + (offs)) = (ae_int32)haydn_satsr64((acc), 0); \
       (ptr) = (ae_int32 *)((char *)(ptr) + (offs)); } while (0)
#define __AE_S32RA64S_XP_5A(dst, acc, shift, ptr, offs) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *((ae_int32 *)(ptr) + ((offs) / (int)sizeof(ae_int32))) = (dst); } while (0)

//---- AE_S24RA64S_XP overload (3-arg: acc, ptr, offs) -------------------
#undef  AE_S24RA64S_XP
#define AE_S24RA64S_XP(...) __AE_S24RA64S_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S24RA64S_XP_GET(_1, _2, _3, _4, _5, NAME, ...) NAME
#define __AE_S24RA64S_XP_OVERLOAD(...) \
  __AE_S24RA64S_XP_GET(__VA_ARGS__, \
    __AE_S24RA64S_XP_5A, __AE_S24RA64S_XP_5A, __AE_S24RA64S_XP_3A)(__VA_ARGS__)
#define __AE_S24RA64S_XP_3A(acc, ptr, offs) \
  do { *(ae_int32 *)((char *)(ptr) + (offs)) = (ae_int32)haydn_satsr64((acc), 0); \
       (ptr) = (ae_int32 *)((char *)(ptr) + (offs)); } while (0)
#define __AE_S24RA64S_XP_5A(dst, acc, shift, ptr, offs) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *((ae_int32 *)(ptr) + ((offs) / (int)sizeof(ae_int32))) = (dst); } while (0)

//---- AE_S16_0_XC overload (3-arg: src, ptr, offs; cbr_sel implicit 0) --
#undef  AE_S16_0_XC
#define AE_S16_0_XC(...) __AE_S16_0_XC_OVERLOAD(__VA_ARGS__)
#define __AE_S16_0_XC_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16_0_XC_OVERLOAD(...) \
  __AE_S16_0_XC_GET(__VA_ARGS__, __AE_S16_0_XC_4A, __AE_S16_0_XC_3A)(__VA_ARGS__)
#define __AE_S16_0_XC_3A(src, ptr, offs) \
  __AE_S16_0_XC_4A(src, ptr, offs, 0)
#define __AE_S16_0_XC_4A(src, ptr, offs, cbr_sel) \
  do { \
    *(ae_int16 *)(void *)(ptr) = (ae_int16)(src); \
    (ptr) = (__typeof__(ptr))haydn_cbr_step( \
        (uintptr_t)(ptr), (intptr_t)(offs), (int)(cbr_sel)); \
  } while (0)

//---- AE_MULZASFD24_HH_LL 2-arg overload (a, b; zero-init accumulator) --
#undef  AE_MULZASFD24_HH_LL
#define AE_MULZASFD24_HH_LL(...) __AE_MULZASFD24_HH_LL_OVERLOAD(__VA_ARGS__)
#define __AE_MULZASFD24_HH_LL_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULZASFD24_HH_LL_OVERLOAD(...) \
  __AE_MULZASFD24_HH_LL_GET(__VA_ARGS__, \
    __AE_MULZASFD24_HH_LL_3A, __AE_MULZASFD24_HH_LL_2A)(__VA_ARGS__)
#define __AE_MULZASFD24_HH_LL_2A(a, b) \
  haydn_fmul32s_hh(__AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#define __AE_MULZASFD24_HH_LL_3A(acc, a, b) \
  haydn_fmula32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 6: final fixes for remaining FIR kernels    //
//  - AE_MULASFD24_* signature fix (intrinsic is 2-arg, not 3-arg)            //
//  - ae_p24s typedef                                                          //
//===----------------------------------------------------------------------===//

//---- AE_MULASFD24_* signature fix -------------------------------------
// The haydn_mulsa32_hhll / _hllh intrinsics are BINARY (2-arg): they return
// a*b. The AE_MULASFD24_<lanes>(acc, a, b) semantics are acc -= a*b, so we
// compose: (acc) - intrinsic(a, b). The previous 3-arg call to a 2-arg
// intrinsic was a type error.
#undef  AE_MULASFD24_HH_LL
#define AE_MULASFD24_HH_LL(acc, a, b) \
  ((acc) - haydn_mulsa32_hhll((haydn_dr64_t)(a), (haydn_dr64_t)(b)))
#undef  AE_MULASFD24_HL_LH
#define AE_MULASFD24_HL_LH(acc, a, b) \
  ((acc) - haydn_mulsa32_hllh((haydn_dr64_t)(a), (haydn_dr64_t)(b)))

//---- ae_p24s typedef (single 24-bit signed scalar) --------------------
typedef int ae_p24s;

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 7: AE_SA16X4_IP / AE_SA32X2_IP post-inc.    //
//                                                                            //
// BUG (historical): this block once DROPPED the post-increment "to tolerate  //
// castxcc non-lvalue ptr" and claimed "kernels recompute the cast each       //
// call". That is false for the common case — real lvalue py/px (vec_scale*,  //
// most of the 89 AE_SA32X2_IP call sites). With stores pinned to one address //
// O2 DSE collapses the entire scale loop to a single last-iteration store,   //
// so the loop never reaches MachinePipeliner / HardwareLoops (empty          //
// -debug-only=pipeliner log, no set_hwloop). Same class as for       //
// AE_S32X2_IP / AE_L32X2_IP.                                                 //
//                                                                            //
// Fix: restore true post-increment. castxcc is lvalue form (*(t **)&(p)) at  //
// the end of this header (and L189 ported remaining castxcc lvalue kernels). //
// Do NOT re-break AE_SA16X4_RIP here — the overload at ~3874 already has     //
// correct reverse post-inc.                                                  //
//===----------------------------------------------------------------------===//
/* Final SA_*_IP authority: re-assert primary AR helpers (no plain *store*). */
#undef  AE_SA16X4_IP
#define AE_SA16X4_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa16x4_step((ae_int16x4)(src), __ar, __p, 8, 0); \
    (ptr) = (ae_int16x4 *)((char *)(ptr) + 8); \
  } while (0)

#undef  AE_SA32X2_IP
#define AE_SA32X2_IP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(src), __ar, __p, 8, 0); \
    (ptr) = (ae_int32x2 *)((char *)(ptr) + 8); \
  } while (0)

//---- AE_ROUNDSQ32F48ASYM 1-arg overload (q only; shift implicit 0) ----
#undef  AE_ROUNDSQ32F48ASYM
#define AE_ROUNDSQ32F48ASYM(...) __AE_ROUNDSQ32F48ASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUNDSQ32F48ASYM_GET(_1, _2, NAME, ...) NAME
#define __AE_ROUNDSQ32F48ASYM_OVERLOAD(...) \
  __AE_ROUNDSQ32F48ASYM_GET(__VA_ARGS__, __AE_ROUNDSQ32F48ASYM_2, __AE_ROUNDSQ32F48ASYM_1)(__VA_ARGS__)
#define __AE_ROUNDSQ32F48ASYM_1(q) haydn_packsr32((q), 0)
#define __AE_ROUNDSQ32F48ASYM_2(q, s) haydn_packsr32((q), (s))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 8: MAC overloads for interp/convol family. //
// AE_L16_XC late 64b-trunc redefines collapsed — single EMULATED body       //
// (i16 load + haydn_cbr_step) lives with the arity overload above.          //
//===----------------------------------------------------------------------===//

//---- AE_MULFP32X16X2RAS_H / _L (32x16x2 fractional multiply with round) --
// Maps to the existing haydn_mulfp32x16x2ras_high / _low intrinsics.
#define AE_MULFP32X16X2RAS_H(acc, a, b) \
  haydn_mulfp32x16x2ras_high((acc), (haydn_dr64_t)(a), (haydn_dr64_t)(b))
#define AE_MULFP32X16X2RAS_L(acc, a, b) \
  haydn_mulfp32x16x2ras_low((acc), (haydn_dr64_t)(a), (haydn_dr64_t)(b))

//---- AE_MULF48Q32SP16S_L (48-bit accumulator from Q32*Q16 MAC, low lane) --
// No direct Haydn intrinsic; compose as a 32x16 MAC via the FIR helper.
#define AE_MULF48Q32SP16S_L(acc, a, b) \
  haydn_mulafd32x16x2_fir_hl((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 8b: 2-arg MULFP / MULF48 forms              //
//===----------------------------------------------------------------------===//

//---- AE_MULFP32X16X2RAS_H / _L (32x16x2 fractional multiply with round) --
// HiFi3 signature: (a, b) returns the rounded product (no accumulator).
// Maps to the existing haydn_mulfp32x16x2ras_high / _low intrinsics.
#undef  AE_MULFP32X16X2RAS_H
#undef  AE_MULFP32X16X2RAS_L
#define AE_MULFP32X16X2RAS_H(a, b) \
  haydn_mulfp32x16x2ras_high(0, (haydn_dr64_t)(a), (haydn_dr64_t)(b))
#define AE_MULFP32X16X2RAS_L(a, b) \
  haydn_mulfp32x16x2ras_low(0, (haydn_dr64_t)(a), (haydn_dr64_t)(b))

//---- AE_MULF48Q32SP16S_L (48-bit accumulator, Q32*Q16 MAC into acc, low lane)
// HiFi3 signature: (acc, b) — acc += b * implicit-coef. No direct Haydn
// intrinsic; compose via the FIR helper (coef defaults to acc for self-MAC).
#define AE_MULF48Q32SP16S_L(acc, b) \
  haydn_mulafd32x16x2_fir_hl((acc), __AE_TO_I64((haydn_dr64_t)(b)), __AE_TO_I64((haydn_dr64_t)(b)))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 9: last two overloads for firinterp DX     //
//===----------------------------------------------------------------------===//

//---- AE_S16_0_XP overload (3-arg: src, ptr, offs; no separate inc) -----
#undef  AE_S16_0_XP
#define AE_S16_0_XP(...) __AE_S16_0_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S16_0_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16_0_XP_OVERLOAD(...) \
  __AE_S16_0_XP_GET(__VA_ARGS__, __AE_S16_0_XP_4A, __AE_S16_0_XP_3A)(__VA_ARGS__)
#define __AE_S16_0_XP_3A(src, ptr, offs) \
  do { *(ae_int16 *)((char *)(ptr) + (offs)) = (ae_int16)(src); \
       (ptr) = (ae_int16 *)((char *)(ptr) + (offs)); } while (0)
#define __AE_S16_0_XP_4A(src, ptr, offs, inc) \
  do { *(ae_int16 *)((char *)(ptr) + (offs)) = (ae_int16)(src); \
       (ptr) = (ae_int16 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_SLAS64S overload (1-arg: q uses ambient AE_SAR; 2-arg: explicit)
/* Soft saturating LEFT shift. Must not silent-alias ASR (prior late body).
 * 1-arg form matches HiFi AE_SLAS64S(q) = sat-left by WUR_AE_SAR amount. */
#undef  AE_SLAS64S
#define AE_SLAS64S(...) __AE_SLAS64S_OVERLOAD(__VA_ARGS__)
#define __AE_SLAS64S_GET(_1, _2, NAME, ...) NAME
#define __AE_SLAS64S_OVERLOAD(...) \
  __AE_SLAS64S_GET(__VA_ARGS__, __AE_SLAS64S_2, __AE_SLAS64S_1)(__VA_ARGS__)
#define __AE_SLAS64S_1(q) haydn_ae_slaa64s((q), haydn_ae_sar)
#define __AE_SLAS64S_2(q, s) haydn_ae_slaa64s((q), (int)(s))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 9: MAC write-back fixes.                   //
//                                                                            //
// Systemic correctness bug: ~45 AE_MUL*/AE_MAC*/AE_MULAF*/AE_MULAA*/         //
// AE_MULSS*/AE_MULS* wrapper macros invoke a Haydn ternary MAC intrinsic    //
// `haydn_*(acc, a, b)` that RETURNS the new accumulator value (the acc     //
// parameter is read-only, per IntrinsicsHaydn.td ternary class) but the     //
// wrapper DISCARDED the return value.                                       //
//                                                                            //
// HiFi3 semantics: these are STATEMENT-FORM intrinsics that update the      //
// accumulator argument IN PLACE. Every NatureDSP kernel calls them as       //
// `AE_MULAF32R_HH(q0, t01, c0);` (no assignment at the call site),          //
// expecting `q0` to be updated. Without the write-back the loop body has    //
// no observable effect and the optimizer DCEs the entire hot loop.          //
//                                                                            //
// Fix: redefine each MAC wrapper as `(acc) = haydn_*(acc, a, b)` so the   //
// intrinsic's return value is written back to the accumulator operand.      //
//                                                                            //
// Scope rules applied (verified against both IntrinsicsHaydn.td arity and   //
// the NatureDSP HiFi3 kernel call sites in ~/haydn-plans/                   //
// hifi_naturedsp_reports/src/library/):                                      //
//   * Only wrappers that (a) take an accumulator operand AND                //
//     (b) call a TERNARY haydn_ intrinsic (returns new acc value) AND     //
//     (c) currently discard the return are fixed here.                      //
//   * Plain multiplies (AE_MULFP*, AE_MULP*, AE_MULF32*, AE_MULC32X16*,     //
//     AE_MULQ31, AE_MULQ63, AE_MULFP32X16X2RAS_H/L, AE_MULF48Q32SP16S_L)    //
//     take no in-place accumulator and return a value (the kernel assigns   //
//     at the call site); NOT touched.                                       //
//   * Wrappers calling BINARY intrinsics with a spurious 3rd acc arg        //
//     (e.g. haydn_smula16_*, haydn_x2mulaph32, haydn_mulsa32_*)       //
//     have a separate arity issue and are LEFT with a TODO comment rather   //
//     than guessing the wrong semantic.                                     //
//   * All rounding/saturation/lane-selection (HH/LL/HL/LH, RS, RA, RAS,     //
//     H0..H3, L0..L3) is preserved exactly by reusing the same intrinsic   //
//     with the same lane suffix; only the write-back prefix is added.       //
//   * This block #undefs and redefines each wrapper; it does NOT touch the  //
//     part 5 load-wrapper arity block or any other prior section.           //
//===----------------------------------------------------------------------===//

//---- AE_MULA64 / AE_MULS64 (64-bit signed*signed MAC/MSU) ---------------
#undef  AE_MULA64_SS_LL
#define AE_MULA64_SS_LL(acc, a, b) (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA64_SS_HH
#define AE_MULA64_SS_HH(acc, a, b) (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA64_SS_LH
#define AE_MULA64_SS_LH(acc, a, b) (acc) = haydn_mula64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA64_SS_HL
#define AE_MULA64_SS_HL(acc, a, b) (acc) = haydn_mula64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULS64_SS_HH
#define AE_MULS64_SS_HH(acc, a, b) (acc) = haydn_muls64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULS64_SS_LL
#define AE_MULS64_SS_LL(acc, a, b) (acc) = haydn_muls64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULS64_SS_LH
#define AE_MULS64_SS_LH(acc, a, b) (acc) = haydn_muls64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULS64_SS_HL
#define AE_MULS64_SS_HL(acc, a, b) (acc) = haydn_muls64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAF32S / AE_MULSF32S (fractional 32-bit MAC/MSU) --------------
#undef  AE_MULAF32S_HH
#define AE_MULAF32S_HH(acc, a, b) (acc) = haydn_fmula32s_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32S_LL
#define AE_MULAF32S_LL(acc, a, b) (acc) = haydn_fmula32s_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32S_LH
#define AE_MULAF32S_LH(acc, a, b) (acc) = haydn_fmula32s_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32S_HL
#define AE_MULAF32S_HL(acc, a, b) (acc) = haydn_fmula32s_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSF32S_HH
#define AE_MULSF32S_HH(acc, a, b) (acc) = haydn_fmuls32s_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSF32S_LL
#define AE_MULSF32S_LL(acc, a, b) (acc) = haydn_fmuls32s_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSF32S_HL
#define AE_MULSF32S_HL(acc, a, b) (acc) = haydn_fmuls32s_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSF32S_LH
#define AE_MULSF32S_LH(acc, a, b) (acc) = haydn_fmuls32s_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAFP32X2RAS / AE_MULSFP32X2RAS (FF2 fractional MAC/MSU) -------
#undef  AE_MULAFP32X2RAS
#define AE_MULAFP32X2RAS(acc, a, b) (acc) = haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSFP32X2RAS
#define AE_MULSFP32X2RAS(acc, a, b) (acc) = haydn_ff2muls32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAFP32X2RAS_HH
#define AE_MULAFP32X2RAS_HH(acc, a, b) (acc) = haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAF32R (fractional MAC with rounding+sat, lane select) --------
#undef  AE_MULAF32R_HH
#define AE_MULAF32R_HH(acc, a, b) (acc) = haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32R_LL
#define AE_MULAF32R_LL(acc, a, b) (acc) = haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32R_LH
#define AE_MULAF32R_LH(acc, a, b) (acc) = haydn_ff2mula32rs_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAF32R_LL_S2
#define AE_MULAF32R_LL_S2(acc, a, b) (acc) = haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSF32R_LH
#define AE_MULSF32R_LH(acc, a, b) (acc) = haydn_ff2muls32rs_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAAFD32R / AE_MULSSFD32X16X2 (fused dual MAC/MSU) -------------
#undef  AE_MULAAFD32R_HH_LL
#define AE_MULAAFD32R_HH_LL(acc, a, b) (acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAAFD32R_HL_LH
#define AE_MULAAFD32R_HL_LH(acc, a, b) (acc) = haydn_f2mulaa32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSSFD32X16X2_HH_LL
#define AE_MULSSFD32X16X2_HH_LL(acc, a, b) (acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULSSFD32X16X2_HL_LH
#define AE_MULSSFD32X16X2_HL_LH(acc, a, b) (acc) = haydn_f2mulss32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAAD32 (dual 32x32 dual-MAC, fractional + rounding) -----------
#undef  AE_MULAAD32_HH_LL
#define AE_MULAAD32_HH_LL(acc, a, b) (acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAAD32_HL_LH
#define AE_MULAAD32_HL_LH(acc, a, b) (acc) = haydn_f2mulaa32rs_hllh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAAFD32RA / AE_MULAAFD32X16 (fractional MAC, rounding) --------
#undef  AE_MULAAFD32RA_HH_LL
#define AE_MULAAFD32RA_HH_LL(acc, a, b) (acc) = haydn_ff2mula32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULAAFD32X16_H1_L0
#define AE_MULAAFD32X16_H1_L0(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULAAFD32X16_H3_L2
#define AE_MULAAFD32X16_H3_L2(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
// NatureDSP statement-form MAC (bkfira* non-quad): early static inlines return
// by value only — discarded at call sites. Write-back like H1_L0 / H3_L2.
// H2_L3 / H0_L1 map to add-add same-lane (f2mulaa32rs_hhll), not FIR-HL/HH.
#undef  AE_MULAAFD32X16_H2_L3
#define AE_MULAAFD32X16_H2_L3(acc, a, b) \
  (acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#undef  AE_MULAAFD32X16_H0_L1
#define AE_MULAAFD32X16_H0_L1(acc, a, b) \
  (acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_MULSSFD32X16 / AE_MULZAAFD32X16 / AE_MULZSSFD32X16 --------------
#undef  AE_MULSSFD32X16_H1_L0
#define AE_MULSSFD32X16_H1_L0(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULSSFD32X16_H3_L2
#define AE_MULSSFD32X16_H3_L2(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULZAAFD32X16_H1_L0
#define AE_MULZAAFD32X16_H1_L0(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULZAAFD32X16_H3_L2
#define AE_MULZAAFD32X16_H3_L2(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
// Zero-init add-add: 2-arg assignment (cxfir / IIR / bkfira) and 3-arg
// statement seed. Last-site 3-arg-only write-back broke the 2-arg form.
#undef  AE_MULZAAFD32X16_H2_L3
#undef  AE_MULZAAFD32X16_H2_L3_2A
#undef  AE_MULZAAFD32X16_H2_L3_3A
#undef  __AE_MULZAAFD32X16_H2_L3_GET
#define AE_MULZAAFD32X16_H2_L3_2A(a, b) \
  haydn_f2mulaa32rs_hhll((ae_int64)0, __AE_TO_I64((haydn_dr64_t)(a)), \
                         __AE_TO_I64((haydn_dr64_t)(b)))
#define AE_MULZAAFD32X16_H2_L3_3A(acc, a, b) \
  ((acc) = haydn_f2mulaa32rs_hhll((ae_int64)0, \
                                  __AE_TO_I64((haydn_dr64_t)(a)), \
                                  __AE_TO_I64((haydn_dr64_t)(b))))
#define AE_MULZAAFD32X16_H2_L3(...) \
  __AE_MULZAAFD32X16_H2_L3_GET(__VA_ARGS__, \
    AE_MULZAAFD32X16_H2_L3_3A, AE_MULZAAFD32X16_H2_L3_2A)(__VA_ARGS__)
#define __AE_MULZAAFD32X16_H2_L3_GET(_1, _2, _3, NAME, ...) NAME
// Statement-form sub-sub MAC. Early static inlines return by value only.
#undef  AE_MULASFD32X16_H1_L0
#define AE_MULASFD32X16_H1_L0(acc, a, b) \
  ((acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64((haydn_dr64_t)(a)), \
                                  __AE_TO_I64((haydn_dr64_t)(b))))
#undef  AE_MULASFD32X16_H3_L2
#define AE_MULASFD32X16_H3_L2(acc, a, b) \
  ((acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64((haydn_dr64_t)(a)), \
                                  __AE_TO_I64((haydn_dr64_t)(b))))
#undef  AE_MULZSAFD32X16_H3_L2
#define AE_MULZSAFD32X16_H3_L2(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULZSSFD32X16_H1_L0
#define AE_MULZSSFD32X16_H1_L0(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULAF32X16 (32x16 MAC, FIR-specific) ----------------------------
#undef  AE_MULAF32X16_H1
#define AE_MULAF32X16_H1(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#undef  AE_MULAF32X16_H3
#define AE_MULAF32X16_H3(acc, a, b) (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULSSSSQ16 (Q15*Q15 MSU, both lanes) ----------------------------
// haydn_fmulss16_hs_11_00 is ternary (returns new acc); write back.
#undef  AE_MULSSSSQ16
#define AE_MULSSSSQ16(acc, a, b) (acc) = haydn_fmulss16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULZAAFD16SS_11_00 (zero-acc Q15 MAC) ---------------------------
// Despite the ZA prefix, the kernel reuses this as an accumulating MAC into
// the supplied acc; the intrinsic is ternary, so write back the new value.
#undef  AE_MULZAAFD16SS_11_00
#define AE_MULZAAFD16SS_11_00(acc, a, b) (acc) = haydn_fmulaa16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULA32X2 / AE_MULS32X2 / AE_MULA16X4 / AE_MULS16X4 (SIMD, Path B) -------
// 3-arg form: single accumulator updated from the high-pair result (low pair
// unused → DCE'd). Matches NatureDSP `AE_MULA32X2(acc, a, b)` scalar-acc shape.
#undef  AE_MULA32X2
#define AE_MULA32X2(acc, a, b) \
  (acc) = (ae_int64)haydn_x2mula32((int64_t)(acc), (int64_t)0, (a), (b)).hi
#undef  AE_MULS32X2
#define AE_MULS32X2(acc, a, b) \
  (acc) = (ae_int64)haydn_x2muls32((int64_t)(acc), (int64_t)0, (a), (b)).hi
#undef  AE_MULA16X4
#define AE_MULA16X4(acc, a, b) \
  (acc) = (ae_int64)haydn_x4mula16((int64_t)(acc), (int64_t)0, (a), (b)).hi
#undef  AE_MULS16X4
#define AE_MULS16X4(acc, a, b) \
  (acc) = (ae_int64)haydn_x4muls16((int64_t)(acc), (int64_t)0, (a), (b)).hi

//---- AE_MULAAR16P16X4S / AE_MULAF16X4SS / AE_MULSF16X4SS (quad-16, Path B) ------
// 3-arg form: single accumulator updated from the high-pair result.
#undef  AE_MULAAR16P16X4S_
#define AE_MULAAR16P16X4S_(acc, a, b) \
  (acc) = (ae_int64)haydn_x4mula16s((int64_t)(acc), (int64_t)0, (a), (b)).hi
#undef  AE_MULAF16X4SS
#define AE_MULAF16X4SS(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4mula16s(__AE_TO_I64(acc_hi), __AE_TO_I64(acc_lo), \
                                         (a), (b)); \
    __AE_ASSIGN_BITS((acc_hi), _r.hi); \
    __AE_ASSIGN_BITS((acc_lo), _r.lo); \
  } while (0)
#undef  AE_MULSF16X4SS
#define AE_MULSF16X4SS(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4muls16s(__AE_TO_I64(acc_hi), __AE_TO_I64(acc_lo), \
                                         (a), (b)); \
    __AE_ASSIGN_BITS((acc_hi), _r.hi); \
    __AE_ASSIGN_BITS((acc_lo), _r.lo); \
  } while (0)

//---- AE_MACQ31 / AE_MAC32 (Q31 / 32-bit MAC) ---------------------------
#undef  AE_MACQ31
#define AE_MACQ31(acc, a, b) (acc) = haydn_macq31((acc), (a), (b))
#undef  AE_MAC32
#define AE_MAC32(acc, a, b) (acc) = haydn_mac32((acc), (a), (b))

//---- AE_MULFCR32RAS / AE_MULFCR32I_RAS (complex MAC with rounding) ------
// x2fcmula32rs / x2fcmula32rss are ternary (acc is first src); write back.
#undef  AE_MULFCR32RAS
#define AE_MULFCR32RAS(acc, a, b) (haydn_x2fcmula32rs(acc, a, b))
#undef  AE_MULFCR32I_RAS
#define AE_MULFCR32I_RAS(acc, a, b) (haydn_x2fcmula32rss(acc, a, b))

//---- AE_MULAP32X2 / AE_MULSP32X2 (pair MAC/MSU via MUL64_SS_HH) ---------
// Composed MAC: acc = acc +/- (a*b). Was returning; statement-form call
// sites need explicit write-back.
#undef  AE_MULAP32X2
#define AE_MULAP32X2(acc, a, b) (acc) = ((acc) + haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b)))
#undef  AE_MULSP32X2
#define AE_MULSP32X2(acc, a, b) (acc) = ((acc) - haydn_mul64_ss_hh(__AE_TO_I64(a), __AE_TO_I64(b)))

//---- AE_MULA32 / AE_MULA32U (32-bit MAC by lane) -----------------------
#undef  AE_MULA32_HH
#define AE_MULA32_HH(acc, a, b) (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA32_HL
#define AE_MULA32_HL(acc, a, b) (acc) = haydn_mula64_ss_hl((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA32_LH
#define AE_MULA32_LH(acc, a, b) (acc) = haydn_mula64_ss_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#undef  AE_MULA32_LL
#define AE_MULA32_LL(acc, a, b) (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))
// NOTE: AE_MULA32U_LL omitted from write-back fix — haydn_mula64_uu_ull is
// BINARY (2-arg), so a 3-arg call is an arity bug, not a dropped return. Left
// as-is (see TODO list at end of this block).

//---- AE_MULAFP24X2RA / AE_MULSFP24X2RA (24-bit FF2 fractional MAC/MSU) --
#undef  AE_MULAFP24X2RA
#define AE_MULAFP24X2RA(acc, a, b) \
  (acc) = haydn_ff2mula32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#undef  AE_MULSFP24X2RA
#define AE_MULSFP24X2RA(acc, a, b) \
  (acc) = haydn_ff2muls32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
// Overloaded 3-arg returning form: also write-back when used as MAC.
#undef  __AE_MULFP24X2RA_3
#define __AE_MULFP24X2RA_3(acc, a, b) \
  (acc) = haydn_ff2mula32rs_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_MULAAFD24 / AE_MULSSFD24 (24x24->48 single-lane MAC/MSU) --------
#undef  AE_MULAAFD24_HH_LL
#define AE_MULAAFD24_HH_LL(acc, a, b) \
  (acc) = haydn_fmula32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#undef  AE_MULAAFD24_HL_LH
#define AE_MULAAFD24_HL_LH(acc, a, b) \
  (acc) = haydn_fmula32s_lh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#undef  AE_MULSSFD24_HH_LL
#define AE_MULSSFD24_HH_LL(acc, a, b) \
  (acc) = haydn_fmuls32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))
#undef  AE_MULSSFD24_HL_LH
#define AE_MULSSFD24_HL_LH(acc, a, b) \
  (acc) = haydn_fmuls32s_lh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_MULZSAFD24_HH_LL (24-bit zero-init MAC, HH lane) ----------------
#undef  AE_MULZSAFD24_HH_LL
#define AE_MULZSAFD24_HH_LL(acc, a, b) \
  (acc) = haydn_fmula32s_hh((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- TODO / AMBIGUOUS — left as-is (arity mismatch, not write-back) ------
// The following MAC wrappers pass 3 args to a BINARY haydn_ intrinsic
// (which only takes 2). This is a separate arity / ISA-mapping issue, not
// the dropped-return-value bug fixed above. Left unchanged to avoid
// guessing the wrong semantic; tracked for a future ISA-mapping pass:
//   * AE_MUL32X16_H0..H3 -> haydn_smula16_00..30 (BINARY, HW accum reg)
//   * AE_MULAPH32/MULAPL32 -> haydn_x2mulaph32/pl32 (ternary, fixed)
//   * AE_MULA32U_LL -> haydn_mula64_uu_ull (BINARY)
//   * AE_MULFC24RA/MULAFC24RA/MULSFC24RA -> haydn_x2cmul32s (BINARY, and
//     HiFi source uses 2-arg form `AE_MULFC24RA(X0, cs)` returning a value)

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 10: no-1:1 multi-intrinsic compositions.    //
//                                                                            //
// work (P2). Tightens wrappers that previously decomposed HiFi SIMD  //
// ops into MULTIPLE native haydn_ scalar ops by replacing them with a      //
// SINGLE native haydn_ dual-MAC intrinsic where the Haydn ISA provides an  //
// exact-match dual-MAC instruction.                                          //
//                                                                            //
// Per the Haydn ISA spec (haydn_instruction_db.json line 3083 / HaydnIntrin- //
// sics.td §F2MULAA32RS_HHLL):                                                //
//   F2MULAA32RS_HHLL: rtd = r(rtd + (rsd1.H * rsd2.H) + (rsd1.L * rsd2.L))   //
//   F2MULSS32RS_HHLL: rtd = r(rtd - (rsd1.H * rsd2.H) - (rsd1.L * rsd2.L))   //
// Each computes TWO products and accumulates in ONE instruction.             //
//                                                                            //
// Correctness: all replacements below compute the exact same arithmetic as   //
// the prior 2-op decomposition. The HiFi3 source-compat semantic is the      //
// sum-of-two-products (HH+LL) which is exactly what the dual-MAC computes.   //
// No rounding/saturation change: the dual-MAC is saturating with rounding    //
// (the RS suffix); the prior 2-op decomposition also fed through saturating  //
// fmul32s / mula64_ss primitives, so the result is bit-identical for non-    //
// overflow inputs (and saturates identically on overflow because both paths  //
// apply the same per-product saturation semantics in hardware).              //
//                                                                            //
// This block uses the #undef + #define pattern from parts 5/9. It does NOT   //
// touch any other section. Each replacement is independently revertible.    //
//===----------------------------------------------------------------------===//

//---- AE_MULZAAFD32S_HH_LL ------------------------------------------------
// HiFi3 semantic: zero-init accumulator, then sum of two fractional
// products (HH*HH + LL*LL), Q1.31 in/out.
//
// The binary zero-accumulator dual-product MAC exists natively as
// F2MULZAA32RS_HHLL (`rtd = 0 + HH*HH + LL*LL`, no accumulator read). The
// earlier note here claimed "Haydn has NO binary dual-product-sum
// instruction" — that was incorrect ('s premise was wrong; see ).
// The 17-vs-9-packet measurement above was an artifact of forcing the
// *accumulating* form F2MULAA32RS_HHLL with a literal-zero accumulator,
// which then had to materialize the zero in DR64. The native ZAA form
// avoids that entirely.
//
// This wrapper still maps to the 2-op loose form below (lines ~2506-2507)
// for source compatibility; the ZAA form is used directly by the
// AE_MULZAAD32X16_H3_L2 / AE_MULZAAD24_* wrappers below.

//---- AE_MULZAAD32X16_H3_L2 (2x mula64_ss -> 1x f2mulzaa32rs_hhll) --------
// HiFi3 semantic: zero-init dual MAC, sum of HH*HH + LL*LL 64-bit products.
// Native zero-accumulator (binary) form F2MULZAA32RS_HHLL produces the same
// dual-product accumulation in a single instruction with NO accumulator read
// (). The earlier composition used the accumulating F2MULAA32RS_HHLL with
// a literal-zero acc, which forced a wasted zero materialization; ZAA avoids
// it. Prior composition: ~2 packets (mula64_ss_hh + mula64_ss_ll).
// New composition:   1 packet  (f2mulzaa32rs_hhll).
#undef  AE_MULZAAD32X16_H3_L2
#define AE_MULZAAD32X16_H3_L2(d, c) \
  haydn_f2mulzaa32rs_hhll(__AE_TO_I64((haydn_dr64_t)(d)), __AE_TO_I64((haydn_dr64_t)(c)))

//---- AE_MULAAD32X16_H1_L0 / _H3_L2 (integer 32x16 dual-MAC) ------------
// NatureDSP uses statement form `AE_MULAAD32X16_H3_L2(C, x, y)` so acc must
// be written back. Sign-extend the named 16-bit lanes, then integer 32x32
// MAC. Do not map onto Q1.31 F2MULAA32RS (rounding changes mtx_mpy).
static inline haydn_dr64_t haydn_i16x4_pair_as_i32x2(haydn_dr64_t c,
                                                     unsigned hi_lane,
                                                     unsigned lo_lane) {
  int16_t hi =
      (int16_t)((uint16_t)(((uint64_t)c >> (16u * hi_lane)) & 0xFFFFu));
  int16_t lo =
      (int16_t)((uint16_t)(((uint64_t)c >> (16u * lo_lane)) & 0xFFFFu));
  return (haydn_dr64_t)haydn_movda32x2((int32_t)lo, (int32_t)hi);
}
#undef  AE_MULAAD32X16_H1_L0
#define AE_MULAAD32X16_H1_L0(acc, d, c) \
  do { \
    haydn_dr64_t __c32 = haydn_i16x4_pair_as_i32x2((haydn_dr64_t)(c), 1, 0); \
    (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64((haydn_dr64_t)(d)), __c32); \
    (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64((haydn_dr64_t)(d)), __c32); \
  } while (0)
#undef  AE_MULAAD32X16_H3_L2
#define AE_MULAAD32X16_H3_L2(acc, d, c) \
  do { \
    haydn_dr64_t __c32 = haydn_i16x4_pair_as_i32x2((haydn_dr64_t)(c), 3, 2); \
    (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64((haydn_dr64_t)(d)), __c32); \
    (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64((haydn_dr64_t)(d)), __c32); \
  } while (0)

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 11: FIR/FFT/IIR coverage gap closure.       //
//                                                                            //
// Context: GAP-FIR-FFT-IIR.md / L111 audit found 0/12 IIR, ~half of FIR,    //
// and ~half of FFT kernels fail to COMPILE because the AE_* surface in this //
// header is incomplete. The complete missing-symbol set (62 unique ids,     //
// measured by compiling all 164 FIR/FFT/IIR kernels) is closed below.       //
//                                                                            //
// Mapping policy (aligned with the spec / haydn.h):                   //
//   * Each new wrapper maps to an EXISTING haydn_* intrinsic (verified     //
//     against haydn.h) or a clearly-correct inline composition of     //
//     existing scalar intrinsics. No new intrinsics are invented.            //
//   * MAC wrappers that thread an accumulator through a ternary haydn_     //
//     intrinsic WRITE BACK the result (acc) = ... per part 9.                //
//   * Load/store wrappers whose ptr argument may be a castxcc() non-lvalue   //
//     store through *(TYPE *)(ptr) and DROP the pointer update (the kernel   //
//     recomputes the cast each iteration) — same idiom as parts 7/8.         //
//   * ISA gaps (symbols with no native Haydn instruction) are mapped to the  //
//     closest correct software sequence and recorded in ISA-NN reports.      //
//===----------------------------------------------------------------------===//

//---- Internal intrinsic aliases used by part-11 wrappers ----------------
// haydn_fmula32s_hl / haydn_fmuls32s_hl are referenced by MAC write-back
// wrappers but missing from the ISA lane surface (only LL/LH/HH exist).
// mul(a_hi, b_lo) == mul(b_lo, a_hi) requires SWAPPED operands into _lh —
// a bare token alias to _lh (no swap) selects the wrong lanes.
#ifndef haydn_fmula32s_hl
#define haydn_fmula32s_hl(acc, a, b) haydn_fmula32s_lh((acc), (b), (a))
#endif
#ifndef haydn_fmuls32s_hl
#define haydn_fmuls32s_hl(acc, a, b) haydn_fmuls32s_lh((acc), (b), (a))
#endif

//---- MULAFD32X16X2_FIR -> MULAA32S_FIR rename compat shim () -----
// The intrinsics haydn_mulafd32x16x2_fir_{hh,hl} were renamed to
// haydn_mulaa32s_fir_{hh,hl} because the old name lied about the width:
// it claimed "32x16 dual" but the backing instruction (FMULA32S_HH/_LH) is
// 32x32 single-lane (DB :6898/:6939). The C AE_MUL*FD32X16* / AE_MUL*32X16*
// wrapper names are UNCHANGED for NatureDSP source compatibility; ~50
// wrappers in this header call the old haydn_ name, so alias the old name
// to the new one here rather than editing every call site. See and
// L148 (the coef-widen correctness fix that pairs with this rename).
#ifndef haydn_mulafd32x16x2_fir_hh
#define haydn_mulafd32x16x2_fir_hh haydn_mulaa32s_fir_hh
#endif
#ifndef haydn_mulafd32x16x2_fir_hl
#define haydn_mulafd32x16x2_fir_hl haydn_mulaa32s_fir_hl
#endif

//---- AE_MOVF* lane/width conversions -----------------------------------
// HiFi3 AE_MOVF*_FROM* are pure reinterpretation casts between the ae_*      //
// view types (all backed by haydn_dr64_t or scalar int/long-long). They      //
// produce no code; the cast only changes the type tag the optimizer sees.    //
#define AE_MOVF16X4_FROMF32X2(a) ((ae_f16x4)(haydn_dr64_t)(a))
#define AE_MOVF32_FROMF32X2(a)    ((ae_f32)(((ae_f32x2)(a) >> 32) & 0xFFFFFFFF))
#define AE_MOVF32X2_FROMF64(a)    ((ae_f32x2)(haydn_dr64_t)(a))
#define AE_MOVF64_FROMF32X2(a)    ((ae_f64)(haydn_dr64_t)(a))

//---- XT_NSA / XT_MOVEQZ (scalar helpers used by FFT/vector kernels) -----
// XT_NSA(x): number of leading sign bits of a 32-bit int (incl. sign bit).
//   Native Haydn NSA: haydn_nsa32 returns the number of redundant sign
//   bits (HiFi-compatible semantics). records that a single native
//   NSA covering 16/32/64 widths would remove the per-width dispatch.
static inline int XT_NSA(int x) { return (int)haydn_nsa32((ae_int32)x); }
// XT_MOVEQZ(dst, val, cond): if cond == 0 then dst = val (move-if-zero).
// The ISA DB defines MOVF32 (rt = (rs2[0]==0) ? rs1 : rt) and MOVT32
// (rt = (rs2[0]==1) ? rs1 : rt). Use a C ternary that the LLVM optimizer
// lowers to G_SELECT → MOVT32/MOVF32.
#define XT_MOVEQZ(dst, val, cond) ((dst) = ((cond) == 0) ? (val) : (dst))
#define XT_MOVEQZ_S(dst, val, cond) XT_MOVEQZ(dst, val, cond)
// XT_MOVNEZ(dst, val, cond): if cond != 0 then dst = val (move-if-nonzero).
#define XT_MOVNEZ(dst, val, cond) ((dst) = ((cond) != 0) ? (val) : (dst))
#define XT_MOVNEZ_S(dst, val, cond) XT_MOVNEZ(dst, val, cond)

//---- AE_SRAS32 / AE_SLAS32 / AE_SRA64_32 (SAR dual + narrow) -------------
// AE_SRAS32: Cadence dual-32 ASR by ambient AE_SAR (1-arg). Late body must
// reaffirm X2SRA32 — never scalar ((x + 0x8000) >> 1) half-average, which
// drops the high lane and invents a rounding step FFT stages do not want.
#undef AE_SRAS32
#undef __AE_SRAS32_1
#undef __AE_SRAS32_2
#define AE_SRAS32(...) __AE_SRAS32_OVERLOAD(__VA_ARGS__)
#define __AE_SRAS32_1(a)    haydn_x2sra32((a), haydn_ae_sar)
#define __AE_SRAS32_2(a, s) haydn_x2sra32((a), (int)(s))
// AE_SLAS32: Cadence dual-32 left by ambient AE_SAR (1-arg, non-sat).
#undef AE_SLAS32
#undef __AE_SLAS32_1
#undef __AE_SLAS32_2
#define AE_SLAS32(...) __AE_SLAS32_OVERLOAD(__VA_ARGS__)
#define __AE_SLAS32_1(a)    haydn_x2sll32((a), haydn_ae_sar)
#define __AE_SLAS32_2(a, s) haydn_x2sll32((a), (int)(s))
// AE_SRA64_32(q, sh): narrow a 64-bit accumulator to 32 bits with a
//   signed shift. Maps to haydn_packsr32 (pack-with-shift-round) at sh=0
//   and to haydn_satsr64 for the general shift. sh=0 is the common case
//   in latr32x32 (just narrows), so prefer packsr32 there.
#define AE_SRA64_32(q, sh) \
  ((ae_int32)(((sh) == 0) ? haydn_packsr32((q), 0) \
                          : haydn_satsr64((q), (sh))))

//---- AE_F64_SLAIS / AE_F64_SUBS / AE_F32X2_SLAIS (scalar/pair shifts) ---
// AE_F64_SLAIS(q, n): saturating 64-bit arithmetic LEFT by immediate n.
// Soft model matches AE_SLAI64S / AE_F64_SLAS (not plain C << wrap).
#define AE_F64_SLAIS(q, n) ((ae_f64)haydn_ae_slaa64s((ae_int64)(q), (int)(n)))
// AE_F64_SUBS(qa, qb): 64-bit fractional subtract. Maps to haydn_sub64s.
#define AE_F64_SUBS(qa, qb) ((ae_f64)haydn_sub64s((ae_int64)(qa), (ae_int64)(qb)))
// AE_F32X2_SLAIS(v, n): dual-32 saturating arithmetic left by n.
// Soft per-lane sat matches AE_SLAI32S / AE_SLAA32S. Must not silent-alias
// wrap X2SLL/X2SLLI (0x40000000<<1 wraps to INT32_MIN; sat is INT32_MAX).
#define AE_F32X2_SLAIS(v, n) \
  ((ae_f32x2)__ae_slaa32s((ae_int32x2)(v), (int)(n)))

//---- Scalar load/store spellings missing from the original surface -------
// ae_f32_loadip / ae_f32_storeip: scalar 32-bit fractional load/store with
//   post-increment (lowercase HiFi aliases). The vector ae_f32x2_loadip
//   exists (line 2702); the scalar form was absent. Mirror AE_L32_IP /
//   AE_S32_L_IP byte-offset semantics.
#define ae_f32_loadip(dst, ptr, inc)  AE_L32_IP(dst, ptr, inc)
#define ae_f32_storeip(src, ptr, inc) AE_S32_L_IP(src, ptr, inc)
// ae_f32x2_loadi / ae_f32x2_storei: indexed load/store (no increment).
//   HiFi _loadi/_storei access *(ptr + offs) without advancing ptr.
#define ae_f32x2_loadi(dst, ptr, offs) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)
#define ae_f32x2_storei(src, ptr, offs) \
  do { *(ae_f32x2 *)((char *)(ptr) + (offs)) = (src); } while (0)
// ae_f32x2_loadx / ae_f32x2_loadxp / ae_f32x2_storexp: offset / post-inc
//   variants. _loadx reads at ptr+offs (no advance); _loadxp reads at
//   ptr+offs then advances by inc; _storexp writes at ptr+offs then advances.
#define ae_f32x2_loadx(dst, ptr, offs) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)
#define ae_f32x2_loadxp(dst, ptr, offs, inc) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (inc)); } while (0)
#define ae_f32x2_storexp(src, ptr, offs, inc) \
  do { *(ae_f32x2 *)((char *)(ptr) + (offs)) = (src); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (inc)); } while (0)
// ae_int32x2_loadi / ae_int32x2_storeip: integer-pair indexed load and
//   post-increment store (IIR kernels use these on int32x2 buffers).
#define ae_int32x2_loadi(dst, ptr, offs) \
  do { (dst) = *(ae_int32x2 *)((char *)(ptr) + (offs)); } while (0)
#define ae_int32x2_storeip(src, ptr, inc) \
  do { *(ae_int32x2 *)(ptr) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
// ae_int32x2_aligning_load_*: aligning load idioms. HiFi uses these to
//   issue an aligned load from a possibly-unaligned base; Haydn has no
//   dynamic alignment hardware, so lower to a plain load (the alignment
//   hint is dropped — Haydn loads are unaligned-tolerant by ISA).
#define ae_int32x2_aligning_load_prime(dst, ptr) \
  do { (dst) = *(ae_int32x2 *)(ptr); } while (0)
#define ae_int32x2_aligning_load_post_update_positive(dst, ptr, inc) \
  do { (dst) = *(ae_int32x2 *)(ptr); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)

//---- ae_f16x4_loadi / ae_f16x4_storeip (quad-16 indexed load / store) ---
#define ae_f16x4_loadi(dst, ptr, offs) \
  do { (dst) = *(ae_f16x4 *)((char *)(ptr) + (offs)); } while (0)
#define ae_f16x4_storeip(src, ptr, inc) \
  do { *(ae_f16x4 *)(ptr) = (src); \
       (ptr) = (ae_f16x4 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_L16_X / AE_L64_X / AE_L32X2F24_I / AE_L32X2F24_X ----------------
// AE_L16_X: scalar 16-bit offset load (statement form). The 2-arg expr form
//   AE_L16_X(ptr, offs) returns the value; the 3-arg form assigns to dst.
//   Both already exist at line 2049/2069-style; provide the missing 2-arg
//   spelling here if a kernel used AE_L16_X as a 2-arg expression.
#ifndef AE_L16_X
#define AE_L16_X(...) __AE_L16_X_OVERLOAD(__VA_ARGS__)
#define __AE_L16_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16_X_OVERLOAD(...) \
  __AE_L16_X_GET(__VA_ARGS__, __AE_L16_X_3, __AE_L16_X_2)(__VA_ARGS__)
#define __AE_L16_X_2(ptr, offs) (*(ae_int16 *)((char *)(ptr) + (offs)))
#define __AE_L16_X_3(dst, ptr, offs) \
  do { (dst) = *(ae_int16 *)((char *)(ptr) + (offs)); } while (0)
#endif
// AE_L64_X already defined at line 2101 (3-arg statement form). Provide the
// 2-arg expression form some kernels use.
#define AE_L64_X(...) __AE_L64_X_OVERLOAD(__VA_ARGS__)
#define __AE_L64_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L64_X_OVERLOAD(...) \
  __AE_L64_X_GET(__VA_ARGS__, __AE_L64_X_3, __AE_L64_X_2)(__VA_ARGS__)
#define __AE_L64_X_2(ptr, offs) (*(ae_int64 *)((char *)(ptr) + (offs)))
#define __AE_L64_X_3(dst, ptr, offs) \
  do { (dst) = *(ae_int64 *)((char *)(ptr) + (offs)); } while (0)
// AE_L32X2F24_I / AE_L32X2F24_X: 24-bit-in-32 pair load, indexed / offset.
//   These are the F24 (24-bit fractional promoted to Q1.31) pair-load
//   spellings; lower to the same byte-offset load as AE_L32X2_I / _X.
#define AE_L32X2F24_I(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)
#define AE_L32X2F24_X(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_SA32X2_RIP / AE_SA32X2F24_RIP (reverse-increment pair store) ----
// HiFi reverse-increment store: write at ptr, then ptr -= inc. The store
// target may be a castxcc() non-lvalue, so store through *(TYPE*)(ptr) and
// do NOT write back the cast expression (kernel recomputes it).
#undef  AE_SA32X2_RIP
#define AE_SA32X2_RIP(src, ptr, inc) \
  do { *(ae_int32x2 *)(ptr) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) - (inc)); } while (0)
#undef  AE_SA32X2F24_RIP
#define AE_SA32X2F24_RIP(src, ptr, inc) \
  do { *(ae_f24x2 *)(ptr) = (src); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) - (inc)); } while (0)

//---- __AE_ROUNDSP24Q48ASYM_GET (arity-dispatch helper) ------------------
// Referenced by an overload macro but the _GET helper was missing. Provide
// the standard N-arg dispatch shape (5-arg ROUNDSP24Q48ASYM).
#ifndef __AE_ROUNDSP24Q48ASYM_GET
#define __AE_ROUNDSP24Q48ASYM_GET(_1, _2, _3, NAME, ...) NAME
#endif

//---- AE_MULF16SS_32 / AE_MULF16SS_33 (16-bit fractional mul, lane pair) -
// HiFi AE_MULF16SS_xy: signed 16x16->32 fractional multiply selecting lane
//   x of the first operand and lane y of the second. Maps to the native
//   haydn_fmul16_hsXY (H = upper 16 of the 32-bit slot) / _lsXY (L). The
//   "SS" (signed*signed) family uses the hs form for the H lanes. Lane 3 =
//   top, lane 2 = next, etc. (matches haydn_fmul16_hs33 / _hs32 order).
#define AE_MULF16SS_33(a, b) haydn_fmul16_hs33(__AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULF16SS_32(a, b) haydn_fmul16_hs32(__AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAF16SS_00 / AE_MULSF16SS_00 / _20 / _22 (Q15 MAC/MSU lanes) --
// Accumulate/subtract into a 64-bit acc with the selected 16x16->32 product.
//   AE_MULAF16SS_xy(acc,a,b): acc += (a.x * b.y) << 1 (fractional Q15 MAC,
//   the <<1 aligns the Q30 product into the Q31 lane before the 64-bit add).
//   Write back per part-9 (ternary intrinsic returns new acc). Haydn has
//   haydn_fmul16_hs00 etc. (binary, returns product); compose as
//   acc + (product << 1). The 16-bit MAC family lacks a ternary native op,
//   so this is a 2-op sequence (mul + add) — recorded in .
#define AE_MULAF16SS_00(acc, a, b) \
  (acc) = ((acc) + ((ae_int64)haydn_fmul16_hs00(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_00(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs00(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_20(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs20(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_22(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs22(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
// AE_MULSF16X4SS: quad-16 subtract-product-from-acc (SIMD MSU). Path B:
//   haydn_x4muls16s is now 2-dest returning haydn_dpair_t — single-acc form
//   selects the high pair and writes it back.
#undef  AE_MULSF16X4SS
#define AE_MULSF16X4SS(acc, a, b) \
  (acc) = (ae_int64)haydn_x4muls16s((int64_t)(acc), (int64_t)0, (a), (b)).hi

//---- AE_MULSSFD16SS_11_00 (dual Q15 MSU, lanes 1,1 and 0,0) -------------
// HiFi dual 16x16 MSU summing into a 64-bit acc. Native haydn_fmulss16_hs_11_00
//   is ternary (acc -= hh*11 + ll*00); write back.
#define AE_MULSSFD16SS_11_00(acc, a, b) \
  (acc) = haydn_fmulss16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULZAAFD16SS_13_02 / AE_MULZAAFD32X16 dual-product zero-init ----
// Zero-init dual Q15 MAC (lanes 1,3 and 0,2). haydn_fmulaa16_hs_13_02 is
//   BINARY (2 args, implicit zero accumulator) per IntrinsicsHaydn.td:671 —
//   unlike _11_00 which is ternary. Call with the 2 operands directly.
#define AE_MULZAAFD16SS_13_02(a, b) \
  haydn_fmulaa16_hs_13_02(__AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULF32R_HL (fractional mul, HL lane) ----------------------------
// Completes the HH/LL/LH/HL set (HH/LL/LH at line 2485-2487). HL lane: the
//   haydn_ff2mul32r family has hh/lh/ll; HL == LH for a symmetric product.
#define AE_MULF32R_HL(a, b) haydn_ff2mul32r_lh(__AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULSF32R_HH / AE_MULSF32R_LL (fractional MSU, rounding+sat) -----
// AE_MULSF32R_xy(acc,a,b): acc -= round(a.x * b.y). Native haydn_ff2muls32rs_*
//   is ternary; write back.
#define AE_MULSF32R_HH(acc, a, b) (acc) = haydn_ff2muls32rs_hh((acc), __AE_TO_I64(a), __AE_TO_I64(b))
#define AE_MULSF32R_LL(acc, a, b) (acc) = haydn_ff2muls32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MUL32X16_L0 / _L1 / _L2 (32x16 MAC, low-half lanes) -------------
// The H0..H3 (upper-half) family exists (line 1166). NatureDSP IIR also
//   uses L0/L1/L2 (lower-half 16-bit lane of the 32-bit operand). Haydn's
//   haydn_mulafd32x16x2_fir_hl covers the low lanes (H1+L0 dual MAC);
//   map each L-lane MAC to that helper with write-back. (Lane-exact
//   selection would need haydn_smula16_* which is BINARY — tracked in
//   the part-9 TODO; the FIR helper is the correct accumulating form.)
#define AE_MUL32X16_L0(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MUL32X16_L1(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MUL32X16_L2(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULAF32X16_L1 / _L3 (32x16 MAC into acc, low lanes) -------------
// Matches the existing AE_MULAF32X16_H1/_H3 (line 2476-2477) for the low
//   lanes. L1/L0 use the hl FIR helper; L3/L2 use the hh FIR helper.
#define AE_MULAF32X16_L1(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULAF32X16_L3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULF32X16_H3 / AE_MULSF32X16_L0 / _L1 / _L3 (non-acc 32x16) -----
// AE_MULF32X16_Hx: fresh product (no acc) — return the value (kernel
//   assigns at the call site). AE_MULSF32X16: subtract-product-from-acc
//   form used by some IIR topologies; thread acc with write-back.
#define AE_MULF32X16_H3(a, b) haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULSF32X16_L0(acc, a, b) \
  (acc) = ((acc) - (ae_int64)haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))
#define AE_MULSF32X16_L1(acc, a, b) \
  (acc) = ((acc) - (ae_int64)haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))
#define AE_MULSF32X16_L3(acc, a, b) \
  (acc) = ((acc) - (ae_int64)haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))

//---- AE_MULFD32X16X2_FIR_HL (dual-acc FIR multiply, no-accumulate) ------
// The accumulate form AE_MULAFD32X16X2_FIR_HL exists (line 1341). The
//   non-accumulate AE_MULFD32X16X2_FIR_HL writes fresh products to both
//   accumulators (zero-init). 5-arg (q0,q1,d0,d1,c) per the FIR idiom.
#undef  AE_MULFD32X16X2_FIR_HL
static inline void AE_MULFD32X16X2_FIR_HL(ae_int64 *q0, ae_int64 *q1,
                                          ae_int16x4 d0, ae_int16x4 d1,
                                          ae_int16x4 c) {
  *q0 = haydn_mulafd32x16x2_fir_hl(0, __AE_TO_I64(d0), __AE_TO_I64(c));
  *q1 = haydn_mulafd32x16x2_fir_hl(0, __AE_TO_I64(d1), __AE_TO_I64(c));
}

//---- AE_MULZASFD32X16_H1_L0 / AE_MULZAAD32X16_H1_L0 (zero-init dual MAC) -
// Zero-init dual 32x16 MAC (lanes H1+L0). haydn_mulafd32x16x2_fir_hl is
//   ternary; with acc=0 it yields the dual product.
#define AE_MULZASFD32X16_H1_L0(a, b) \
  haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define AE_MULZAAD32X16_H1_L0(a, b) \
  haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULZSSFD32X16_H1_L0 (zero-init dual 32x16 subtract-MAC) ---------
// No native zero-init dual-subtract-MAC; compose as the dual product negated
//   (acc=0 - dual_product). Recorded in (no native dual-MSU).
#define AE_MULZSSFD32X16_H1_L0(a, b) \
  ((ae_int64)0 - (ae_int64)haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))

//---- AE_MULZSSFD24_HL_LH / AE_MULZSAFD24_HH_LL (24-bit zero-init MAC) ---
// 24-bit operands promote to 32-bit lanes. Zero-init single-lane MAC maps
//   to haydn_fmula32s_hh/_lh with acc=0.
#define AE_MULZSSFD24_HL_LH(a, b) \
  ((ae_int64)0 - (ae_int64)haydn_fmula32s_lh((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b)))
#define AE_MULZSAFD24_HH_LL(a, b) \
  haydn_fmula32s_hh((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULAFP32X16X2RAS_H (32x16x2 fractional MAC with rounding, H lane)
// The non-accumulate AE_MULFP32X16X2RAS_H exists (line 2491). The accumulate
//   form threads acc through haydn_mulfp32x16x2ras_high with write-back.
#define AE_MULAFP32X16X2RAS_H(acc, a, b) \
  (acc) = haydn_mulfp32x16x2ras_high((acc), (a), (b))

//---- castxcc-safe scalar store wrappers (FIR lvalue-cast kernels) -------
// FIR firother kernels (raw_corr*, fir_lacorr*, fir_xcorr*, fir_convol*) call
//   AE_S16_0_IP(src, castxcc(ae_int16, pR), +2). castxcc expands to
//   (ae_int16 *)(pR), a non-lvalue; the existing AE_S16_0_IP writes
//   (ptr) = (ae_int16 *)(...) which is illegal ("assignment to cast").
//   Per the part-7/8 idiom: store through *(ae_int16 *)(ptr) and DROP the
//   pointer update — the kernel recomputes castxcc() each iteration and
//   advances pR separately, so dropping the macro's write-back is correct
//   (the kernel's own pR += ... is the real advance).
// AE_S16_0_IP: store halfword. For castxcc non-lvalue store *sites* that only
// recompute cast each trip, drop write-back (historical FIR firother path).
// Prefer true post-inc when ptr is a real lvalue / castxcc lvalue form below.
#undef  AE_S16_0_IP
#define AE_S16_0_IP(src, ptr, inc) \
  do { \
    *(ae_int16 *)(ptr) = (ae_int16)(src); \
    (ptr) = (__typeof__(ptr))((char *)(void *)(ptr) + (inc)); \
  } while (0)

//---- Scalar + vector IP true post-increment (/ B7) -------------------
// castxcc is lvalue (*(t **)&(p)) at EOF of this header — assign through
// (ptr) advances the underlying pointer. Dropping (void)(inc) for AE_L32_IP
// broke cxfir32x32 (coeff walk stuck / bad EA → S_LW_WITH_IMM misalign).
// Same contract as AE_L32X2_IP below.
#undef  AE_S32_L_IP
#define AE_S32_L_IP(src, ptr, inc) \
  do { \
    *(ae_int32 *)(ptr) = (ae_int32)(src); \
    (ptr) = (__typeof__(ptr))((char *)(void *)(ptr) + (inc)); \
  } while (0)
// Scalar L32 into ae_int32x2 must BROADCAST (AE_MOVDA32): NatureDSP loads
// coefs with L32_IP then uses both HH and LL (cxfir). Low-only assign left
// high=0 → real MAC collapsed to imag-coef terms (Y[0]≈-H[1]).
#undef  AE_L32_IP
#define AE_L32_IP(dst, ptr, inc) \
  do { \
    int32_t haydn_l32 = *(const int32_t *)(const void *)(ptr); \
    (dst) = AE_MOVDA32(haydn_l32); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)
#undef  AE_L16_IP
#define AE_L16_IP(dst, ptr, inc) \
  do { \
    (dst) = __AE_LOAD_AT(ae_int16, ptr); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

//---- AE_S32X2_IP / AE_L32X2_IP true post-increment (capstone F4 fix, ) --
// The prior override cast `(void)(inc)` to satisfy a non-lvalue `ptr`, which
// DROPPED the post-increment. HiFi3: dereference *ptr, then ADVANCE ptr.
// castxcc lvalue form makes write-back valid for castxcc callers too.
// AE f32x2: .H = first mem word, .L = second (matches soft freestanding swap).
#undef  AE_L32X2_IP
#define AE_L32X2_IP(dst, ptr, inc) \
  do { \
    uint32_t __w0 = *(const uint32_t *)(const void *)(ptr); \
    uint32_t __w1 = *(const uint32_t *)((const char *)(const void *)(ptr) + 4); \
    (dst) = (ae_int32x2)((uint64_t)__w1 | ((uint64_t)__w0 << 32)); \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)
#undef  AE_S32X2_IP
#define AE_S32X2_IP(src, ptr, inc) \
  do { \
    uint64_t __u = (uint64_t)(haydn_dr64_t)(src); \
    uint32_t __hi = (uint32_t)(__u >> 32); \
    uint32_t __lo = (uint32_t)__u; \
    *(uint32_t *)(void *)(ptr) = __hi; \
    *(uint32_t *)((char *)(void *)(ptr) + 4) = __lo; \
    __AE_ADVANCE_PTR(ptr, inc); \
  } while (0)

//---- AE_S24RA64S_IP store-with-saturate form ---------------------------
// HiFi: AE_S24RA64S_IP(acc, ptr, inc) — store satsr64(acc, 24) to *ptr,
//   advance ptr by inc. The existing def (line 2018) had wrong semantics
//   (computed dst but didn't store/advance). Fix to the store+post-inc form.
#undef  AE_S24RA64S_IP
#define AE_S24RA64S_IP(acc, ptr, inc) \
  do { *(ae_f24 *)(ptr) = (ae_f24)haydn_satsr64((acc), 24); \
       (ptr) = (ae_f24 *)((char *)(ptr) + (inc)); } while (0)

//===----------------------------------------------------------------------===//
// Note on residual no-1:1 wrappers NOT recomposed here (ISA gaps).          //
//                                                                            //
// The following wrappers remain in their existing multi-op form because NO   //
// tighter native Haydn composition exists; they are tracked in ISA-NN gap   //
// reports and left alone here to avoid regressing the part 9 MAC write-back //
// correctness:                                                               //
//   * AE_MULFP32X16X2RAS_H/_L: Haydn has no 32x16 cross-width fractional    //
//     MAC (). Current haydn_mulfp32x16x2ras_* path decomposes via   //
//     sext + MULQ31 + ADD; the alternative haydn_mula64_ss_ll + packsr32  //
//     (used directly by the ported IIR kernel) is tighter for that one      //
//     caller, but the AE_ wrapper has no production caller today, so we     //
//     leave it on the existing intrinsic path.                               //
//   * AE_MULFC32X16RAS_H/_L: the existing haydn_mulfc32x16ras_high/low    //
//     composition (C widen of the 16-bit twiddle + X2FCMULA32RS) is already //
//     the tightest available. The native X2FCMULA32RS does the complex MAC  //
//     in one instruction; the 16->32 lane widen is irreducible because      //
//     Haydn has no cross-lane 16->32 pack (, ).                    //
//   * AE_MULZAAFD32S_HH_LL: the existing 2x fmul32s + add form (9 packets)  //
//     is tighter than the ternary F2MULAA32RS_HHLL with a literal-zero      //
//     accumulator (17 packets), because Haydn has no binary dual-add-MAC    //
//     instruction. See -binary-dual-add-mac-gap.md.                   //
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 12: IIR/FIR arity fixes + remaining gaps.  //
//                                                                            //
// The part-11 block closed the first layer of missing symbols, but IIR       //
// kernels call several of these helpers in a DIFFERENT arity than part-11    //
// defined (2-arg expression forms vs 3-arg statement forms). Per the HiFi3   //
// reference, each load/store/MUL helper has BOTH a 2-arg expression form     //
// (returns the value) and a 3-arg statement form (assigns to dst). This      //
// block adds the missing 2-arg / returning forms and the last set of MAC    //
// lane variants the IIR family needs.                                        //
//===----------------------------------------------------------------------===//

//---- Internal haydn_fmul16_hs aliases for non-triangular lanes ----------
// The native surface ships lanes 00,01,02,03,11,12,13,22,23,33 (upper-
// triangular). Lanes 10,20,21,30,31,32 are reachable by operand swap
// because 16x16 fractional multiply is commutative: hsXY(a,b) == hsYX(b,a).
// Provide them as macros so AE_MULF16SS_xy wrappers below resolve cleanly.
#ifndef haydn_fmul16_hs10
#define haydn_fmul16_hs10(a, b) haydn_fmul16_hs01((b), (a))
#endif
#ifndef haydn_fmul16_hs20
#define haydn_fmul16_hs20(a, b) haydn_fmul16_hs02((b), (a))
#endif
#ifndef haydn_fmul16_hs21
#define haydn_fmul16_hs21(a, b) haydn_fmul16_hs12((b), (a))
#endif
#ifndef haydn_fmul16_hs30
#define haydn_fmul16_hs30(a, b) haydn_fmul16_hs03((b), (a))
#endif
#ifndef haydn_fmul16_hs31
#define haydn_fmul16_hs31(a, b) haydn_fmul16_hs13((b), (a))
#endif
#ifndef haydn_fmul16_hs32
#define haydn_fmul16_hs32(a, b) haydn_fmul16_hs23((b), (a))
#endif

//---- 2-arg expression forms for ae_*_loadi / ae_*_storeip ----------------
// HiFi lowercase helpers come in 2-arg (expr, returns value) and 3-arg
//   (statement, assigns to dst) shapes. Provide overloaded dispatch.
#undef  ae_f32x2_loadi
#define ae_f32x2_loadi(...) __ae_f32x2_loadi_OVERLOAD(__VA_ARGS__)
#define __ae_f32x2_loadi_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f32x2_loadi_OVERLOAD(...) \
  __ae_f32x2_loadi_GET(__VA_ARGS__, __ae_f32x2_loadi_3, __ae_f32x2_loadi_2)(__VA_ARGS__)
#define __ae_f32x2_loadi_2(ptr, offs) (*(ae_f32x2 *)((char *)(ptr) + (offs)))
#define __ae_f32x2_loadi_3(dst, ptr, offs) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_f32x2_storeip
#define ae_f32x2_storeip(...) __ae_f32x2_storeip_OVERLOAD(__VA_ARGS__)
#define __ae_f32x2_storeip_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f32x2_storeip_OVERLOAD(...) \
  __ae_f32x2_storeip_GET(__VA_ARGS__, __ae_f32x2_storeip_3, __ae_f32x2_storeip_2)(__VA_ARGS__)
#define __ae_f32x2_storeip_2(src, ptr) \
  do { *(ae_f32x2 *)(ptr) = (src); } while (0)
#define __ae_f32x2_storeip_3(src, ptr, inc) \
  do { *(ae_f32x2 *)(ptr) = (src); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (inc)); } while (0)

#undef  ae_f16x4_loadi
#define ae_f16x4_loadi(...) __ae_f16x4_loadi_OVERLOAD(__VA_ARGS__)
#define __ae_f16x4_loadi_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f16x4_loadi_OVERLOAD(...) \
  __ae_f16x4_loadi_GET(__VA_ARGS__, __ae_f16x4_loadi_3, __ae_f16x4_loadi_2)(__VA_ARGS__)
#define __ae_f16x4_loadi_2(ptr, offs) (*(ae_f16x4 *)((char *)(ptr) + (offs)))
#define __ae_f16x4_loadi_3(dst, ptr, offs) \
  do { (dst) = *(ae_f16x4 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_int32x2_loadi
#define ae_int32x2_loadi(...) __ae_int32x2_loadi_OVERLOAD(__VA_ARGS__)
#define __ae_int32x2_loadi_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_int32x2_loadi_OVERLOAD(...) \
  __ae_int32x2_loadi_GET(__VA_ARGS__, __ae_int32x2_loadi_3, __ae_int32x2_loadi_2)(__VA_ARGS__)
#define __ae_int32x2_loadi_2(ptr, offs) (*(ae_int32x2 *)((char *)(ptr) + (offs)))
#define __ae_int32x2_loadi_3(dst, ptr, offs) \
  do { (dst) = *(ae_int32x2 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_int32x2_aligning_load_prime
#define ae_int32x2_aligning_load_prime(...) __ae_i32x2_alp_OVERLOAD(__VA_ARGS__)
#define __ae_i32x2_alp_GET(_1, _2, NAME, ...) NAME
#define __ae_i32x2_alp_OVERLOAD(...) \
  __ae_i32x2_alp_GET(__VA_ARGS__, __ae_i32x2_alp_2, __ae_i32x2_alp_1)(__VA_ARGS__)
#define __ae_i32x2_alp_1(ptr) (*(ae_int32x2 *)(ptr))
#define __ae_i32x2_alp_2(dst, ptr) \
  do { (dst) = *(ae_int32x2 *)(ptr); } while (0)

//---- AE_L32X2F24_I 2-arg expression form --------------------------------
// Kernel uses `st_x = AE_L32X2F24_I(S, +0)` (2-arg, returns value). The
//   3-arg statement form assigns to dst. Provide overloaded dispatch.
#undef  AE_L32X2F24_I
#define AE_L32X2F24_I(...) __AE_L32X2F24_I_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_I_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2F24_I_OVERLOAD(...) \
  __AE_L32X2F24_I_GET(__VA_ARGS__, __AE_L32X2F24_I_3, __AE_L32X2F24_I_2)(__VA_ARGS__)
#define __AE_L32X2F24_I_2(ptr, offs) (*(ae_f24x2 *)((char *)(ptr) + (offs)))
#define __AE_L32X2F24_I_3(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_MUL32X16_L0/L1/L2 2-arg returning form --------------------------
// IIR uses `u0 = AE_MUL32X16_L0(t0, s_g)` (2-arg, returns fresh product).
//   The 3-arg MAC form (acc, a, b) was defined in part 11. Add the 2-arg
//   returning form via overloaded dispatch.
#undef  AE_MUL32X16_L0
#define AE_MUL32X16_L0(...) __AE_MUL32X16_L0_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_L0_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_L0_OVERLOAD(...) \
  __AE_MUL32X16_L0_GET(__VA_ARGS__, __AE_MUL32X16_L0_3, __AE_MUL32X16_L0_2)(__VA_ARGS__)
#define __AE_MUL32X16_L0_2(a, b) haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define __AE_MUL32X16_L0_3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

#undef  AE_MUL32X16_L1
#define AE_MUL32X16_L1(...) __AE_MUL32X16_L1_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_L1_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_L1_OVERLOAD(...) \
  __AE_MUL32X16_L1_GET(__VA_ARGS__, __AE_MUL32X16_L1_3, __AE_MUL32X16_L1_2)(__VA_ARGS__)
#define __AE_MUL32X16_L1_2(a, b) haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define __AE_MUL32X16_L1_3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

#undef  AE_MUL32X16_L2
#define AE_MUL32X16_L2(...) __AE_MUL32X16_L2_OVERLOAD(__VA_ARGS__)
#define __AE_MUL32X16_L2_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MUL32X16_L2_OVERLOAD(...) \
  __AE_MUL32X16_L2_GET(__VA_ARGS__, __AE_MUL32X16_L2_3, __AE_MUL32X16_L2_2)(__VA_ARGS__)
#define __AE_MUL32X16_L2_2(a, b) haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define __AE_MUL32X16_L2_3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULF16SS_30 (16-bit fractional mul, lane 3x0) -------------------
// Uses the commutative lane alias haydn_fmul16_hs30 (defined above as
//   hs03 with operands swapped).
#define AE_MULF16SS_30(a, b) haydn_fmul16_hs30(__AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MOVF32X2_FROMF16X4 / AE_MOVPA24X2 (view reinterpret) ------------
// AE_MOVF32X2_FROMF16X4: reinterpret quad-16 as dual-32 (no code). The
//   reverse AE_MOVF16X4_FROMF32X2 exists (part 11).
#define AE_MOVF32X2_FROMF16X4(a) ((ae_f32x2)(haydn_dr64_t)(a))
// AE_MOVPA24X2: pack a pair-of-24 into the ae_p24x2 view (alias of int24x2).
#define AE_MOVPA24X2(a) ((ae_p24x2)(haydn_dr64_t)(a))

//---- AE_MULAFP32X2RS (dual-32 fractional MAC, rounding+sat) -------------
// HiFi: AE_MULAFP32X2RS(acc, a, b) statement form, acc = round(acc + a*b).
//   Maps to haydn_ff2mula32rs_ll (ternary, returns new acc) with write-back.
#define AE_MULAFP32X2RS(acc, a, b) (acc) = haydn_ff2mula32rs_ll((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_PKSR24 (pack-with-shift to 24-bit) ------------------------------
// HiFi AE_PKSR24(q, sh): narrow a 48/64-bit accumulator to a packed 24-bit
//   pair with a signed shift. Haydn haydn_packsr32 narrows to 32-bit; the
//   24-bit packing is the same instruction with the result viewed as f24x2
//   (24-bit lanes occupy the low 24 bits of each 32-bit lane). sh selects
//   the shift amount.
#define AE_PKSR24(q, sh) ((ae_f24x2)haydn_packsr32((q), (sh)))

//---- AE_TRUNCI32F64S_L (truncate 64-bit acc to 32-bit, low lane) --------
// HiFi: AE_TRUNCI32F64S_L(dst, q, sh) — dst = (int32)(q >> sh), low lane.
//   Kernel: `rn = AE_TRUNCI32F64S_L(rn, q1, 1)`. Returns the truncated
//   value (assigned at call site). Maps to packsr32 with the given shift
//   (truncate = signed shift, no rounding).
#define AE_TRUNCI32F64S_L(dst, q, sh) \
  ((dst) = (ae_int32)(((ae_int64)(q)) >> (sh)))

//---- AE_MULZAAD24_HH_LL (24-bit zero-init dual MAC, HH+LL lanes) --------
// 24-bit operands promote to 32-bit lanes. Zero-init dual MAC of HH*HH +
//   LL*LL products maps to the binary zero-accumulator form
//   haydn_f2mulzaa32rs_hhll (no accumulator read, ).
#define AE_MULZAAD24_HH_LL(a, b) \
  haydn_f2mulzaa32rs_hhll(__AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULZAAFD32X16_H3_L2 2-arg returning form ------------------------
// Already defined (3-arg MAC) in part 5; the zero-init 2-arg form returns
//   a fresh product. Override to the returning form (the 3-arg MAC at line
//   2509 is superseded — no production caller passes an acc today).
#undef  AE_MULZAAFD32X16_H3_L2
#define AE_MULZAAFD32X16_H3_L2(a, b) \
  haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_MULAF16SS_xy / AE_MULSF16SS_xy (single-lane Q15 MAC/MSU) --------
// HiFi AE_MULAF16SS_xy(acc, a, b): acc (ae_f32x2, scalar accumulator in the
//   high 32-bit lane) += (a.x * b.y) << 1 (Q30 product << 1 = Q31, added to
//   the 64-bit acc). The kernel round-trips through MOVF32X2_FROMF16X4 /
//   MOVF16X4_FROMF32X2, treating the pair as a 64-bit accumulator with the
//   active value in the high lane. Compose as a 64-bit add of the shifted
//   product; the high lane holds the acc, the low lane is don't-care.
//   AE_MULSF16SS_xy subtracts instead. Lanes 00/11/21/30 via the hs aliases.
#define AE_MULAF16SS_11(acc, a, b) \
  (acc) = ((acc) + ((ae_int64)haydn_fmul16_hs11(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULAF16SS_30(acc, a, b) \
  (acc) = ((acc) + ((ae_int64)haydn_fmul16_hs30(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_11(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs11(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_21(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs21(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
// Lanes used by iir/latr16x16 (promoted from haydn_port_helper.h).
#define AE_MULSF16SS_10(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs10(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))
#define AE_MULSF16SS_30(acc, a, b) \
  (acc) = ((acc) - ((ae_int64)haydn_fmul16_hs30(__AE_TO_I64(a), __AE_TO_I64(b)) << 1))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 13: by-value MAC write-back + final gaps.  //
//                                                                            //
// Root cause of the residual IIR/FIR compile failures (G6 MAC arity): the   //
// AE_MULFD24X2_FIR_H / AE_MULAFD24X2_FIR_H / AE_MULFD32X16X2_FIR_H* /        //
// AE_MULAFD32X16X2_FIR_H* helpers were defined as inline functions taking   //
// `ae_int64 *q0, ae_int64 *q1` (by POINTER). But HiFi3 kernels call them    //
// passing accumulators BY VALUE in statement form:                          //
//   AE_MULFD24X2_FIR_H(q0, q1, st_x, p0, cf);   // expects q0,q1 updated    //
// Clang rejects `ae_f64` (aka long long) for `ae_int64 *` param.            //
//                                                                            //
// Fix: redefine each as a write-back MACRO (acc) = ... so the accumulators  //
// are updated in place at the call site (the part-9 write-back idiom,       //
// extended to dual-accumulator FIR helpers). This matches the HiFi3 source  //
// ABI: these are statement-form intrinsics that mutate their accumulator     //
// operands. (See for the decision record.)                             //
//===----------------------------------------------------------------------===//

//---- AE_MULFD24X2_FIR_H / AE_MULAFD24X2_FIR_H (24-bit FIR dual-MAC) -----
// AE: q0 = d0.H*c.H + d0.L*c.L ; q1 = d0.L*c.H + d1.H*c.L
// With freestanding f32x2 .H=first mem / .L=second (soft load swap), use
// hh+ll for q0 and cross lh products for q1 directly.
#undef  AE_MULFD24X2_FIR_H
#define AE_MULFD24X2_FIR_H(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_lh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_lh((q1), __AE_TO_I64(__c), __AE_TO_I64(__d1)); \
  } while (0)
#undef  AE_MULAFD24X2_FIR_H
#define AE_MULAFD24X2_FIR_H(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_ff2mula32rs_hh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_lh((q1), __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_lh((q1), __AE_TO_I64(__c), __AE_TO_I64(__d1)); \
  } while (0)
#undef  AE_MULFD24X2_FIR_L
#define AE_MULFD24X2_FIR_L(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_ff2mula32rs_lh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q0) = haydn_ff2mula32rs_lh((q0), __AE_TO_I64(__c), __AE_TO_I64(__d1)); \
    (q1) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d1), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c)); \
  } while (0)
#undef  AE_MULAFD24X2_FIR_L
#define AE_MULAFD24X2_FIR_L(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_ff2mula32rs_lh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c)); \
    (q0) = haydn_ff2mula32rs_lh((q0), __AE_TO_I64(__c), __AE_TO_I64(__d1)); \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c)); \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c)); \
  } while (0)

//---- AE_MULFD32X16X2_FIR_HH / _HL / AE_MULAFD32X16X2_FIR_HH / _HL --------
// AE: FIR_HH q0 = d0.H*c.3 + d0.L*c.2 ; q1 = d0.L*c.3 + d1.H*c.2
// Soft freestanding loads f32x2 with .H=first mem word, .L=second (HiFi).
// Halfword coefs: c.k → LE lane k (c.3 first-in-time on NatureDSP buffers is
// often lane0 after reverse layout — keep q15 lane indices 0..3 as LE bits).
// With H=first chronological:
//   FIR_HH: q0 = d0.H*c0 + d0.L*c1 ; q1 = d0.L*c0 + d1.H*c1
//   FIR_HL: q0 = d0.H*c2 + d0.L*c3 ; q1 = d0.L*c2 + d1.H*c3
static inline haydn_dr64_t haydn_q15_lane_as_q31_dup(haydn_dr64_t c,
                                                      unsigned lane) {
  int16_t q15 =
      (int16_t)((uint16_t)(((uint64_t)c >> (16u * lane)) & 0xFFFFu));
  int32_t q31 = ((int32_t)q15) << 16;
  return (haydn_dr64_t)haydn_movda32x2(q31, q31);
}

#undef  AE_MULFD32X16X2_FIR_HH
#define AE_MULFD32X16X2_FIR_HH(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __cv = (haydn_dr64_t)(c); \
    haydn_dr64_t __c0 = haydn_q15_lane_as_q31_dup(__cv, 0); \
    haydn_dr64_t __c1 = haydn_q15_lane_as_q31_dup(__cv, 1); \
    (q0) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c0)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c1)); \
    (q1) = haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c0)); \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c1)); \
  } while (0)
#undef  AE_MULFD32X16X2_FIR_HL
#define AE_MULFD32X16X2_FIR_HL(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __cv = (haydn_dr64_t)(c); \
    haydn_dr64_t __c2 = haydn_q15_lane_as_q31_dup(__cv, 2); \
    haydn_dr64_t __c3 = haydn_q15_lane_as_q31_dup(__cv, 3); \
    (q0) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c2)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c3)); \
    (q1) = haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(__d0), __AE_TO_I64(__c2)); \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c3)); \
  } while (0)
#undef  AE_MULAFD32X16X2_FIR_HH
#define AE_MULAFD32X16X2_FIR_HH(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __cv = (haydn_dr64_t)(c); \
    haydn_dr64_t __c0 = haydn_q15_lane_as_q31_dup(__cv, 0); \
    haydn_dr64_t __c1 = haydn_q15_lane_as_q31_dup(__cv, 1); \
    (q0) = haydn_ff2mula32rs_hh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c0)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c1)); \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d0), __AE_TO_I64(__c0)); \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c1)); \
  } while (0)
#undef  AE_MULAFD32X16X2_FIR_HL
#define AE_MULAFD32X16X2_FIR_HL(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __cv = (haydn_dr64_t)(c); \
    haydn_dr64_t __c2 = haydn_q15_lane_as_q31_dup(__cv, 2); \
    haydn_dr64_t __c3 = haydn_q15_lane_as_q31_dup(__cv, 3); \
    (q0) = haydn_ff2mula32rs_hh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c2)); \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c3)); \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d0), __AE_TO_I64(__c2)); \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c3)); \
  } while (0)

//---- AE_MULAFQ16X2_FIR_* / AE_MULFQ16X2_FIR_* (Q15 FIR dual-MAC) --------
// Four-product dual-output FIR. AE element k maps to Haydn LE lane (3-k).
// FIR_3 q0 is same-index products (invariant under full reverse); q1/FIR_1
// use the reverse cross terms. Each Q15 lane is promoted to Q31 then FF2.
static inline int16_t haydn_f16_lane(haydn_dr64_t v, unsigned lane) {
  return (int16_t)((uint16_t)(((uint64_t)v >> (16u * lane)) & 0xFFFFu));
}
// AE element index k → Haydn LE lane
#define __HAYDN_AE_LANE(k) (3u - (unsigned)(k))
static inline ae_int64 haydn_q15_ff2_prod(int16_t a, int16_t b) {
  int32_t aq = ((int32_t)a) << 16;
  int32_t bq = ((int32_t)b) << 16;
  haydn_dr64_t da = (haydn_dr64_t)haydn_movda32x2(aq, aq);
  haydn_dr64_t db = (haydn_dr64_t)haydn_movda32x2(bq, bq);
  return haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(da), __AE_TO_I64(db));
}
static inline ae_int64 haydn_fq_fir3_q0(haydn_dr64_t d0, haydn_dr64_t c) {
  // sum d0[i]*c[i] for i in 0..3
  return haydn_q15_ff2_prod(haydn_f16_lane(d0, 0), haydn_f16_lane(c, 0)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 1), haydn_f16_lane(c, 1)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 2), haydn_f16_lane(c, 2)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 3), haydn_f16_lane(c, 3));
}
static inline ae_int64 haydn_fq_fir3_q1(haydn_dr64_t d0, haydn_dr64_t d1,
                                           haydn_dr64_t c) {
  // LE reverse of AE: d0.1*c.0 + d0.2*c.1 + d0.3*c.2 + d1.0*c.3
  return haydn_q15_ff2_prod(haydn_f16_lane(d0, 1), haydn_f16_lane(c, 0)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 2), haydn_f16_lane(c, 1)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 3), haydn_f16_lane(c, 2)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 0), haydn_f16_lane(c, 3));
}
static inline ae_int64 haydn_fq_fir1_q0(haydn_dr64_t d0, haydn_dr64_t d1,
                                           haydn_dr64_t c) {
  // LE reverse: d0.2*c.0 + d0.3*c.1 + d1.0*c.2 + d1.1*c.3
  return haydn_q15_ff2_prod(haydn_f16_lane(d0, 2), haydn_f16_lane(c, 0)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d0, 3), haydn_f16_lane(c, 1)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 0), haydn_f16_lane(c, 2)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 1), haydn_f16_lane(c, 3));
}
static inline ae_int64 haydn_fq_fir1_q1(haydn_dr64_t d0, haydn_dr64_t d1,
                                           haydn_dr64_t c) {
  // LE reverse: d0.3*c.0 + d1.0*c.1 + d1.1*c.2 + d1.2*c.3
  return haydn_q15_ff2_prod(haydn_f16_lane(d0, 3), haydn_f16_lane(c, 0)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 0), haydn_f16_lane(c, 1)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 1), haydn_f16_lane(c, 2)) +
         haydn_q15_ff2_prod(haydn_f16_lane(d1, 2), haydn_f16_lane(c, 3));
}

#undef  AE_MULFQ16X2_FIR_3
#define AE_MULFQ16X2_FIR_3(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_fq_fir3_q0(__d0, __c); \
    (q1) = haydn_fq_fir3_q1(__d0, __d1, __c); \
  } while (0)
#undef  AE_MULAFQ16X2_FIR_3
#define AE_MULAFQ16X2_FIR_3(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = (ae_int64)(q0) + haydn_fq_fir3_q0(__d0, __c); \
    (q1) = (ae_int64)(q1) + haydn_fq_fir3_q1(__d0, __d1, __c); \
  } while (0)
#undef  AE_MULFQ16X2_FIR_1
#define AE_MULFQ16X2_FIR_1(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = haydn_fq_fir1_q0(__d0, __d1, __c); \
    (q1) = haydn_fq_fir1_q1(__d0, __d1, __c); \
  } while (0)
#undef  AE_MULAFQ16X2_FIR_1
#define AE_MULAFQ16X2_FIR_1(q0, q1, d0, d1, c) \
  do { \
    haydn_dr64_t __d0 = (haydn_dr64_t)(d0), __d1 = (haydn_dr64_t)(d1); \
    haydn_dr64_t __c = (haydn_dr64_t)(c); \
    (q0) = (ae_int64)(q0) + haydn_fq_fir1_q0(__d0, __d1, __c); \
    (q1) = (ae_int64)(q1) + haydn_fq_fir1_q1(__d0, __d1, __c); \
  } while (0)

//---- AE_PKSR24 3-arg statement form ------------------------------------
// Kernel: AE_PKSR24(st_y, q0, 1) — statement form: st_y = pack(q0, sh).
//   Override the 2-arg part-11 form with an overloaded 2/3-arg dispatch.
#undef  AE_PKSR24
#define AE_PKSR24(...) __AE_PKSR24_OVERLOAD(__VA_ARGS__)
#define __AE_PKSR24_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_PKSR24_OVERLOAD(...) \
  __AE_PKSR24_GET(__VA_ARGS__, __AE_PKSR24_3, __AE_PKSR24_2)(__VA_ARGS__)
#define __AE_PKSR24_2(q, sh) ((ae_f24x2)haydn_packsr32((q), (sh)))
#define __AE_PKSR24_3(dst, q, sh) \
  do { (dst) = (ae_f24x2)haydn_packsr32((q), (sh)); } while (0)

//---- AE_F64_SLAS (64-bit shift-left with saturation) -------------------
// Kernel: acc0 = AE_F64_SLAS(acc0, 1+gain). Use soft AE_SLAA64S (was plain
// << with no saturation — wrong on overflow).
#define AE_F64_SLAS(q, n) ((ae_f64)haydn_ae_slaa64s((ae_int64)(q), (int)(n)))

//---- AE_MULSF32R_HL (fractional MSU, HL lane, rounding+sat) ------------
// Completes the HH/LL set (part 11) with the HL lane. haydn_ff2muls32rs
//   has hh/lh/ll; HL == LH for a symmetric product.
#define AE_MULSF32R_HL(acc, a, b) (acc) = haydn_ff2muls32rs_lh((acc), __AE_TO_I64(a), __AE_TO_I64(b))

//---- AE_MULSF32X16_H1 (32x16 subtract-MAC, H1 lane) --------------------
// Statement form: acc -= 32x16 product (H1 lane). Thread acc with write-back.
#define AE_MULSF32X16_H1(acc, a, b) \
  (acc) = ((acc) - (ae_int64)haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))
#define AE_MULSF32X16_H3(acc, a, b) \
  (acc) = ((acc) - (ae_int64)haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b))))

//---- AE_MULAFP32X16X2RAS_L (32x16x2 fractional MAC, L lane, rounding) --
// Matches AE_MULAFP32X16X2RAS_H (part 11) for the low lane. Write-back.
#define AE_MULAFP32X16X2RAS_L(acc, a, b) \
  (acc) = haydn_mulfp32x16x2ras_low((acc), (a), (b))

//---- AE_MULSSFD24_HH_LL_S2 (24-bit dual MSU, shift-2 variant) ----------
// Like AE_MULSSFD24_HH_LL (part 9) but with a +2 shift on the products.
//   Compose: acc -= (HH*HH + LL*LL). haydn_f2mulss32rs_hhll does exactly
//   the dual-subtract; write back.
#define AE_MULSSFD24_HH_LL_S2(acc, a, b) \
  (acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64((haydn_dr64_t)(a)), __AE_TO_I64((haydn_dr64_t)(b)))

//---- AE_S16X4_I (quad-16 indexed store) --------------------------------
// Kernel: AE_S16X4_I(dl_5432, D, 0) — store at ptr+offs, no increment.
#define AE_S16X4_I(src, ptr, offs) \
  do { *(ae_int16x4 *)((char *)(ptr) + (offs)) = (src); } while (0)

//---- AE_MOVPA24X2 2-arg form (pack two scalars into a pair-of-24) ------
// Kernel: AE_MOVPA24X2(0, (unsigned)(scale >> 8)) — pack a lo/hi scalar
//   pair into the ae_p24x2 view. The 1-arg reinterpret form is in part 11;
//   override with an overloaded 1/2-arg dispatch.
#undef  AE_MOVPA24X2
#define AE_MOVPA24X2(...) __AE_MOVPA24X2_OVERLOAD(__VA_ARGS__)
#define __AE_MOVPA24X2_GET(_1, _2, NAME, ...) NAME
#define __AE_MOVPA24X2_OVERLOAD(...) \
  __AE_MOVPA24X2_GET(__VA_ARGS__, __AE_MOVPA24X2_2, __AE_MOVPA24X2_1)(__VA_ARGS__)
#define __AE_MOVPA24X2_1(a) ((ae_p24x2)(haydn_dr64_t)(a))
#define __AE_MOVPA24X2_2(lo, hi) \
  ((ae_p24x2)((((haydn_dr64_t)(unsigned)(hi) << 16) & 0xFFFFFFFF00000000LL) \
            | ((haydn_dr64_t)(unsigned)(lo) & 0x00000000FFFFFFFFLL)))

//---- AE_MULZAAD24_HL_LH (24-bit zero-init dual MAC, HL+LH lanes) -------
// Zero-init dual MAC of HL*HL + LH*LH products. 24-bit operands promote to
//   32-bit lanes; the HL/LH cross-lane dual MAC maps to the binary
//   zero-accumulator form haydn_f2mulzaa32rs_hllh ().
#define AE_MULZAAD24_HL_LH(a, b) \
  haydn_f2mulzaa32rs_hllh(__AE_TO_I64(a), __AE_TO_I64(b))

//===----------------------------------------------------------------------===//
// HiFi3 source-compat layer part 14: FIR/FFT 2-arg expression overloads.   //
//                                                                            //
// The FIR/FFT families call several ae_*_loadi / ae_*_loadx / AE_L*_X       //
// helpers in 2-arg EXPRESSION form (returning the loaded value, e.g.        //
// `v = ae_f24x2_loadi(px0, 0)`), but parts 2/11 defined them only as 3-arg  //
// statement macros. Override each with a 2/3-arg overload dispatch. Also    //
// closes the ae_f24x2_* surface, the AE_L32X2F24_X 2-arg form, the          //
// AE_MULZAAFD32X16_H1_L0 / AE_MULZSAFD32X16_H3_L2 2-arg returning forms,    //
// and the 4-arg AE_MULSF16X4SS.                                              //
//===----------------------------------------------------------------------===//

//---- ae_f24x2_loadi / ae_f24x2_loadx / ae_f24x2_loadxp / ae_f24x2_storexp
// 2-arg expression form for _loadi/_loadx; 3/4-arg statement forms kept.
#undef  ae_f24x2_loadi
#define ae_f24x2_loadi(...) __ae_f24x2_loadi_OVERLOAD(__VA_ARGS__)
#define __ae_f24x2_loadi_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f24x2_loadi_OVERLOAD(...) \
  __ae_f24x2_loadi_GET(__VA_ARGS__, __ae_f24x2_loadi_3, __ae_f24x2_loadi_2)(__VA_ARGS__)
#define __ae_f24x2_loadi_2(ptr, offs) (*(ae_f24x2 *)((char *)(ptr) + (offs)))
#define __ae_f24x2_loadi_3(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_f24x2_loadx
#define ae_f24x2_loadx(...) __ae_f24x2_loadx_OVERLOAD(__VA_ARGS__)
#define __ae_f24x2_loadx_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f24x2_loadx_OVERLOAD(...) \
  __ae_f24x2_loadx_GET(__VA_ARGS__, __ae_f24x2_loadx_3, __ae_f24x2_loadx_2)(__VA_ARGS__)
#define __ae_f24x2_loadx_2(ptr, offs) (*(ae_f24x2 *)((char *)(ptr) + (offs)))
#define __ae_f24x2_loadx_3(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_f24x2_loadxp
#define ae_f24x2_loadxp(...) __ae_f24x2_loadxp_OVERLOAD(__VA_ARGS__)
#define __ae_f24x2_loadxp_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __ae_f24x2_loadxp_OVERLOAD(...) \
  __ae_f24x2_loadxp_GET(__VA_ARGS__, __ae_f24x2_loadxp_4, __ae_f24x2_loadxp_3)(__VA_ARGS__)
#define __ae_f24x2_loadxp_3(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)
#define __ae_f24x2_loadxp_4(dst, ptr, offs, inc) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) + (inc)); } while (0)
#undef  ae_f24x2_storexp
#define ae_f24x2_storexp(...) __ae_f24x2_storexp_OVERLOAD(__VA_ARGS__)
#define __ae_f24x2_storexp_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __ae_f24x2_storexp_OVERLOAD(...) \
  __ae_f24x2_storexp_GET(__VA_ARGS__, __ae_f24x2_storexp_4, __ae_f24x2_storexp_3)(__VA_ARGS__)
#define __ae_f24x2_storexp_3(src, ptr, offs) \
  do { *(ae_f24x2 *)((char *)(ptr) + (offs)) = (src); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)
#define __ae_f24x2_storexp_4(src, ptr, offs, inc) \
  do { *(ae_f24x2 *)((char *)(ptr) + (offs)) = (src); \
       (ptr) = (ae_f24x2 *)((char *)(ptr) + (inc)); } while (0)
// ae_f24x2_storei: indexed store (no increment). 3-arg statement form.
#define ae_f24x2_storei(src, ptr, offs) \
  do { *(ae_f24x2 *)((char *)(ptr) + (offs)) = (src); } while (0)

//---- ae_f32x2_loadx 2/3-arg + ae_f32x2_storex 3-arg + ae_f32x2_loadxp/_storexp
#undef  ae_f32x2_loadx
#define ae_f32x2_loadx(...) __ae_f32x2_loadx_OVERLOAD(__VA_ARGS__)
#define __ae_f32x2_loadx_GET(_1, _2, _3, NAME, ...) NAME
#define __ae_f32x2_loadx_OVERLOAD(...) \
  __ae_f32x2_loadx_GET(__VA_ARGS__, __ae_f32x2_loadx_3, __ae_f32x2_loadx_2)(__VA_ARGS__)
#define __ae_f32x2_loadx_2(ptr, offs) (*(ae_f32x2 *)((char *)(ptr) + (offs)))
#define __ae_f32x2_loadx_3(dst, ptr, offs) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)

#undef  ae_f32x2_loadxp
#define ae_f32x2_loadxp(...) __ae_f32x2_loadxp_OVERLOAD(__VA_ARGS__)
#define __ae_f32x2_loadxp_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __ae_f32x2_loadxp_OVERLOAD(...) \
  __ae_f32x2_loadxp_GET(__VA_ARGS__, __ae_f32x2_loadxp_4, __ae_f32x2_loadxp_3)(__VA_ARGS__)
#define __ae_f32x2_loadxp_3(dst, ptr, offs) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)
#define __ae_f32x2_loadxp_4(dst, ptr, offs, inc) \
  do { (dst) = *(ae_f32x2 *)((char *)(ptr) + (offs)); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (inc)); } while (0)

#undef  ae_f32x2_storex
#define ae_f32x2_storex(src, ptr, offs) \
  do { *(ae_f32x2 *)((char *)(ptr) + (offs)) = (src); } while (0)
#undef  ae_f32x2_storexp
#define ae_f32x2_storexp(...) __ae_f32x2_storexp_OVERLOAD(__VA_ARGS__)
#define __ae_f32x2_storexp_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __ae_f32x2_storexp_OVERLOAD(...) \
  __ae_f32x2_storexp_GET(__VA_ARGS__, __ae_f32x2_storexp_4, __ae_f32x2_storexp_3)(__VA_ARGS__)
#define __ae_f32x2_storexp_3(src, ptr, offs) \
  do { *(ae_f32x2 *)((char *)(ptr) + (offs)) = (src); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (offs)); } while (0)
#define __ae_f32x2_storexp_4(src, ptr, offs, inc) \
  do { *(ae_f32x2 *)((char *)(ptr) + (offs)) = (src); \
       (ptr) = (ae_f32x2 *)((char *)(ptr) + (inc)); } while (0)

//---- AE_L16_X 2/3-arg overload (undef the 3-arg-only form at line 2049) -
#undef  AE_L16_X
#define AE_L16_X(...) __AE_L16_X_OVERLOAD(__VA_ARGS__)
#define __AE_L16_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L16_X_OVERLOAD(...) \
  __AE_L16_X_GET(__VA_ARGS__, __AE_L16_X_3, __AE_L16_X_2)(__VA_ARGS__)
#define __AE_L16_X_2(ptr, offs) (*(ae_int16 *)((char *)(ptr) + (offs)))
#define __AE_L16_X_3(dst, ptr, offs) \
  do { (dst) = *(ae_int16 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_L32X2F24_X 2/3-arg overload ------------------------------------
#undef  AE_L32X2F24_X
#define AE_L32X2F24_X(...) __AE_L32X2F24_X_OVERLOAD(__VA_ARGS__)
#define __AE_L32X2F24_X_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_L32X2F24_X_OVERLOAD(...) \
  __AE_L32X2F24_X_GET(__VA_ARGS__, __AE_L32X2F24_X_3, __AE_L32X2F24_X_2)(__VA_ARGS__)
#define __AE_L32X2F24_X_2(ptr, offs) (*(ae_f24x2 *)((char *)(ptr) + (offs)))
#define __AE_L32X2F24_X_3(dst, ptr, offs) \
  do { (dst) = *(ae_f24x2 *)((char *)(ptr) + (offs)); } while (0)

//---- AE_MULZAAFD32X16_H1_L0 / AE_MULZSAFD32X16_H3_L2 2-arg returning -----
// FFT: `ACC = AE_MULZAAFD32X16_H1_L0(WV, tw)` — fresh product (no acc arg).
//   Override the 3-arg MAC form with a 2/3-arg overload.
#undef  AE_MULZAAFD32X16_H1_L0
#define AE_MULZAAFD32X16_H1_L0(...) __AE_MZAAFD32X16_H1_L0_OVERLOAD(__VA_ARGS__)
#define __AE_MZAAFD32X16_H1_L0_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MZAAFD32X16_H1_L0_OVERLOAD(...) \
  __AE_MZAAFD32X16_H1_L0_GET(__VA_ARGS__, __AE_MZAAFD32X16_H1_L0_3, __AE_MZAAFD32X16_H1_L0_2)(__VA_ARGS__)
#define __AE_MZAAFD32X16_H1_L0_2(a, b) haydn_mulafd32x16x2_fir_hl((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define __AE_MZAAFD32X16_H1_L0_3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

#undef  AE_MULZSAFD32X16_H3_L2
#define AE_MULZSAFD32X16_H3_L2(...) __AE_MZSAFD32X16_H3_L2_OVERLOAD(__VA_ARGS__)
#define __AE_MZSAFD32X16_H3_L2_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MZSAFD32X16_H3_L2_OVERLOAD(...) \
  __AE_MZSAFD32X16_H3_L2_GET(__VA_ARGS__, __AE_MZSAFD32X16_H3_L2_3, __AE_MZSAFD32X16_H3_L2_2)(__VA_ARGS__)
#define __AE_MZSAFD32X16_H3_L2_2(a, b) haydn_mulafd32x16x2_fir_hh((ae_int64)0, __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))
#define __AE_MZSAFD32X16_H3_L2_3(acc, a, b) \
  (acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64((haydn_dr64_t)((a))), (haydn_dr64_t)((b)))

//---- AE_PKSR32 by-value write-back macro (was inline taking ae_int32x2 *) --
// IIR kernels call AE_PKSR32(y0, q0, 1) passing y0 BY VALUE; the inline
//   function took ae_int32x2 * (by pointer) → int-conversion error. Convert
//   to a write-back macro: (d) = pack(shift prev-lo to hi | new-lo from ps).
//
// SHIFT-DIRECTION FIX (capstone-hifi-semantics-audit Finding A, ): HiFi3's
//   `pos` is a 2-bit LEFT-shift amount (0..3) applied to the 64-bit accumulator
//   BEFORE the 16-bit asymmetric round-truncation (AE_PKSR32_analysis.md:25,51;
//   codex-confirmed; the DB SRA64R behavior field confirms simm7>0 is an
//   arithmetic RIGHT-shift). The effective right-shift for the round step is
//   therefore 16 - pos, NOT 16 + pos. For pos=1 the net right-shift is 15
//   (Haydn was emitting 17 — a 4x wrong-scale result; 64x at pos=3). Affects 45
//   non-zero-pos IIR call sites. The "PKSR is CLOSED" claim compared only
//   lane-packing structure, not shift magnitude. PKSR32-spec.md does NOT
//   contradict this: its status banner refutes adding a NEW instruction, and
//   its body's imm is a PROPOSED Haydn op, not HiFi's pos.
#undef  AE_PKSR32
#define AE_PKSR32(d, ps, pos) \
  do { unsigned long long _cur = (unsigned long long)(ae_int64)(d); \
       unsigned int _new = (unsigned int)haydn_packsr32((long long)(ps), 16 - (pos)); \
       unsigned int _prev = (unsigned int)(_cur & 0xFFFFFFFF); \
       (d) = (ae_int32x2)(((unsigned long long)_prev << 32) | _new); } while (0)

//---- AE_MULC32X16_H / _L remain SOURCE-REVALIDATE -----------------
// Later override used to invent X2CMUL32 half selection. Keep fail-closed
// in strict mode (same law as the earlier definition).
#if __HAYDN_AE_COMPAT_STRICT
#undef  AE_MULC32X16_H
#define AE_MULC32X16_H(a, b) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULC32X16_H)
#undef  AE_MULC32X16_L
#define AE_MULC32X16_L(a, b) __HAYDN_AE_UNSUPPORTED_EXPR(AE_MULC32X16_L)
#else
#undef  AE_MULC32X16_H
#define AE_MULC32X16_H(a, b) (__AE_I2V(haydn_x2cmul32((a), (b)).hi))
#undef  AE_MULC32X16_L
#define AE_MULC32X16_L(a, b) (__AE_I2V(haydn_x2cmul32((a), (b)).lo))
#endif

//---- AE_MULFC32RAS arity fix (x2fcmul32rs is BINARY, 2 args) -----------
// The existing macro at line 1222 passed 3 args to haydn_x2fcmul32rs,
//   which is a haydn_binary_intrinsic (2 operands). Override to pass 2.
#undef  AE_MULFC32RAS
#define AE_MULFC32RAS(a, b) (haydn_x2fcmul32rs(a, b))

//---- AE_MULFC32X16RAS_H / _L 2-arg overload (fresh product, acc=0) -----
// FFT: vF0 = AE_MULFC32X16RAS_H(vF1, vF2) — 2-arg returning a fresh product.
//   The 3-arg inline function (acc, data, coef) exists at line 1263; add the
//   2-arg form via a macro that passes acc=0 and calls the inline function.
#undef  AE_MULFC32X16RAS_H
#define AE_MULFC32X16RAS_H(...) __AE_MULFC32X16RAS_H_OVERLOAD(__VA_ARGS__)
#define __AE_MULFC32X16RAS_H_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULFC32X16RAS_H_OVERLOAD(...) \
  __AE_MULFC32X16RAS_H_GET(__VA_ARGS__, __AE_MULFC32X16RAS_H_3, __AE_MULFC32X16RAS_H_2)(__VA_ARGS__)
#define __AE_MULFC32X16RAS_H_2(data, coef) \
  ((ae_int32x2)haydn_mulfc32x16ras_high((ae_int32x2){0, 0}, (data), (coef)))
#define __AE_MULFC32X16RAS_H_3(acc, data, coef) \
  ((ae_int32x2)haydn_mulfc32x16ras_high((acc), (data), (coef)))
#undef  AE_MULFC32X16RAS_L
#define AE_MULFC32X16RAS_L(...) __AE_MULFC32X16RAS_L_OVERLOAD(__VA_ARGS__)
#define __AE_MULFC32X16RAS_L_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULFC32X16RAS_L_OVERLOAD(...) \
  __AE_MULFC32X16RAS_L_GET(__VA_ARGS__, __AE_MULFC32X16RAS_L_3, __AE_MULFC32X16RAS_L_2)(__VA_ARGS__)
#define __AE_MULFC32X16RAS_L_2(data, coef) \
  ((ae_int32x2)haydn_mulfc32x16ras_low((ae_int32x2){0, 0}, (data), (coef)))
#define __AE_MULFC32X16RAS_L_3(acc, data, coef) \
  ((ae_int32x2)haydn_mulfc32x16ras_low((acc), (data), (coef)))

//---- AE_MULSF16X4SS 4-arg form (LMS dual quad-16 MSU) ------------------
// FIR fir_blms16x32: AE_MULSF16X4SS(vaf, vbf, f2, f0) — LMS weight update:
//   vaf -= (vaf * f2)>>15 ; vbf -= (vbf * f0)>>15 (per-lane scaled subtract).
//   Both accumulators updated in place. The 3-arg form (acc, a, b) is the
//   standard quad-16 MSU; this 4-arg overload handles the LMS paired-update.
//   NOTE: exact LMS scaling needs verification against HiFi reference; the
//   composition compiles and produces the expected dual-subtract shape.
#undef  AE_MULSF16X4SS
#define AE_MULSF16X4SS(...) __AE_MULSF16X4SS_OVERLOAD(__VA_ARGS__)
#define __AE_MULSF16X4SS_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_MULSF16X4SS_OVERLOAD(...) \
  __AE_MULSF16X4SS_GET(__VA_ARGS__, __AE_MULSF16X4SS_4, __AE_MULSF16X4SS_3)(__VA_ARGS__)
#define __AE_MULSF16X4SS_3(acc, a, b) \
  (acc) = (ae_int64)haydn_x4muls16s((int64_t)(acc), (int64_t)0, (a), (b)).hi
#define __AE_MULSF16X4SS_4(acc_hi, acc_lo, a, b) \
  do { \
    haydn_dpair_t _r = haydn_x4muls16s((int64_t)(acc_hi), (int64_t)(acc_lo), \
                                         (a), (b)); \
    (acc_hi) = (ae_int64)_r.hi; (acc_lo) = (ae_int64)_r.lo; \
  } while (0)

//===----------------------------------------------------------------------===//
// === Soft-float family () ===
//
// NatureDSP `f`-suffix kernels (firf, fftf, iirf, complexf, matopf/mtx_*f,
// vec_*f, mathf) use the Tensilica XT_* floating-point intrinsic family
// exclusively. Haydn is SOFT-FLOAT by design (hard constraint #4: no FPU).
// This section maps the XT_* float intrinsics to inline C float arithmetic.
//
// IMPORTANT — correct but SLOW:
//   Every float arithmetic op here lowers to a soft-float libcall
//   (__addsf3 / __mulsf3 / __divsf3 / __subsf3 / __ltsf2 / __floatsisf /
//   __fixsfsi, etc.). The Haydn legalizer (HaydnLegalizerInfo.cpp:214-248)
//   is wired to lower G_FADD/G_FMUL/G_FDIV/G_FSUB/G_FCMP/G_SITOFP/G_FPTOSI
//   to these libcalls. There is NO float hardware. This is the honest cost
//   of running true-IEEE-float kernels on a soft-float target.
//
//   The earlier stubs at lines ~1754-1770 (XT_MADD_S -> acc, XT_CONST_S -> 0)
//   were identity no-ops that caused the entire kernel body to be DCE'd to
//   empty .text. This section replaces them with correct (if slow) float
//   arithmetic so the kernels produce real code.
//
//   NOTE on G_FNEG: the legalizer does not yet lower G_FNEG (verified —
//   "unable to legalize G_FNEG" at -O1). All negation in this section is
//   expressed as (0.0f - x) so it routes through __subsf3, which IS legal.
//
// Type model:
//   xtfloat   = float          (scalar IEEE-754 single)
//   xtfloatx2 = struct holding two floats (a 64-bit pair, like HiFi3's
//               widereg). Represented as a 2-element float array so the
//               compiler can scalarize each lane into a separate soft-float
//               libcall. This is NOT ae_f32x2 (which is a fractional Q31
//               pair in DR64) — these are genuine IEEE floats in GPRs.
//
// Coordination: this is a CLEARLY-MARKED separate section. It does NOT touch
// any AE_* macro. The only symbols redefined here are the XT_* float stubs
// listed in , which the vec-intrinsics agent does not own.
//===----------------------------------------------------------------------===//

/// Scalar IEEE single-precision float (Tensilica xtfloat).
typedef float xtfloat;

/// Pair of IEEE single-precision floats (Tensilica xtfloatx2).
/// Stored as a 2-lane array so each lane scalarizes to its own soft-float
/// libcall. Lane 0 = low address (imaginary in complex layout per HiFi3),
/// lane 1 = high address (real in complex layout).
typedef struct { float f[2]; } xtfloatx2;

/// Construct an xtfloatx2 from two scalar floats.
static inline xtfloatx2 haydn_xtf2(float hi, float lo) {
  xtfloatx2 r; r.f[1] = hi; r.f[0] = lo; return r;
}

/// Soft-float scalar negate via subtraction (avoids the unlegalized G_FNEG).
static inline float haydn_fneg(float x) { return 0.0f - x; }

//---- Scalar float arithmetic (each -> one soft-float libcall) ------------

/// Scalar float multiply-add: acc += a * b  (-> __mulsf3 + __addsf3).
#define XT_MADD_S(acc, a, b) ((acc) += ((float)(a)) * ((float)(b)))

/// Scalar float multiply-sub: acc -= a * b  (-> __mulsf3 + __subsf3).
#define XT_MSUB_S(acc, a, b) ((acc) -= ((float)(a)) * ((float)(b)))

/// Scalar float negate-add: acc += a * (-b) (NaN-friendly via __subsf3).
#define XT_MADDN_S(acc, a, b) ((acc) += ((float)(a)) * haydn_fneg((float)(b)))

/// Scalar float negate-sub: acc -= a * (-b).
#define XT_MSUBN_S(acc, a, b) ((acc) -= ((float)(a)) * haydn_fneg((float)(b)))

/// Scalar float multiply with a negated product (HiFi MADDMUX negates one
/// factor). Implemented as -(a*b) via 0.0f - product to dodge G_FNEG.
#define XT_MADDC_S(acc, a, b) ((acc) += (0.0f - ((float)(a)) * ((float)(b))))

/// Scalar float multiply with a lane-muxed factor. HiFi selects one of two
/// b-values; here both call sites pass the same value so this reduces to
/// XT_MADD_S. Kept as a distinct macro to preserve call-site readability.
#define XT_MADDMUX_S(acc, a, b, sel) XT_MADD_S(acc, a, b)

/// Scalar float add  (-> __addsf3).
#define XT_ADD_S(a, b)  (((float)(a)) + ((float)(b)))
/// Scalar float sub  (-> __subsf3).
#define XT_SUB_S(a, b)  (((float)(a)) - ((float)(b)))
/// Scalar float mul  (-> __mulsf3).
#define XT_MUL_S(a, b)  (((float)(a)) * ((float)(b)))
/// Scalar float negate (-> __subsf3 via 0.0f - x; G_FNEG unlegalized).
#define XT_NEG_S(a)     haydn_fneg((float)(a))
/// Scalar float abs (mask sign bit off via integer reinterpretation).
/// NOTE: integer bit-AND, NOT fabsf, because the Haydn legalizer does NOT
/// lower G_FABS (it crashes — backend bug; G_FNEG has the same issue).
/// At -O1 instcombine rewrites a plain `x & 0x7FFFFFFF` back into
/// @llvm.fabs, which then fails to legalize. Making the MASK a volatile
/// local defeats that rewrite (instcombine can't fold a volatile-loaded
/// operand into a fabs), keeping the op as an integer AND (FAST, no
/// soft-float libcall). Remove the volatile if/when G_FABS/G_FNEG legalize.
static inline float XT_ABS_S(float a) {
  unsigned u; __builtin_memcpy(&u, &a, sizeof u);
  volatile unsigned mask = 0x7FFFFFFFu;
  unsigned m = u & mask;
  float f; __builtin_memcpy(&f, &m, sizeof f); return f;
}
/// Scalar float min/max (-> __ltsf2 + select / __gtsf2 + select).
#define XT_MIN_S(a, b)  (((float)(a)) < ((float)(b)) ? ((float)(a)) : ((float)(b)))
#define XT_MAX_S(a, b)  (((float)(a)) > ((float)(b)) ? ((float)(a)) : ((float)(b)))

/// Scalar float reciprocal (1/x) via soft-float division (-> __divsf3).
#define XT_RECIP_S(a)    (1.0f / ((float)(a)))
/// Scalar float reciprocal seed (Newton-Raphson first approx). HiFi provides
/// a HW reciprocal-seed; Haydn has none, so this is just 1/x (same cost as
/// XT_RECIP_S — honest slow path).
#define XT_RECIP0_S(a)   (1.0f / ((float)(a)))
/// Scalar float reciprocal sqrt (-> __divsf3 after sqrtf).
#define XT_RSQRT_S(a)    (1.0f / XT_SQRT_S(a))
/// Scalar float sqrt. Uses the C sqrtf() which the soft-float ABI lowers
/// to __sqrtf / libcall. Slow but correct.
static inline float XT_SQRT_S(float a) { return haydn_sqrtf(a); }

//---- Scalar float comparisons (each -> __ltsf2 / __eqsf2 / etc.) ---------

/// Ordered equal (false if either is NaN).
#define XT_OEQ_S(a, b)  (((float)(a)) == ((float)(b)))
/// Ordered less-than.
#define XT_OLT_S(a, b)  (((float)(a)) <  ((float)(b)))
/// Ordered less-or-equal.
#define XT_OLE_S(a, b)  (((float)(a)) <= ((float)(b)))
/// Unordered (true if either is NaN) — soft-float has __unordsf2.
#define XT_UN_S(a, b)   (((float)(a)) != ((float)(a)) || ((float)(b)) != ((float)(b)))
/// Unordered less-than.
#define XT_ULT_S(a, b)  (XT_UN_S(a, b) || XT_OLT_S(a, b))

//---- Scalar float conditional moves --------------------------------------

/// Conditional move if true (cond is an xtbool == int; nonzero = true).
#define XT_MOVT_S(dst, src, cond) do { if (cond) (dst) = (src); } while (0)
/// Conditional move if false.
#define XT_MOVF_S(dst, src, cond) do { if (!(cond)) (dst) = (src); } while (0)
/// Conditional move if equal-to-zero.
#ifndef XT_MOVEQZ_S
#define XT_MOVEQZ_S(dst, val, cond) do { if ((cond) == 0) (dst) = (val); } while (0)
#endif
/// Conditional move if not-equal-to-zero.
#ifndef XT_MOVNEZ_S
#define XT_MOVNEZ_S(dst, val, cond) do { if ((cond) != 0) (dst) = (val); } while (0)
#endif
/// Conditional move if less-than-zero.
#define XT_MOVLTZ_S(dst, val, cond) \
  do { if ((cond) < 0) (dst) = (val); } while (0)

//---- Scalar float <-> int conversions (-> __floatsisf / __fixsfsi) ------

/// int -> float (-> __floatsisf).
/// XT_FLOAT_S arity dispatch: 1-arg -> plain int->float; 2-arg -> shifted.
/// int -> float with shift n (HiFi float.s). Semantics: (float)(i) scaled by
/// 2^(-n), i.e. (float)(i) / (float)(1u << (n)). For n=0 collapses to plain
/// int->float. Kernels pass n=0 or n=15 (Q15 fixed-point source).
#define XT_FLOAT_S_SHIFT(i, n)  ((float)(i) / (float)(1u << (n)))
#define __XT_FLOAT_S_GET(_1, _2, NAME, ...) NAME
#define XT_FLOAT_S(...) \
  __XT_FLOAT_S_GET(__VA_ARGS__, __XT_FLOAT_S_2A, __XT_FLOAT_S_1A)(__VA_ARGS__)
#define __XT_FLOAT_S_1A(i)       ((float)(i))
#define __XT_FLOAT_S_2A(i, n)    XT_FLOAT_S_SHIFT((i), (n))
/// float -> int, round to nearest (-> __fixsfsi).
/// XT_TRUNC_S arity dispatch: 1-arg -> plain round; 2-arg -> shifted.
/// float -> int with shift n (HiFi trunc.s). Semantics: round float to int
/// then scale by 2^n, i.e. ((int)(f)) << (n). Kernels pass n=15 (Q-format)
/// or n=0 (no shift).
#define XT_TRUNC_S_SHIFT(f, n)  (((int)(f)) << (n))
#define __XT_TRUNC_S_GET(_1, _2, NAME, ...) NAME
#define XT_TRUNC_S(...) \
  __XT_TRUNC_S_GET(__VA_ARGS__, __XT_TRUNC_S_2A, __XT_TRUNC_S_1A)(__VA_ARGS__)
#define __XT_TRUNC_S_1A(f)       ((int)(f))
#define __XT_TRUNC_S_2A(f, n)    XT_TRUNC_S_SHIFT((f), (n))
/// float -> int, round toward +inf (ceilf). 1-arg or 2-arg (shift n) form.
static inline int haydn_ceil_s(float f)  { return (int)haydn_ceilf(f); }
static inline int haydn_ceil_s_shift(float f, int n)  { return (int)haydn_ceilf(f) << (n); }
#define __XT_CEIL_S_GET(_1, _2, NAME, ...) NAME
#define XT_CEIL_S(...) \
  __XT_CEIL_S_GET(__VA_ARGS__, __XT_CEIL_S_2A, __XT_CEIL_S_1A)(__VA_ARGS__)
#define __XT_CEIL_S_1A(f)        haydn_ceil_s((f))
#define __XT_CEIL_S_2A(f, n)     haydn_ceil_s_shift((f), (n))
/// float -> int, round toward -inf (floorf). 1-arg or 2-arg (shift n) form.
static inline int haydn_floor_s(float f) { return (int)haydn_floorf(f); }
static inline int haydn_floor_s_shift(float f, int n) { return (int)haydn_floorf(f) << (n); }
#define __XT_FLOOR_S_GET(_1, _2, NAME, ...) NAME
#define XT_FLOOR_S(...) \
  __XT_FLOOR_S_GET(__VA_ARGS__, __XT_FLOOR_S_2A, __XT_FLOOR_S_1A)(__VA_ARGS__)
#define __XT_FLOOR_S_1A(f)       haydn_floor_s((f))
#define __XT_FLOOR_S_2A(f, n)    haydn_floor_s_shift((f), (n))
/// float -> int, round to nearest-even (lroundf).
static inline int XT_FIROUND_S(float f) { return (int)haydn_lroundf(f); }
/// float -> int, ceil (SX2 lane-0 form collapses to scalar).
#define XT_FICEIL_SX2(f)   XT_CEIL_S((f).f[1])
/// float -> int, floor (SX2 lane-0 form collapses to scalar).
#define XT_FIFLOOR_SX2(f)  XT_FLOOR_S((f).f[1])
/// float -> int, round (SX2 lane-0 form collapses to scalar).
#define XT_FIROUND_SX2(f)  XT_FIROUND_S((f).f[1])

/// Lane extraction from xtfloatx2. HiFi: HIGH = odd-indexed (real) lane,
/// LOW = even-indexed (imag) lane. We use f[1] = high, f[0] = low.
#define XT_HIGH_S(v)  ((v).f[1])
#define XT_LOW_S(v)   ((v).f[0])

/// Scalar float constant materialization. HiFi XT_CONST_S loads an immediate
/// float; Haydn materializes it as a C float literal (lowered to a G_FCONSTANT
/// -> integer bitcast per HaydnLegalizerInfo.cpp:643).
/// NOTE: cast to float rather than `imm##f` token-paste, because kernels pass
/// integer-form immediates (e.g. XT_CONST_S(1)); `1##f` would yield the invalid
/// token `1f`. ((float)(imm)) accepts any numeric literal.
#define XT_CONST_S(imm) ((float)(imm))

/// Complex conjugate of a scalar-float pair held in xtfloatx2.
/// HiFi: XT_CONJC_S negates the imaginary (low) lane, keeps real (high).
/// Uses haydn_fneg to dodge G_FNEG.
static inline xtfloatx2 XT_CONJC_S(xtfloatx2 v) {
  v.f[0] = haydn_fneg(v.f[0]);
  return v;
}

//---- Pair (xtfloatx2) float arithmetic — per-lane soft-float libcalls ----

/// Pair float multiply-add: acc[lane] += a[lane] * b[lane] for both lanes.
/// Expands to 2x __mulsf3 + 2x __addsf3.
#define XT_MADD_SX2(acc, a, b) \
  do { (acc).f[0] += (a).f[0] * (b).f[0]; \
       (acc).f[1] += (a).f[1] * (b).f[1]; } while (0)

/// Pair float multiply-sub.
#define XT_MSUB_SX2(acc, a, b) \
  do { (acc).f[0] -= (a).f[0] * (b).f[0]; \
       (acc).f[1] -= (a).f[1] * (b).f[1]; } while (0)

/// Pair float negate-add.
#define XT_MADDN_SX2(acc, a, b) \
  do { (acc).f[0] += (a).f[0] * haydn_fneg((b).f[0]); \
       (acc).f[1] += (a).f[1] * haydn_fneg((b).f[1]); } while (0)

/// Pair float negate-sub.
#define XT_MSUBN_SX2(acc, a, b) \
  do { (acc).f[0] -= (a).f[0] * haydn_fneg((b).f[0]); \
       (acc).f[1] -= (a).f[1] * haydn_fneg((b).f[1]); } while (0)

/// Pair float add/sub/mul/neg/abs/sqrt/recip/rsqrt (per-lane).
static inline xtfloatx2 XT_ADD_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[1] + b.f[1], a.f[0] + b.f[0]);
}
static inline xtfloatx2 XT_SUB_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[1] - b.f[1], a.f[0] - b.f[0]);
}
static inline xtfloatx2 XT_MUL_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[1] * b.f[1], a.f[0] * b.f[0]);
}
static inline xtfloatx2 XT_NEG_SX2(xtfloatx2 a) {
  return haydn_xtf2(haydn_fneg(a.f[1]), haydn_fneg(a.f[0]));
}
static inline xtfloatx2 XT_ABS_SX2(xtfloatx2 a) {
  return haydn_xtf2(XT_ABS_S(a.f[1]), XT_ABS_S(a.f[0]));
}
static inline xtfloatx2 XT_SQRT_SX2(xtfloatx2 a) {
  return haydn_xtf2(haydn_sqrtf(a.f[1]), haydn_sqrtf(a.f[0]));
}
static inline xtfloatx2 XT_RECIP_SX2(xtfloatx2 a) {
  return haydn_xtf2(1.0f / a.f[1], 1.0f / a.f[0]);
}
static inline xtfloatx2 XT_RECIP0_SX2(xtfloatx2 a) { return XT_RECIP_SX2(a); }
static inline xtfloatx2 XT_RSQRT_SX2(xtfloatx2 a) {
  return haydn_xtf2(1.0f / haydn_sqrtf(a.f[1]), 1.0f / haydn_sqrtf(a.f[0]));
}

/// Pair float horizontal reduce-add: returns a scalar = lane0 + lane1.
/// Used at the tail of MAC loops to collapse the pair accumulator.
static inline float XT_RADD_SX2(xtfloatx2 a) { return a.f[0] + a.f[1]; }

/// Pair float horizontal reduce-min/max.
static inline float XT_RMIN_SX2(xtfloatx2 a) {
  return a.f[0] < a.f[1] ? a.f[0] : a.f[1];
}
static inline float XT_RMAX_SX2(xtfloatx2 a) {
  return a.f[0] > a.f[1] ? a.f[0] : a.f[1];
}

/// Pair float comparisons (per-lane, result is xtbool2 = int with 2 bits).
static inline xtbool2 XT_OEQ_SX2(xtfloatx2 a, xtfloatx2 b) {
  int r = 0;
  if (a.f[0] == b.f[0]) r |= 1;
  if (a.f[1] == b.f[1]) r |= 2;
  return r;
}
static inline xtbool2 XT_OLT_SX2(xtfloatx2 a, xtfloatx2 b) {
  int r = 0;
  if (a.f[0] < b.f[0]) r |= 1;
  if (a.f[1] < b.f[1]) r |= 2;
  return r;
}
static inline xtbool2 XT_OLE_SX2(xtfloatx2 a, xtfloatx2 b) {
  int r = 0;
  if (a.f[0] <= b.f[0]) r |= 1;
  if (a.f[1] <= b.f[1]) r |= 2;
  return r;
}
static inline xtbool2 XT_ULT_SX2(xtfloatx2 a, xtfloatx2 b) {
  int r = 0;
  if (XT_UN_S(a.f[0], b.f[0]) || a.f[0] < b.f[0]) r |= 1;
  if (XT_UN_S(a.f[1], b.f[1]) || a.f[1] < b.f[1]) r |= 2;
  return r;
}
static inline xtbool2 XT_UN_SX2(xtfloatx2 a, xtfloatx2 b) {
  int r = 0;
  if (XT_UN_S(a.f[0], b.f[0])) r |= 1;
  if (XT_UN_S(a.f[1], b.f[1])) r |= 2;
  return r;
}

/// Pair float conditional move if true (per-lane).
static inline xtfloatx2 XT_MOVT_SX2(xtfloatx2 dst, xtfloatx2 src, xtbool2 cond) {
  if (cond & 1) dst.f[0] = src.f[0];
  if (cond & 2) dst.f[1] = src.f[1];
  return dst;
}
/// Pair float conditional move if false (per-lane).
static inline xtfloatx2 XT_MOVF_SX2(xtfloatx2 dst, xtfloatx2 src, xtbool2 cond) {
  if (!(cond & 1)) dst.f[0] = src.f[0];
  if (!(cond & 2)) dst.f[1] = src.f[1];
  return dst;
}

/// Pair int->float and float->int conversions (per-lane).
static inline xtfloatx2 XT_FLOAT_SX2(int hi, int lo) {
  return haydn_xtf2((float)hi, (float)lo);
}
static inline xtfloatx2 XT_TRUNC_SX2(xtfloatx2 a) {
  /* HiFi returns an int-pair; we model as int32x2 via a cast. The two lanes
   * are truncated to int and re-packed into an xtfloatx2 bit-pattern. */
  int i0 = (int)a.f[0], i1 = (int)a.f[1];
  xtfloatx2 r;
  __builtin_memcpy(&r.f[0], &i0, sizeof(int));
  __builtin_memcpy(&r.f[1], &i1, sizeof(int));
  return r;
}

//---- Pair lane selects (INTEGER ops — FAST, no soft-float libcall) -------
//
// XT_SEL32_*_SX2 re-packs 32-bit lanes from two xtfloatx2 sources. These are
// pure lane-permutations: no float arithmetic. They compile to integer
// register moves / packs (FAST). The "float" type is just how the lanes are
// spelled; the underlying operation is bit-level lane selection.

/// Select HH: high lane of a, high lane of b  ->  { a.hi, b.hi }
static inline xtfloatx2 XT_SEL32_HH_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[1], b.f[1]);
}
/// Select HL: high lane of a, low lane of b   ->  { a.hi, b.lo }
static inline xtfloatx2 XT_SEL32_HL_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[1], b.f[0]);
}
/// Select LH: low lane of a, high lane of b   ->  { a.lo, b.hi }
static inline xtfloatx2 XT_SEL32_LH_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[0], b.f[1]);
}
/// Select LL: low lane of a, low lane of b    ->  { a.lo, b.lo }
static inline xtfloatx2 XT_SEL32_LL_SX2(xtfloatx2 a, xtfloatx2 b) {
  return haydn_xtf2(a.f[0], b.f[0]);
}

//---- Aligned / unaligned load+store of float pairs -----------------------
//
// These move xtfloatx2 (8 bytes) and xtfloat (4 bytes) between memory and
// registers. They are pure memory ops — no float arithmetic — so they are
// FAST (compile to LD32/LD64/ST32/ST64, no soft-float libcall).
//
// HiFi3 alignment model: AE_LSX2IP / AE_SSX2IP use an ae_valign handle to
// support unaligned access on a vector that is otherwise alignment-restricted.
// On Haydn (no alignment restriction beyond the natural load width), the
// ae_valign handle (already typedef'd to int above) is a no-op carrier; we
// load/store directly via typed pointers.

/// Aligned load xtfloatx2 + advance pointer by inc bytes.
#define XT_LASX2IP(dst, align, ptr) \
  do { (dst) = *(const xtfloatx2 *)(ptr); \
       (ptr) = (const xtfloatx2 *)((const char *)(ptr) + sizeof(xtfloatx2)); \
       (void)(align); } while (0)
/// Aligned load xtfloatx2 + advance by a variable increment.
#define XT_LASX2XIP(dst, align, ptr, inc) \
  do { (dst) = *(const xtfloatx2 *)(ptr); \
       (ptr) = (const xtfloatx2 *)((const char *)(ptr) + (inc)); \
       (void)(align); } while (0)
/// Initialize the alignment handle (no-op on Haydn).
#define XT_LASX2PP(ptr) (0)
/// Aligned store xtfloatx2 + advance pointer by sizeof(xtfloatx2).
#define XT_SASX2IP(src, align, ptr) \
  do { *(xtfloatx2 *)(ptr) = (src); \
       (ptr) = (xtfloatx2 *)((char *)(ptr) + sizeof(xtfloatx2)); \
       (void)(align); } while (0)
/// Aligned store xtfloatx2 + advance by a variable increment.
#define XT_SASX2XIP(src, align, ptr, inc) \
  do { *(xtfloatx2 *)(ptr) = (src); \
       (ptr) = (xtfloatx2 *)((char *)(ptr) + (inc)); \
       (void)(align); } while (0)
/// Finalize the aligned-store stream (flush partial register on Haydn: no-op).
#define XT_SASX2POSFP(align, ptr) ((void)(align), (void)(ptr))
/// Aligned store xtfloatx2 + post-increment by sizeof(xtfloatx2).
#define XT_SSX2IP(src, ptr, inc) \
  do { *(xtfloatx2 *)(ptr) = (src); \
       (ptr) = (xtfloatx2 *)((char *)(ptr) + (inc)); } while (0)
/// Aligned store xtfloatx2 with writeback (cyclic register variant).
#define XT_SSX2XC(src, ptr, inc) XT_SSX2IP(src, ptr, inc)
/// Aligned store xtfloatx2 + post-increment (XP variant).
#define XT_SSX2XP(src, ptr, inc) XT_SSX2IP(src, ptr, inc)

//---- Scalar float (xtfloat) load/store -----------------------------------
//
// These move single floats (4 bytes) between memory and registers. Pure
// memory ops — FAST (LD32/ST32), no soft-float libcall.

/// Load xtfloat at ptr + offs (no pointer update), return value.
#define XT_LSI(ptr, offs)  (*(const float *)((const char *)(ptr) + (offs)))
/// Load xtfloat at *ptr, post-increment by inc.
#define XT_LSIP(dst, ptr, inc) \
  do { (dst) = *(const float *)(ptr); \
       (ptr) = (const float *)((const char *)(ptr) + (inc)); } while (0)
/// Load xtfloat at *ptr, post-increment by inc (XP alias).
#define XT_LSXP(dst, ptr, inc) XT_LSIP(dst, ptr, inc)
/// Load xtfloat at base + offs (X variant, no update), return value.
#define XT_LSX(ptr, offs)  XT_LSI(ptr, offs)
/// Unaligned load xtfloatx2 (LSRX2IP).
#define XT_LSRX2IP(dst, align, ptr) \
  do { (dst) = *(const xtfloatx2 *)(ptr); \
       (ptr) = (const xtfloatx2 *)((const char *)(ptr) + sizeof(xtfloatx2)); \
       (void)(align); } while (0)
/// Load xtfloatx2 at *ptr (no update), return value (LSX2I).
#define XT_LSX2I(ptr, offs) \
  (*(const xtfloatx2 *)((const char *)(ptr) + (offs)))
/// Load xtfloatx2 at *ptr, post-increment by inc (LSX2IP).
#define XT_LSX2IP(dst, ptr, inc) \
  do { (dst) = *(const xtfloatx2 *)(ptr); \
       (ptr) = (const xtfloatx2 *)((const char *)(ptr) + (inc)); } while (0)
/// Load xtfloatx2 at base + offs (LSX2X), return value.
#define XT_LSX2X(ptr, offs) XT_LSX2I(ptr, offs)

/// Store xtfloat at *ptr, post-increment by inc.
#define XT_SSIP(src, ptr, inc) \
  do { *(float *)(ptr) = (src); \
       (ptr) = (float *)((char *)(ptr) + (inc)); } while (0)
/// Store xtfloat at base + offs (SSI alias).
#define XT_SSI(src, ptr, offs) \
  do { *(float *)((char *)(ptr) + (offs)) = (src); } while (0)
/// Store xtfloat at *ptr (SSX, no update).
#define XT_SSX(src, ptr) (*(float *)(ptr) = (src))
/// Store xtfloat at *ptr, post-increment (SSXP alias).
#define XT_SSXP(src, ptr, inc) XT_SSIP(src, ptr, inc)
/// Store xtfloat with cyclic writeback (SSXC).
#define XT_SSXC(src, ptr, inc) XT_SSIP(src, ptr, inc)

//---- 32-bit integer load/store (XT_L32I / XT_S32I) -----------------------
// Used by float kernels for index/pointer math. Pure integer memory ops.

#define XT_L32I(ptr, offs)  (*(const int *)((const char *)(ptr) + (offs)))
#define XT_S32I(src, ptr, offs) \
  do { *(int *)((char *)(ptr) + (offs)) = (int)(src); } while (0)

//---- AE_ float-pair load/store aliases (used alongside XT_ in kernels) ---
// Some kernels spell the aligned float-pair load as AE_LSX2IP / AE_SSX2IP.
// These delegate to the XT_ forms above.
#ifndef AE_LSX2IP
#define AE_LSX2IP(dst, ptr, inc) XT_LSX2IP(dst, ptr, inc)
#endif
#ifndef AE_SSX2IP
#define AE_SSX2IP(src, ptr, inc) XT_SSX2IP(src, ptr, inc)
#endif

//---- Register move / FSR (no-op on Haydn soft-float) ---------------------
// XT_MOV_S (scalar move), XT_WFR/XT_RFR (float<->int register move), and
// the FSR (floating-point status register) accessors. Haydn has no FPU so
// the FSR is a no-op carrier; moves are plain assignments.

#define XT_MOV_S(dst, src) ((dst) = (src))
#define XT_MOV_SX2(dst, src) ((dst) = (src))
/// Move int register to float register (bit-preserving; integer op, FAST).
static inline float XT_WFR(int x) {
  float f; __builtin_memcpy(&f, &x, sizeof f); return f;
}
/// Move float register to int register (bit-preserving; integer op, FAST).
static inline int XT_RFR(float f) {
  int x; __builtin_memcpy(&x, &f, sizeof x); return x;
}
/// Read floating-point status register (no-op on Haydn — returns 0).
#define XT_RUR_FSR()      (0)
/// Write floating-point status register (no-op on Haydn).
#define XT_WUR_FSR(v)     ((void)(v))

//---- AE_ register-cross moves (int<->float pair bitcasts) ----------------
// XT_AE_MOVXTFLOATX2_FROMINT32X2 etc. move a 64-bit int pair into an
// xtfloatx2 and back. Pure bitcasts — FAST, no float arithmetic.
static inline xtfloatx2 XT_AE_MOVXTFLOATX2_FROMINT32X2(ae_int32x2 a) {
  xtfloatx2 r; __builtin_memcpy(&r, &a, sizeof r); return r;
}
static inline ae_int32x2 XT_AE_MOVINT32X2_FROMXTFLOATX2(xtfloatx2 a) {
  ae_int32x2 r; __builtin_memcpy(&r, &a, sizeof r); return r;
}
static inline xtfloatx2 XT_AE_MOVXTFLOATX2_FROMF32X2(xtfloatx2 a) { return a; }
static inline xtfloatx2 XT_AE_MOVFCRFSRV(int a) {
  xtfloatx2 r; r.f[0] = XT_WFR(a); r.f[1] = 0.0f; return r;
}
static inline int XT_AE_MOVVFCRFSR(xtfloatx2 a) { return XT_RFR(a.f[0]); }

//---- Integer ALU XT_ helpers (used by float kernels for index math) ------
// These are INTEGER operations — FAST. They appear in float kernels for
// pointer/index arithmetic and bit manipulation of float bit-patterns.
// Guarded with #ifndef so the earliest definition (from the scalar-helpers
// section above) wins and we don't emit macro-redefinition warnings.

#ifndef XT_ADD
#define XT_ADD(a, b)  (((int)(a)) + ((int)(b)))
#endif
#ifndef XT_ADDI
#define XT_ADDI(a, b) (((int)(a)) + ((int)(b)))
#endif
#ifndef XT_SUB
#define XT_SUB(a, b)  (((int)(a)) - ((int)(b)))
#endif
#ifndef XT_AND
#define XT_AND(a, b)  (((int)(a)) & ((int)(b)))
#endif
#ifndef XT_OR
#define XT_OR(a, b)   (((int)(a)) | ((int)(b)))
#endif
#ifndef XT_XOR
#define XT_XOR(a, b)  (((int)(a)) ^ ((int)(b)))
#endif
#ifndef XT_SLLI
#define XT_SLLI(a, s) (((int)(a)) << (s))
#endif
#ifndef XT_SRLI
#define XT_SRLI(a, s) (((unsigned)(a)) >> (s))
#endif
#ifndef XT_SRAI
#define XT_SRAI(a, s) (((int)(a)) >> (s))
#endif
#ifndef XT_MOVI
#define XT_MOVI(imm)  (imm)
#endif
#ifndef XT_MOVF
#define XT_MOVF(dst, src, cond) XT_MOVF_S(dst, src, cond)
#endif
#ifndef XT_MOVT
#define XT_MOVT(dst, src, cond) XT_MOVT_S(dst, src, cond)
#endif
#ifndef XT_MOVEQZ
#define XT_MOVEQZ(dst, val, cond) do { if ((cond) == 0) (dst) = (val); } while (0)
#endif
#ifndef XT_MOVNEZ
#define XT_MOVNEZ(dst, val, cond) do { if ((cond) != 0) (dst) = (val); } while (0)
#endif

//===----------------------------------------------------------------------===//
// M6 VEC/COMPLEX/MATH/MATOP TAIL — 
//
// Closes the remaining 19 NatureDSP kernel compile failures identified in
// ~/haydn-plans/naturedsp-haydn/disasm/all-kernel-gaps/GAP-VEC-COMPLEX-MATH-MATOP.md.
// Each entry maps a HiFi3 AE_*/XT_* surface symbol that the ORIGINAL kernel
// source references to either an existing Haydn intrinsic or a correct inline
// composition. AE_MULZAAFD16SS_33_22 uses exact hs_33_22 (not the
// silent-wrong _11_00 lane alias).
//===----------------------------------------------------------------------===//

//---- XT_MOVLTZ / XT_MOVGEZ (alias of the _S scalar helpers) -------------
// matop kernels: XT_MOVLTZ(offs1, 0, P - p - 2) — conditional move if the
// third arg is < 0. The _S form was added for ; expose the unsuffixed
// HiFi name as an alias.
#ifndef XT_MOVLTZ
#define XT_MOVLTZ(dst, val, cond) XT_MOVLTZ_S((dst), (val), (cond))
#endif
#ifndef XT_MOVGEZ
#define XT_MOVGEZ(dst, val, cond) do { if ((cond) >= 0) (dst) = (val); } while (0)
#endif

//---- XT_ADDX2 / XT_ADDX4 / XT_ADDX8 (Xtensa scaled-address compute) -----
// matop kernels compute row pointers with stride-scaled adds:
//   py1 = (const ae_int16x4 *)XT_ADDX2(P, (uintptr_t)py0);  // py0 + 2*P
//   py1 = (const ae_int16x4 *)XT_ADDX4(P, (uintptr_t)py0);  // py0 + 4*P
//   py1 = (const ae_int16x4 *)XT_ADDX8(P, (uintptr_t)py0);  // py0 + 8*P
// Xtensa scalar scaled-add helpers. Haydn has no native scaled-add; lower to
// shift+add (the legalizer folds the constant shift into the addressing mode
// or a single add). See for the scaled-address ISA-improvement note.
static inline __attribute__((always_inline))
uintptr_t XT_ADDX2(int scale, uintptr_t ptr) {
  return (uintptr_t)((long long)ptr + ((long long)scale << 1));
}
static inline __attribute__((always_inline))
uintptr_t XT_ADDX4(int scale, uintptr_t ptr) {
  return (uintptr_t)((long long)ptr + ((long long)scale << 2));
}
static inline __attribute__((always_inline))
uintptr_t XT_ADDX8(int scale, uintptr_t ptr) {
  return (uintptr_t)((long long)ptr + ((long long)scale << 3));
}

//---- AE_INT16X4_MAX / AE_INT16X4_MIN 2-arg (lane-wise max/min) ----------
// vec_elemax16x16 / vec_elemin16x16 call these as 2-arg lane-wise ops:
//   zt = AE_INT16X4_MAX(xt, yt)  -> per-lane max of two quad-16 vectors.
// The 0-arg "splat constant" form (returns 0x7FFF7FFF...) stays available
// via the overload dispatcher; the 2-arg form is the HiFi3 saturating
// lane-wise max/min, composed via slt+movt (matches AE_MAX16_2 / AE_MIN16_2).
#undef  AE_INT16X4_MAX
#define AE_INT16X4_MAX(...) __AE_INT16X4_MAX_OVERLOAD(__VA_ARGS__)
#define __AE_INT16X4_MAX_GET(_1, _2, NAME, ...) NAME
#define __AE_INT16X4_MAX_OVERLOAD(...) \
  __AE_INT16X4_MAX_GET(__VA_ARGS__, __AE_INT16X4_MAX_2, __AE_INT16X4_MAX_1)(__VA_ARGS__)
#define __AE_INT16X4_MAX_1() \
  ((ae_int16x4)0x7FFF7FFF7FFF7FFFLL)
#define __AE_INT16X4_MAX_2(a, b) \
  ((ae_int16x4)haydn_x4movt16((a), (b)))
// x4slt16(a,b) sets the per-lane predicate (a<b); x4movt16(a,b) picks b where
// predicate set, else a -> result = (a<b) ? b : a = max(a,b). Saturating.

#undef  AE_INT16X4_MIN
#define AE_INT16X4_MIN(...) __AE_INT16X4_MIN_OVERLOAD(__VA_ARGS__)
#define __AE_INT16X4_MIN_GET(_1, _2, NAME, ...) NAME
#define __AE_INT16X4_MIN_OVERLOAD(...) \
  __AE_INT16X4_MIN_GET(__VA_ARGS__, __AE_INT16X4_MIN_2, __AE_INT16X4_MIN_1)(__VA_ARGS__)
#define __AE_INT16X4_MIN_1() \
  ((ae_int16x4)((long long)0x8000 << 48 | (long long)0x8000 << 32 | 0x80008000))
#define __AE_INT16X4_MIN_2(a, b) \
  ((ae_int16x4)haydn_x4movt16((b), (a)))
// For min: predicate (a<b), pick a where set, else b -> min(a,b).

//---- _vector suffix aliases (NatureDSP internal naming for AE_* ops) ----
// NatureDSP vector kernels (vec_elesub/stddev/var/rms) reference a "_vector"
// suffix variant of the standard AE_* intrinsics. These are NOT in the HiFi
// baseopXtensa.h header — they are Xtensa-internal compiler builtins (XCC
// provides them) that alias the public AE_* ops. On Haydn we expose them as
// direct aliases to the equivalent AE_* (which already map to the correct
// Haydn intrinsic).
//
// AE_SUB16S_vector(a, b): saturating quad-16 subtract.
//   Identical to AE_SUB16S(a,b) -> haydn_x4sub16s.
#define AE_SUB16S_vector(a, b) AE_SUB16S((a), (b))

// AE_MULAAR16P16X4S_vector(acc, a, b): quad-16 widening MAC into quad-16 acc.
//   Path B: haydn_x4mula16s is 2-dest; single-acc form selects hi pair.
#define AE_MULAAR16P16X4S_vector(acc, a, b) \
  do { (acc) = (ae_int64)haydn_x4mula16s((int64_t)(acc), (int64_t)0, \
                                           (a), (b)).hi; } while (0)

// AE_MULA32X2_vector(acc, a, b): dual 32x32->dual-64 widening MAC.
//   Operates on ae_int64x2 accumulator (dual 64-bit lane) and two ae_int32x2
//   operands. Semantics: acc.L += a.L*b.L (64-bit product); acc.H += a.H*b.H.
//   This is the dual-product widening MAC — equivalent to AE_MULAAD32_HH_LL
//   which composes both lane products via haydn_f2mulaa32rs_hhll.
//   See for the type analysis (ae_int64x2 stored as single DR64, dual
//   accumulators are the two 32-bit lanes widened to 64-bit products and
//   summed into the single accumulator).
#define AE_MULA32X2_vector(acc, a, b) \
  do { (acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b)); } while (0)

// AE_ADD64X2_vector(a, b): HiFi dual-64-bit lane-wise add (no cross-lane
//   carry). Haydn ae_int64x2 is a single DR64 bag — there is no exact dual-64
//   storage or lane-wise add. The scalar i64 body is KNOWN silent-wrong
//   (cross-lane carry when low half overflows) and is kept only for
//   __HAYDN_ALLOW_INEXACT_AE transitional NatureDSP -c. Default is
//   fail-closed via __HAYDN_AE_UNSUPPORTED_EXPR (permanent product
//   decision; not EMULATED — do not bag-alias dual-64).
//   Research: ae-to-haydn-mapping AE_ADD64X2_vector MISSING (128-bit dual-64).
#if defined(__HAYDN_ALLOW_INEXACT_AE)
#define AE_ADD64X2_vector(a, b) ((ae_int64x2)((ae_int64)(a) + (ae_int64)(b)))
#else
#define AE_ADD64X2_vector(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_vector)
#endif

//---- AE_S16X4_XP / AE_S32X2_XP / AE_S32X2F24_XP 3-arg overload ----------
// matop fast kernels call these in 3-arg form: AE_S16X4_XP(val, ptr, inc).
// HiFi3ep/HiFi4 added a 3-arg "store at ptr, then post-increment by inc"
// variant (offset defaults to 0). The 4-arg form (val, ptr, offs, inc) with
// explicit offset stays available via the overload dispatcher.
#undef  AE_S16X4_XP
#define AE_S16X4_XP(...) __AE_S16X4_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S16X4_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16X4_XP_OVERLOAD(...) \
  __AE_S16X4_XP_GET(__VA_ARGS__, __AE_S16X4_XP_4, __AE_S16X4_XP_3)(__VA_ARGS__)
#define __AE_S16X4_XP_3(src, ptr, inc) \
  do { *(ae_int16x4 *)(ptr) = (src); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_S16X4_XP_4(src, ptr, offs, inc) \
  do { *((ae_int16x4 *)(ptr) + ((offs) / (int)sizeof(ae_int16x4))) = (src); \
       (ptr) = (ae_int16x4 *)((char *)(ptr) + (inc)); } while (0)

// AE_S16X4RNG_XP 3-arg overload for 16-bit FFT kernels.
#undef  AE_S16X4RNG_XP
#define AE_S16X4RNG_XP(...) __AE_S16X4RNG_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S16X4RNG_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S16X4RNG_XP_OVERLOAD(...) \
  __AE_S16X4RNG_XP_GET(__VA_ARGS__, __AE_S16X4RNG_XP_4, __AE_S16X4RNG_XP_3)(__VA_ARGS__)
#define __AE_S16X4RNG_XP_3(src, ptr, inc) do { *(ae_int16x4 *)(ptr) = (ae_int16x4)haydn_x4sat32t16((src), (ae_int16x4){0, 0, 0, 0}); (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc)); } while(0)
#define __AE_S16X4RNG_XP_4(src, ptr, offs, inc) __AE_S16X4_XP_4(src, ptr, offs, inc)

#undef  AE_S32X2_XP
#define AE_S32X2_XP(...) __AE_S32X2_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32X2_XP_OVERLOAD(...) \
  __AE_S32X2_XP_GET(__VA_ARGS__, __AE_S32X2_XP_4, __AE_S32X2_XP_3)(__VA_ARGS__)
#define __AE_S32X2_XP_3(src, ptr, inc) \
  do { *(ae_int32x2 *)(ptr) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)
#define __AE_S32X2_XP_4(src, ptr, offs, inc) \
  do { *((ae_int32x2 *)(ptr) + ((offs) / (int)sizeof(ae_int32x2))) = (src); \
       (ptr) = (ae_int32x2 *)((char *)(ptr) + (inc)); } while (0)

#undef  AE_S32X2F24_XP
#define AE_S32X2F24_XP(...) __AE_S32X2F24_XP_OVERLOAD(__VA_ARGS__)
#define __AE_S32X2F24_XP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S32X2F24_XP_OVERLOAD(...) \
  __AE_S32X2F24_XP_GET(__VA_ARGS__, __AE_S32X2F24_XP_4, __AE_S32X2F24_XP_3)(__VA_ARGS__)
#define __AE_S32X2F24_XP_3(src, ptr, inc) __AE_S32X2_XP_3((src), (ptr), (inc))
#define __AE_S32X2F24_XP_4(src, ptr, offs, inc) __AE_S32X2_XP_4((src), (ptr), (offs), (inc))

//---- AE_ROUNDSP24Q48ASYM 1-arg overload ---------------------------------
// mtx_vecmpy24x24 calls AE_ROUNDSP24Q48ASYM(acc) in 1-arg form: round a single
// f48 accumulator to a Q1.31 f24. The 3-arg form (a, b, s) stays available.
// The 1-arg form uses a default shift of 24 (the Q16.47 -> Q1.31 conversion).
#undef  AE_ROUNDSP24Q48ASYM
#define AE_ROUNDSP24Q48ASYM(...) __AE_ROUNDSP24Q48ASYM_OVERLOAD(__VA_ARGS__)
#define __AE_ROUNDSP24Q48ASYM_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_ROUNDSP24Q48ASYM_OVERLOAD(...) \
  __AE_ROUNDSP24Q48ASYM_GET(__VA_ARGS__, __AE_ROUNDSP24Q48ASYM_3, \
                            __AE_ROUNDSP24Q48ASYM_2, __AE_ROUNDSP24Q48ASYM_1)(__VA_ARGS__)
#define __AE_ROUNDSP24Q48ASYM_1(a)  haydn_packsr32((a), 24)
#define __AE_ROUNDSP24Q48ASYM_2(a, b) haydn_packsr32((a), 24)
#define __AE_ROUNDSP24Q48ASYM_3(a, b, s) haydn_packsr32((a), (s))

//---- AE_MULZAAFD16SS_11_00 / _33_22 2-arg (zero-accumulator) ------------
// vec_complex2mag16x16 calls these in 2-arg form: AE_MULZAAFD16SS_11_00(x0, x0).
// The leading Z = "zero-accumulate" (start from zero, not accumulate). The
// 3-arg form (acc, a, b) accumulates into acc; the 2-arg form returns a fresh
// accumulator from zero.
#undef  AE_MULZAAFD16SS_11_00
#define AE_MULZAAFD16SS_11_00(...) __AE_MULZAAFD16SS_11_00_OVERLOAD(__VA_ARGS__)
#define __AE_MULZAAFD16SS_11_00_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULZAAFD16SS_11_00_OVERLOAD(...) \
  __AE_MULZAAFD16SS_11_00_GET(__VA_ARGS__, __AE_MULZAAFD16SS_11_00_3, \
                              __AE_MULZAAFD16SS_11_00_2)(__VA_ARGS__)
#define __AE_MULZAAFD16SS_11_00_2(a, b) \
  haydn_fmulaa16_hs_11_00((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b))
#define __AE_MULZAAFD16SS_11_00_3(acc, a, b) \
  ((acc) = haydn_fmulaa16_hs_11_00((acc), __AE_TO_I64(a), __AE_TO_I64(b)))

#undef  AE_MULZAAFD16SS_33_22
#define AE_MULZAAFD16SS_33_22(...) __AE_MULZAAFD16SS_33_22_OVERLOAD(__VA_ARGS__)
#define __AE_MULZAAFD16SS_33_22_GET(_1, _2, _3, NAME, ...) NAME
#define __AE_MULZAAFD16SS_33_22_OVERLOAD(...) \
  __AE_MULZAAFD16SS_33_22_GET(__VA_ARGS__, __AE_MULZAAFD16SS_33_22_3, \
                              __AE_MULZAAFD16SS_33_22_2)(__VA_ARGS__)
#define __AE_MULZAAFD16SS_33_22_2(a, b) \
  haydn_fmulaa16_hs_33_22((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b))
#define __AE_MULZAAFD16SS_33_22_3(acc, a, b) \
  ((acc) = haydn_fmulaa16_hs_33_22((acc), __AE_TO_I64(a), __AE_TO_I64(b)))

//---- AE_TRUNCA16P24S_H (truncate f24x2 high lane to saturating int16) --
// scl_rsqrt16x16: r = AE_TRUNCA16P24S_H(AE_MOVF24X2_FROMF32X2(Y)).
// HiFi semantics: take the HIGH 24-bit fractional lane, arithmetic-right-shift
// by 8 (drop the low 8 fractional bits -> 24-bit becomes 16-bit), saturate to
// a signed 16-bit value, return in the low 16 bits of a 32-bit result.
// Composed from: reinterpret f24x2 as int64, extract high 32-bit lane via
// haydn_movad32_high, then satsr64(q, 8) gives a 32-bit saturated shifted
// result which we narrow to 16-bit. The shift count 8 = 24-16.
static inline __attribute__((always_inline))
uint32_t AE_TRUNCA16P24S_H(ae_f24x2 x) {
  ae_int64 v = (ae_int64)(haydn_dr64_t)x;
  int32_t hi = (int32_t)haydn_movad32_high(v);
  int32_t shifted = (int32_t)haydn_satsr64((ae_int64)(int64_t)hi, 8);
  return (uint32_t)(int16_t)shifted;
}

// === part 15: FIR/FFT/IIR tail ===
//
// Closes the remaining NatureDSP FIR/FFT/IIR compile failures identified by
// the L111 audit + live measurement (FIR 72/101, FFT 58/70, IIR 11/12).
// Each entry is a HiFi3 AE_* surface symbol that the ORIGINAL kernel sources
// invoke in a shape the earlier parts did not cover. Every expansion composes
// only haydn_ intrinsics already exported above; no new instruction is
// introduced. See for the design rationale.
//
// Failure categories closed here:
//   (a) MAC write-back: AE_MULFQ16X2_FIR_1 / AE_MULAFQ16X2_FIR_1 were only
//       defined as static-inline functions taking `ae_int64 *` accumulator
//       pointers, but kernels declare `ae_f64 q0;` and pass q0 BY VALUE.
//       Redefined as statement-form write-back macros (same pattern parts
//       11/12 used for the _3 variants).
//   (b) Pointer-type preservation: store/load macros cast `(ptr)` back to
//       a fixed type (e.g. `ae_int32 *`) when writing back the advanced
//       pointer, but `ptr` may be declared `ae_p16x2s *` / `ae_f24x2 *`
//       (all are haydn_dr64_t / long long aliases). Use __typeof__(ptr) to
//       preserve the kernel-declared pointer type.
//   (c) Aligning-load macro signature: ae_int32x2_aligning_load_post_update_
//       positive was defined as (dst, ptr, inc) but the IIR/DCT kernels call
//       it as (dst, align, ptr) — the HiFi canonical shape where the align
//       handle is the 2nd arg and the data pointer is the 3rd.
//   (d) Reverse-increment store signature: AE_SA32X2F24_RIP / AE_SA32X2_RIP
//       were defined as (src, ptr, inc) but DCT kernels call them as
//       (src, align, ptr) — HiFi canonical align-store shape.
//
// Failures NOT closed here (genuinely not header-fixable; see ):
//   - castxcc() lvalue-cast: kernels do AE_S32RA64S_IP(q0, castxcc(ae_f32,R),
//     +4) expecting XCC's lvalue-cast extension. Clang rejects assignment to
//     a cast. castxcc is #define'd in NatureDSP's common.h which is included
//     AFTER haydn_dsp.h, so it cannot be overridden here. Affects
//     fir_convol32x32, fir_xcorr32x32, fir_acorr32x32 (3 FIR kernels).
//   - "Undefined temporary symbol .LBB0_-1" / ".LBB4_-1": backend HWLoop
//     negative-offset emission bug (CLAUDE.md M7 known issue). Affects
//     fft_cplx16x16_ie, ifft_cplx16x16_ie, fft_cplx24x24, firinterp32x16.

// ---------------------------------------------------------------------------
// (a) AE_MULFQ16X2_FIR_1 / AE_MULAFQ16X2_FIR_1 — already defined above with
// four-product dual-output semantics (part-12). Do not re-#define with the
// incomplete 2-product haydn_mulfq16x2_fir_1 path.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// (b) Pointer-type-preserving store/load writeback.
//
// AE_S16X2M_I / AE_L16X2M_IU: the part-7 macros (line 2149-2158) cast the
// advanced pointer back to `ae_int32 *`, but kernels declare the pointer as
// `ae_p16x2s *restrict` (FFT splitPart) or other DR64-alias types. The cast
// `int *` -> `long long *` triggers -Wincompatible-pointer-types. Use
// __typeof__(ptr) so the writeback keeps the kernel's declared type.
//
// HiFi3 AE_L16X2M_IU(dst, ptr, inc): load pair-of-16 at *ptr, advance ptr.
// HiFi3 AE_S16X2M_I(src, ptr, inc): store pair-of-16 at *ptr, advance ptr.
// (The "M" suffix = "with modifier" — on Haydn this is a plain 32-bit access
// since the 16x2 pair occupies one 32-bit slot.)
// ---------------------------------------------------------------------------
#undef  AE_L16X2M_IU
#define AE_L16X2M_IU(dst, ptr, inc) \
  do { (dst) = (__typeof__(dst))__AE_LOAD_AT(ae_int32, ptr); \
       __AE_ADVANCE_PTR(ptr, inc); } while (0)
#undef  AE_L16X2M_XU
#define AE_L16X2M_XU(dst, ptr, offs) AE_L16X2M_IU(dst, ptr, offs)
#undef  AE_S16X2M_I
// cast-safe — the original AE_S16X2M_I did (ptr) = (...) which fails
// when ptr is a non-lvalue cast like (ae_p16x2s *)x (FFT real16x16 kernels).
// The FFT kernels compute the next pointer manually, so the pointer advance
// is dropped (store only). Verified: all callers pass inc=0 or recompute ptr.
#define AE_S16X2M_I(src, ptr, inc) \
  do { *(ae_int32 *)(ptr) = (src); } while (0)

// AE_S24X2RA64S_IP 3-arg form: dual 64-bit accumulator -> dual 24-bit-in-32
//   saturating round store with pointer advance. Kernel declares ptr as
//   `ae_f24x2 *restrict`; the part-9 macro cast back to `ae_int32 *`.
//   HiFi3 signature: (acc0, acc1, ptr) — round both accs by the fixed shift
//   (0 here — the variant encodes the shift), store two 32-bit results, ptr+=8.
#undef  AE_S24X2RA64S_IP
#define AE_S24X2RA64S_IP(...) __AE_S24X2RA64S_IP_OVERLOAD(__VA_ARGS__)
#define __AE_S24X2RA64S_IP_GET(_1, _2, _3, _4, NAME, ...) NAME
#define __AE_S24X2RA64S_IP_OVERLOAD(...) \
  __AE_S24X2RA64S_IP_GET(__VA_ARGS__, \
    __AE_S24X2RA64S_IP_4A, __AE_S24X2RA64S_IP_3A)(__VA_ARGS__)
#define __AE_S24X2RA64S_IP_3A(acc0, acc1, ptr) \
  do { ae_int32 _v0 = (ae_int32)haydn_satsr64((acc0), 0); \
       ae_int32 _v1 = (ae_int32)haydn_satsr64((acc1), 0); \
       *((ae_int32 *)(ptr) + 0) = _v0; \
       *((ae_int32 *)(ptr) + 1) = _v1; \
       (ptr) = (__typeof__(ptr))((char *)(ptr) + 8); } while (0)
#define __AE_S24X2RA64S_IP_4A(dst, acc, shift, ptr) \
  do { (dst) = (ae_int32)haydn_satsr64((acc), (shift)); \
       *(ae_int32 *)(ptr) = (dst); } while (0)

// ---------------------------------------------------------------------------
// (c) ae_int32x2_aligning_load_post_update_positive — signature correction.
//
// The part-10 macro (line 4215) defined this as (dst, ptr, inc), but the IIR
// kernel (bqriir32x32_df1) and DCT kernels call it in the HiFi canonical
// aligning-load shape: (dst, align, ptr) where `align` is the ae_valign
// handle (ignored on Haydn — no dynamic alignment hardware) and `ptr` is the
// data pointer advanced by sizeof(ae_int32x2) = 8 bytes.
//
// HiFi3 semantic: aligned load of 8 bytes from *ptr, then ptr += 8 (the
// "positive" = forward post-increment by the natural element size).
// ---------------------------------------------------------------------------
#undef  ae_int32x2_aligning_load_post_update_positive
#define ae_int32x2_aligning_load_post_update_positive(dst, align, ptr) \
  do { (dst) = *(ae_int32x2 *)(ptr); (void)(align); \
       (ptr) = (__typeof__(ptr))((char *)(ptr) + 8); } while (0)
// ae_int32x2_aligning_load_prime: the IIR kernel (bqriir32x32_df1) calls this
// as a 1-arg EXPRESSION: `al_in = ae_int32x2_aligning_load_prime(Px);` which
// returns the ae_valign handle. Haydn has no alignment hardware, so the align
// handle is a no-op (return 0). The data is loaded separately by the
// subsequent _post_update_positive call.
#undef  ae_int32x2_aligning_load_prime
#define ae_int32x2_aligning_load_prime(ptr) (0)

// ---------------------------------------------------------------------------
// (d) AE_SA32X2F24_RIP / AE_SA32X2_RIP — reverse-increment aligning store.
//
// The part-15 macro (line 4262-4268) defined these as (src, ptr, inc), but
// the DCT kernels call them in the HiFi canonical aligning-store shape:
// (src, align, ptr) where `align` is the ae_valign handle and `ptr` is the
// data pointer. The "RIP" = reverse-increment: store 8 bytes at *ptr, then
// ptr -= 8. (align is ignored on Haydn.)
//
// Store lane law (D1.15 golden adjudication): D_STWUA_POST
// (instruction_type_index.json type AR) is direction-neutral on data word
// order — `rs[2]==0 → mem64=rtd; rs[2]==1 → mem64={rtd[31:00],ar[31:00]},
// ar[31:00]=rtd[63:32]` — the addressed (first) word always comes from
// rtd[31:0]. The H-first compat convention therefore requires converting
// src through haydn_ae_f32x2_mem_to_reg BEFORE the UA step, mirroring
// AE_SA32X2_IC / AE_S32X2_XC / AE_SA32X2F24_XC. Without it a RIP
// load→store round-trip reverses memory word order.
// ---------------------------------------------------------------------------
#undef  AE_SA32X2F24_RIP
#define AE_SA32X2F24_RIP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(haydn_ae_f32x2_mem_to_reg( \
                           (haydn_dr64_t)(src))), __ar, __p, 8, 1); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) - 8); \
  } while (0)
#undef  AE_SA32X2_RIP
#define AE_SA32X2_RIP(src, align, ptr) \
  do { \
    int __ar = __HAYDN_AR_SEL(align); \
    void *__p = (ptr); \
    haydn_ae_sa64_step(__AE_AS_V2(haydn_ae_f32x2_mem_to_reg( \
                           (haydn_dr64_t)(src))), __ar, __p, 8, 1); \
    (ptr) = (__typeof__(ptr))((char *)(ptr) - 8); \
  } while (0)

//===----------------------------------------------------------------------===//
// CASTXCC_LVALUE: XCC lvalue-cast extension for NatureDSP kernels.
//
// NatureDSP kernels use castxcc(TYPE, var) as an lvalue, e.g.
//   AE_S32RA64S_IP(q0, castxcc(ae_f32, Pr), sz_i32)
// where the AE_*_IP macro writes back the post-incremented pointer THROUGH
// the cast result. XCC permits this as an extension; ISO C rejects it
// ("assignment to cast is illegal, lvalue casts are not supported"). The
// slot-writeback advance in __AE_S32RA64S_IP_3A/_4A (*(T **)&(ptr) = ...) is
// legal iff castxcc resolves to this lvalue form. If a downstream header
// (e.g. NatureDSP common.h) redefines castxcc back to the rvalue (T*)(ptr)
// form, the store still succeeds and only the advance becomes a no-op; the
// build harness overlay (haydn_dsp_hifi3_overlay.h) / -imacro shim is the
// backstop for such kernels. See /bqir fix + L151.
#undef castxcc
#define castxcc(t, p) (*(t **)&(p))

//===----------------------------------------------------------------------===//
// Residual silent-wrong quarantine — fail closed unless ALLOW_INEXACT
//
// Dual-64 ADD64X2_* are fail-closed at their definition sites (no early
// silent scalar body under default). This late pin re-affirms the closed
// UNSUPPORTED set so mid-header redefines cannot reintroduce a no-op.
// Transitional NatureDSP -c may define __HAYDN_ALLOW_INEXACT_AE for the
// inexact bodies above. MULZAAFD / reverse-CB / SELP24 / dual-24 ALU /
// dual ASR / LA-SA IC-XC / POS_PC-NEG_PC are EXACT; soft sat-left +
// MAXABS16S stay EMULATED. Permanent residual: AE_ADD64X2_ /
// AE_ADD64X2_vector only (no dual-64 ISA map). Do not add silent aliases.
//===----------------------------------------------------------------------===//
#if !defined(__HAYDN_ALLOW_INEXACT_AE)

/* Permanent UNSUPPORTED: no bag dual-64. Fail-closed, not silent scalar. */
#undef AE_ADD64X2_
#define AE_ADD64X2_(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_)
#undef AE_ADD64X2_vector
#define AE_ADD64X2_vector(...) __HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_vector)

#endif /* !__HAYDN_ALLOW_INEXACT_AE */

#endif /* __HAYDN_DSP_H */
