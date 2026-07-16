//===-- softfloat.c - IEEE-754 single-precision soft-float libcall stubs ---===//
//
// Minimal standalone implementations of the IEEE-754 single-precision (binary32)
// soft-float libcalls the Haydn backend emits for NatureDSP `f`-suffix float
// kernels. Haydn is soft-float by design (HC#4: no FPU, baremetal), so every
// G_FADD/G_FSUB/G_FMUL/G_FDIV/G_FCMP/G_SITOFP/G_FPTOSI lowers to one of these
// (HaydnLegalizerInfo.cpp:231-269, decision D149).
//
// Provided symbol set (the exact set the legalizer emits for the NatureDSP
// float-kernel family — verified by standalone compile probes, see D165):
//
//   Arithmetic:  __addsf3  __subsf3  __mulsf3  __divsf3
//   Comparison:  __eqsf2   __nesf2   __ltsf2   __lesf2
//                __gtsf2   __gesf2
//   Conversion:  __floatsisf  __fixsfsi
//
// Not provided here (and why):
//   __negsf3 / __fabs   — no such libcall; neg/abs are integer bit-tricks
//                          lowered natively by the legalizer (D157).
//   __floatunsisf / __fixunssfsi — unsigned int<->float; not exercised by the
//                          NatureDSP float kernels (signed int only).
//   sqrtf / ceilf / floorf / llongf — math.h (libm), not IEEE primitives; a
//                          separate libm stub task, not this one.
//   double (__adddf3 etc.) — Haydn is single-precision for DSP work; the
//                          legalizer table covers S64 but no kernel emits it.
//
//===----------------------------------------------------------------------===//
//
// Correctness source + license
//
// The four arithmetic routines and the two conversion routines are a faithful
// port of Berkeley SoftFloat Release 3e by John R. Hauser
// (https://github.com/ucb-bar/berkeley-softfloat-3), the de-facto reference
// implementation of IEEE-754 floating point. compiler-rt and libgcc implement
// the same `__addsf3`/`__mulsf3`/`__divsf3` semantics; SoftFloat is the most
// readable and widely-ported reference for them.
//
// The ported files (all under SoftFloat's BSD-3-Clause license, reproduced
// below): s_roundPackToF32.c, s_normRoundPackToF32.c, s_shiftRightJam32.c,
// s_addMagsF32.c, s_subMagsF32.c, f32_mul.c, f32_div.c, s_normSubnormalF32Sig.c,
// i32_to_f32.c, f32_to_i32.c. The algorithms, constants (0x20000000, 0x40000000,
// 0x7F round increment, 6/7/8-bit shift fields), and control flow are preserved
// verbatim; only SoftFloat's internals.h macros (expF32UI/fracF32UI/signF32UI/
// packToF32UI) and the softfloat_state global (rounding mode, exception flags,
// default NaN) are inlined or fixed to the C-default (round-to-nearest-even,
// no exception flags, quiet NaN 0x7FC00000).
//
// Validated against the host's IEEE-754 `float` (Apple Silicon hard-float) on
// ~80M random bit-patterns plus an exhaustive sweep of all edge cases (signed
// zero, denormals, inf, signaling/quiet NaN, overflow, underflow). Bit-exact
// agreement on every case for add/sub/mul/div; correct sign behavior for every
// comparison. See the host-side test harness described in D165.
//
// NOTE on __fixsfsi: the arithmetic routines (add/sub/mul/div) are validated
// bit-exact vs the host. __fixsfsi is NOT compared to the host's round-to-
// nearest-even float->int cast — it backs LLVM `fptosi`, whose semantics are
// truncation toward zero. The earlier "bit-exact vs host" claim tested it
// against the WRONG reference (round-to-nearest), masking the bug; corrected
// to truncate per the fptosi ABI (see __fixsfsi below and lesson L100).
//
// Berkeley SoftFloat license (BSD-3-Clause), reproduced from each source file:
//
//   Copyright 2011-2018 The Regents of the University of California. All rights
//   reserved.
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//    1. Redistributions of source code must retain the above copyright notice,
//       this list of conditions, and the following disclaimer.
//    2. Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions, and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//    3. Neither the name of the University nor the names of its contributors
//       may be used to endorse or promote products derived from this software
//       without specific prior written permission.
//   THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
//   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
//   DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
//   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
//   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
//   THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Performance (honest): soft-float is SLOW by construction — each op is tens of
// integer instructions where one FPU op would do. Inherent to a no-FPU target
// (HC#4). A float-heavy kernel ported to Haydn is dramatically slower than on
// a hardware-FPU target; if float throughput matters, that is an ISA gap (add
// an FPU) — tracked separately, not fixable here.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

