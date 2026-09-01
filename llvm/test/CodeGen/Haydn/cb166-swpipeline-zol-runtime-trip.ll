; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: CB-166 (2026-08-27) — ZOL loops with RUNTIME trip counts must be
; SMS-able via a dynamic per-prologue guard, not blanket-refused by the
; static MinTripCount law.
;
; The old law copied AIE's ZeroOverheadLoop refusal (MinTripCount==0 ->
; reject every multi-stage schedule), which locked the common FIR shape
; (`for (k = 0; k + 2 <= m; k += 2)`, runtime m) out of modulo scheduling:
; no cross-iteration overlap, loads serialized before MAC bundles, ~4 empty
; parcels per trip. The new law (Hexagon J2_loop0r peer): when LoopStart's
; count operand is a runtime register, createTripCountGreaterCondition
; emits a runtime skip-prologue condition on that register (the exact
; value SET_HWLOOP_F2_W consumes), so trips that cannot cover the peel
; branch around the prologue/kernel — the iteration-count invariant holds
; without a static bound. Constant-trip ZOL loops keep the static
; MinTripCount>PrologueCount law (guard-free acceptance).
;
; Body shape (bkfir32x32 tap core): a loop-carried post-inc load STRAND
; (each iteration's data load advances the pointer the next iteration
; consumes — the recurrence that makes multi-stage overlap profitable),
; a second coefficient post-inc load, x2sel window selects, and two
; INDEPENDENT dual-product accumulator MAC chains.

; HC#0 II-retry (2026-08-28): shouldUseSchedule now sits inside the II
; search, so the accept lines print BEFORE "Schedule Found? 1" and a
; rejected II keeps searching instead of aborting the loop.

; SWP: ZOL: runtime trip reg live — dynamic prologue guards cover the peel
; SWP: SMS-SHOULDUSE: accept stages={{[2-9]}}
; SWP: Schedule Found? 1
; SWP-NOT: ZOL: reject SMS (MaxStageCount={{[0-9]+}} MinTripCount=0 — no runtime trip reg for a dynamic guard)
; SWP-NOT: SMS: reject ZOL loop with ungrounded PHI chain
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: zol_runtime_trip_strand:
; ASM: set_hwloop_f2
; ASM: jalr

@ze = external global [64 x i64], align 8
@hcq = external global [64 x i64], align 8

define void @zol_runtime_trip_strand(i32 noundef %m, i64 noundef %a) {
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
  %q1 = phi i64 [ 0, %entry ], [ %q1.n, %loop ]
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
  %m1 = tail call i64 @llvm.haydn.ff2mula32r.ll(i64 %q1, <2 x i32> %p2.sel, <2 x i32> %bc.c)
  %q0.n = tail call i64 @llvm.haydn.ff2mula32r.hh(i64 %m0, <2 x i32> %p.sel, <2 x i32> %bc.c)
  %q1.n = tail call i64 @llvm.haydn.ff2mula32r.hh(i64 %m1, <2 x i32> %p2.sel, <2 x i32> %bc.c)
  %k.next = add nuw nsw i32 %k, 1
  %done = icmp sgt i32 %k.next, %m
  br i1 %done, label %exit, label %loop

exit:
  store i64 %q0.n, ptr @ze, align 8
  store i64 %q1.n, ptr getelementptr inbounds ([64 x i64], ptr @ze, i32 0, i32 1), align 8
  ret void
}

declare { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr, i32 immarg)
declare <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.hh(i64, <2 x i32>, <2 x i32>)
