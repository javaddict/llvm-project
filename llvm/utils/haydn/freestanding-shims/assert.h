/*
 * Freestanding assert.h shim for Haydn baremetal compiles.
 *
 * The NatureDSP private common.h includes <assert.h> for the assert() macro.
 * Baremetal haydn-unknown-elf has no libc; we provide a no-op assert.
 *
 * If NDEBUG is defined, assert is a no-op (standard behavior). Otherwise,
 * assert(x) evaluates x once and silently ignores failure (no abort on
 * baremetal; the kernels are compile-validated, not executed).
 */
#ifndef HAYDN_FREESTANDING_ASSERT_H
#define HAYDN_FREESTANDING_ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((void)(expression))
#endif

#endif /* HAYDN_FREESTANDING_ASSERT_H */
