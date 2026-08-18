; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -mattr=+hwloop < %s | FileCheck %s

; Role: semantic — single-BB Role-A ZOL expand must arm the GOLDEN inner
; selector 0 (GOALS W46 / pass-design audit PA2-G1).

; REGRESSION TEST: the hwlr_sel convention was INVERTED vs the golden
; Reference Manual.
;
; Bug: HaydnHWLoopContracts.h defined
;   InnermostProductSelector = 1, OuterProductSelector = 0
; but the golden Reference Manual states (§ hardware loop registers, "The
; VLIW engine provides two independent hardware loop registers..."):
;   "For nested loops, HWLR_*[0] is used for the inner loop, and HWLR_*[1]
;    for the outer loop."
; i.e. the INNER seat is 0. Role-A single-BB expand armed sel=1 — writing
; HWLR_BEGIN/END/COUNT[1], the golden OUTER seat. Harmless while nesting is
; unimplemented and only one loop ever arms one selector (any in-domain sel
; programs the same loop), but the day nesting lands, the inversion becomes
; wrong-loop-writes-wrong-CSR: an expanded innermost loop would clobber the
; outer loop's HWLR state (HWLR_END[1]/HWLR_COUNT[1]) mid-nest.
;
; Fix: InnermostProductSelector = 0, OuterProductSelector = 1 (one-line
; constant swap + golden static_assert in HaydnHWLoopContracts.h). All
; consumers reference the constant by name, so the swap is total.
;
; Test design: one single-BB count-up loop (the Role-A shape). The emitted
; set_hwloop_f2 selector operand must be 0 (golden inner seat). Before the
; fix this printed "set_hwloop_f2 1, ...". The adjacent CHECK-NOT pins the
; old wrong seat so a future re-inversion cannot silently pass.
;
; Companion unit pin: HaydnHWLoopContractsTest.SelectorConventionMatches-
; GoldenManual freezes the constant VALUES (0/1), not just domain membership.

define i32 @innermost_arms_golden_inner_sel(ptr %p) {
; CHECK-LABEL: innermost_arms_golden_inner_sel:
; CHECK: set_hwloop_f2 0, .LLhwloop_start{{[0-9]+}}, .LLhwloop_end{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: set_hwloop_f2 1,
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
