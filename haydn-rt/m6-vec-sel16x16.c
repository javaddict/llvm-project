//===-- m6-vec-sel16x16.c - X4SELI16 byte-select port ---------------------===//
//
// NatureDSP port: lane-permute / byte-select kernel -> Haydn. The NatureDSP
// analog is a v4i16 byte-shuffle / cmplx-pair-swap (used in cxfir post-
// processing). The Haydn mapping is X4SELI16 (quad 16-bit lane-select with a
// 4-bit immediate), now wave-5 encoded.
//
// Validates the X4SELI16 lane-permute code path with an immediate selector.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 4

long long x4d[N] = {
    ((long long)0x00040003 << 32) | (long long)0x00020001,
    ((long long)0x00080007 << 32) | (long long)0x00060005,
    ((long long)0x000C000B << 32) | (long long)0x000A0009,
    ((long long)0x0010000F << 32) | (long long)0x000E000D,
};
long long z4d[N];

void vec_sel16x16(long long *restrict zout, const long long *restrict xin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;
    for (int i = 0; i < N; ++i) {
        /* Immediate 0xB = swap lanes 0<->2, 1<->3 (reversal). */
        pz[i] = __haydn_x4seli16(px[i], 0, 0xB);
    }
}

int main(void) {
    vec_sel16x16(z4d, x4d);
    return (int)z4d[0];
}
