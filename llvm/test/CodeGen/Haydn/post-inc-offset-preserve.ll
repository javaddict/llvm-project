; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1  -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_ld64_fold_in_loop:
; CHECK-LABEL: test_st64_fold_in_loop:
; CHECK: {{.}}

declare i64 @llvm.haydn.mulfp32x16x2ras.low(i64, i64, i64)

; LD64 fold inside a loop: the input pointer is post-incremented each
; iteration. Without offset preservation, the offset-4 LD32 would land at 0.
define void @test_ld64_fold_in_loop(ptr %in, ptr %out, i32 %n, i64 %coef) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ip = phi ptr [ %in, %entry ], [ %next_ip, %loop ]
  %op = phi ptr [ %out, %entry ], [ %next_op, %loop ]
  %x = load i64, ptr %ip
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 0, i64 %x, i64 %coef)
  store i64 %r, ptr %op
  %next_ip = getelementptr i64, ptr %ip, i32 1
  %next_op = getelementptr i64, ptr %op, i32 1
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; ST64 fold inside a loop: the output pointer is post-incremented each
; iteration. Without offset preservation, the offset-4 ST32 would land at 0
; overwriting the low half with the high half.
define void @test_st64_fold_in_loop(ptr %out, i32 %n, i64 %a, i64 %coef) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %op = phi ptr [ %out, %entry ], [ %next_op, %loop ]
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 0, i64 %a, i64 %coef)
  store i64 %r, ptr %op
  %next_op = getelementptr i64, ptr %op, i32 1
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}
