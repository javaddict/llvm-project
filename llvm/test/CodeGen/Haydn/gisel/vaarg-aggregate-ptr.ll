; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — aggregate va_arg is pointer-typed (Clang Indirect, ByVal=false).
; va-arg-22 walks many struct sizes as GPR/stack pointer slots. Contents
; never occupy those 8-byte slots (overlap / garbage pointers).

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; LEG-LABEL: name: vaarg_ptr
; LEG-NOT: G_VAARG
; LEG: G_LOAD
; LEG: G_SELECT
; ASM-LABEL: vaarg_ptr:
; ASM: ld32
define ptr @vaarg_ptr(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  %p = va_arg ptr %ap, ptr
  call void @llvm.va_end(ptr %ap)
  ret ptr %p
}

; Six leftover GPRs after %fixed, then 8-byte stack overflow.
; LEG-LABEL: name: vaarg_ptr_overflow
; LEG-NOT: G_VAARG
; LEG: G_SELECT
; ASM-LABEL: vaarg_ptr_overflow:
; ASM: ld32
define ptr @vaarg_ptr_overflow(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  %p0 = va_arg ptr %ap, ptr
  %p1 = va_arg ptr %ap, ptr
  %p2 = va_arg ptr %ap, ptr
  %p3 = va_arg ptr %ap, ptr
  %p4 = va_arg ptr %ap, ptr
  %p5 = va_arg ptr %ap, ptr
  %p6 = va_arg ptr %ap, ptr
  %p7 = va_arg ptr %ap, ptr
  call void @llvm.va_end(ptr %ap)
  ret ptr %p7
}

declare ptr @sink_va(i32, ...)

; ASM-LABEL: vaarg_ptr_caller:
; ASM: jal
define ptr @vaarg_ptr_caller(ptr %a0, ptr %a1, ptr %a2, ptr %a3, ptr %a4,
                             ptr %a5, ptr %a6, ptr %a7) {
  %r = call ptr (i32, ...) @sink_va(i32 8, ptr %a0, ptr %a1, ptr %a2, ptr %a3,
                                    ptr %a4, ptr %a5, ptr %a6, ptr %a7)
  ret ptr %r
}