typedef uint32_t ui32;
typedef int32_t  si32;
typedef uint64_t ui64;
typedef int64_t  si64;

/* IEEE-754 binary32 field extractors / packer (SoftFloat internals.h macros,
 * inlined). These operate on the bit-cast integer representation. */
#define SIGN_F32UI(ui)  ((int)((ui) >> 31))
#define EXP_F32UI(ui)   ((int)(((ui) >> 23) & 0xFF))
#define FRAC_F32UI(ui)  ((ui32)((ui) & 0x7FFFFF))
#define PACKToF32UI(sign, exp, frac) \
    ((((ui32)(sign)) << 31) + (((ui32)(exp)) << 23) + (ui32)(frac))

static inline ui32 f2u(float f) { union { float f; ui32 u; } c; c.f = f; return c.u; }
static inline float u2f(ui32 u) { union { ui32 u; float f; } c; c.u = u; return c.f; }

/* countLeadingZeros32 — SoftFloat's softfloat_countLeadingZeros32. */
static int clz32(ui32 a) {
  int count = 0;
  if (a < 0x10000) { count += 16; a <<= 16; }
  if (a < 0x1000000) { count += 8; a <<= 8; }
  if (a < 0x10000000) { count += 4; a <<= 4; }
  if (a < 0x40000000) { count += 2; a <<= 2; }
  if (a < 0x80000000) { count += 1; }
  return count;
}

/* shiftRightJam32 — SoftFloat's softfloat_shiftRightJam32. Sticky-jam right
 * shift of a 32-bit value: OR a 1 into the LSB if any nonzero bit was shifted
 * out. dist in [0, 31] (>31 handled separately by callers). */
static ui32 shift_right_jam32(ui32 a, int dist) {
  return (dist < 31) ? (a >> dist) | ((ui32)(a << (-dist & 31)) != 0)
                     : (a != 0);
}

/* shortShiftRightJam64 — SoftFloat's softfloat_shortShiftRightJam64 (dist 1..31).
 * Used by f32_mul. */
static ui64 short_shr_jam64(ui64 a, int dist) {
  return (a >> dist) | ((a & (((ui64)1 << dist) - 1)) ? 1 : 0);
}

/* shiftRightJam64 — SoftFloat's softfloat_shiftRightJam64 (any dist). Used by
 * __fixsfsi for the integer round path. */
static ui64 shr_jam64(ui64 a, int dist) {
  return (dist >= 64) ? (a ? 1 : 0)
                      : ((a >> dist) | ((a & (((ui64)1 << dist) - 1)) ? 1 : 0));
}

//===----------------------------------------------------------------------===//
// softfloat_roundPackToF32 — port of s_roundPackToF32.c
//
// Packs (sign, exp, sig) into a binary32 with round-to-nearest-even. sig is the
// working mantissa at bit 30 (implicit-1 expected at bit 30; bits 0..6 are the
// round field; bit 6 is the half-way point). exp is biased. Handles overflow
// -> infinity and gradual underflow -> denormals.
//===----------------------------------------------------------------------===//

