//===-- softfloat_fixsfsi_trunc.c - host harness for __fixsfsi --------------===//
//
// REGRESSION TEST: __fixsfsi must truncate toward zero, not round to nearest.
//
// Bug: haydn-rt/softfloat.c __fixsfsi backed LLVM `fptosi` (it is bound to
// RTLIB::FPTOSINT_F32_I32 in HaydnSubtarget.cpp:230), whose semantics are
// truncation toward zero. But the body rounded to nearest-even via
// `sig64 += 0x800` plus tie-to-even (port of SoftFloat's f32_to_i32). So
// (int)1.9f returned 2 instead of 1 — silently wrong for every NatureDSP
// float->int conversion.
//
// Why a host harness: __fixsfsi is a plain C function whose correctness is
// independent of Haydn codegen. Compiling softfloat.c for the host and
// linking the harness's references to __fixsfsi against it (the object-file
// strong symbol overrides the host libm's weak one) proves the libcall body
// in isolation, without needing the Haydn backend or a cross build.
//
// What breaks if this regresses: (int)1.9f == 2 (and (int)0.9f == 1, etc.)
// — every float->int truncation in a soft-float kernel drifts off by one.
//
// Build/run (host clang, NOT ninja — single-build-lock rule):
//   cc -std=c11 -Wall -Werror -I.. softfloat_fixsfsi_trunc.c ../softfloat.c \
//      -lm -o softfloat_fixsfsi_trunc && ./softfloat_fixsfsi_trunc
// Exits 0 on pass, nonzero on failure.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include <stdio.h>

/* Declared in softfloat.c; defined here only to force linkage to our copy
 * (strong symbol from softfloat.o overrides the host lib's). */
int __fixsfsi(float af);

/* Local bit-pattern -> float constructor. Named ui2f_ (trailing underscore) to
 * avoid clashing with softfloat.c's static `u2f` (file-scope, no linkage). */
static float ui2f_(uint32_t u) {
  union { uint32_t u; float f; } c;
  c.u = u;
  return c.f;
}

static int failures = 0;

static void check_int(const char *desc, int got, int want) {
  if (got != want) {
    printf("FAIL: %s: got %d, want %d\n", desc, got, want);
    failures++;
  } else {
    printf("ok:   %s: %d\n", desc, got);
  }
}

int main(void) {
  /* Truncation toward zero (the core contract — these caught the bug). */
  check_int("__fixsfsi(1.9f)",   __fixsfsi(1.9f),   1);
  check_int("__fixsfsi(-1.9f)",  __fixsfsi(-1.9f),  -1);
  check_int("__fixsfsi(1.1f)",   __fixsfsi(1.1f),   1);
  check_int("__fixsfsi(-1.1f)",  __fixsfsi(-1.1f),  -1);

  /* |x| < 1 truncates to 0 (sign of zero irrelevant for int 0). */
  check_int("__fixsfsi(0.9f)",   __fixsfsi(0.9f),   0);
  check_int("__fixsfsi(-0.9f)",  __fixsfsi(-0.9f),  0);
  check_int("__fixsfsi(0.0f)",   __fixsfsi(0.0f),   0);
  check_int("__fixsfsi(-0.0f)",  __fixsfsi(-0.0f),  0);

  /* Boundary values around 2^31. */
  check_int("__fixsfsi(2147483520.0f)",  __fixsfsi(2147483520.0f),  2147483520);
  check_int("__fixsfsi(-2147483648.0f)", __fixsfsi(-2147483648.0f), INT32_MIN);

  /* Exact integers (sanity). */
  check_int("__fixsfsi(2.0f)",   __fixsfsi(2.0f),   2);
  check_int("__fixsfsi(-2.0f)",  __fixsfsi(-2.0f),  -2);

  /* --- Overflow / edge cases: aligned to compiler-rt fp_fixint_impl.inc. --- */
  /* The bit patterns are constructed directly so the test is independent of
   * the host compiler's constant-folder (which may itself call a __fixsfsi). */

  /* +/-Inf, +/-NaN (quiet and signaling): saturate BY SIGN per fp_fixint
   * (positive -> INT_MAX, negative -> INT_MIN). NaN sign bit is well-defined
   * per IEEE 754-2008 §6.2.1, and fp_fixint reads it. */
  {
    float pinf = ui2f_(0x7F800000u);   /*  inf */
    float ninf = ui2f_(0xFF800000u);   /* -inf */
    float pnan = ui2f_(0x7FC00000u);   /* quiet NaN, sign + */
    float nnan = ui2f_(0xFFC00000u);   /* quiet NaN, sign - */
    float psnan = ui2f_(0x7F800001u);  /* signaling NaN, sign + */
    float nsnan = ui2f_(0xFF800001u);  /* signaling NaN, sign - */
    check_int("__fixsfsi(+inf)",       __fixsfsi(pinf),   INT32_MAX);
    check_int("__fixsfsi(-inf)",       __fixsfsi(ninf),   INT32_MIN);
    check_int("__fixsfsi(+qNaN)",      __fixsfsi(pnan),   INT32_MAX);
    check_int("__fixsfsi(-qNaN)",      __fixsfsi(nnan),   INT32_MIN);
    check_int("__fixsfsi(+sNaN)",      __fixsfsi(psnan),  INT32_MAX);
    check_int("__fixsfsi(-sNaN)",      __fixsfsi(nsnan),  INT32_MIN);
  }

  /* +2^31 (2147483648.0f): fp_fixint yields significand(0x800000)<<(31-23) =
   * 0x80000000, whose bit pattern IS INT_MIN for BOTH signs. So the positive
   * boundary lands on INT_MIN, NOT INT_MAX. (This is the compiler-rt quirk
   * the prior Haydn impl got wrong — it saturated +2^31 -> INT_MAX.) */
  {
    float p2_31 = ui2f_(0x4F000000u);  /*  2147483648.0f =  2^31 */
    float n2_31 = ui2f_(0xCF000000u);  /* -2147483648.0f = -2^31 */
    check_int("__fixsfsi(+2^31)",  __fixsfsi(p2_31), INT32_MIN);
    check_int("__fixsfsi(-2^31)",  __fixsfsi(n2_31), INT32_MIN);
  }

  /* Values strictly above 2^31 saturate by sign. 4294967296.0f = 2^32. */
  {
    float p2_32 = ui2f_(0x4F800000u);  /*  4294967296.0f =  2^32 */
    float n2_32 = ui2f_(0xCF800000u);  /* -4294967296.0f = -2^32 */
    check_int("__fixsfsi(+2^32)",  __fixsfsi(p2_32), INT32_MAX);
    check_int("__fixsfsi(-2^32)",  __fixsfsi(n2_32), INT32_MIN);
  }

  /* A float strictly between INT_MAX and 2^31 does not exist (INT_MAX+1 in f32
   * IS 2^31). 2147483520.0f is the largest f32 below 2^31 and is in-range,
   * covered by the truncation block above. The 2.0e9f sanity below is an
   * in-range value near (but below) INT_MAX. */
  check_int("__fixsfsi(2.0e9f)", __fixsfsi(2.0e9f), 2000000000);

  if (failures) {
    printf("%d FAILURES\n", failures);
    return 1;
  }
  printf("ALL PASS\n");
  return 0;
}
