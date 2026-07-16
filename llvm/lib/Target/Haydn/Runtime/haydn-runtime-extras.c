//===-- haydn-runtime-extras.c - mem*/atomic/abort for Haydn -----*- C -*-==
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Baremetal runtime extras that complete libhaydn.o: memory primitives
// single-core atomics, and abort/exit stubs. Split from haydn-rt.c so the
// integer div/mod helpers stay in one file and these orthogonal primitives
// live in another.
//
// Atomics are single-core baremetal: plain aligned loads/stores. The Haydn
// target has no SMP, no interrupts during arithmetic, so a plain access IS
// atomic. (If/when interrupts are added, mask-based or LL/SC versions will
// be needed.)
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>

//===----------------------------------------------------------------------===//
// mem* — byte-wise loops. The Haydn backend has no bulk-transfer instruction
// yet; these are slow but correct. Optimisation: future DMA / wide-load
// instructions can replace the inner loop.
//===----------------------------------------------------------------------===//

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n--) *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d < s) {
    while (n--) *d++ = *s++;
  } else if (d > s) {
    d += n; s += n;
    while (n--) *--d = *--s;
  }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  while (n--) *d++ = (unsigned char)c;
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (n--) {
    if (*pa != *pb) return (int)*pa - (int)*pb;
    ++pa; ++pb;
  }
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  while (n--) {
    if (*p == (unsigned char)c) return (void *)p;
    ++p;
  }
  return 0;
}

//===----------------------------------------------------------------------===//
// Atomics — single-core baremetal, plain accesses.
//===----------------------------------------------------------------------===//

#define DEFINE_ATOMIC_LOAD(N, TYPE) \
  TYPE __atomic_load_##N(const TYPE *p, int memorder) { \
    (void)memorder; return *p; \
  }
#define DEFINE_ATOMIC_STORE(N, TYPE) \
  void __atomic_store_##N(TYPE *p, TYPE val, int memorder) { \
    (void)memorder; *p = val; \
  }
#define DEFINE_ATOMIC_EXCHANGE(N, TYPE) \
  TYPE __atomic_exchange_##N(TYPE *p, TYPE val, int memorder) { \
    (void)memorder; TYPE old = *p; *p = val; return old; \
  }
#define DEFINE_ATOMIC_CMPXCHG(N, TYPE) \
  _Bool __atomic_compare_exchange_##N(TYPE *p, TYPE *expected, TYPE desired, \
                                       int weak, int succ, int fail) { \
    (void)weak; (void)succ; (void)fail; \
    if (*p == *expected) { *p = desired; return 1; } \
    *expected = *p; return 0; \
  }

DEFINE_ATOMIC_LOAD(1, uint8_t)
DEFINE_ATOMIC_LOAD(2, uint16_t)
DEFINE_ATOMIC_LOAD(4, uint32_t)
DEFINE_ATOMIC_LOAD(8, uint64_t)
DEFINE_ATOMIC_STORE(1, uint8_t)
DEFINE_ATOMIC_STORE(2, uint16_t)
DEFINE_ATOMIC_STORE(4, uint32_t)
DEFINE_ATOMIC_STORE(8, uint64_t)
DEFINE_ATOMIC_EXCHANGE(1, uint8_t)
DEFINE_ATOMIC_EXCHANGE(2, uint16_t)
DEFINE_ATOMIC_EXCHANGE(4, uint32_t)
DEFINE_ATOMIC_EXCHANGE(8, uint64_t)
DEFINE_ATOMIC_CMPXCHG(1, uint8_t)
DEFINE_ATOMIC_CMPXCHG(2, uint16_t)
DEFINE_ATOMIC_CMPXCHG(4, uint32_t)
DEFINE_ATOMIC_CMPXCHG(8, uint64_t)

#define DEFINE_ATOMIC_FETCH(OPNAME, OP, N, TYPE) \
  TYPE __atomic_fetch_##OPNAME##_##N(TYPE *p, TYPE val, int memorder) { \
    (void)memorder; TYPE old = *p; *p = old OP val; return old; \
  }

DEFINE_ATOMIC_FETCH(add, +, 1, uint8_t)
DEFINE_ATOMIC_FETCH(add, +, 2, uint16_t)
DEFINE_ATOMIC_FETCH(add, +, 4, uint32_t)
DEFINE_ATOMIC_FETCH(add, +, 8, uint64_t)
DEFINE_ATOMIC_FETCH(sub, -, 1, uint8_t)
DEFINE_ATOMIC_FETCH(sub, -, 2, uint16_t)
DEFINE_ATOMIC_FETCH(sub, -, 4, uint32_t)
DEFINE_ATOMIC_FETCH(sub, -, 8, uint64_t)
DEFINE_ATOMIC_FETCH(and, &, 1, uint8_t)
DEFINE_ATOMIC_FETCH(and, &, 2, uint16_t)
DEFINE_ATOMIC_FETCH(and, &, 4, uint32_t)
DEFINE_ATOMIC_FETCH(and, &, 8, uint64_t)
DEFINE_ATOMIC_FETCH(or,  |, 1, uint8_t)
DEFINE_ATOMIC_FETCH(or,  |, 2, uint16_t)
DEFINE_ATOMIC_FETCH(or,  |, 4, uint32_t)
DEFINE_ATOMIC_FETCH(or,  |, 8, uint64_t)
DEFINE_ATOMIC_FETCH(xor, ^, 1, uint8_t)
DEFINE_ATOMIC_FETCH(xor, ^, 2, uint16_t)
DEFINE_ATOMIC_FETCH(xor, ^, 4, uint32_t)
DEFINE_ATOMIC_FETCH(xor, ^, 8, uint64_t)

//===----------------------------------------------------------------------===//
// abort / exit — baremetal halt loop. No diagnostic output (no UART yet).
//===----------------------------------------------------------------------===//

void abort(void) {
  for (;;) { /* spin forever*/ }
}

void exit(int status) {
  (void)status;
  for (;;) { /* spin forever*/ }
}

void _exit(int status) {
  (void)status;
  for (;;) { /* spin forever*/ }
}