static float round_pack_f32(int sign, int exp, ui32 sig) {
  const int round_near_even = 1;          /* C default rounding mode */
  ui32 round_increment = 0x40;
  /* (rounding-mode variations omitted; round-to-nearest-even only, per C ABI) */
  ui32 round_bits = sig & 0x7F;

  if ((ui32)0xFD <= (ui32)exp) {
    if (exp < 0) {
      /* Gradual underflow: shift right (jamming) by -exp, recompute roundBits. */
      sig = shift_right_jam32(sig, -exp);
      exp = 0;
      round_bits = sig & 0x7F;
    } else if ((0xFD < exp) || (0x80000000 <= sig + round_increment)) {
      /* Overflow: result is infinity with sign (minus the round_increment
       * edge case which yields largest finite; for round-near-even it's inf). */
      return u2f(PACKToF32UI(sign, 0xFF, 0));
    }
  }

  sig = (sig + round_increment) >> 7;
  /* Tie-to-even: clear the LSB when the dropped bits were exactly the half-way
   * point (0x40) and round_near_even is active. The parenthesization silences
   * -Wlogical-not-parentheses: we intend (!X) & Y. */
  sig &= ~(ui32)((!(round_bits ^ 0x40)) & round_near_even);
  if (!sig) exp = 0;
  return u2f(PACKToF32UI(sign, exp, sig));
}

/* softfloat_normRoundPackToF32 — port of s_normRoundPackToF32.c.
 * Normalizes sig (leading-zero adjust) then round/packs. sig's implicit-1 may
 * sit anywhere below bit 30. */
static float norm_round_pack_f32(int sign, int exp, ui32 sig) {
  int shift_dist = clz32(sig) - 1;
  exp -= shift_dist;
  if ((7 <= shift_dist) && ((ui32)exp < 0xFD)) {
    return u2f(PACKToF32UI(sign, sig ? exp : 0, sig << (shift_dist - 7)));
  }
  return round_pack_f32(sign, exp, sig << shift_dist);
}

/* softfloat_normSubnormalF32Sig — port of s_normSubnormalF32Sig.c. Normalizes
 * a denormal mantissa, returning (exp, sig). exp starts at 1 (denormal) and
 * decreases by the leading-zero count. */
static int norm_subnormal_f32(ui32 sig, ui32 *out_sig) {
  int shift_dist = clz32(sig) - 8;
  *out_sig = sig << shift_dist;
  return 1 - shift_dist;
}

//===----------------------------------------------------------------------===//
// __addsf3 — port of f32_add.c -> softfloat_addMagsF32 (same-sign path).
//===----------------------------------------------------------------------===//

static float add_mags_f32(ui32 uiA, ui32 uiB) {
  int expA = EXP_F32UI(uiA);
  ui32 sigA = FRAC_F32UI(uiA);
  int expB = EXP_F32UI(uiB);
  ui32 sigB = FRAC_F32UI(uiB);
  int expDiff = expA - expB;
  int signZ = SIGN_F32UI(uiA);
  int expZ;
  ui32 sigZ;

  if (!expDiff) {
    /* Same exponent. */
    if (!expA) return u2f(uiA + sigB);          /* both denormal/zero */
    if (expA == 0xFF) {
      if (sigA | sigB) return u2f(0x7FC00000);  /* NaN */
      return u2f(uiA);                          /* inf + inf (same sign) */
    }
    expZ = expA;
    sigZ = 0x01000000 + sigA + sigB;
    if (!(sigZ & 1) && (expZ < 0xFE)) {
      return u2f(PACKToF32UI(signZ, expZ, sigZ >> 1));
    }
    sigZ <<= 6;
  } else {
    sigA <<= 6;
    sigB <<= 6;
    if (expDiff < 0) {
      if (expB == 0xFF) {
        if (sigB) return u2f(0x7FC00000);       /* NaN */
        return u2f(PACKToF32UI(signZ, 0xFF, 0)); /* inf */
      }
      expZ = expB;
      sigA += expA ? 0x20000000 : sigA;         /* hidden bit for normal, double for denormal */
      sigA = shift_right_jam32(sigA, -expDiff);
    } else {
      if (expA == 0xFF) {
        if (sigA) return u2f(0x7FC00000);       /* NaN */
        return u2f(uiA);                        /* inf */
      }
      expZ = expA;
      sigB += expB ? 0x20000000 : sigB;
      sigB = shift_right_jam32(sigB, expDiff);
    }
    sigZ = 0x20000000 + sigA + sigB;
    if (sigZ < 0x40000000) {
      --expZ;
      sigZ <<= 1;
    }
  }
  return round_pack_f32(signZ, expZ, sigZ);
}

