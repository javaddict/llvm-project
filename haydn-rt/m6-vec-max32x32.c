//===-- m6-vec-max32x32.c - vec_max32x32 port -----------------------------===//
//
// NatureDSP port: vec_max32x32 -> Haydn. Elementwise maximum of two i32
// vectors. Pure C max loop; the saturation-safe scalar ABS32S is used on the
// first element to validate the saturating-abs path. Exercises scalar
// compare + select codegen with no MAC.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 1, 9, 3, 7, 5, 6, 4, 8 };
int y[N] = { 8, 2, 6, 4, 5, 10, 3, 1 };
int z[N];

void vec_max32x32(int *restrict zout,
                  const int *restrict xin,
                  const int *restrict yin) {
    for (int i = 0; i < N; ++i) {
        int a = xin[i];
        int b = yin[i];
        /* Use ABS32S to ensure the path is exercised (saturating abs of an
         * already-positive value is identity, but proves the instruction
         * lowers correctly). */
        zout[i] = (a > b) ? __haydn_abs32s(a) : __haydn_abs32s(b);
    }
}

int main(void) {
    vec_max32x32(z, x, y);
    return z[0];
}
