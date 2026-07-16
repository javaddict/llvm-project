//===-- m6-fft-butterfly-rad2.c - FFT radix-2 DIT butterfly ---------------===//
//
// NatureDSP port: fft_cplx32x32 radix-2 DIT butterfly -> Haydn.
//   For each pair (a, b) with twiddle w=(wr,wi):
//     a' = a + b*w     ->  ar'=ar+(br*wr-bi*wi), ai'=ai+(br*wi+bi*wr)
//     b' = a - b*w
//   The NatureDSP source (fft_cplx_stages_S2_32x32_hifi3.c) uses
//   AE_MULFC32X16RAS / AE_MULFC32X16RAS_HH; the Haydn X2CMUL32 (dual 32-bit
//   complex multiply, ternary form) does the complex product in one instr.
//   We use the ternary form: X2CMUL32(acc0, b, w) -> (br*wr-bi*wi,
//   br*wi+bi*wr). Then a +/- complex-product via X2ADD32 / X2SUB32.
//
// Validates the now-3-arg MAC arity fix for the complex-multiply family.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NPAIRS 4

/* Complex data interleaved R,I,R,I,... as DR64 (2 lanes = 1 complex). */
long long data[2 * NPAIRS] = {
    0x10000000, 0x00000000,  0x08000000, 0x08000000,
    0x04000000, 0x10000000,  0x20000000, 0x00100000,
};
/* Twiddle (wr, wi) for the highest-frequency pair (radix-2 last stage w=1+j0
 * -> (0x40000000, 0)). */
long long twiddle = ((long long)0x40000000 << 32) | (long long)0x00000000;

void fft_butterfly_rad2(long long *restrict d, long long w, int npairs) {
    for (int k = 0; k < npairs; ++k) {
        long long a = d[k];
        long long b = d[k + npairs];
        /* bw = b * w  (complex product, dual 32-bit). */
        long long bw = __haydn_x2cmul32(0, b, w);
        /* a' = a + bw ; b' = a - bw. */
        d[k] = __haydn_x2add32(a, bw);
        d[k + npairs] = __haydn_x2sub32(a, bw);
    }
}

int main(void) {
    fft_butterfly_rad2(data, twiddle, NPAIRS);
    return (int)data[0];
}
