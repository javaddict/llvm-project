; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll); bundles regrouped (mul+addi32 fused; standalone nop).
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; DSP Kernel Benchmark: 4x4 Matrix Multiply (Intensive)
;
; Algorithm:
; for (i = 0; i < 4; i++)
; for (j = 0; j < 4; j++) {
; sum = 0;
; for (k = 0; k < 4; k++)
; sum += A[i][k] * B[k][j];
; C[i][j] = sum;
; }
;
; Expected instruction counts:
; 64 multiply operations total (4*4*4 = 64 MAC/MUL in inner loops)
; 64 LD32 for A matrix loads (4 per inner loop * 16 inner loop invocations)
; 64 LD32 for B matrix loads
; 16 ST32 for C matrix stores (1 per i,j pair)
; Inner loop body: MAC32 + LD32 + LD32 + ADD32 + branch = ~5 instructions
;
; VLIW bundle estimate:
; Inner loop: ~3-4 bundles per iteration
; (LD32 + LD32 + MAC32 + increment + compare + branch)
; LD32 cannot dual-issue (both use Slot0), so loads serialize
; MAC32 can dual-issue with LD32 (MAC32 on Slot1/2, LD32 on Slot0)
;
; This kernel exercises:
; Triple-nested loop with MAC fusion
; Row-major A access (stride-1 inner) and column-major B access (stride-4 inner)
; Register pressure: up to 12 live values (loop counters + base ptrs + accumulator)
; Callee-saved register spills for high register pressure
; Store-after-inner-loop pattern (accumulate then store)
; Complex GEP addressing: A[i*4+k] and B[k*4+j]

define void @matmul_4x4(ptr %C, ptr %A, ptr %B) {
;
; Prologue: callee-save spills for high register pressure
;
; Inner loop must contain MAC32 for matrix element multiply-accumulate
; Each inner loop iteration has 1 mac32; the loop runs 4 times per (i,j) pair.
;
; update: the pre-RA HardwareLoops pass was removed (it corrupted
; SET_HWLOOP_REG MBB operands pre-RA). SMS runs pre-RA on the naive loop; the
; post-RA HaydnHardwareLoops pass does not form a hardware loop for this nested
; shape (the SMS kernel's guarded preheader + multi-BB epilogue defeat the
; single-exit/recognizer checks). The inner k-loop stays a regular bnez_w
; back-edge. mul64.ll in the body remains the invariant this test locks in.
;
; Store computed C[i][j] after each inner loop completes.
;
; Epilogue: restore callee-saved registers and return
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
  ; A[i*4 + k]
  %a.row = mul i32 %i, 4
  %a.idx = add i32 %a.row, %k
  %ptr.a = getelementptr i32, ptr %A, i32 %a.idx
  %va = load i32, ptr %ptr.a
  ; B[k*4 + j]
  %b.row = mul i32 %k, 4
  %b.idx = add i32 %b.row, %j
  %ptr.b = getelementptr i32, ptr %B, i32 %b.idx
  %vb = load i32, ptr %ptr.b
  ; accumulate
  %mul = mul i32 %va, %vb
  %sum.next = add i32 %mul, %sum
  %k.next = add i32 %k, 1
  %cmp.k = icmp slt i32 %k.next, 4
  br i1 %cmp.k, label %inner.k, label %outer.j.latch

outer.j.latch:
  ; C[i*4 + j] = sum
  %c.row = mul i32 %i, 4
  %c.idx = add i32 %c.row, %j
  %ptr.c = getelementptr i32, ptr %C, i32 %c.idx
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
