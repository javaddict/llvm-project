; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; REGRESSION TEST: SF1 — format-aware SMS placement oracle (Band 2S).
;
; Bug: every post-RA multistage placement probe (fitInInterval, first-iter
; scoreboard, steady-state, resourcesConverged) ended at HR checkConflict,
; which is ports/itinerary-only. Format E row/entry coverage first appeared
; at the commit tail, so a modulo cycle that cannot pack as one parcel was
; silently sequentialized while recordSWPSAnnotation still reported the
; assumed II (topics/scheduling TOPIC.md 2026-08-14 finding 1/2).
; Fix: ModuloCyclePlacementOracle (HaydnBundleFormatSolver.h) — one
; CycleCandidateSet per modulo cycle, probe canExactTryAddProduct, mutate
; exactTryAddProduct on the accept path only; ResMII row-capacity term
; moduloRowCapacityII prices E2-only bodies (NBody=4, all four body ops
; E2-capable -> RowCapII = ceil((4 + ceil(2/2))/3) = 2 at the all-E2 limit).
;
; Test design: the steady-state loop body lowers to two E2-only addi32 ops
; per iteration (12/-12 stride pairs). An oracle that over-rejects (e.g. an
; occupancy-count model refusing E2 rows entirely, or RowCapII inflated past
; the flat term on zero E3-only deficit) kills the II=2 accept below; an
; oracle that under-rejects (dropped from the probe chain) is caught by the
; solver unit tests (SF1_ModuloOracleE2OnlyTripleRejectsAtII1). This file
; pins the search-side integration: the accept stays II=2 with ResMII=2 —
; the E2 pair shares one parcel per modulo cycle, exactly the legal shape.
;
; F39 (2026-08-15): the loop's trip comes from %n with no preheader
; constant, so the backedge now carries llvm.loop.itercount.range as the
; static min-trip proof the post-RA gate requires (stages=2 needs >= 2).
; Without that proof this loop must PF-TRIP reject — by design, the
; prologue has no runtime guard for trip < NStages.
;
; SF1 wiring rebaseline: II=2 stays refused (RowCap / format). SF5 now
; allows II==LinearLength as a kernel-only schedule, so the same body
; accepts II=3 with parcels-per-iter == searched II. Sequential leftover
; emission remains banned.
;
; ASM-LABEL: sf1_e2pair_oracle:
; ASM: jalr
; RMK: accepted II=[[II:[0-9]+]]
; RMK-SAME: measured-II=[[II]]
; RMK: qualify parcels-per-iter=[[II]]
; RMK-SAME: searched-II=[[II]]
; RMK-NOT: sequential (preflight)

define i32 @sf1_e2pair_oracle(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %x = phi i32 [ 0, %pre ], [ %x2, %body ]
  %y = phi i32 [ 0, %pre ], [ %y2, %body ]
  %inext = add nsw i32 %i, -1
  %x2 = add i32 %x, 5
  %y2 = add i32 %y, 7
  %v0 = load i32, ptr %a, align 4
  %v1 = load i32, ptr %b, align 4
  %t1 = add i32 %v0, %x2
  %z2 = add i32 %t1, %y2
  %t2 = add i32 %z2, %v1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %w = phi i32 [ 0, %entry ], [ %t2, %body ]
  ret i32 %w
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 8}
