; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: CB-166 (2026-08-27) — the FIR kernel shape (ZOL + runtime trip +
; sliding-window delay PHIs + loop-carried post-inc load strand) must reach
; SMS. This is the combination of both CB-166 laws: the delay-line PHIs are
; grounded (not blanket shift-register-rejected) AND the runtime trip feeds
; the dynamic per-prologue guard (not MinTripCount-refused). This is the
; loop family of bkfir32x32's tap core: 2 post-inc loads + x2sel window
; selects + dual-MAC per trip.
;
; The J1/J2 delay PHIs are the exact "shift register" the init-era
; hasShiftRegisterPhiChain blanket-rejected; here J1's latch value is J2
; (a PHI) whose latch value is the load result (a body def), so the chain
; grounds and the classic expander's phi-of-phi walk applies.

; HC#0 II-retry (2026-08-28): shouldUseSchedule now sits inside the II
; search, so the accept lines print BEFORE "Schedule Found? 1".

; SWP: ZOL: runtime trip reg live — dynamic prologue guards cover the peel
; SWP: SMS-SHOULDUSE: accept stages={{[2-9]}}
; SWP: Schedule Found? 1
; SWP-NOT: SMS: reject ZOL loop with ungrounded PHI chain
; SWP-NOT: ZOL: reject SMS (MaxStageCount={{[0-9]+}} MinTripCount=0 — no runtime trip reg for a dynamic guard)
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: zol_fir_window_strand:
; ASM: set_hwloop_f2
; ASM: jalr

@ze = external global [64 x i64], align 8
@hcq = external global [64 x i64], align 8

define void @zol_fir_window_strand(i32 noundef %m, i64 noundef %a) {
entry:
  %pre = getelementptr inbounds i64, ptr @ze, i32 0
  %l0 = tail call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %pre, i32 1)
  %p0 = extractvalue { i64, ptr } %l0, 1
  %j1v = extractvalue { i64, ptr } %l0, 0
  %l1 = tail call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %p0, i32 1)
  %j2v = extractvalue { i64, ptr } %l1, 0
  br label %loop

loop:
  %ptr = phi ptr [ %p0, %entry ], [ %ptr.next, %loop ]
  %j1 = phi i64 [ %j1v, %entry ], [ %j2, %loop ]
  %j2 = phi i64 [ %j2v, %entry ], [ %v2, %loop ]
  %q0 = phi i64 [ 0, %entry ], [ %q0.n, %loop ]
  %k = phi i32 [ 0, %entry ], [ %k.next, %loop ]
  %ld = tail call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %ptr, i32 1)
  %v2 = extractvalue { i64, ptr } %ld, 0
  %ptr.next = extractvalue { i64, ptr } %ld, 1
  %cp = getelementptr inbounds i64, ptr @hcq, i32 %k
  %lc = tail call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %cp, i32 1)
  %c = extractvalue { i64, ptr } %lc, 0
  %bc.j2 = bitcast i64 %j2 to <2 x i32>
  %bc.j1 = bitcast i64 %j1 to <2 x i32>
  %bc.v2 = bitcast i64 %v2 to <2 x i32>
  %bc.c = bitcast i64 %c to <2 x i32>
  %p.sel = tail call <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32> %bc.j2, <2 x i32> %bc.j1)
  %p2.sel = tail call <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32> %bc.v2, <2 x i32> %bc.j2)
  %m0 = tail call i64 @llvm.haydn.ff2mula32r.ll(i64 %q0, <2 x i32> %p.sel, <2 x i32> %bc.c)
  %q0.n = tail call i64 @llvm.haydn.ff2mula32r.hh(i64 %m0, <2 x i32> %p.sel, <2 x i32> %bc.c)
  %m1 = tail call i64 @llvm.haydn.ff2mula32r.ll(i64 %q0.n, <2 x i32> %p2.sel, <2 x i32> %bc.c)
  %q1.n = tail call i64 @llvm.haydn.ff2mula32r.hh(i64 %m1, <2 x i32> %p2.sel, <2 x i32> %bc.c)
  %k.next = add nuw nsw i32 %k, 1
  %done = icmp sgt i32 %k.next, %m
  br i1 %done, label %exit, label %loop

exit:
  store i64 %q1.n, ptr @ze, align 8
  ret void
}

declare { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr, i32 immarg)
declare <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.hh(i64, <2 x i32>, <2 x i32>)