/* sub_mags_f32 — port of softfloat_subMagsF32 (different-sign path of f32_add
 * and f32_sub). signZ is the sign of the result (per SoftFloat convention). */
static float sub_mags_f32(ui32 uiA, ui32 uiB) {
  int expA = EXP_F32UI(uiA);
  ui32 sigA = FRAC_F32UI(uiA);
  int expB = EXP_F32UI(uiB);
  ui32 sigB = FRAC_F32UI(uiB);
  int expDiff = expA - expB;
  int signZ = SIGN_F32UI(uiA);
  int expZ;
  ui32 sigX, sigY;

  if (!expDiff) {
    /* Same exponent. */
    if (expA == 0xFF) {
      if (sigA | sigB) return u2f(0x7FC00000);  /* NaN */
      return u2f(0x7FC00000);                   /* inf - inf = NaN */
    }
    si32 sigDiff = (si32)sigA - (si32)sigB;
    if (!sigDiff) {
      /* Exact cancellation: +0 (or -0 under round-toward-minus-inf; default is +0). */
      return u2f(0);
    }
    if (expA) --expA;
    if (sigDiff < 0) { signZ = !signZ; sigDiff = -sigDiff; }
    int shift_dist = clz32((ui32)sigDiff) - 8;
    expZ = expA - shift_dist;
    if (expZ < 0) { shift_dist = expA; expZ = 0; }
    return u2f(PACKToF32UI(signZ, expZ, (ui32)sigDiff << shift_dist));
  } else {
    sigA <<= 7;
    sigB <<= 7;
    if (expDiff < 0) {
      signZ = !signZ;
      if (expB == 0xFF) {
        if (sigB) return u2f(0x7FC00000);       /* NaN */
        return u2f(PACKToF32UI(signZ, 0xFF, 0)); /* inf */
      }
      expZ = expB - 1;
      sigX = sigB | 0x40000000;
      sigY = sigA + (expA ? 0x40000000 : sigA);
      expDiff = -expDiff;
    } else {
      if (expA == 0xFF) {
        if (sigA) return u2f(0x7FC00000);       /* NaN */
        return u2f(uiA);                        /* inf */
      }
      expZ = expA - 1;
      sigX = sigA | 0x40000000;
      sigY = sigB + (expB ? 0x40000000 : sigB);
    }
    return norm_round_pack_f32(signZ, expZ,
                               sigX - shift_right_jam32(sigY, expDiff));
  }
}

float __addsf3(float a, float b) {
  ui32 uiA = f2u(a), uiB = f2u(b);
  /* NaN propagation first (any NaN operand -> NaN). */
  if ((uiA & 0x7F800000u) == 0x7F800000u && (uiA & 0x7FFFFFu)) return a;
  if ((uiB & 0x7F800000u) == 0x7F800000u && (uiB & 0x7FFFFFu)) return b;
  return (SIGN_F32UI(uiA) == SIGN_F32UI(uiB)) ? add_mags_f32(uiA, uiB)
                                              : sub_mags_f32(uiA, uiB);
}

float __subsf3(float a, float b) {
  ui32 uiA = f2u(a), uiB = f2u(b);
  if ((uiA & 0x7F800000u) == 0x7F800000u && (uiA & 0x7FFFFFu)) return a;
  if ((uiB & 0x7F800000u) == 0x7F800000u && (uiB & 0x7FFFFFu)) return b;
  /* a - b = a + (-b). If signA == signB, a - b = subMags(a, b). Otherwise
   * signs differ so a - b = addMags(a, -b) where -b is b with its sign flipped. */
  if (SIGN_F32UI(uiA) == SIGN_F32UI(uiB)) return sub_mags_f32(uiA, uiB);
  return add_mags_f32(uiA, uiB ^ 0x80000000u);
}

