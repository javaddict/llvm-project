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
; REGRESSION TEST: F44 — golden Constraints:67 pack-boundary alias law.
;
; Bug: the multi-stage engine had NO store→load may-alias separation at the
; pack boundary. HaydnResourceCycle checkConflict is ports/itinerary-only;
; the two-copy Order dep latency is 0, so a store of iteration k and a
; load of iteration k+1 could be placed in the SAME modulo cycle (one
; Format E pack window) whenever their execution units are injective
; (LOADSTORE0 store + LOAD1 load at commit time). Golden Constraints:67:
; a store and a load must not target overlapping addresses within one
; bundle unless non-overlap is PROVEN — else the hardware raises an
; exception. The ordinary list scheduler is protected only via the
; ReadyCycle invariant, not a pack-layer law.
;
; Fix: certificatePackAlias (HaydnPostRAMultiStage.cpp) — one fail-closed
; gate over every modulo-cycle pack group: any mayStore×mayLoad pair in the
; same group must be proven NoAlias (MachineInstr::mayAlias with AA), else
; the II candidate rejects (tryII) and materialize re-verifies before any
; MIR mutation.
;
; Test design: in-place may-alias load/store of %p (same base pointer) in a
; countdown loop. The engine must NEVER accept this loop into a multi-stage
; plan whose pack windows could carry the pair — asserted as the absence of
; any MultiStageAccept remark in analysis mode. Today the engine's logical
; LD32/ST32 both book LOADSTORE0 so HR also separates them; the gate is the
; durable law that stays fail-closed the day any load/store pair becomes
; same-cycle placeable (LOAD1/ALU2 member loads at commit, relaxed booking).
; If certificatePackAlias is deleted AND such a pair becomes placeable, an
; accept appears here and RMK-NOT fails.

; ASM: name: inplace_mayalias
; RMK-NOT: accepted II=
; RMK-NOT: MultiStageStageMBB

define i32 @inplace_mayalias(ptr nocapture %p, i32 %n, i32 %k) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
exit:
  %r = phi i32 [ 0, %entry ], [ %acc, %body ]
  ret i32 %r
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %acc = phi i32 [ 0, %pre ], [ %y2, %body ]
  %v = load i32, ptr %p, align 4
  %u = load i32, ptr %p, align 4
  %w = xor i32 %v, %u
  %y = xor i32 %w, %k
  store i32 %y, ptr %p, align 4
  %y2 = xor i32 %acc, %y
  %inext = add nsw i32 %i, -1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 64}
