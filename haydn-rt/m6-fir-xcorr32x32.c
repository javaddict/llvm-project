//===-- m6-fir-xcorr32x32.c - cross-correlation FIR port ------------------===//
//
// NatureDSP port: fir_xcorr32x32 (cross-correlation) -> Haydn.
//   r[m] = sum_n x[n] * y[n+m],  m = 0..L-1
// The NatureDSP source uses AE_MULAF32P / AE_L32_IP. The Haydn mapping uses
// MULA64_SS_LL (signed-signed low-low 64-bit MAC) per tap, with a single
// saturating right shift at the end (Q62 -> Q31). This is the canonical
// accumulator-form MAC inner loop and is a faithful algorithmic port.
//
// Validates the MULA64_SS_LL accumulator-form MAC path on a different access
// pattern than fir_convol32x32 (forward-in-y instead of reverse-in-x).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NX 8
#define NY 8
#define NR 4

int x[NX] = { 0x10000000, 0x20000000, 0x30000000, 0x40000000,
              0x10000000, 0x20000000, 0x30000000, 0x40000000 };
int y[NY] = { 0x04000000, 0x04000000, 0x04000000, 0x04000000,
              0x04000000, 0x04000000, 0x04000000, 0x04000000 };
int r[NR];

void fir_xcorr32x32(const int *restrict xa, const int *restrict ya,
                    int *restrict ra, int nx, int nr) {
    int limit = nx < nr ? nx : nr;
    for (int m = 0; m < limit; ++m) {
        long long acc = 0;
        int inner = nx - m;
        for (int n = 0; n < inner; ++n) {
            acc = __haydn_mula64_ss_ll(acc, (long long)xa[n],
                                       (long long)ya[n + m]);
        }
        ra[m] = __haydn_satsr64(acc, 31);
    }
}

int main(void) {
    fir_xcorr32x32(x, y, r, NX, NR);
    return r[0];
}
