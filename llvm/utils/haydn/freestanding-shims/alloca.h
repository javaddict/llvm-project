/*
 * Freestanding alloca.h shim for Haydn baremetal compiles.
 *
 * Some NatureDSP kernels (mtx_mpy*) use alloca() for small temp arrays.
 * Baremetal haydn-unknown-elf has no libc; we map alloca to a stack-alloc
 * builtin that clang lowers to a dynamic alloca.
 */
#ifndef HAYDN_FREESTANDING_ALLOCA_H
#define HAYDN_FREESTANDING_ALLOCA_H

#define alloca(size) __builtin_alloca(size)

#endif /* HAYDN_FREESTANDING_ALLOCA_H */
