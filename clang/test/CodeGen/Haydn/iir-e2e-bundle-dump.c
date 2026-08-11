// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -mllvm -global-isel-abort=1 -O2 \
// RUN:   -S -o %t.s %s
// RUN: FileCheck %s --check-prefix=ASM --input-file %t.s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -mllvm -global-isel-abort=1 -O2 \
// RUN:   -c -o %t.o %s
// RUN: llvm-objdump -d -r %t.o | FileCheck %s --check-prefix=BUNDLE
// REQUIRES: haydn-registered-target
//
// E2E Clang → Haydn pipeline for a cascaded biquad IIR workload.
// FileCheck-only (re-scoped 2026-07-15): pins stable contracts, not schedule
// noise or retired phantom MAC32; wrap mul is mull.
//
// Contracts:
//   - s64 DF1 uses widening MUL64_LL / MULA64_LL + sub64
//   - s32 DF1 uses MULL (GPR32 low-half product)
//   - Block/cascade loops: slt32 + branch + jal to biquad_df1
//   - ELF objdump shows same mnemonics + call reloc class

#include <stdint.h>

typedef struct {
    int32_t x1, x2;
    int32_t y1, y2;
} biquad_state_t;

// 64-bit accumulator DF1 — exercises DR64 mul/mac + state ld/st.
__attribute__((noinline))
int32_t biquad_df1(int32_t xn, int32_t b0, int32_t b1, int32_t b2,
                   int32_t a1, int32_t a2, biquad_state_t *state) {
    int64_t acc = (int64_t)b0 * xn
                + (int64_t)b1 * state->x1
                + (int64_t)b2 * state->x2
                - (int64_t)a1 * state->y1
                - (int64_t)a2 * state->y2;
    int32_t yn = (int32_t)(acc >> 30);
    state->x2 = state->x1;
    state->x1 = xn;
    state->y2 = state->y1;
    state->y1 = yn;
    return yn;
}

// 32-bit product sum — expect mull (golden MAC GRR).
__attribute__((noinline))
int32_t biquad_df1_32bit(int32_t xn, int32_t b0, int32_t b1, int32_t b2,
                         int32_t a1, int32_t a2, biquad_state_t *state) {
    int32_t yn = b0 * xn + b1 * state->x1 + b2 * state->x2
                 - a1 * state->y1 - a2 * state->y2;
    state->x2 = state->x1;
    state->x1 = xn;
    state->y2 = state->y1;
    state->y1 = yn;
    return yn;
}

__attribute__((noinline))
void biquad_process_block(int32_t *input, int32_t *output, int N,
                          int32_t b0, int32_t b1, int32_t b2,
                          int32_t a1, int32_t a2,
                          biquad_state_t *state) {
    for (int i = 0; i < N; i++)
        output[i] = biquad_df1(input[i], b0, b1, b2, a1, a2, state);
}

__attribute__((noinline))
void biquad_cascade(int32_t *input, int32_t *output, int N,
                    int32_t *coeffs, int M, biquad_state_t *states) {
    for (int i = 0; i < N; i++) {
        int32_t sample = input[i];
        for (int m = 0; m < M; m++) {
            int32_t *c = coeffs + m * 5;
            sample = biquad_df1(sample, c[0], c[1], c[2], c[3], c[4],
                                &states[m]);
        }
        output[i] = sample;
    }
}

// === Assembly checks (stable contracts) ===

// ASM-LABEL: biquad_df1:
// ASM-DAG: subi32
// 64-bit mul may be mul64_ll or widened mul64_ulul + add64 (legalization).
// Format E member AsmStrings use `_`, not `.` — both spellings assemble but
// only the underscore one is printed.
// ASM-DAG: {{mul64_ll|mul64_ulul}}
// ASM-DAG: mula64_ll
// ASM-DAG: {{sub64|add64}}
// ASM-DAG: s_lw_{{[a-z_]*}}
// ASM-DAG: s_sw_{{[a-z_]*}}
// ASM: jalr

// ASM-LABEL: biquad_df1_32bit:
// ASM-DAG: subi32
// ASM-DAG: mull
// ASM-DAG: sub32
// ASM-DAG: s_lw_{{[a-z_]*}}
// ASM-DAG: s_sw_{{[a-z_]*}}
// ASM: jalr

// ASM-LABEL: biquad_process_block:
// ASM-DAG: subi32
// ASM-DAG: slt32
// ASM-DAG: beqz
// ASM-DAG: jal{{.*}}biquad_df1
// ASM: jalr

// ASM-LABEL: biquad_cascade:
// ASM-DAG: subi32
// ASM-DAG: slt32
// ASM-DAG: beqz
// ASM-DAG: jal{{.*}}biquad_df1
// ASM-DAG: seq32
// ASM-DAG: bnez
// ASM: jalr

// === Objdump checks ===

// BUNDLE-LABEL: <biquad_df1>:
// BUNDLE-DAG: {{mul64_ll|mul64_ulul}}
// BUNDLE-DAG: mula64_ll
// BUNDLE-DAG: sub64
// BUNDLE-DAG: s_sw_{{[a-z_]*}}
// BUNDLE-DAG: s_lw_{{[a-z_]*}}
// BUNDLE: jalr

// BUNDLE-LABEL: <biquad_df1_32bit>:
// BUNDLE-DAG: mull
// BUNDLE-DAG: sub32
// BUNDLE-DAG: s_lw_{{[a-z_]*}}
// BUNDLE-DAG: s_sw_{{[a-z_]*}}
// BUNDLE: jalr

// BUNDLE-LABEL: <biquad_process_block>:
// BUNDLE-DAG: slt32
// BUNDLE-DAG: beqz
// BUNDLE-DAG: R_HAYDN_{{.*}}CallSImm20{{.*}}biquad_df1
// BUNDLE: jalr

// BUNDLE-LABEL: <biquad_cascade>:
// BUNDLE-DAG: slt32
// BUNDLE-DAG: beqz
// BUNDLE-DAG: R_HAYDN_{{.*}}CallSImm20{{.*}}biquad_df1
// BUNDLE-DAG: seq32
// BUNDLE-DAG: bnez
// BUNDLE: jalr
