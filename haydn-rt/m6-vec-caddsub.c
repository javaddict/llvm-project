//===-- m6-vec-caddsub.c - complex SIMD add+sub port ----------------------===//
//
// NatureDSP port: complex add+sub (cxfir / fft prep stage) -> Haydn. Given
// two DR64 complex pairs (a=ar,ai ; b=br,bi), produce the sum and difference
// used by FFT butterflies and cxfir stages:
//   c = a + b   (X2ADD32)
//   d = a - b   (X2SUB32)
// and a saturating variant (X2ADDSUB32S) producing c=a+b, d=a-b in one instr.
// The NatureDSP source (cxfir_convol32x16 / fft_stages_S2) issues these as
// AE_ADDSUB32S; Haydn has a native X2ADDSUB32S dual-output op.
//
// Validates the X2ADD32 / X2SUB32 / X2ADDSUB32S dual-output code path.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 4

long long xa[N] = { 0x0000000100000002LL, 0x0000000300000004LL,
                    0x0000000500000006LL, 0x0000000700000008LL };
long long xb[N] = { 0x0000000100000001LL, 0x0000000200000002LL,
                    0x0000000300000003LL, 0x0000000400000004LL };
long long sumz[N];
long long difz[N];

void vec_caddsub(long long *restrict sumout, long long *restrict difout,
                 const long long *restrict ain, const long long *restrict bin) {
    const haydn_dr64_t *restrict pa = (const haydn_dr64_t *)ain;
    const haydn_dr64_t *restrict pb = (const haydn_dr64_t *)bin;
    haydn_dr64_t *restrict ps = (haydn_dr64_t *)sumout;
    haydn_dr64_t *restrict pd = (haydn_dr64_t *)difout;
    for (int i = 0; i < N; ++i) {
        ps[i] = __haydn_x2add32(pa[i], pb[i]);
        pd[i] = __haydn_x2sub32(pa[i], pb[i]);
    }
}

int main(void) {
    vec_caddsub(sumz, difz, xa, xb);
    return (int)sumz[0];
}
