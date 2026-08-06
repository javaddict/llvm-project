; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s

; Role: MIR — pre-RA HardwareLoops converts pointer-IV streaming loops; pin SET_HWLOOP_REG/HWLOOP_END without hardcoding vregs.

; This test exercises the pre-RA HardwareLoops pass (Stream A,), which
; runs BEFORE SMS and converts countable loops to SET_HWLOOP_REG + HWLOOP_END
; pseudos on virtual registers (do not pin vreg numbers in CHECKs). The pre-RA
; pass derives the trip count directly from the IV init (logic) and hands
; the runtime loop bound straight to SET_HWLOOP_REG, so unlike the post-RA pass
; it does NOT emit a SUB32/SRLI32 trip-compute sequence in the preheader.
;
; REGRESSION TEST: GAP-3 pointer-IV hardware-loop conversion.
;
; Bug (before): Haydn's HWLoop recognizer identified the pointer-IV bump
; (LD32_POST $r1, 1 — the post-increment load with tied-def base-writeback on
; $r1) as the IV, but extractIVBump returned 0 ("Cannot determine IV step")
; because it read the scaled-stride immediate from operand index 2. The real
; operand layout of LD32_POST / LD64_POST (verified via
; HaydnExpandPostIncEarly.cpp:124-128 which builds the 4-operand form, and via
; HaydnInstrInfo.td LD32_POST / LD64_POST `(outs $rt, $rs_wb) (ins $rs
; $scaled_imm)`) is:
; operand 0 = $rt (data destination, def)
; operand 1 = $rs_wb (base writeback, def — tied to $rs)
; operand 2 = $rs (input base, tied use)
; operand 3 = $scaled_imm (imm6 element index)
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
; Test design: a streaming reduction whose only IV is the pointer %q stepping
; from %p to %end via LD32_POST (4-byte stride, i32 elements). If the
; recognizer regresses (the operand-index bug returns), the SET_HWLOOP_REG
; disappears, no SUB32/SRLI32 trip-compute appears in the preheader, and a
; BLTU back-edge appears instead.
;
; Reference: ~/haydn-plans/decisions/-hwloop-pointer-iv-operand-index-fix.md
; ~/haydn-plans/decisions/-hwloop-recognizer-broaden-g2-g3-g4.md

define i32 @pointer_iv_i32(ptr readonly %p, ptr readnone %end) nounwind {
; Product form: pointer IV fuses to S_LW_POST_IMM. Contract: HWLoop
; conversion still fires (SET_HWLOOP + PseudoLoopEnd).
; CHECK-LABEL: name: pointer_iv_i32
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

; Variant: i64 elements (8-byte stride). Dual-sched form splits the i64 load
; to LD32 pairs + ADDI32 stride-8; HWLoop still converts the pointer-IV loop.
define i64 @pointer_iv_i64(ptr readonly %p, ptr readnone %end) nounwind {
; CHECK-LABEL: name: pointer_iv_i64
; CHECK: SET_HWLOOP
; Accept split LD32 pairs or fused D_LDW_POST_IMM.
; CHECK-DAG: {{LD32|D_LDW_POST_IMM|LD64}}
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
