; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s

; Dual-load accumulator-MAC reduction (NatureDSP vec_dot32x32 shape).
; Requires:
; G_PTR_ADD → ADDI32 (packable) not ADDI32_W (S0-only)
; Bundle pickSlot S2→S0 so loads keep S0/S1
; load→acc-MAC edge latency 2 (multi-stage span)
; ResMII honesty
;
; CHECK: Return Res MII:{{[3-9]|[1-9][0-9]}}
; CHECK: Schedule Found? 1 (II={{[1-9]}})
; CHECK-NOT: Unable to analyzeLoop

define i64 @dot_dual_load_mac(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  %pa = getelementptr inbounds i64, ptr %a, i32 %i
  %pb = getelementptr inbounds i64, ptr %b, i32 %i
  %va = load i64, ptr %pa, align 4
  %vb = load i64, ptr %pb, align 4
  %acc.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, i64 %va, i64 %vb)
  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  ret i64 %r
}

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, i64, i64)
