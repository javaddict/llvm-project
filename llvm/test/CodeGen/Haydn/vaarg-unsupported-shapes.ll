; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/va_i128.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=I128
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/va_v2i32.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=V2I32
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -filetype=null %t/va_v4i16.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=V4I16

; Role: verifier — G_VAARG product ABI is s32/s64/p0 only.
; Vectors and >64-bit scalars must fail closed at legalize (no silent
; VAARG_I32 map of every non-64-bit type). Values are intentionally unused
; so legalize reports G_VAARG (not a later G_STORE of the result).

;--- va_i128.ll
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
define void @va_i128(i32 %n, ...) {
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i128
  call void @llvm.va_end(ptr %ap)
  ret void
}
; I128: unable to legalize instruction: {{.*}}s128{{.*}}G_VAARG

;--- va_v2i32.ll
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
define void @va_v2i32(i32 %n, ...) {
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, <2 x i32>
  call void @llvm.va_end(ptr %ap)
  ret void
}
; V2I32: unable to legalize instruction: {{.*}}<2 x s32>{{.*}}G_VAARG

;--- va_v4i16.ll
declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
define void @va_v4i16(i32 %n, ...) {
  %ap = alloca i8, i32 48
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, <4 x i16>
  call void @llvm.va_end(ptr %ap)
  ret void
}
; V4I16: unable to legalize instruction: {{.*}}G_VAARG
