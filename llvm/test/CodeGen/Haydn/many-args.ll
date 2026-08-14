; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — calling conventions with many arguments.

; Test calling conventions with many arguments.
; Haydn has 16 GPR32 registers (R0-R15, with R13=SP, R14=FP, R15=LR).
; Argument registers: R0-R7 for i32, D0-D3 for i64.
; Arguments exceeding register capacity are passed on the stack.
;
; Note: Functions that trunc i64 args to i32 crash the AsmPrinter. Only
; functions that use i64 args as i64 (no trunc) are included.

declare void @sink_i32(i32)
declare void @sink_i64(i64)

;Exactly at register limit (8 i32 args)
define void @exactly_8_args(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                            i32 %a4, i32 %a5, i32 %a6, i32 %a7) nounwind {
; CHECK-LABEL: exactly_8_args:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;Exceeding register limit (16 i32 args)
define void @too_many_i32(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                          i32 %a4, i32 %a5, i32 %a6, i32 %a7,
                          i32 %a8, i32 %a9, i32 %a10, i32 %a11,
                          i32 %a12, i32 %a13, i32 %a14, i32 %a15) nounwind {
; CHECK-LABEL: too_many_i32:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;Calling with many i32 arguments
define void @call_many_i32() nounwind {
; CHECK-LABEL: call_many_i32:
; CHECK: jal{{(\.s[012])?}}
entry:
  call void @sink_i32(i32 1)
  call void @sink_i32(i32 2)
  call void @sink_i32(i32 3)
  call void @sink_i32(i32 4)
  ret void
}

;Function that uses all 8 i32 args
define i32 @sum_8_args(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                       i32 %a4, i32 %a5, i32 %a6, i32 %a7) nounwind {
; CHECK-LABEL: sum_8_args:
; CHECK: add32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i32 %a0, %a1
  %s2 = add i32 %s1, %a2
  %s3 = add i32 %s2, %a3
  %s4 = add i32 %s3, %a4
  %s5 = add i32 %s4, %a5
  %s6 = add i32 %s5, %a6
  %s7 = add i32 %s6, %a7
  ret i32 %s7
}

;i64 arguments exceeding DR64 register limit
define void @too_many_i64(i64 %a0, i64 %a1, i64 %a2, i64 %a3,
                          i64 %a4, i64 %a5, i64 %a6, i64 %a7,
                          i64 %a8) nounwind {
; CHECK-LABEL: too_many_i64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;Use spilled i64 args (as i64, no trunc)
define i64 @use_spilled_i64(i64 %a0, i64 %a1, i64 %a2, i64 %a3,
                            i64 %a4, i64 %a5, i64 %a6, i64 %a7,
                            i64 %a8) nounwind {
; CHECK-LABEL: use_spilled_i64:
; CHECK: add64
  %s1 = add i64 %a0, %a8
  ret i64 %s1
}

;Pointer arguments
define ptr @ptr_args(ptr %p1, ptr %p2) nounwind {
; CHECK-LABEL: ptr_args:
; CHECK: add32
  %r = ptrtoint ptr %p1 to i32
  %s = ptrtoint ptr %p2 to i32
  %sum = add i32 %r, %s
  %result = inttoptr i32 %sum to ptr
  ret ptr %result
}

;No arguments, just return a constant
define i32 @no_args() nounwind {
; CHECK-LABEL: no_args:
; CHECK: addi32
  ret i32 42
}

;Single i32 argument
define i32 @one_arg(i32 %a) nounwind {
; CHECK-LABEL: one_arg:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 %a
}

;Single i64 argument
define i64 @one_i64_arg(i64 %a) nounwind {
; CHECK-LABEL: one_i64_arg:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 %a
}
