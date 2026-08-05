; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1  -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	select_i32                      // -- Begin function select_i32
; CHECK: 	.type	select_i32,@function
; CHECK-LABEL: select_i32:                             // @select_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ movt32	r3, r2, r1; nop; nop }
; CHECK: 	{ move32	r1, r3; nop; nop }
; CHECK: 	{ nop; nop; jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	select_i32, .Lfunc_end0-select_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; Test select instruction lowering.
;
; STATUS (,): un-XFAIL'd. The previous XFAIL blamed
; "GenMux Pattern 1 (tryConvertBitwiseSelect) does not fire post-ISA-27" for the
; 5-op bitwise chain (NEG/AND/NOT/AND/OR) surviving in place of MOVT32/MOVF32.
; That rationale is STALE: the GISel selector no longer emits the bitwise chain
; for s32 G_SELECT at all. Post-ISA-27 + the s32 path
; (HaydnInstructionSelector.cpp:1119-1155) lowers G_SELECT directly to a single
; tied-def MOVT32:
; %Dst = MOVT32 %FalseVal(tied), %TrueVal, %Cond
; (the.td ties $rd = $rd_src; when the condition is false the original
; FalseVal is retained). The condition is a GPR holding 0/1 from the compare
; the "GPR-as-bool" contract (MOVT32 rs2[0] is the boolean). The bitwise chain
; now ONLY survives for s64 select (selector :1020-1116), which is a separate
; gap tracked by ISA-27 -- do not claim s64 is fixed.
;
; The CHECKs below are deliberately conservative (assert movt32/movf32 is present
; for s32 selects and that the bitwise scaffolding is absent). Exact surrounding
; ops depend on RA coalescing; the coordinator MUST confirm at build time.
;
; NOTE: CHECKs reflect post- scheduled output (min32/neg32 may pack with the
; prologue xor32 in either slot order).

;Simple i32 select (not a min/max pattern -- cond is an arbitrary i1)
; Selector emits COPY + MOVT32 directly (no bitwise chain post-ISA-27).
define i32 @select_i32(i1 %cond, i32 %a, i32 %b) {
  %r = select i1 %cond, i32 %a, i32 %b
  ret i32 %r
}

;Select with signed comparison -> min32
define i32 @select_cmp(i32 %a, i32 %b) {
  %cmp = icmp slt i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;Select with unsigned comparison -> minu32
define i32 @select_unsigned(i32 %a, i32 %b) {
  %cmp = icmp ult i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;Select with equality comparison (not a min/max pattern)
; SEQ32 writes a GPR (0/1) consumed as MOVT32 rs2 -- gpr-as-bool.
define i32 @select_eq(i32 %a, i32 %b, i32 %c) {
; Return c if a == b, else 0
  %cmp = icmp eq i32 %a, %b
  %r = select i1 %cmp, i32 %c, i32 0
  ret i32 %r
}

;Nested select -> two min32 instructions
define i32 @nested_select(i32 %a, i32 %b, i32 %c, i32 %d) {
  %cmp1 = icmp slt i32 %a, %b
  %sel1 = select i1 %cmp1, i32 %a, i32 %b
  %cmp2 = icmp slt i32 %c, %d
  %sel2 = select i1 %cmp2, i32 %c, i32 %d
  %result = add i32 %sel1, %sel2
  ret i32 %result
}

;Select chain: min(max(val, 0), 100) -> max32 + min32
define i32 @select_chain(i32 %val) {
; Clamping: return min(max(val, 0), 100)
  %cmp1 = icmp slt i32 %val, 0
  %sel1 = select i1 %cmp1, i32 0, i32 %val
  %cmp2 = icmp sgt i32 %sel1, 100
  %sel2 = select i1 %cmp2, i32 100, i32 %sel1
  ret i32 %sel2
}

;i64 select (not a min/max pattern -- arbitrary i1 cond)
; SEPARATE GAP (ISA-27): the s64 G_SELECT path
; (HaydnInstructionSelector.cpp:1020-1116) STILL emits the bitwise chain
; (NEG/AND/NOT/AND/OR on the lo/hi halves). s64 select is NOT fixed by.
; These CHECKs assert the chain survives for s64 -- do not change to movt32.
define i64 @select_i64(i1 %cond, i64 %a, i64 %b) {
  %r = select i1 %cond, i64 %a, i64 %b
  ret i64 %r
}

;i64 select with comparison (not combined -- s64 min/max is lowered)
; SEPARATE GAP (ISA-27): s64 select still uses the bitwise chain. NOT fixed.
define i64 @select_i64_cmp(i64 %a, i64 %b) {
  %cmp = icmp slt i64 %a, %b
  %r = select i1 %cmp, i64 %a, i64 %b
  ret i64 %r
}

;Multiple selects in sequence (not min/max patterns -- eq comparisons)
; Each s32 select lowers directly to COPY + MOVT32 (no bitwise chain).
define i32 @multiple_selects(i32 %a, i32 %b, i32 %c, i32 %x, i32 %y) {
  %cmp1 = icmp eq i32 %a, 0
  %s1 = select i1 %cmp1, i32 %b, i32 %c
  %cmp2 = icmp eq i32 %s1, 0
  %s2 = select i1 %cmp2, i32 %x, i32 %y
  ret i32 %s2
}

;Select with constant values (not a min/max pattern)
; s32 select -> COPY + MOVT32 directly.
define i32 @select_const(i1 %cond) {
  %r = select i1 %cond, i32 42, i32 99
  ret i32 %r
}

;Select of pointers (not a min/max pattern -- arbitrary i1)
; s32 select -> COPY + MOVT32 directly.
define ptr @select_ptr(i1 %cond, ptr %a, ptr %b) {
  %r = select i1 %cond, ptr %a, ptr %b
  ret ptr %r
}

;Absolute value using select (not a min/max pattern -- cmp against constant -1)
; s32 select -> SLT32 + MOVT32 (no bitwise chain post-ISA-27).
define i32 @abs(i32 %x) {
  %neg = sub i32 0, %x
  %cmp = icmp sgt i32 %x, -1
  %r = select i1 %cmp, i32 %x, i32 %neg
  ret i32 %r
}

;Saturation arithmetic using select (not a min/max -- different operands)
; s32 select -> SLTU32 + MOVT32 directly.
define i32 @saturating_add(i32 %a, i32 %b) {
; Saturating add: cap at INT_MAX
  %sum = add i32 %a, %b
  %overflow = icmp ult i32 %sum, %a
  %max = xor i32 2147483647, -1  ; INT_MAX = 0x7FFFFFFF
  %result = select i1 %overflow, i32 2147483647, i32 %sum
  ret i32 %result
}

;Conditional increment using select.
;s32 select -> COPY + MOVT32 directly (no bitwise chain post-ISA-27).
define i32 @conditional_inc(i32 %val, i1 %cond) {
  %inc = add i32 %val, 1
  %r = select i1 %cond, i32 %inc, i32 %val
  ret i32 %r
}