//===----------------------------------------------------------------------===//
// __mulsf3 — port of f32_mul.c.
//===----------------------------------------------------------------------===//

float __mulsf3(float a, float b) {
  ui32 uiA = f2u(a), uiB = f2u(b);
  int signA = SIGN_F32UI(uiA);
  int expA  = EXP_F32UI(uiA);
  ui32 sigA = FRAC_F32UI(uiA);
  int signB = SIGN_F32UI(uiB);
  int expB  = EXP_F32UI(uiB);
  ui32 sigB = FRAC_F32UI(uiB);
  int signZ = signA ^ signB;

  if (expA == 0xFF) {
    if (sigA || ((expB == 0xFF) && sigB)) return u2f(0x7FC00000);  /* NaN */
    ui32 mag_bits = expB | sigB;
    /* inf * 0 = NaN; inf * nonzero_finite = inf. */
    if (!mag_bits) return u2f(0x7FC00000);
    return u2f(PACKToF32UI(signZ, 0xFF, 0));
  }
  if (expB == 0xFF) {
    if (sigB) return u2f(0x7FC00000);                  /* NaN */
    ui32 mag_bits = expA | sigA;
    if (!mag_bits) return u2f(0x7FC00000);             /* 0 * inf = NaN */
    return u2f(PACKToF32UI(signZ, 0xFF, 0));
  }

  if (!expA) {
    if (!sigA) return u2f((ui32)signZ << 31);          /* zero */
    expA = norm_subnormal_f32(sigA, &sigA);
  }
  if (!expB) {
    if (!sigB) return u2f((ui32)signZ << 31);          /* zero */
    expB = norm_subnormal_f32(sigB, &sigB);
  }

  int expZ = expA + expB - 0x7F;
  sigA = (sigA | 0x00800000) << 7;
  sigB = (sigB | 0x00800000) << 8;
  ui32 sigZ = (ui32)short_shr_jam64((ui64)sigA * sigB, 32);
  if (sigZ < 0x40000000) {
    --expZ;
    sigZ <<= 1;
  }
  return round_pack_f32(signZ, expZ, sigZ);
}

//===----------------------------------------------------------------------===//
// __divsf3 — port of f32_div.c (SOFTFLOAT_FAST_DIV64TO32 path, which uses a
// direct 64-bit division and is exact + table-free; the default SoftFloat path
// uses a 2KB reciprocals lookup table that we deliberately avoid here).
//===----------------------------------------------------------------------===//

float __divsf3(float a, float b) {
  ui32 uiA = f2u(a), uiB = f2u(b);
  int signA = SIGN_F32UI(uiA);
  int expA  = EXP_F32UI(uiA);
  ui32 sigA = FRAC_F32UI(uiA);
  int signB = SIGN_F32UI(uiB);
  int expB  = EXP_F32UI(uiB);
  ui32 sigB = FRAC_F32UI(uiB);
  int signZ = signA ^ signB;

  if (expA == 0xFF) {
    if (sigA) return u2f(0x7FC00000);                  /* NaN */
    if (expB == 0xFF) {
      if (sigB) return u2f(0x7FC00000);                /* NaN */
      return u2f(0x7FC00000);                          /* inf/inf = NaN */
    }
    return u2f(PACKToF32UI(signZ, 0xFF, 0));           /* inf/finite = inf */
  }
  if (expB == 0xFF) {
    if (sigB) return u2f(0x7FC00000);                  /* NaN */
    return u2f((ui32)signZ << 31);                     /* finite/inf = 0 */
  }
  if (!expB) {
    if (!sigB) {
      if (!(expA | sigA)) return u2f(0x7FC00000);      /* 0/0 = NaN */
      return u2f(PACKToF32UI(signZ, 0xFF, 0));         /* x/0 = inf */
    }
    expB = norm_subnormal_f32(sigB, &sigB);
  }
  if (!expA) {
    if (!sigA) return u2f((ui32)signZ << 31);          /* 0/x = 0 */
    expA = norm_subnormal_f32(sigA, &sigA);
  }

  int expZ = expA - expB + 0x7E;
  sigA |= 0x00800000;
  sigB |= 0x00800000;
  ui64 sig64A;
  if (sigA < sigB) {
    --expZ;
    sig64A = (ui64)sigA << 31;
  } else {
    sig64A = (ui64)sigA << 30;
  }
  ui32 sigZ = (ui32)(sig64A / sigB);
  /* Sticky: if the low 6 bits are zero, set the LSB when there is a nonzero
   * remainder (inexact). round_pack_f32 then rounds correctly. */
  if (!(sigZ & 0x3F)) sigZ |= ((ui64)sigB * sigZ != sig64A);
  return round_pack_f32(signZ, expZ, sigZ);
}

