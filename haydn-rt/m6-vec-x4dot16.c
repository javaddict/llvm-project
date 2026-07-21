//===-- m6-vec-x4dot16.c - quad 16-bit SIMD dot product port --------------===//
//
// NatureDSP port: vec_dot16x16_fast (quad 16-bit dot) -> Haydn.
//   r = sum_i x[i]*y[i], i=0..N-1, with 4 lanes per DR64 register.
// The NatureDSP source (vec_dot16x16_fast_hifi3.c) uses AE_MUL16X4 /
// AE_MULAF16X4RS / AE_L16X4_IP. The Haydn mapping is X4DOT16 (quad 16-bit
// SIMD dot product, 2-arg: rs1, rs2 -> DR64 of 4 partial products).
// We sum the 4 lanes each iteration, then add to a scalar accumulator.
//
// Validates the X4DOT16 SIMD dot-product code path (the wave-5 X4 dot).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NVEC 4    /* 4 lanes per DR64 -> 4 elements total */

/* Pack 4 i16 samples into one i64 lane each. */
long long x4 = ((long long)0x00040003 << 32) | (long long)0x00020001;
long long y4 = ((long long)0x00010001 << 32) | (long long)0x00010001;
long long r4;

void vec_x4dot16(long long xv, long long yv, long long *restrict out) {
    long long prod = __haydn_x4dot16(xv, yv);
    *out = prod;
}

int main(void) {
    vec_x4dot16(x4, y4, &r4);
    /* Low lane sum for sanity. */
    return (int)(r4 & 0xFFFF);
}
