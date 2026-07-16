/*
 * Freestanding string.h shim for Haydn baremetal compiles.
 *
 * A few NatureDSP kernels reference memcpy/memset for small static inits.
 * We map them to the clang builtins which lower to inline loops.
 */
#ifndef HAYDN_FREESTANDING_STRING_H
#define HAYDN_FREESTANDING_STRING_H

#define memcpy(dst, src, n) __builtin_memcpy((dst), (src), (n))
#define memset(dst, val, n) __builtin_memset((dst), (val), (n))
#define memmove(dst, src, n) __builtin_memmove((dst), (src), (n))
#define memcmp(a, b, n)     __builtin_memcmp((a), (b), (n))

#endif /* HAYDN_FREESTANDING_STRING_H */