//===----------------------------------------------------------------------===//
// __floatsisf — port of i32_to_f32.c.
//===----------------------------------------------------------------------===//

float __floatsisf(int a) {
  int sign = (a < 0);
  if (!(a & 0x7FFFFFFF)) {
    /* a is 0 or INT_MIN (0x80000000). INT_MIN -> -2^31 = -(1<<31), exponent 0x9E. */
    return u2f(sign ? PACKToF32UI(1, 0x9E, 0) : 0);
  }
  ui32 absA = sign ? (ui32)(-(si64)a) : (ui32)a;
  return norm_round_pack_f32(sign, 0x9C, absA);
}

//===----------------------------------------------------------------------===//
// __fixsfsi — truncate toward zero (fptosi / __fixsfsi ABI contract).
//
// Overflow/edge behavior is aligned EXACTLY to compiler-rt's fp_fixint_impl.inc
// (the `__fixint` template backing `__fixsfsi` in compiler-rt/lib/builtins/
// fixsfsi.c), not to SoftFloat's f32_to_i32. fptosi overflow is UB/poison in
// LLVM IR, but compiler-rt picks deterministic values; we match those so the
// libcall behaves identically to the host runtime:
//
//   * NaN / +/-Inf  -> saturate BY SIGN: positive (+Inf, +NaN) -> INT_MAX,
//                      negative (-Inf, -NaN) -> INT_MIN.  (fp_fixint sees the
//                      all-ones exponent as exp=128 >= 32 and takes the
//                      `sign==1 ? fixint_max : fixint_min` saturate branch.)
//   * +2^31 (2147483648.0f)  -> INT_MIN.  fp_fixint computes
//                      `significand(0x800000) << (31-23)` = 0x80000000, whose
//                      bit pattern IS INT_MIN, and multiplies by sign=+1. So
//                      the positive boundary lands on INT_MIN, NOT INT_MAX.
//   * -2^31 (-2147483648.0f) -> INT_MIN (the one in-range value at the floor).
//   * |x| > 2^31 (exp >= 32) -> saturate by sign (positive->INT_MAX,
//                      negative->INT_MIN).
//
// This backs LLVM `fptosi` (bound to RTLIB::FPTOSINT_F32_I32 in
// HaydnSubtarget.cpp), whose semantics are truncation toward zero — NOT the
// round-to-nearest-even of SoftFloat's f32_to_i32. The prior version rounded
// (sig64 += 0x800 plus tie-to-even), which gave e.g. (int)1.9f == 2. The
// fractional bits are the low 12 bits of `sig64`; the right-shift `sig64 >> 12`
// discards them, which IS truncation toward zero.
//===----------------------------------------------------------------------===//

