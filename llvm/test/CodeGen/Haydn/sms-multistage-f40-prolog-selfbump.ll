; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -stop-before=haydn-finalize-mi-bundles \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -stop-before=haydn-finalize-mi-bundles \
; RUN:     < %s | FileCheck %s --check-prefix=ASM
;
; REGRESSION TEST: F40 — prologue must clone loop-carried pointer-IV bumps
; (or reject); only the soft TRIP bump may be skipped (QUALIFY blocker).
;
; Bug: the prologue peel skipped ALL loop-carried self-bumps (a stale P2
; vec_scale liveness workaround, not semantics). A stage-s consumer of a
; bumped pointer IV needs k+s bumps before the kernel starts; the kernel
; body runs only the remaining k, so every skipped bump is a stale-address
; deficit: the pipelined body reads/writes the WRONG (pre-bump) addresses
; — silent wrong code whenever a self-bump's dest was not live at the
; prolog insert point. The skip was sound ONLY for the soft trip bump
; (absorbed by adjustSoftTripCount's preheader ADDI32).
;
; Fix: isSoftTripBumpMI is the sole exclusion (HaydnPostRAMultiStage.cpp).
; Every other self-bump is peeled like any other node (AIE PostPipeliner
; shape — no bump exclusion); when its uses are not live at the insert
; point the plan rejects fail-closed (unsafe-prolog-peel / PF-LIVE), never
; silently drops the bump.
;
; Test design: a countdown loop whose pointer advances by an explicit ADDI
; self-bump each iteration (post-inc addressing defeated via two users of
; %q, so the bump stays a visible self-bump MI), plus may-alias in-place
; load/store of %p. The engine must not accept this loop into a
; multi-stage plan that skips the bump: no MultiStageStageMBB / accepted
; materialization may appear. (II window + liveness laws reject it here —
; the assert pins that outcome; if the F40 fix regresses, the prologue
; clone path is what keeps the reject fail-closed rather than wrong-code.)

; ASM: name: ptrbump_selfbump
; RMK-NOT: MultiStageStageMBB

define void @ptrbump_selfbump(ptr nocapture %p, ptr nocapture readonly %q,
                              i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
exit:
  ret void
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %t = phi ptr [ %q, %pre ], [ %tnext, %body ]
  %v = load i32, ptr %p, align 4
  %w = xor i32 %v, 305419896
  store i32 %w, ptr %p, align 4
  %u1 = load i32, ptr %t, align 4
  %tnext = getelementptr inbounds i32, ptr %t, i32 1
  %u2 = load i32, ptr %tnext, align 4
  %inext = add nsw i32 %i, -1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 64}
