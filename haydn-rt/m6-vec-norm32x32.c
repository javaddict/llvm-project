//===-- m6-vec-norm32x32.c - vec_norm32x32 port ---------------------------===//
//
// NatureDSP port: vec_norm32x32 (number-of-leading/excess bits) -> Haydn.
// For each i32 sample, computes the number of bits the value can be left-
// shifted without overflow (NSA-style "norm"). The NatureDSP source uses
// AE_NSAZ32 / AE_NSA32. The Haydn mapping is the scalar NSA32 builtin.
//
// Validates the NSA32 code path (scalar shift-quantifier) in a vector loop.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 0x40000000, 0x20000000, 0x10000000, 0x08000000,
             -1, -16, 0x7FFFFFFF, 1 };
int z[N];

void vec_norm32x32(int *restrict zout, const int *restrict xin) {
    for (int i = 0; i < N; ++i) {
        zout[i] = __haydn_nsa32(xin[i]);
    }
}

int main(void) {
    vec_norm32x32(z, x);
    return z[0];
}
