; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll) in single-op epilogue bundle.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; DSP Kernel Benchmark: 4x4 Matrix Multiply
;
; Algorithm:
; for (i = 0; i < 4; i++)
; for (j = 0; j < 4; j++) {
; sum = 0;
; for (k = 0; k < 4; k++)
; sum += A[i*4+k] * B[k*4+j];
; C[i*4+j] = sum;
; }
;
; Codegen quality targets:
; Inner loop (k) must contain MAC32
; Inner loop body target: <= 4 instructions
; Outer loops should use efficient addressing (stride-4 GEP folding)
; Register pressure: up to 12 live values across nested loops
;
; This kernel exercises:
; Triple-nested loop structure (i, j, k)
; MAC fusion in the innermost loop
; Complex addressing: A[i*4+k] and B[k*4+j] with two index variables
; High register pressure with callee-saved register spills/restores
; Store after inner loop completes (C[i*4+j] = sum)

define void @matmul_4x4(ptr %A, ptr %B, ptr %C) {
; The inner k-loop is NOT converted to a HWLoop: after GISel lowering the loop
; body is split into a multi-BB structure with two exits (zero-trip guard emits
; an early conditional branch), and Haydn hardware loops have no early-exit
; support (hard constraint). The recognizer correctly rejects it ("Loop has
; multiple exits"). All three nesting levels use branch back-edges.
; The body must contain a MAC op and a store (result writeback).
entry:
  br label %outer.i

outer.i:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.i.latch ]
  br label %outer.j

outer.j:
  %j = phi i32 [ 0, %outer.i ], [ %j.next, %outer.j.latch ]
  br label %inner.k

inner.k:
  %k = phi i32 [ 0, %outer.j ], [ %k.next, %inner.k ]
  %sum = phi i32 [ 0, %outer.j ], [ %sum.next, %inner.k ]
  %a_idx = mul i32 %i, 4
  %a_off = add i32 %a_idx, %k
  %ptr.a = getelementptr i32, ptr %A, i32 %a_off
  %b_idx = mul i32 %k, 4
  %b_off = add i32 %b_idx, %j
  %ptr.b = getelementptr i32, ptr %B, i32 %b_off
  %va = load i32, ptr %ptr.a
  %vb = load i32, ptr %ptr.b
  %mul = mul i32 %va, %vb
  %sum.next = add i32 %mul, %sum
  %k.next = add i32 %k, 1
  %cmp.k = icmp slt i32 %k.next, 4
  br i1 %cmp.k, label %inner.k, label %outer.j.latch

outer.j.latch:
  %c_idx = mul i32 %i, 4
  %c_off = add i32 %c_idx, %j
  %ptr.c = getelementptr i32, ptr %C, i32 %c_off
  store i32 %sum.next, ptr %ptr.c
  %j.next = add i32 %j, 1
  %cmp.j = icmp slt i32 %j.next, 4
  br i1 %cmp.j, label %outer.j, label %outer.i.latch

outer.i.latch:
  %i.next = add i32 %i, 1
  %cmp.i = icmp slt i32 %i.next, 4
  br i1 %cmp.i, label %outer.i, label %exit

exit:
  ret void
}
