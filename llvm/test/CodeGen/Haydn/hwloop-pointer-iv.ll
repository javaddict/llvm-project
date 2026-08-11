; This test exercises the pre-RA HardwareLoops pass (Stream A,), which
; runs BEFORE SMS and converts countable loops to SET_HWLOOP_F2 + HWLOOP_END
; pseudos.
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s \
; RUN:   | FileCheck %s --implicit-check-not=BLTU
;
; REGRESSION TEST: GAP-3 pointer-IV hardware-loop conversion.
;
; Bug (before): Haydn's HWLoop recognizer identified the pointer-IV bump
; (the post-increment load with tied-def base-writeback on $r1) as the IV, but
; extractIVBump returned 0 ("Cannot determine IV step") because it read the
; scaled-stride immediate from operand index 2. The real operand layout of the
; post-increment loads (verified via HaydnExpandPostIncEarly.cpp:124-128 which
; builds the 4-operand form, and via the `(outs $rt, $rs_wb) (ins $rs
; $scaled_imm)` declaration) is:
;   operand 0 = $rt (data destination, def)
;   operand 1 = $rs_wb (base writeback, def — tied to $rs)
;   operand 2 = $rs (input base, tied use)
;   operand 3 = $scaled_imm (imm6 element index)
; Operand 2 is therefore the tied $rs REGISTER, not the immediate. The imm is
; at operand index 3. The bug: `!BumpMI->getOperand(2).isImm` always
; evaluated true (operand 2 is a reg), so extractIVBump short-circuited to 0
; and every pointer-IV streaming loop stayed on a BLTU/BLT back-edge.
;
; Fix : extractIVBump reads the stride from operand 3, with an
; explicit getNumOperands >= 4 guard. findIVBumpInLoop also gained the
; operand-count guard on its operand(1) check for robustness. findTripCount
; Case 3 then handles the runtime-start / runtime-limit / power-of-two-stride
; form by emitting SUB32 LimitReg, LimitReg, IVReg; SRLI32 LimitReg, LimitReg
; shift in the preheader (computing trip = (end - start) >> log2(stride)).
;
; WHAT THE CHECKS PIN, and why it is the shift amount. An earlier revision
; asserted only the load's spelling, through a CHECK-DAG line whose pattern
; nested one regex block inside another — S_LW_ followed by a bracketed class,
; alternated with two D_LDW_ spellings, all wrapped in a further pair of
; braces. FileCheck has no nested regex blocks: it closes at the FIRST closing
; pair, so that line was one broken regex followed by literal text and could
; never match anything. It was failing, but the more useful thing to notice is
; that it would have tested nothing had it passed — the load's spelling is not
; what the bug was about.
;
; (Written out in prose deliberately. A directive prefix inside a comment is
; still a directive — quoting the broken line verbatim here made FileCheck
; adopt it, which is the same class of mistake one layer up.)
;
; The stride IS. `SRLI32 ..., log2(stride)` in the preheader is the direct
; reading of extractIVBump's result: 2 for i32 elements, 3 for i64. An
; operand-index regression cannot produce those and still convert the loop.
; The BLTU the bug left behind is excluded file-wide via --implicit-check-not
; rather than a placed CHECK-NOT, so it cannot be scoped away by accident.
;
; Reference: ~/haydn-plans/decisions/-hwloop-pointer-iv-operand-index-fix.md
; ~/haydn-plans/decisions/-hwloop-recognizer-broaden-g2-g3-g4.md

define i32 @pointer_iv_i32(ptr readonly %p, ptr readnone %end) nounwind {
; Product form: pointer IV fuses to S_LW_POST_IMM. Contract: HWLoop
; conversion still fires (SET_HWLOOP + PseudoLoopEnd).
; CHECK-LABEL: name: pointer_iv_i32
; trip = (end - start) >> 2 — i32 elements, stride 4.
; CHECK: SUB32
; CHECK: SRLI32 {{.*}}, 2
; CHECK: SET_HWLOOP
; CHECK: S_LW_POST_IMM
; CHECK: PseudoLoopEnd
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

; Variant: i64 elements (8-byte stride). The dual-sched form splits the i64
; load into a pair of 32-bit loads; HWLoop still converts the pointer-IV loop,
; and the trip-count shift follows the ELEMENT stride, not the load width.
define i64 @pointer_iv_i64(ptr readonly %p, ptr readnone %end) nounwind {
; CHECK-LABEL: name: pointer_iv_i64
; trip = (end - start) >> 3 — i64 elements, stride 8. This is the value the
; operand-index bug got wrong, so it is the one worth pinning.
; CHECK: SUB32
; CHECK: SRLI32 {{.*}}, 3
; CHECK: SET_HWLOOP
; Accept the split 32-bit pair or a fused 64-bit post-increment load.
; CHECK-DAG: {{S_LW_WITH_IMM|S_LW_POST_IMM|D_LDW_POST_IMM|D_LDW_WITH_IMM}}
; CHECK: PseudoLoopEnd
entry:
  br label %loop

loop:
  %q = phi ptr [ %p, %entry ], [ %q.next, %loop ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %loop ]
  %v = load i64, ptr %q
  %sum.next = add i64 %sum, %v
  %q.next = getelementptr inbounds i64, ptr %q, i32 1
  %cmp = icmp ult ptr %q.next, %end
  br i1 %cmp, label %loop, label %exit

exit:
  ret i64 %sum.next
}
