/*
 * Freestanding errno.h shim for Haydn baremetal compiles.
 *
 * The NatureDSP math/mathf family headers include <errno.h> but only use
 * the EDOM/ERANGE constants. Baremetal haydn-unknown-elf has no libc, so
 * we provide the minimal definitions needed for compilation.
 *
 * No runtime errno storage is provided (the kernels are compile-validated,
 * not executed); if a future port links against a real libc, this shim is
 * shadowed by the libc version.
 */
#ifndef HAYDN_FREESTANDING_ERRNO_H
#define HAYDN_FREESTANDING_ERRNO_H

#define EDOM    1
#define ERANGE  2
#define EILSEQ  3
#define EINVAL  4

/* Some NatureDSP code references `errno` directly. Provide a weak stub. */
extern int errno;

#endif /* HAYDN_FREESTANDING_ERRNO_H */
