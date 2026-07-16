/*
 * Freestanding math.h shim for Haydn baremetal compiles.
 *
 * The NatureDSP mathf/vectorf family headers include <math.h> for NaN/Inf
 * constants and a few classification macros. Baremetal haydn-unknown-elf
 * has no libc; we provide the minimum the kernels reference at compile time.
 *
 * The Haydn target is soft-float and these mathf kernels are ported to
 * fixed-point in practice; this shim only needs to compile, not link.
 */
#ifndef HAYDN_FREESTANDING_MATH_H
#define HAYDN_FREESTANDING_MATH_H

#include <stdint.h>

/* Classification macros - all return 0 (no NaN/Inf in Q-format port). */
#define isnan(x)   0
#define isinf(x)   0
#define isfinite(x) 1
#define isnormal(x) 1
#define signbit(x) 0

/* IEEE 754 constants for float (used in some fallback paths). */
#define INFINITY   (__builtin_inff())
#define NAN        (__builtin_nanf(""))
#define HUGE_VAL   (__builtin_inf())
#define HUGE_VALF  (__builtin_inff())

/* Error handling tie-in with errno (mathf uses DOMAIN/RANGE). */
#define DOMAIN     1
#define RANGE      2
#define OVERFLOW   3
#define UNDERFLOW  4

/* Scalar math function declarations (no definitions - compile only). */
extern double sqrt(double);
extern float  sqrtf(float);
extern double fabs(double);
extern float  fabsf(float);
extern double floor(double);
extern float  floorf(float);
extern double ceil(double);
extern float  ceilf(float);
extern double pow(double, double);
extern float  powf(float, float);
extern double exp(double);
extern float  expf(float);
extern double log(double);
extern float  logf(float);

#endif /* HAYDN_FREESTANDING_MATH_H */
