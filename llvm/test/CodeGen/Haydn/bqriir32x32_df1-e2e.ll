; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -filetype=obj -enable-misched=false -enable-post-misched=false %s -o %t.o && llvm-objdump -d %t.o | FileCheck --check-prefix=BUNDLE %s
; REQUIRES: haydn-registered-target

; Role: object — NatureDSP-style biquad IIR object dump: stack, 64-bit MAC chain, stores, return.

; IR to GlobalISel to MC to ELF to objdump. Pins process/main mnemonics, not
; printer-only asm. Co-issued members use BUNDLE-DAG (slot order free).
; Live Format E objdump names: st64/ld32/st32 for memory, jal/jalr (not
; legacy jal_w/jalr_w tokens). -verify-machineinstrs is live.
;
; E2E test: BiQuad IIR filter (Direct Form 1), 32x32-bit fixed-point.
;
; Ported from the Haydn DSP library NatureDSP bqriir32x32_df1 kernel.
; This test exercises the following Haydn backend capabilities in a real DSP
; workload pattern:
; 64-bit multiply via libcall (__muldi3) — 32x32->64 MAC pattern
; 64-bit accumulation (add64, sub64) — IIR accumulator
; 64-bit right-shift for Q-format extraction (sra32, srl32, sll32)
; Struct field access (ld32/st32 with offset) — coefficient and state access
; Array processing loop (beqz_w/bnez_w branch) — per-sample iteration
; Cascaded sections (function calls with pointers) — multi-biquad
; DR64 register save/restore (st64/ld64) — callee-saved 64-bit regs
; GPR32 callee-saved save/restore — standard ABI compliance
;
; The algorithm (per sample, per biquad section):
; y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
;
; Coefficients are Q1.30, input/output are Q1.31, accumulator is Q17.46.
;=============================================================================;

; === BiQuad IIR DF1 state structure (mirrors the C struct) ===;
; Layout: { b0, b1, b2, a1, a2, sx0, sx1, sy0, sy1 } — 9 x i32 = 36 bytes

%bqriir32_df1_state = type { i32, i32, i32, i32, i32, i32, i32, i32, i32 }

; === Single-section IIR DF1 process function ===;
; C: void bqriir32x32_df1_process(bqriir32_df1_state_t *st,
; int32_t *r, const int32_t *x, int N)
define void @bqriir32x32_df1_process(ptr %st, ptr %r, ptr %x, i32 %N) {
entry:
  ; Load coefficients from struct
  %b0ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 0
  %b1ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 1
  %b2ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 2
  %a1ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 3
  %a2ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 4
  %sx0ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 5
  %sx1ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 6
  %sy0ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 7
  %sy1ptr = getelementptr %bqriir32_df1_state, ptr %st, i32 0, i32 8

  %b0 = load i32, ptr %b0ptr
  %b1 = load i32, ptr %b1ptr
  %b2 = load i32, ptr %b2ptr
  %a1 = load i32, ptr %a1ptr
  %a2 = load i32, ptr %a2ptr
  %sx0.init = load i32, ptr %sx0ptr
  %sx1.init = load i32, ptr %sx1ptr
  %sy0.init = load i32, ptr %sy0ptr
  %sy1.init = load i32, ptr %sy1ptr

  %cmp0 = icmp sgt i32 %N, 0
  br i1 %cmp0, label %for.body, label %for.end

for.body:
  %i = phi i32 [ 0, %entry ], [ %i.next, %for.body ]
  %sx0 = phi i32 [ %sx0.init, %entry ], [ %xn, %for.body ]
  %sx1 = phi i32 [ %sx1.init, %entry ], [ %sx0, %for.body ]
  %sy0 = phi i32 [ %sy0.init, %entry ], [ %yn, %for.body ]
  %sy1 = phi i32 [ %sy1.init, %entry ], [ %sy0, %for.body ]

  ; Load input sample x[i]
  %xiptr = getelementptr i32, ptr %x, i32 %i
  %xn = load i32, ptr %xiptr

  ; Compute DF1: acc = b0*xn + b1*sx0 + b2*sx1 - a1*sy0 - a2*sy1
  ; All 32x32->64 multiplies, accumulated in 64-bit
  %b0.ext = sext i32 %b0 to i64
  %xn.ext = sext i32 %xn to i64
  %term0 = mul i64 %b0.ext, %xn.ext

  %b1.ext = sext i32 %b1 to i64
  %sx0.ext = sext i32 %sx0 to i64
  %term1 = mul i64 %b1.ext, %sx0.ext

  %b2.ext = sext i32 %b2 to i64
  %sx1.ext = sext i32 %sx1 to i64
  %term2 = mul i64 %b2.ext, %sx1.ext

  %a1.ext = sext i32 %a1 to i64
  %sy0.ext = sext i32 %sy0 to i64
  %term3 = mul i64 %a1.ext, %sy0.ext

  %a2.ext = sext i32 %a2 to i64
  %sy1.ext = sext i32 %sy1 to i64
  %term4 = mul i64 %a2.ext, %sy1.ext

  %sum01 = add i64 %term0, %term1
  %sum012 = add i64 %sum01, %term2
  %sum34 = add i64 %term3, %term4
  %acc = sub i64 %sum012, %sum34

  ; Extract Q31 output: shift right by 30
  %acc.shr = ashr i64 %acc, 30
  %yn = trunc i64 %acc.shr to i32

  ; Store output r[i]
  %riptr = getelementptr i32, ptr %r, i32 %i
  store i32 %yn, ptr %riptr

  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %N
  br i1 %cmp, label %for.body, label %for.end.latch

for.end.latch:
  ; Store updated state back to struct
  store i32 %xn, ptr %sx0ptr
  store i32 %sx0, ptr %sx1ptr
  store i32 %yn, ptr %sy0ptr
  store i32 %sy0, ptr %sy1ptr
  br label %for.end

for.end:
  ret void
}
; BUNDLE-LABEL: <bqriir32x32_df1_process>:
; BUNDLE: subi32 sp, sp
; Product memory forms (Format E objdump): st64 spill/store, ld32 load.
; BUNDLE-DAG: st64
; BUNDLE-DAG: ld32
; Back-edge: slt32 + bnez (not fused blt_w) under SFR-aware scheduling.
; BUNDLE-DAG: {{slt32|set_hwloop}}
; BUNDLE-DAG: {{mul64|mula64|add64}}
; BUNDLE-DAG: sub64
; BUNDLE-DAG: sra64
; BUNDLE-DAG: st32
; BUNDLE-DAG: {{bnez|beqz|set_hwloop}}
; BUNDLE: jalr{{.*}}r0, lr, 0
;
; Note: process uses compare+branch loop control (not JAL). The only JALs are
; in main (calls to process). Return is jalr via lr. Live objdump omits the
; legacy `_w` suffix on jal/jalr.

