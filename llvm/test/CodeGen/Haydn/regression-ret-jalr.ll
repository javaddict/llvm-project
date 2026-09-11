; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — RET pseudo must expand to JALR R0, LR, 0 (immediate offset).

; REGRESSION TEST: RET pseudo must expand to JALR R0, LR, 0 (immediate offset)
;
; Bug: RET was expanding to JALR R0, R15, R15 (register operand for offset)
; which is incorrect -- JALR offset should be an immediate, not a register.
; The LR register alias should be used for the link register (R15).
;
; This test verifies that every function return emits "jalr_w r0, lr, 0"
; with an immediate zero offset. If the RET expansion regresses to using
; a register operand for the offset, the assembler or verifier will error.
;
; Do NOT update CHECK lines without understanding the root cause.

;Simple return with constant

define i32 @simple_ret() nounwind {
; CHECK-LABEL: simple_ret:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 42
}

;Void return
define void @void_ret() nounwind {
; CHECK-LABEL: void_ret:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;Return after arithmetic
define i32 @ret_after_arith(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: ret_after_arith:
; CHECK: add32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i32 %a, %b
  ret i32 %r
}

;Return after call
declare i32 @helper_fn(i32)
define i32 @ret_after_call(i32 %a) nounwind {
; CHECK-LABEL: ret_after_call:
; CHECK: lui{{.*}}helper_fn
; CHECK: addi32{{.*}}helper_fn
; CHECK: jalr{{.*}}lr
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i32 @helper_fn(i32 %a)
  ret i32 %r
}

;Return with conditional branch (multiple return points)
define i32 @multi_ret(i32 %x) nounwind {
; CHECK-LABEL: multi_ret:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %pos, label %neg
pos:
  ret i32 %x
neg:
  %negx = sub i32 0, %x
  ret i32 %negx
}

;Return i64
define i64 @ret_i64(i64 %a) nounwind {
; CHECK-LABEL: ret_i64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 %a
}
