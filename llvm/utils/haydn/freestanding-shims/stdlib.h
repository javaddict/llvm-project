/*
 * Freestanding stdlib.h shim for Haydn baremetal compiles.
 *
 * The ported NatureDSP headers include <stdlib.h> but only use `size_t`
 * (via struct field types). Baremetal haydn-unknown-elf has no libc, so
 * we provide the minimal type definition needed.
 *
 * `size_t` is provided by <stddef.h> in the clang resource directory; we
 * re-expose it here so the include resolves cleanly.
 */
#ifndef HAYDN_FREESTANDING_STDLIB_H
#define HAYDN_FREESTANDING_STDLIB_H

#include <stddef.h>  /* brings in size_t */

#endif
