; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — triggers a "MBB has unexpected successors" verifier error in the Control Flow Optimizer pass (the branch on i1 produces an empty entry block with.

; NOTE: Does not use -verify-machineinstrs because the i1_branch function
; triggers a "MBB has unexpected successors" verifier error in the Control
; Flow Optimizer pass (the branch on i1 produces an empty entry block with
; two successors but no terminators after optimization).
;
; Test i1 (boolean) operations.
; i1 values are stored in GPR32 registers with bits [31:1] undefined.
; Only bit 0 carries the boolean value.
;
; i1 AND, OR, XOR use the same 32-bit instructions (and32, or32, xor32)
; since only bit 0 matters and the upper bits are don't-care.
; i1 comparisons use seq32 after masking to bit 0.

;i1 AND

define i1 @i1_and(i1 %a, i1 %b) nounwind {
; CHECK-LABEL: i1_and:
; CHECK: {{and32|andi32}}
  %r = and i1 %a, %b
  ret i1 %r
}

;i1 OR
define i1 @i1_or(i1 %a, i1 %b) nounwind {
; CHECK-LABEL: i1_or:
; CHECK: {{or32|ori32}}
  %r = or i1 %a, %b
  ret i1 %r
}

;i1 XOR
define i1 @i1_xor(i1 %a, i1 %b) nounwind {
; CHECK-LABEL: i1_xor:
; CHECK: {{xor32|xori32}}
  %r = xor i1 %a, %b
  ret i1 %r
}

;i1 NOT (XOR with true)
define i1 @i1_not(i1 %a) nounwind {
; CHECK-LABEL: i1_not:
; CHECK: {{xor32|xori32}}
  %r = xor i1 %a, true
  ret i1 %r
}

;i1 equality comparison
define i1 @i1_cmp_eq(i1 %a, i1 %b) nounwind {
; CHECK-LABEL: i1_cmp_eq:
; CHECK: {{and32|andi32}}
; CHECK: seq32
  %r = icmp eq i1 %a, %b
  ret i1 %r
}

;i1 not-equal comparison
define i1 @i1_cmp_ne(i1 %a, i1 %b) nounwind {
; CHECK-LABEL: i1_cmp_ne:
; CHECK: {{and32|andi32}}
; CHECK: seq32
; CHECK: {{xor32|xori32}}
  %r = icmp ne i1 %a, %b
  ret i1 %r
}

;i1 zero-extend to i32
define i32 @i1_zext(i1 %a) nounwind {
; CHECK-LABEL: i1_zext:
; CHECK: {{and32|andi32}}
  %r = zext i1 %a to i32
  ret i32 %r
}

;i1 sign-extend to i32
define i32 @i1_sext(i1 %a) nounwind {
; CHECK-LABEL: i1_sext:
; CHECK: {{sll32|slli32}}
; CHECK: {{sra32|srai32}}
  %r = sext i1 %a to i32
  ret i32 %r
}

;Truncate i32 to i1
define i1 @trunc_i32_to_i1(i32 %a) nounwind {
; CHECK-LABEL: trunc_i32_to_i1:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = trunc i32 %a to i1
  ret i1 %r
}

;i1 used in branch
define i32 @i1_branch(i1 %cond, i32 %a, i32 %b) nounwind {
; CHECK-LABEL: i1_branch:
; CHECK: b{{eq|ne}}z{{(\.s[012])?}}
entry:
  br i1 %cond, label %t, label %f
t:
  ret i32 %a
f:
  ret i32 %b
}

;i1 select
; Lowered as a single MOVT32 (prior revision "Lower s32 G_SELECT to a
; single tied-def MOVT32"). Old neg32+and32+or32 bitwise-select pattern no
; longer applies for s32 G_SELECT.
define i32 @i1_select(i1 %cond, i32 %a, i32 %b) nounwind {
; CHECK-LABEL: i1_select:
; CHECK: movt32
  %r = select i1 %cond, i32 %a, i32 %b
  ret i32 %r
}

;i1 zero-extend to i64
define i64 @i1_zext_i64(i1 %a) nounwind {
; CHECK-LABEL: i1_zext_i64:
  %r = zext i1 %a to i64
  ret i64 %r
}

;Chain of i1 operations
define i1 @i1_chain(i1 %a, i1 %b, i1 %c) nounwind {
; CHECK-LABEL: i1_chain:
; (a & b) | c
; CHECK: {{and32|andi32}}
; CHECK: {{or32|ori32}}
  %ab = and i1 %a, %b
  %r = or i1 %ab, %c
  ret i1 %r
}
