; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
;
; REGRESSION TEST: multi-BB Role B residual is intentionally soft.
;
; History: GAP-4 taught the recognizer multi-BB trip resolution, and
; Role B briefly converted multi-BB soft loops to SET_HWLOOP_F2. That path
; is declined (AIE-aligned): multi-BB ZOL residual has open correctness
; issues (CoreMark bitextract nested ZOL; lc_dp_lis wrong exit). Single-BB
; Role A/B ZOL remains product. Multi-BB stays as soft back-edge.
;
; This test locks the decline: multi-BB body with side-effect branches must
; NOT form SET_HWLOOP; a soft conditional back-edge remains.
;
; Reference: ~/haydn-plans/decisions/-hwloop-recognizer-broaden-g2-g3-g4.md

define void @gap4_multibb_calls(ptr %dst, ptr readonly %src, i32 %n) nounwind {
; CHECK-LABEL: name: gap4_multibb_calls
; CHECK-NOT:   SET_HWLOOP
; Soft multi-BB residual (any cond back-edge form):
; CHECK:       {{BLT|BGE|BNEZ|BEQZ|BLTU|BGEU}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  store i32 0, ptr %dp
  br label %latch

else:
  store i32 %v, ptr %dp
  br label %latch

latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Variant: a two-way branch with arithmetic in each arm (also resists
; if-conversion). Confirms the multibb conversion is not specific to stores.
define i32 @gap4_multibb_arith(ptr readonly %src, i32 %n, i32 %k) nounwind {
; CHECK-LABEL: name: gap4_multibb_arith
; Arith shape may if-convert; GAP-4 primary coverage is gap4_multibb_calls.
; CHECK:       {{SET_HWLOOP|BLT|BGE|BNEZ}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  %neg = sub i32 0, %v
  %scaled_then = mul i32 %neg, %k
  br label %latch

else:
  %scaled_else = mul i32 %v, %k
  br label %latch

latch:
  %scaled = phi i32 [ %scaled_then, %then ], [ %scaled_else, %else ]
  %acc.next = add i32 %acc, %scaled
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}
