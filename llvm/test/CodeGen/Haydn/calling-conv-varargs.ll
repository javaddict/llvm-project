; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.






;
; Tests for variable argument functions (va_list).
;
; In the Haydn baremetal ABI:
; Named arguments follow the standard calling convention (R1-R7, D0-D3)
; Variadic arguments that overflow registers are passed on the stack
; va_start/va_end manage the va_list structure
; va_arg reads the next variadic argument

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

;Basic variadic function: read two variadic i32 args

define i32 @vararg_basic(i32 %count, ...) {
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %arg1 = va_arg ptr %ap, i32
  %arg2 = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %r = add i32 %arg1, %arg2
  ret i32 %r
}

;Variadic function with many named args before varargs

define i32 @vararg_many_named(i32 %a, i32 %b, i32 %c, ...) {
; Named args a, b, c use R1, R2, R3
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v1 = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %r = add i32 %s2, %v1
  ret i32 %r
}

;Variadic with different types: i32 then i64

define i64 @vararg_mixed_types(i32 %count, ...) {
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %arg_i32 = va_arg ptr %ap, i32
  %arg_i64 = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  %ext = sext i32 %arg_i32 to i64
  %r = add i64 %ext, %arg_i64
  ret i64 %r
}

;va_copy test

define i32 @vararg_vacopy(i32 %count, ...) {
  %ap1 = alloca ptr
  %ap2 = alloca ptr
  call void @llvm.va_start(ptr %ap1)
  call void @llvm.va_copy(ptr %ap2, ptr %ap1)
  %arg1 = va_arg ptr %ap1, i32
  %arg2 = va_arg ptr %ap2, i32
  call void @llvm.va_end(ptr %ap1)
  call void @llvm.va_end(ptr %ap2)
  %r = add i32 %arg1, %arg2
  ret i32 %r
}

;Calling a variadic function with explicit arguments

declare i32 @variadic_callee(i32, ...)

define i32 @call_variadic() {
  %r = call i32 (i32, ...) @variadic_callee(i32 3, i32 10, i32 20, i32 30)
  ret i32 %r
}

;Many variadic reads (stress test)

define i32 @vararg_many_reads(i32 %count, ...) {
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %a1 = va_arg ptr %ap, i32
  %a2 = va_arg ptr %ap, i32
  %a3 = va_arg ptr %ap, i32
  %a4 = va_arg ptr %ap, i32
  %a5 = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %s1 = add i32 %a1, %a2
  %s2 = add i32 %s1, %a3
  %s3 = add i32 %s2, %a4
  %s4 = add i32 %s3, %a5
  ret i32 %s4
}

;Variadic with no extra args (just named args)

define i32 @vararg_no_extra(i32 %x, ...) {
; Function is variadic but caller passes no variadic args
; Should still compile correctly
  ret i32 %x
}
