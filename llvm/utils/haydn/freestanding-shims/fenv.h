/*
 * Freestanding fenv.h shim for Haydn baremetal compiles.
 *
 * The NatureDSP mathf family pulls in __fenv.h which includes <fenv.h>
 * for the fenv_t type and rounding-mode macros. Baremetal haydn-unknown-elf
 * has no libc; we provide the minimum the kernels reference at compile time.
 *
 * The Haydn target is soft-float and these mathf kernels are compile-only;
 * no floating-point environment is actually manipulated.
 */
#ifndef HAYDN_FREESTANDING_FENV_H
#define HAYDN_FREESTANDING_FENV_H

typedef unsigned int fenv_t;
typedef unsigned int fexcept_t;

#define FE_DIVBYZERO  0x04
#define FE_INEXACT    0x20
#define FE_INVALID    0x01
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_ALL_EXCEPT (FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW)

#define FE_TONEAREST  0
#define FE_DOWNWARD   1
#define FE_UPWARD     2
#define FE_TOWARDZERO 3

/* Stubs - compile-only, no runtime fenv on baremetal. */
extern int fegetround(void);
extern int fesetround(int);

#endif /* HAYDN_FREESTANDING_FENV_H */
