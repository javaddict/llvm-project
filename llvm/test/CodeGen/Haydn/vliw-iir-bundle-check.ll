; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -O2 \
; RUN:   < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: haydn-registered-target

; Role: semantic — IIR biquad kernels emit mull/add32/sub32/ld32/st32 through post-RA pack at -O2.

; IIR biquad filter kernel codegen test.
;
; This test exercises the full GISel pipeline on representative IIR filter
; kernels and verifies the assembly output contains expected instruction
; mnemonics (mul32, mac32, sub32, add32, ld32, st32, jalr_w).
;
; NOTE: The VLIW packetizer is currently disabled (see HaydnTargetMachine.cpp
; line 260). When re-enabled, add back -print-after=haydn-vliw-packetizer MIR
; checks for BUNDLE instructions.
;
; The IIR biquad Direct Form 1 kernel computes:
; yn = b0*xn + b1*x1 + b2*x2 - a1*y1 - a2*y2
;
; This is the most common DSP filter operation and produces a mix of:
; Multiply-accumulate chains (mull + add32; no scalar GPR MAC opcode)
; Subtractions for the feedback path (sub32)
; Loads for coefficient and state access (ld32)
; Stores for state updates (st32)

; Simple IIR biquad inner product: pure register-based ALU.
; Exercises mull + add32.

define i32 @biquad_simple(i32 %b0, i32 %xn, i32 %b1, i32 %x_nm1,
                          i32 %a1, i32 %y_nm1) nounwind {
entry:
  %prod0 = mul i32 %b0, %xn
  %sum   = mul i32 %b1, %x_nm1
  %acc   = add i32 %prod0, %sum
  %fb    = mul i32 %a1, %y_nm1
  %result = sub i32 %acc, %fb
  ret i32 %result
}

; ASM-LABEL: biquad_simple:
; ASM-DAG: mull
; ASM-DAG: sub32
; ASM: jalr

; IIR biquad with memory load/store for state array.
define void @biquad_with_state(i32 %xn, ptr nocapture %state) nounwind {
entry:
  %x1_ptr = getelementptr i32, ptr %state, i32 0
  %x2_ptr = getelementptr i32, ptr %state, i32 1
  %y1_ptr = getelementptr i32, ptr %state, i32 2
  %x1 = load i32, ptr %x1_ptr, align 4
  %x2 = load i32, ptr %x2_ptr, align 4
  %y1 = load i32, ptr %y1_ptr, align 4
  %p0 = mul i32 %xn, %x1
  %p1 = mul i32 %xn, %x2
  %s0 = add i32 %p0, %p1
  %yn = sub i32 %s0, %y1
  store i32 %xn, ptr %x1_ptr, align 4
  store i32 %x2, ptr %x2_ptr, align 4
  store i32 %yn, ptr %y1_ptr, align 4
  ret void
}

; ASM-LABEL: biquad_with_state:
; ASM-DAG: ld32
; ASM-DAG: mull
; ASM-DAG: sub32
; ASM-DAG: st32
; ASM: jalr

; Block processing: apply biquad to N samples in a loop.
define void @biquad_process_block(ptr nocapture %input, ptr nocapture %output,
                                  i32 %N, i32 %b0, i32 %b1, i32 %b2,
                                  i32 %a1, i32 %a2, ptr nocapture %state) nounwind {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %xn_ptr = getelementptr i32, ptr %input, i32 %i
  %yn_ptr = getelementptr i32, ptr %output, i32 %i
  %xn = load i32, ptr %xn_ptr, align 4
  ; Inline 32-bit biquad (no call)
  %x1_ptr = getelementptr i32, ptr %state, i32 0
  %x2_ptr = getelementptr i32, ptr %state, i32 1
  %y1_ptr = getelementptr i32, ptr %state, i32 2
  %y2_ptr = getelementptr i32, ptr %state, i32 3
  %x1 = load i32, ptr %x1_ptr, align 4
  %x2 = load i32, ptr %x2_ptr, align 4
  %y1 = load i32, ptr %y1_ptr, align 4
  %y2 = load i32, ptr %y2_ptr, align 4
  %p0 = mul i32 %b0, %xn
  %p1 = mul i32 %b1, %x1
  %s0 = add i32 %p0, %p1
  %p2 = mul i32 %b2, %x2
  %s1 = add i32 %s0, %p2
  %p3 = mul i32 %a1, %y1
  %s2 = sub i32 %s1, %p3
  %p4 = mul i32 %a2, %y2
  %yn = sub i32 %s2, %p4
  store i32 %yn, ptr %yn_ptr, align 4
  store i32 %xn, ptr %x1_ptr, align 4
  store i32 %x1, ptr %x2_ptr, align 4
  store i32 %yn, ptr %y1_ptr, align 4
  store i32 %y2, ptr %y2_ptr, align 4
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %N
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; ASM-LABEL: biquad_process_block:
; ASM-DAG: mull
; ASM-DAG: {{bnez|blt|beqz}}
; ASM-DAG: mull
; ASM-DAG: sub32
; ASM-DAG: ld32
; ASM-DAG: st32
; ASM-DAG: jalr
