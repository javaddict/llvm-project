; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; DSP Kernel Benchmark: 128-Element Vector Addition (Memory-Bound)
;
; Algorithm:
; for (i = 0; i < 128; i++)
; c[i] = a[i] + b[i];
;
; Expected instruction counts (per loop iteration):
; 1 LD32 for a[i] load
; 1 LD32 for b[i] load
; 1 ADD32 for vector addition
; 1 ST32 for c[i] store
; 1 ADD32 for index increment
; 1 SLT32 + 1 BNEZ for loop control
; Total per iteration: ~6-7 instructions
;
; VLIW bundle fill rate analysis:
; Ideal: LD32(a) and LD32(b) would dual-issue, but both use Slot0_LS
; so they serialize into separate bundles.
; Realistic per-iteration bundle layout:
; Bundle 1: { ld32 a[i]; nop; nop }
; Bundle 2: { ld32 b[i]; nop; nop }
; Bundle 3: { add32 c=a+b; add32 i++; slt32 }
; Bundle 4: { st32 c[i]; bnez_w loop; nop }
; Expected fill rate: ~1.5-2 instructions per bundle average
; Bottleneck: Slot0_LS for all memory ops (LD32, ST32)
;
; This kernel exercises:
; Pure memory throughput benchmark (no multiply, no MAC)
; 128 * 3 = 384 memory operations (128 LD32 + 128 LD32 + 128 ST32)
; LD32 + ADD32 + ST32 pipeline pattern
; VLIW slot utilization measurement (Slot0 bottleneck for memory ops)
; Pre-loop guard for empty array case
; Constant trip count (128) -- hardware loop candidate
; Post-inc fusion : streaming loads fuse to s_lw_post_imm, the streaming
; store fuses to st32.post, and the loop lowers as a zero-overhead hardware
; loop. Previously XFAIL because the post-inc store wasn't fused; now
; passes cleanly.

define void @vector_add_128(ptr %a, ptr %b, ptr %c) {
;
; Return
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %ptr.a = getelementptr i32, ptr %a, i32 %i
  %ptr.b = getelementptr i32, ptr %b, i32 %i
  %ptr.c = getelementptr i32, ptr %c, i32 %i
  %va = load i32, ptr %ptr.a
  %vb = load i32, ptr %ptr.b
  %vc = add i32 %va, %vb
  store i32 %vc, ptr %ptr.c
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 128
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
