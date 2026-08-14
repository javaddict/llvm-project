; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — B.2 varargs overflow: * Five fixed i64 args exhaust D0–D3; the 5th and further varargs spill to.

; B.2 varargs overflow:
;   * Five fixed i64 args exhaust D0–D3; the 5th and further varargs spill to
;     the stack (CCAssignToStack<8,8>).
;   * Many fixed GPRs (R1–R7 + stack) with a trailing fixed i64 still leave
;     the DR bank available for i64 va_arg.
;
; Structured AArch64-style va_list (gr/vr banks) is exercised via va_start +
; va_arg; this file only checks that overflow paths compile and return.

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; Five fixed i64 (D0–D3 + stack) + one i64 vararg (stack overflow path).
define i64 @varargs_five_i64(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, ...) nounwind {
; CHECK-LABEL: varargs_five_i64:
; CHECK: add64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  %s1 = add i64 %a, %b
  %s2 = add i64 %s1, %c
  %s3 = add i64 %s2, %d
  %s4 = add i64 %s3, %e
  %r = add i64 %s4, %v
  ret i64 %r
}

; Eight fixed i32 (R1–R7 + one stack) + fixed i64 in D0 + i64 vararg.
define i64 @varargs_many_gpr_then_i64(i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5, i32 %a6, i32 %a7, i32 %a8, i64 %x, ...) nounwind {
; CHECK-LABEL: varargs_many_gpr_then_i64:
; CHECK: {{add64|sext32t64}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  %ext = sext i32 %a1 to i64
  %s = add i64 %ext, %x
  %r = add i64 %s, %v
  ret i64 %r
}

declare i64 @varargs_five_i64_callee(i64, i64, i64, i64, i64, ...)

; Caller: five fixed i64 + two overflow i64 varargs on the stack.
define i64 @call_five_i64_overflow() nounwind {
; CHECK-LABEL: call_five_i64_overflow:
; Stack-passed overflow args: st64 / d_sdw to SP slots before the call.
; CHECK: st64
; CHECK: jal{{(\.s[012])?}} {{.*}}, varargs_five_i64_callee
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %r = call i64 (i64, i64, i64, i64, i64, ...) @varargs_five_i64_callee(
      i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7)
  ret i64 %r
}
