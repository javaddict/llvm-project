; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s \
; RUN:   | FileCheck %s
;
; D1.16 asm-level END-anchor pin (plan pin b) for the product-kernel defect
; class (fir_convol16x16 BB#9 / fir_convol32x16 BB#16 / vec_bexp32 BB#4):
; the wrap-cover parcel must follow the tail load, and the END label must
; anchor at that cover parcel — never between the load and its cover and
; never at the load itself (a pad before the load shifts the load with it
; and covers nothing; the dest window would still cross the
; HWLR_END -> HWLR_BEGIN wrap).
;
; Inclusive-END convention (HaydnAsmPrinter::getLastRealInstr): the label is
; emitted immediately before the LAST size-bearing body parcel, and the
; parcel AT the END address executes every iteration — body trailing pads
; are legal last parcels ("useful LD/MAC/ST then appear before the END
; label while still executing inside the inclusive [BEGIN, END] window").
; So the pinned order is: load, Lhwloop_end, nop — the nop after the label
; is the in-body wrap cover at the END address.
;
; Shape: ZOL body [move32 reads prev load] [s_lw_post_imm defines v2/r1];
; the recurrence read at the body top is one cycle after the tail load's
; issue (wrap distance 1 < latency 2), so a wrap-cover parcel after the
; load is required (here left by the scheduler; the residual-emission arm
; is pinned on the same order by d116-zol-end-is-wrap-point.mir).

define i32 @residual_wrap_pad(ptr %p, i32 %n, i32 %c) nounwind {
entry:
  %first = getelementptr inbounds i32, ptr %p, i32 1
  %v0 = load i32, ptr %first, align 4
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %ptr = phi ptr [ %first, %entry ], [ %next, %loop ]
  %v = phi i32 [ %v0, %entry ], [ %v2, %loop ]
  %sum = add i32 %v, %c
  %next = getelementptr inbounds i32, ptr %ptr, i32 1
  %v2 = load i32, ptr %next, align 4
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %sum
}

; CHECK-LABEL: residual_wrap_pad:
; CHECK: Lhwloop_start
; CHECK: move32
; The tail load is the last def-bearing body parcel.
; CHECK: s_lw_post_imm
; The END label anchors at the wrap-cover parcel (never before the load's
; cover): the parcel at the END address is the pad, executing per-iteration.
; CHECK-NEXT: Lhwloop_end
; CHECK-NEXT: nop
