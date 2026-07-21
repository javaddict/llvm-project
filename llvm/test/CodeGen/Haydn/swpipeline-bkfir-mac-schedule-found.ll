; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s

; NatureDSP bkfir32x32 MAC hot-loop shape: dual coef loads + 4×4 acc-MAC
; chains + dual circular-buffer sample loads. Requires:
; MAC acc→acc latency kept at 1 (not collapsed by ALU→ALU 1→0)
; MachinePipeliner computeNodeOrder pred_L filtered by NodeSet so
; loads order before the pointer PHI after MAC circuits
; load→acc-MAC latency 2, S2-first pickSlot, ADDI32 (not ADDI32_W)
;
; Before the residual fix: Res MII:10 Rec:1–4 Schedule Found? 0 (II=20)
; with es>ls on LD64_S1.
;
; CHECK: Return Res MII:{{[1-9][0-9]*}}
; CHECK: MII = {{[1-9][0-9]*}} MAX_II = {{[1-9][0-9]*}} (rec={{[1-9]}}, res={{[1-9][0-9]*}})
; Prefer II at ResMII (typically 9–10); accept any Found II ≤ 12.
; CHECK: Schedule Found? 1 (II={{([1-9]|1[0-2])}})

define void @bkfir_mac_hot(
    ptr nocapture readonly %C,
    ptr nocapture readonly %D0,
    ptr nocapture readonly %D1,
    ptr nocapture %Q,
    i32 %M4) {
entry:
  %cmp = icmp sgt i32 %M4, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pc = phi ptr [ %C, %entry ], [ %pc.n, %loop ]
  %q0 = phi i64 [ 0, %entry ], [ %q0.n, %loop ]
  %q1 = phi i64 [ 0, %entry ], [ %q1.n, %loop ]
  %q2 = phi i64 [ 0, %entry ], [ %q2.n, %loop ]
  %q3 = phi i64 [ 0, %entry ], [ %q3.n, %loop ]
  %d01 = phi i64 [ 0, %entry ], [ %d45, %loop ]
  %d12 = phi i64 [ 0, %entry ], [ %d56, %loop ]

  %d23 = load i64, ptr %D0, align 8
  %d45 = load i64, ptr %D0, align 8
  %d34 = load i64, ptr %D1, align 8
  %d56 = load i64, ptr %D1, align 8

  %c0 = load i64, ptr %pc, align 4
  %pc1 = getelementptr inbounds i64, ptr %pc, i32 1
  %c1 = load i64, ptr %pc1, align 4
  %pc.n = getelementptr inbounds i64, ptr %pc, i32 2

  %q0.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0, i64 %d01, i64 %c0)
  %q0.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.a, i64 %d01, i64 %c0)
  %q0.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.b, i64 %d23, i64 %c1)
  %q0.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q0.c, i64 %d23, i64 %c1)

  %q1.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1, i64 %d12, i64 %c0)
  %q1.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.a, i64 %d12, i64 %c0)
  %q1.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.b, i64 %d34, i64 %c1)
  %q1.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q1.c, i64 %d34, i64 %c1)

  %q2.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2, i64 %d23, i64 %c0)
  %q2.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.a, i64 %d23, i64 %c0)
  %q2.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.b, i64 %d45, i64 %c1)
  %q2.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q2.c, i64 %d45, i64 %c1)

  %q3.a = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3, i64 %d34, i64 %c0)
  %q3.b = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.a, i64 %d34, i64 %c0)
  %q3.c = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.b, i64 %d56, i64 %c1)
  %q3.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %q3.c, i64 %d56, i64 %c1)

  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, %M4
  br i1 %done, label %exit, label %loop

exit:
  %r0 = phi i64 [ 0, %entry ], [ %q0.n, %loop ]
  %r1 = phi i64 [ 0, %entry ], [ %q1.n, %loop ]
  %r2 = phi i64 [ 0, %entry ], [ %q2.n, %loop ]
  %r3 = phi i64 [ 0, %entry ], [ %q3.n, %loop ]
  store i64 %r0, ptr %Q, align 8
  %Q1 = getelementptr inbounds i64, ptr %Q, i32 1
  store i64 %r1, ptr %Q1, align 8
  %Q2 = getelementptr inbounds i64, ptr %Q, i32 2
  store i64 %r2, ptr %Q2, align 8
  %Q3 = getelementptr inbounds i64, ptr %Q, i32 3
  store i64 %r3, ptr %Q3, align 8
  ret void
}

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, i64, i64)
