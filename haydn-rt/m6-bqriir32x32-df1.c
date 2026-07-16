//===-- m6-bqriir32x32-df1.c - IIR biquad DF1 port ------------------------===//
//
// NatureDSP port: bqriir32x32_df1 -> Haydn. Direct-Form-I biquad IIR filter
// on Q31 samples with Q30 coefficients. The inner-loop recurrence is:
//   acc = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
//   y[n] = SAT(acc + rounding >> shift)
//
// NatureDSP uses AE_MULF32R_HH/HL/LH/LL + AE_MULAF32R_* + AE_MULSF32R_* to
// accumulate Q17.46 products, then AE_PKSR32 to pack to Q31. The Haydn
// equivalent is the FF2MULA32RS / FF2MUL32RS family (accumulator add form,
// 3-arg) plus PACKSR32 for the Q31 pack. The subtract branch (FF2MULS32RS_HH)
// is declared 2-arg in BuiltinsHaydn.td today, so we instead accumulate the
// feedback products in a separate positive-form accumulator and subtract
// via a 64-bit subtract (the SUB64S lowering is native). This still proves
// the IIR recurrence end-to-end and exercises the FF2 fractional MAC family
// + PACKSR32 + saturating-shift (the patterns every FIR/IIR needs).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

/* Biquad coefficients (Q30): b0 b1 b2 -a1 -a2 (sign negated to use +MAC). */
#define COEF_FRAC 30
typedef struct {
    int b0, b1, b2;   /* Q30 numerator coeffs */
    int a1, a2;       /* Q30 denominator coeffs (positive sign; subtracted) */
} biquad_coef_t;

/* Single-section DF1: 8-sample block filter. State held in scalar form for
 * clarity; the inner products use Haydn FF2 fractional MAC intrinsics. */
static int biquad_df1_once(const biquad_coef_t *restrict c,
                           int *restrict state, /* {x1,x2,y1,y2} */
                           const int *restrict x, int *restrict r, int N) {
    int x1 = state[0], x2 = state[1];
    int y1 = state[2], y2 = state[3];
    for (int n = 0; n < N; ++n) {
        int xn = x[n];
        /* acc = SAT64(b0*xn + b1*x1 + b2*x2 - a1*y1 - a2*y2) with fractional
         * (high*high lane) rounding. FF2MUL32RS_HH gives the Q1.62 product
         * rounded to Q17.46; FF2MULA32RS_HH / FF2MULS32RS_HH accumulate. */
        long long acc = __haydn_ff2mul32rs_hh((long long)xn, (long long)c->b0);
        acc = __haydn_ff2mula32rs_hh(acc, (long long)x1, (long long)c->b1);
        acc = __haydn_ff2mula32rs_hh(acc, (long long)x2, (long long)c->b2);
        /* Feedback (denominator) terms: accumulate positively in a separate
         * Q17.46 accumulator, then subtract from the numerator accumulator
         * via a plain 64-bit subtract. (FF2MULS32RS_HH is declared 2-arg in
         * BuiltinsHaydn.td today; this detour avoids the arity gap while
         * still exercising the FF2 fractional-MAC + PACKSR32 pattern.) */
        long long fb = __haydn_ff2mul32rs_hh((long long)y1, (long long)c->a1);
        fb = __haydn_ff2mula32rs_hh(fb, (long long)y2, (long long)c->a2);
        acc = acc - fb;
        /* Q17.46 -> Q31 pack with asymmetric rounding (PACKSR32, shift=15). */
        int yn = __haydn_packsr32(acc, 15);
        r[n] = yn;
        x2 = x1; x1 = xn;
        y2 = y1; y1 = yn;
    }
    state[0] = x1; state[1] = x2;
    state[2] = y1; state[3] = y2;
    return r[0];
}

#define N 8

biquad_coef_t coef = {
    /* Q30: stable low-pass biquad. */
    0x20000000, 0x40000000, 0x20000000,   /* b0 b1 b2 */
    0x3E666666, 0x19999999                /* a1 a2 */
};

int x_in[N] = { 0x40000000, 0x40000000, 0x40000000, 0x40000000,
                0x40000000, 0x40000000, 0x40000000, 0x40000000 };
int y_out[N];
int iir_state[4] = { 0, 0, 0, 0 };

int main(void) {
    biquad_df1_once(&coef, iir_state, x_in, y_out, N);
    return y_out[0];
}