; === Entry point: 2-section cascaded IIR ===;
; C: int main(void) { ... filter process ... return r[0]; }
define i32 @main() {
; BUNDLE-LABEL: <main>:
; BUNDLE-DAG: lui
; BUNDLE-DAG: addi32
; Unrelocated call target prints as "jal lr, 0" under Format E objdump.
; BUNDLE: jal{{.*}}lr, 0
; BUNDLE: ld32
; BUNDLE: jalr{{.*}}r0, lr, 0
entry:
  ; Allocate 2 sections (36 bytes each) + input/output arrays (32 bytes each)
  ; on the stack. Simplified: use static globals.

  ; Process section 0
  call void @bqriir32x32_df1_process(ptr @section0, ptr @output_buf, ptr @input_buf, i32 8)

  ; Process section 1 (reads from output_buf)
  call void @bqriir32x32_df1_process(ptr @section1, ptr @output_buf, ptr @output_buf, i32 8)

  ; Return first output sample
  %r0ptr = load i32, ptr @output_buf
  ret i32 %r0ptr
}

; === Static data ===;

; Section 0: low-pass biquad coefficients (Q1.30) + zero initial state
@section0 = internal global [9 x i32] [
  i32 536870912,   ; b0 = 0x20000000 = 0.25 in Q30
  i32 268435456,   ; b1 = 0x10000000 = 0.125 in Q30
  i32 134217728,   ; b2 = 0x08000000 = 0.0625 in Q30
  i32 805306368,   ; a1 = 0x30000000 = 0.375 in Q30
  i32 402653184,   ; a2 = 0x18000000 = 0.1875 in Q30
  i32 0, i32 0,    ; sx0, sx1 = 0
  i32 0, i32 0     ; sy0, sy1 = 0
]

; Section 1: second biquad section
@section1 = internal global [9 x i32] [
  i32 805306368,   ; b0 = 0x30000000 = 0.375 in Q30
  i32 536870912,   ; b1 = 0x20000000 = 0.25 in Q30
  i32 268435456,   ; b2 = 0x10000000 = 0.125 in Q30
  i32 671088640,   ; a1 = 0x28000000 = 0.3125 in Q30
  i32 335544320,   ; a2 = 0x14000000 = 0.15625 in Q30
  i32 0, i32 0,    ; sx0, sx1 = 0
  i32 0, i32 0     ; sy0, sy1 = 0
]

; Input: impulse response test signal (single non-zero sample)
@input_buf = internal global [8 x i32] [
  i32 1073741824,  ; 0x40000000 = 0.5 in Q31 (impulse)
  i32 0, i32 0, i32 0,
  i32 0, i32 0, i32 0, i32 0
]

; Output buffer
@output_buf = internal global [8 x i32] zeroinitializer
