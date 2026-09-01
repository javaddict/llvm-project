; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=O2
;
; D1.16 primary ZOL wrap reproducer (probe shape: post-inc load at the body
; tail, its data dest and base writeback consumed at the next iteration's
; body top). The loop back-edge wrap law requires the wrap distance to
; cover the Data_Latency-2 window across HWLR_END -> HWLR_BEGIN:
;
;   * the wrap pads (this pass's residual, or the scheduler's idle parcel
;     plus the residual) must execute INSIDE [BEGIN,END] — the END label
;     anchors at the last real parcel, so the parcel after the tail load
;     but before END is on every iteration (pin b);
;   * the END label lands AFTER those parcels (never between the load and
;     its wrap cover);
;   * no wrap pair → no extra wrap parcels (negative control: the body's
;     own scheduler shape is untouched).
;
; BundleSim cannot catch this (functional simulator, no timing model).

define i32 @wrap_tail_load(ptr %p, i32 %n, i32 %c) nounwind {
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

; O2 ZOL body: [move32 (reads r4)] [s_lw_post_imm (defs r4/r1)]. The
; END label anchors at the trailing idle parcel, so that parcel is INSIDE
; [BEGIN,END] and covers the wrap distance for the latency-2 dests.
; O2-LABEL: wrap_tail_load:
; O2: Lhwloop_start
; O2: move32
; O2: s_lw_post_imm
; O2: Lhwloop_end
; O2: nop
; O0 arm: the spill-shaped body keeps its own schedule; every ld32 dest
; read in the next parcel still has its intra-block stall, and the latch
; keeps its scheduler/exit parcels — no under-stall at the wrap.
; O0-LABEL: wrap_tail_load:
; O0: ld32
; O0: ld32

; Negative control: ALU-only recurrence (latency 1) has no wrap pair.
define i32 @no_wrap_alu(ptr %p, i32 %n, i32 %c) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %v = phi i32 [ %c, %entry ], [ %v2, %loop ]
  %v2 = add i32 %v, 1
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %v2
}

; O2-LABEL: no_wrap_alu:
; O2-NOT: s_lw_post_imm
