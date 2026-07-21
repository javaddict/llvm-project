//===-- m6-vec-mul16x16.c - quad 16-bit SIMD multiply port ----------------===//
//
// NatureDSP port: vec_mult16x16 (quad 16-bit product) -> Haydn. The NatureDSP
// source (vec_mult16x16_hifi3.c) uses AE_MUL16X4 / AE_MULAF16X4RS / AE_L16X4_IP.
// The Haydn mapping is X4MUL16 (ternary: rs1, rs2, ra -> 4 lane-wise products).
//
// Validates the X4MUL16 quad-16 SIMD-multiply ternary code path.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 4

long long x4[N] = {
    ((long long)0x00020001 << 32) | (long long)0x00020001,
    ((long long)0x00030003 << 32) | (long long)0x00030003,
    ((long long)0x00040004 << 32) | (long long)0x00040004,
    ((long long)0x00050005 << 32) | (long long)0x00050005,
};
long long y4[N] = {
    ((long long)0x00010001 << 32) | (long long)0x00010001,
    ((long long)0x00010001 << 32) | (long long)0x00010001,
    ((long long)0x00010001 << 32) | (long long)0x00010001,
    ((long long)0x00010001 << 32) | (long long)0x00010001,
};
long long z4[N];

void vec_mul16x16(long long *restrict zout,
                  const long long *restrict xin,
                  const long long *restrict yin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    const haydn_dr64_t *restrict py = (const haydn_dr64_t *)yin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;
    for (int i = 0; i < N; ++i) {
        /* Destructive-accumulator constraint: result aliases first operand. */
        haydn_dr64_t acc = px[i];
        pz[i] = __haydn_x4mul16(acc, px[i], py[i]);
    }
}

int main(void) {
    vec_mul16x16(z4, x4, y4);
    return (int)z4[0];
}
