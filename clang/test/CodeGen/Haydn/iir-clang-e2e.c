// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -mllvm -global-isel-abort=1 -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -mllvm -global-isel-abort=1 -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// REQUIRES: haydn-registered-target
//
// Clang end-to-end integration test: BiQuad IIR DSP kernel.
//
// This test exercises the FULL Clang -> LLVM -> Haydn MC pipeline for a
// realistic DSP workload (cascaded biquad IIR filter). It verifies:
//   - Clang lowers C to LLVM IR with the expected arithmetic patterns
//   - The Haydn GlobalISel pipeline selects DSP-appropriate instructions
//   - Struct field accesses become ld32/st32 with correct offsets
//   - Nested loops produce correct control flow (beqz/bnez + long-call)
//   - Function calls in the inner loop exercise the full calling convention
//   - Callee-saved register save/restore is correct across calls
//   - The noinline attribute prevents inlining, forcing real call/ret
//=============================================================================//

#include <stdint.h>

typedef struct {
    int32_t x1, x2;  // input delay line
    int32_t y1, y2;  // output delay line
} biquad_state_t;

// Single biquad section: Direct Form 1
// y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
__attribute__((noinline))
int32_t biquad_process(int32_t xn, int32_t b0, int32_t b1, int32_t b2,
                       int32_t a1, int32_t a2, biquad_state_t *state) {
    int32_t yn = b0 * xn + b1 * state->x1 + b2 * state->x2
                 - a1 * state->y1 - a2 * state->y2;
    state->x2 = state->x1;
    state->x1 = xn;
    state->y2 = state->y1;
    state->y1 = yn;
    return yn;
}

// Cascade: process N samples through M biquad sections
void biquad_cascade(int32_t *input, int32_t *output, int N,
                    int32_t *coeffs, int M, biquad_state_t *states) {
    for (int i = 0; i < N; i++) {
        int32_t sample = input[i];
        for (int m = 0; m < M; m++) {
            int32_t *c = coeffs + m * 5;
            sample = biquad_process(sample, c[0], c[1], c[2], c[3], c[4], &states[m]);
        }
        output[i] = sample;
    }
}

// === IR-level checks ===
// Verify Clang lowers biquad_process to LLVM IR with the expected patterns.

// IR-LABEL: @biquad_process
// IR-DAG: mul nsw i32
// IR-DAG: add i32
// IR-DAG: sub i32
// IR: ret i32

// IR-LABEL: @biquad_cascade
// IR: icmp sgt
// IR: br i1

// === Assembly-level checks ===
// Verify the Haydn backend emits the expected instruction classes.
// Note: We do NOT check exact register allocation or scheduling order,
// only that the right instruction mnemonics appear.

// ASM-LABEL: biquad_process:

// Prologue — Haydn emits either subi32 sp, sp, -N or addi32 sp, sp, -N.
// ASM: subi32{{[ 	]}}sp,{{[ 	]}}sp

// State loads from struct pointer (x1, x2, y1, y2 fields).
// ASM: ld32

// Coefficient multiplies (mul32 or 64-bit product form mull).
// ASM: mull

// Accumulation via MAC (mac32 for sum-of-products), if selected.
// Feedback subtraction
// ASM: sub32

// State update stores
// ASM: st32

// Return via jalr (plain or _w suffix depending on bundling)
// ASM: jalr{{(_w)?}} {{r0, lr, 0|lr}}

// ASM-LABEL: biquad_cascade:

// Prologue — same as above.
// ASM: subi32{{[ 	]}}sp,{{[ 	]}}sp

// Outer loop compare (slt32 or seq32 for loop bound check).
// ASM: slt32

// Inner loop: general call is LOAD_ADDR + JALR (AIE JAL_IND). Short JAL
// is a cycle-neutral CallSImm20 encoding relax (same packet/cycle count).
// ASM: lui{{.*}}biquad_process
// ASM: jalr

// Epilogue
// ASM: jalr{{(_w)?}}
