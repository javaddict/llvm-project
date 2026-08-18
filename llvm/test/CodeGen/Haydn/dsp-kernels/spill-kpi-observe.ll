; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -O2 -global-isel-abort=1 -enable-misched=false -enable-post-misched=false \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — spill/reload KPI observe on hot DSP kernels (fail-open).

; Spill/reload KPI observe on hot DSP kernels (fail-open).
; Port of AIE stack-slot predicates + AsmPrinter emitComments + Haydn
; function-level #<spill-kpi> summary (HaydnAsmPrinter).
;
; Fail-open soft baseline: only checks metric presence and non-negative
; digit fields. Format E product only; no schedule change from this path.

; CHECK-LABEL: fir_filter:
; CHECK: #<spill-kpi> @fir_filter spills={{[0-9]+}} spill-bytes={{[0-9]+}} reloads={{[0-9]+}} reload-bytes={{[0-9]+}}

; CHECK-LABEL: iir_biquad:
; High register pressure → CSR FrameSetup/Destroy traffic observed.
; Soft baseline expects non-zero spills on this kernel.
; CHECK: #<spill-kpi> @iir_biquad spills={{[1-9][0-9]*}} spill-bytes={{[1-9][0-9]*}} reloads={{[1-9][0-9]*}} reload-bytes={{[1-9][0-9]*}}

; CHECK-LABEL: dot_product:
; CHECK: #<spill-kpi> @dot_product spills={{[0-9]+}} spill-bytes={{[0-9]+}} reloads={{[0-9]+}} reload-bytes={{[0-9]+}}

define i32 @fir_filter(ptr %input, ptr %coeffs, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ptr.in = getelementptr i32, ptr %input, i32 %i
  %ptr.co = getelementptr i32, ptr %coeffs, i32 %i
  %in.val = load i32, ptr %ptr.in
  %co.val = load i32, ptr %ptr.co
  %mul = mul i32 %in.val, %co.val
  %acc.next = add i32 %mul, %acc
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}

define i32 @iir_biquad(i32 %input, ptr %a_coeffs, ptr %b_coeffs, ptr %state) {
entry:
  %b0.ptr = getelementptr i32, ptr %b_coeffs, i32 0
  %b1.ptr = getelementptr i32, ptr %b_coeffs, i32 1
  %b2.ptr = getelementptr i32, ptr %b_coeffs, i32 2
  %a0.ptr = getelementptr i32, ptr %a_coeffs, i32 0
  %a1.ptr = getelementptr i32, ptr %a_coeffs, i32 1
  %s0.ptr = getelementptr i32, ptr %state, i32 0
  %s1.ptr = getelementptr i32, ptr %state, i32 1

  %b0 = load i32, ptr %b0.ptr
  %b1 = load i32, ptr %b1.ptr
  %b2 = load i32, ptr %b2.ptr
  %a0 = load i32, ptr %a0.ptr
  %a1 = load i32, ptr %a1.ptr
  %s0 = load i32, ptr %s0.ptr
  %s1 = load i32, ptr %s1.ptr

  %m0 = mul i32 %b0, %input
  %acc = add i32 %m0, %s0

  %m1 = mul i32 %b1, %input
  %m2 = mul i32 %a0, %acc
  %t1 = add i32 %m1, %m2
  %new_s0 = add i32 %t1, %s1
  store i32 %new_s0, ptr %s0.ptr

  %m3 = mul i32 %b2, %input
  %m4 = mul i32 %a1, %acc
  %new_s1 = add i32 %m3, %m4
  store i32 %new_s1, ptr %s1.ptr

  ret i32 %acc
}

define i32 @dot_product(ptr %a, ptr %b, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %ptr.a = getelementptr i32, ptr %a, i32 %i
  %ptr.b = getelementptr i32, ptr %b, i32 %i
  %va = load i32, ptr %ptr.a
  %vb = load i32, ptr %ptr.b
  %mul = mul i32 %va, %vb
  %sum.next = add i32 %mul, %sum
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
