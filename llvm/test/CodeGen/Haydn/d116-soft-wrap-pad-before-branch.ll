; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-enable-hwloops=false < %s | FileCheck %s --check-prefix=SOFT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s \
; RUN:   | FileCheck %s --check-prefix=ZOL
;
; D1.16 soft-latch form of the wrap law. With HWLoops disabled the latch
; keeps its backedge branch; the branch IS the wrap point, and the pads
; before it also cover the exit path (one insertion point, never two).
; The tail load's data dest / base writeback are consumed at the next
; iteration's body top; at -O2 the scheduler keeps compare/branch parcels
; after the load, and the pass adds the residual deficit only when the
; distance is short — pinned by the MIR tests (d116-soft-wrap-pad-before-
; branch.mir). Here the asm-level law is: the backedge branch is the LAST
; parcel of the loop body and no under-stall remains between the tail
; load and the body top across the wrap.

define i32 @soft_wrap_tail(ptr %p, i32 %c) nounwind {
entry:
  %first = getelementptr inbounds i32, ptr %p, i32 1
  %v0 = load i32, ptr %first, align 4
  br label %loop
loop:
  %ptr = phi ptr [ %first, %entry ], [ %next, %loop ]
  %v = phi i32 [ %v0, %entry ], [ %v2, %loop ]
  %sum = add i32 %v, %c
  %next = getelementptr inbounds i32, ptr %ptr, i32 1
  %v2 = load i32, ptr %next, align 4
  %done = icmp eq i32 %v2, %v0
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %sum
}

; SOFT arm: no set_hwloop (software loop); body top reads the previous
; iteration's load dest (move32), tail load defines it, and at least one
; parcel (compare or pad) sits between the load and the backedge branch.
; SOFT-LABEL: soft_wrap_tail:
; SOFT-NOT: set_hwloop
; SOFT: move32
; SOFT: s_lw_post_imm
; SOFT: bnez

; ZOL arm of the same function: this loop compares against a loaded
; invariant, so HWLoop formation is not selected; the countable ZOL form
; is covered by d116-zol-wrap-pad-before-end.ll. Here both arms are the
; soft latch: tail load then in-flight parcels (idle + compare + xori)
; before the backedge — wrap distance covered, branch is the wrap point.
; ZOL-LABEL: soft_wrap_tail:
; ZOL-NOT: set_hwloop
; ZOL: s_lw_post_imm
; ZOL: nop
; ZOL: bnez
