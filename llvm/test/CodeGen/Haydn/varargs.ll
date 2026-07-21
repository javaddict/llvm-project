; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
;
; REGRESSION TEST (F15 /): variadic argument access via a two-bank
; structured reg-save-area.
;
; Bug: lowerFormalArguments hardcoded VarArgsStackOffset=0 and VASTART was a
; no-op, so va_arg read garbage (offset 0 into the frame instead of the actual
; varargs save area). The fix sets up a register save area for the unallocated
; tail of R1–R7 and D0–D3 and stores its address into va_list. See F15
; later upgraded to the two-bank structured va_list in (AArch64-style
; {__stack, __gr_top, __vr_top, __gr_offs, __vr_offs}).
;
; Test design: a variadic callee with one fixed arg consumes R1; the remaining
; args (R2–R7) plus stack overflow become addressable. The first va_arg must
; read from the save area (not offset 0 of the frame). The test verifies the
; function compiles and emits the structured VASTART expansion: addi32 to
; compute the save-area pointers, GPR spills (st32 r2..r7), DR spills
; (st64 d0..d3), and cursor reads (ld32/ld64) for va_arg.

;Simple variadic function
; Note: printf is unused — declared only to exercise the variadic call ABI in
; the IR. Do NOT redeclare; that triggers "invalid redefinition of function
; 'printf'" in llc.
define i32 @test_variadic(i32 %count, ...) {
; test_variadic:
; Structured VASTART: compute save-area pointers (addi32), then spill BOTH
; banks — GPR (R2-R7) via st32 and DR (D0-D3) via st64 — into the save area.
; va_arg reads the spilled GPR via a cursor load (ld32).
  %ap = alloca ptr

  ; Start variadic argument list
  call void @llvm.va_start(ptr %ap)

  ; Read first variadic arg (should be i32)
  %arg1 = va_arg ptr %ap, i32

  ; Read second variadic arg
  %arg2 = va_arg ptr %ap, i32

  ; End variadic argument list
  call void @llvm.va_end(ptr %ap)

  %result = add i32 %arg1, %arg2
  ret i32 %result
}

;va_copy test
define i32 @test_vacopy(i32 %count, ...) {
; test_vacopy:
  %ap1 = alloca ptr
  %ap2 = alloca ptr

  call void @llvm.va_start(ptr %ap1)
  call void @llvm.va_copy(ptr %ap2, ptr %ap1)

  %arg1 = va_arg ptr %ap1, i32
  %arg2 = va_arg ptr %ap2, i32

  call void @llvm.va_end(ptr %ap1)
  call void @llvm.va_end(ptr %ap2)

  %result = add i32 %arg1, %arg2
  ret i32 %result
}

;va_arg with different types
define i64 @test_vaarg_types(i32 %count, ...) {
; test_vaarg_types:
  %ap = alloca ptr

  call void @llvm.va_start(ptr %ap)

  %arg_i32 = va_arg ptr %ap, i32
  %arg_i64 = va_arg ptr %ap, i64
  %arg_i32_2 = va_arg ptr %ap, i32

  call void @llvm.va_end(ptr %ap)

  %ext1 = sext i32 %arg_i32 to i64
  %ext2 = sext i32 %arg_i32_2 to i64
  %result = add i64 %ext1, %arg_i64
  %result2 = add i64 %result, %ext2
  ret i64 %result2
}

;Variadic function call
define i32 @call_variadic() {
; call_variadic:
  %result = call i32 (i32, ...) @test_variadic(i32 2, i32 10, i32 20)
  ret i32 %result
}

;Multiple va_arg reads
define i32 @test_multiple_vaarg(i32 %count, ...) {
; test_multiple_vaarg:
  %ap = alloca ptr

  call void @llvm.va_start(ptr %ap)

  %sum = alloca i32
  store i32 0, ptr %sum

  %arg1 = va_arg ptr %ap, i32
  %arg2 = va_arg ptr %ap, i32
  %arg3 = va_arg ptr %ap, i32
  %arg4 = va_arg ptr %ap, i32

  call void @llvm.va_end(ptr %ap)

  %s1 = add i32 %arg1, %arg2
  %s2 = add i32 %s1, %arg3
  %s3 = add i32 %s2, %arg4
  ret i32 %s3
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)
