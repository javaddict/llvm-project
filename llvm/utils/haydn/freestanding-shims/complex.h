/*
 * Freestanding complex.h shim for Haydn baremetal compiles.
 *
 * Background: NatureDSP_types.h selects its complex type representation via
 *   #if defined(COMPILER_MSVC)
 *       ... struct-based complex_float + inline crealf/cimagf accessors ...
 *   #else
 *       #include <complex.h>
 *       #define complex_float  __complex__ float
 *       #define complex_double __complex__ double
 *   #endif
 * Haydn compiles with -DCOMPILER_XTENSA (not MSVC), so the #else branch is
 * taken: complex_float becomes the real C99 _Complex float, and the code
 * relies on <complex.h> to provide crealf/cimagf. Under -nostdinc the host
 * glibc <complex.h> is not on the path, so this freestanding shim supplies
 * the accessors instead.
 *
 * ONLY the accessors the corpus actually uses are provided:
 *   crealf / cimagf  (float _Complex)   -- used by baseop.h mul_fl32c, etc.
 *   creal  / cimag   (double _Complex)  -- defensive; complex_double exists
 *                                          in the #else branch but no kernel
 *                                          calls the double accessors today.
 * These are implemented BITWISE via the clang __real__ / __imag__ operators,
 * which lower to plain field loads (ld32 offset 0 / 4). They perform NO
 * floating-point arithmetic, so they emit NO soft-float libcalls (ISA-24).
 *
 * Arithmetic complex ops (cexpf/clogf/csqrtf/cmulf/...) are deliberately NOT
 * shimmed: they would lower to soft-float (ISA-24). Grep confirms the corpus
 * does not call them. conjf is also not shimmed: it is never called in any
 * .c kernel, and NatureDSP_types.h already macro-redirects it to conjf_libdsp.
 *
 * Guard strategy: define each function only when not already declared, so this
 * shim stays benign if a future header (or the MSVC branch) provides its own.
 */
#ifndef HAYDN_FREESTANDING_COMPLEX_H
#define HAYDN_FREESTANDING_COMPLEX_H

#ifndef __cplusplus

/* crealf: real part of float _Complex (bitwise field load, no float math). */
#ifndef crealf_declared
#define crealf_declared
static inline float crealf(float _Complex __z) { return __real__ __z; }
#endif

/* cimagf: imaginary part of float _Complex (bitwise field load, no float math). */
#ifndef cimagf_declared
#define cimagf_declared
static inline float cimagf(float _Complex __z) { return __imag__ __z; }
#endif

/* creal: real part of double _Complex (defensive; not currently called). */
#ifndef creal_declared
#define creal_declared
static inline double creal(double _Complex __z) { return __real__ __z; }
#endif

/* cimag: imaginary part of double _Complex (defensive; not currently called). */
#ifndef cimag_declared
#define cimag_declared
static inline double cimag(double _Complex __z) { return __imag__ __z; }
#endif

#endif /* __cplusplus */

#endif /* HAYDN_FREESTANDING_COMPLEX_H */
