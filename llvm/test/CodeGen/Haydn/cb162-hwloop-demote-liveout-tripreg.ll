; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -O1 < %s | FileCheck %s
;
; REGRESSION TEST: CB-162 — a hardware-loop trip register that is live
; AFTER the loop must never be destroyed by the loop lowering.
;
; Bug (found via naturedsp bkfir16x16 at -O3): the h-fill hwloop's trip
; register was demoted to a software SUBI32/BNEZ_W countdown on the SAME
; register that still held a live value (the descriptor M field stored
; after the loop). The countdown left 0 in the register; the descriptor
; then said M=0 and the kernel silently skipped its whole fast path
; (output frozen across reps).
;
; Fix: canUsePreferAsCounter (HaydnHardwareLoops.cpp demote path) now
; requires the trip register to be dead at every loop exit before it may
; serve as the software countdown; otherwise the copy/materialize counter
; path preserves it. The ZOL form never writes the trip register.
;
; Test design: this loop stays a ZOL (small body), so the contract pinned
; here is the ZOL side of the same law: the trip value in r2 must be
; stored AFTER the loop with its original value (the hwloop reads r2,
; never writes it). A demote regression shows up as the stored register
; being the countdown register (value zero at store time). The full
; demote-path value contract is exercised end-to-end by the BundleSim
; repro benchmarks/naturedsp_kernels/tests/fir/bkfir16x16_o3_unroll_repro.c.

define void @cb162_tripreg_live_after_loop(ptr nocapture writeonly %out, i32 %trip, i32 %n) {
entry:
  %cmp = icmp sgt i32 %trip, 0
  br i1 %cmp, label %for.body.preheader, label %exit

for.body.preheader:
  br label %for.body

for.body:
  %i = phi i32 [ 0, %for.body.preheader ], [ %inc, %for.body ]
  %sum = phi i32 [ 0, %for.body.preheader ], [ %add, %for.body ]
  %mul = mul nsw i32 %i, %n
  %add = add nsw i32 %sum, %mul
  %inc = add nuw nsw i32 %i, 1
  %cond = icmp slt i32 %inc, %trip
  br i1 %cond, label %for.body, label %for.exit

for.exit:
  store i32 %add, ptr %out, align 4
  %gep1 = getelementptr i32, ptr %out, i32 1
  store i32 %trip, ptr %gep1, align 4
  ret void

exit:
  ret void
}

; CHECK-LABEL: cb162_tripreg_live_after_loop
; The ZOL setup reads the trip register (r2 here) ...
; CHECK: set_hwloop_f2 {{[^;]*}}r2
; ... and the trip value must still be stored intact after the loop (the
; loop lowering never defines r2). If this regresses to a software demote
; counting r2 down, this store emits 0.
; CHECK: st32 r2,
; No software countdown on the live trip register may appear:
; CHECK-NOT: subi32 r2, r2, 1
