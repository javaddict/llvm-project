; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s
;
; REGRESSION TEST: i64 icmp ne loop-exit must use LOGICAL not, not bitwise not.
;
; Bug: HaydnInstructionSelector lowered i64 ICMP_NE as
; SEQ32 hi; SEQ32 lo; AND32; NOT32 (NOT32 = bitwise ~)
; producing `~(hi_eq && lo_eq)` = 0xFFFFFFFF (when ==) or 0xFFFFFFFE (when !=).
; Both are nonzero, so a downstream `bnez_w` ALWAYS branches to the loop body.
; A `while (x != 0)` loop therefore iterates forever once x reaches 0
; (observed: cycle-limit / NOEXIT on the simulator at -O0). The same defect
; affected ICMP_UGE/SGE (NOT(LT)) and ICMP_ULE/SLE (NOT(GT)).
;
; Root cause: Haydn's NOT32 instruction is bitwise one's-complement
; (`rt = ~rs`, per ISA reference). Logical not of a 0/1 compare predicate
; must be `rs ^ 1` (i.e. XORI32 rd, rs, 1).
;
; Fix: the i64 icmp NE/GE/LE cases in HaydnInstructionSelector.cpp now emit
; XORI32 rd, rs, 1 in place of NOT32 rd, rs. If this regresses, the loop
; below will fail to terminate again: re-introducing NOT32 makes
; `while(popcount != 0)` always re-enter the body.
;
; Test design: a 64-bit counter shifted right by 1 each iteration, looping
; while nonzero. At -O0 the i64 ICMP_NE feeds a BNEZ directly. The exit
; predicate is materialized by SEQ32/SEQ32/AND32 then the negation, so we
; check that the negation is `xori32 _, _, 1` and NOT `not32`.

define i32 @while_i64_ne_loop(i64 %x) {
; CHECK-LABEL: while_i64_ne_loop:
entry:
  br label %while.cond

while.cond:
  %cmp = icmp ne i64 %x, 0
  ; CHECK-NOT: not32
  ; CHECK-DAG: seq32
  ; CHECK-DAG: seq32
  ; CHECK-DAG: and32
  ; CHECK-DAG: xori32{{.*}} {{[a-z0-9]+}}, {{[a-z0-9]+}}, 1
  br i1 %cmp, label %while.body, label %while.end

while.body:
  %shr = lshr i64 %x, 1
  br label %while.cond

while.end:
  ret i32 0
}

; Same defect for ICMP_SGE (NOT(LT)) on i64. x >= 0 is always true for the
; non-negative input exercised below; the loop body must be entered. With the
; buggy NOT32 the loop body would be entered for BOTH polarities and the loop
; would never exit on the boundary (LT == 0 case).

define i32 @while_i64_sge_loop(i64 %x) {
; CHECK-LABEL: while_i64_sge_loop:
entry:
  br label %while.cond

while.cond:
  %cmp = icmp sge i64 %x, 0
  ; Order of seq/slt/sltu free under dual-sched pre-RA; require logical not.
  ; CHECK-NOT: not32
  ; CHECK-DAG: seq32
  ; CHECK-DAG: sltu32
  ; CHECK-DAG: or32
  ; CHECK-DAG: xori32{{.*}} {{[a-z0-9]+}}, {{[a-z0-9]+}}, 1
  br i1 %cmp, label %while.body, label %while.end

while.body:
  %shr = lshr i64 %x, 1
  br label %while.cond

while.end:
  ret i32 0
}

; And ICMP_SLE (NOT(GT)) on i64 — third site of the same bug.

define i32 @while_i64_sle_loop(i64 %x) {
; CHECK-LABEL: while_i64_sle_loop:
entry:
  br label %while.cond

while.cond:
  %cmp = icmp sle i64 %x, 0
  ; Order of seq/slt/sltu free under dual-sched pre-RA; require logical not.
  ; CHECK-NOT: not32
  ; CHECK-DAG: seq32
  ; CHECK-DAG: sltu32
  ; CHECK-DAG: or32
  ; CHECK-DAG: xori32{{.*}} {{[a-z0-9]+}}, {{[a-z0-9]+}}, 1
  br i1 %cmp, label %while.body, label %while.end

while.body:
  %shr = shl i64 %x, 1
  br label %while.cond

while.end:
  ret i32 0
}