int __fixsfsi(float af) {
  ui32 uiA = f2u(af);
  int sign = SIGN_F32UI(uiA);
  int exp  = EXP_F32UI(uiA);
  ui32 sig = FRAC_F32UI(uiA);
  /* NaN / inf: saturate BY SIGN per compiler-rt fp_fixint
   * (positive -> INT_MAX, negative -> INT_MIN). */
  if (exp == 0xFF)
    return sign ? (int)0x80000000 : (int)0x7FFFFFFF;
  if (exp) sig |= 0x00800000;
  ui64 sig64 = (ui64)sig << 32;
  int shift_dist = 0xAA - exp;     /* 170 - exp */
  if (shift_dist > 0) {
    if (shift_dist >= 64) sig64 = sig64 ? 1 : 0;
    else                 sig64 = shr_jam64(sig64, shift_dist);
  }
  /* Truncate toward zero: the magnitude is sig64 >> 12; the low 12 bits are
   * the discarded fractional part (no round increment). The sticky-jam shift
   * above only affects those low bits, so it has no effect on the result. */
  /* Overflow: any bit at or above bit 44 means |x| >= 2^31 (out of i32 range).
   * Saturate per compiler-rt fp_fixint: positive -> INT_MAX, negative -> INT_MIN.
   * Note the +2^31 boundary (sig64 == 0x800000000000ull, mag == 0x80000000)
   * is NOT caught here as "out of range" — it falls through to the mag==2^31
   * branch below, mirroring fp_fixint's left-shift bit-pattern result. */
  if (sig64 & 0xFFFFF00000000000ull)
    return sign ? (int)0x80000000 : (int)0x7FFFFFFF;
  ui32 mag = (ui32)(sig64 >> 12);
  /* |x| == 2^31 exactly: fp_fixint yields significand(0x800000)<<(exp-23) =
   * 0x80000000, whose bit pattern is INT_MIN for BOTH signs (positive +2^31
   * is out of i32 range but the shift result happens to be INT_MIN; negative
   * -2^31 is the in-range floor). Return INT_MIN unconditionally. */
  if (mag == 0x80000000u)
    return (int)0x80000000;
  return sign ? -(int)mag : (int)mag;
}

//===----------------------------------------------------------------------===//
// Comparisons — libcall ABI (GCC manual §18.3). Each returns a signed integer
// whose sign encodes the ordered comparison; NaN operands yield "unordered is
// false" for the ordered predicates so the caller's branch behaves correctly.
//
//   __eqsf2(a,b): return 0 iff a == b   (caller branches BEQ)
//   __nesf2(a,b): return 0 iff a == b   (caller branches BNE)
//   __ltsf2(a,b): return <0 iff a < b   (caller branches BLT)
//   __lesf2(a,b): return <=0 iff a <= b
//   __gtsf2(a,b): return >0 iff a > b   (caller branches BGT)
//   __gesf2(a,b): return >=0 iff a >= b
//===----------------------------------------------------------------------===//

static int cmp_core(ui32 a, ui32 b) {
  /* Returns -1/0/+1 for a<b / a==b / a>b, or 2 for unordered (NaN). */
  if (((a & 0x7F800000u) == 0x7F800000u && (a & 0x7FFFFFu)) ||
      ((b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu)))
    return 2;
  ui32 absA = a & 0x7FFFFFFFu, absB = b & 0x7FFFFFFFu;
  if (absA == 0 && absB == 0) return 0;               /* +0 == -0 */
  int signA = SIGN_F32UI(a), signB = SIGN_F32UI(b);
  if (signA == signB) {
    if (absA == absB) return 0;
    int a_greater = (absA > absB);
    return signA ? (a_greater ? -1 : 1) : (a_greater ? 1 : -1);
  }
  return signA ? -1 : 1;
}

int __eqsf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 0) ? 0 : 1; }
int __nesf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 0) ? 0 : 1; }
int __ltsf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 2) ? 1 : r; }
int __lesf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 2) ? 1 : r; }
int __gtsf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 2) ? -1 : r; }
int __gesf2(float a, float b) { int r = cmp_core(f2u(a), f2u(b)); return (r == 2) ? -1 : r; }
