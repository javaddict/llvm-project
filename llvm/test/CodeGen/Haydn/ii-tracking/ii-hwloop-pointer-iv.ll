; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST (GAP-3, closed by): HWLoop recognizer converts
; pointer-IV loops whose only induction is a LD32_POST post-increment load.
;
; Status (closed by): the recognizer now emits set_hwloop_f2
; for this shape. The pointer IV bump (LD32_POST base-writeback at operand 1)
; is recognized by findIVBumpInLoop, the byte stride is read from operand 3
; ($scaled_imm, shifted left 2 for LD32 / 3 for LD64), and findTripCount Case 3
; emits SUB32 + SRLI32 in the preheader to compute the runtime trip count
; (end - start) >> log2(stride).
;
; Bug fixed (GAP-3): extractIVBump previously read the imm at operand 2
; which is the tied $rs register — so it always returned 0 ("Cannot determine
; IV step") and every pointer-IV streaming loop stayed on a BLTU back-edge.
; The imm is at operand index 3. Before the fix this test was XFAIL; the XFAIL
; has been removed now that the live -stop-after=haydn-hwloops output shows
; set_hwloop_f2.
;
; Test design: a streaming reduction whose only IV is the pointer %q stepping
; from %p to %end (4-byte stride, i32 elements). The integer trip count is
; (%end - %p) / 4, computed at runtime by the SET_HWLOOP_REG emission. If the
; recognizer regresses (operand-index bug returns), the set_hwloop_f2
; disappears and a BLTU back-edge appears instead.
;
; Reference: ~/haydn-plans/decisions/-hwloop-recognizer-broaden-g2-g3-g4.md
; ~/haydn-plans/decisions/-hwloop-pointer-iv-operand-index-fix.md

define i32 @ii_hwloop_pointer_iv(ptr readonly %p, ptr readnone %end) {
; CHECK-LABEL: ii_hwloop_pointer_iv:
; CHECK:       set_hwloop_f2
entry:
  br label %loop

loop:
  %q = phi ptr [ %p, %entry ], [ %q.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %v = load i32, ptr %q
  %sum.next = add i32 %sum, %v
  %q.next = getelementptr inbounds i32, ptr %q, i32 1
  %cmp = icmp ult ptr %q.next, %end
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
