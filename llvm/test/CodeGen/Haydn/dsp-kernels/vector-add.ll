; REQUIRES: haydn-registered-target
; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Un-XFAIL'd : vector-add ISel works; was stale-CHECK only.

; Un-XFAIL'd : vector-add ISel works; was stale-CHECK only.
;
; DSP Kernel Benchmark: Vector Addition
;
; Algorithm:
; for (i = 0; i < n; i++)
; c[i] = a[i] + b[i];
;
; Codegen quality targets:
; Loop body should be minimal: LD32 + LD32 + ADD32 + ST32 + increment + branch
; Hardware loop candidate: single-BB, countable trip count
; No multiply, so no MAC -- purely tests load/store/add pipeline
;
; This kernel exercises:
; Triple array access pattern (read-read-compute-write per iteration)
; Pre-loop guard (n > 0 check) to skip empty arrays
; Simple loop with no multiply -- baseline for memory throughput tests
;
; NOTE: At default optimization level, the optimizer may eliminate dead
; stores. The key instructions checked are: add32 (the vector add)
; slt32 (pre-loop guard), and the hardware loop (set_hwloop_f2).

define void @vector_add(ptr %a, ptr %b, ptr %c, i32 %n) {
; CHECK-LABEL: vector_add:
; (SFR-strip) changed bundle layout — rebaselined.
; The countable loop now converts to a hardware loop (set_hwloop_f2), so the
; back-edge branch (blt_w) is gone — replaced by the hwloop boundary. The compute
; (add32), the pre-loop guard (slt32), and the return (jalr_w) all survive.
; CHECK:       // %bb.0:
; CHECK:         slt32
; CHECK:         set_hwloop_f2
; CHECK:         add32
; CHECK:         jalr{{(\.s[012])?}}
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

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
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
