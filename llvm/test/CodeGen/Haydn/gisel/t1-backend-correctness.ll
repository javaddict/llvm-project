; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
;
; Role: MIR — T1 GISel/CC seats: s64 add, G_SELECT, switch/JT, varargs
; VASTART, indirect call, register-only musttail. ISR fail-closed lives
; in tailcall-isr-fail-closed.ll.

declare i32 @ext(i32)
declare void @callee(i32)
@fp = external global ptr

; ISEL-LABEL: name: add64
; ISEL: ADD64
define i64 @add64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

; ISEL-LABEL: name: sel_s32
; ISEL: MOVT32
define i32 @sel_s32(i1 %c, i32 %t, i32 %f) {
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

; ISEL-LABEL: name: sel_s64
; ISEL: MOVT64
define i64 @sel_s64(i1 %c, i64 %t, i64 %f) {
  %r = select i1 %c, i64 %t, i64 %f
  ret i64 %r
}

; ISEL-LABEL: name: jt_dispatch
; ISEL: LOAD_ADDR
; ISEL: BR_JT
define i32 @jt_dispatch(i32 %x) {
  switch i32 %x, label %def [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
  ]
bb0:
  ret i32 10
bb1:
  ret i32 21
bb2:
  ret i32 32
bb3:
  ret i32 43
def:
  ret i32 99
}

; ISEL-LABEL: name: call_indirect
; ISEL: JALR_CALL
define i32 @call_indirect(ptr %f, i32 %x) {
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; ISEL-LABEL: name: musttail_sib
; ISEL: JAL_TCO
; ISEL-NOT: RET
define void @musttail_sib(i32 %x) nounwind {
  musttail call void @callee(i32 %x)
  ret void
}

; ISEL-LABEL: name: musttail_indirect
; ISEL: JALR_TCO
; ISEL-NOT: RET
define void @musttail_indirect(i32 %x) nounwind {
  %f = load ptr, ptr @fp
  musttail call void (i32) %f(i32 %x)
  ret void
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; ISEL-LABEL: name: va_i32
; Legalizer expands llvm.va_start / va_arg before ISel (two-bank list
; stores + G_LOAD). Pin that ISel still completes.
; ISEL: ST32
; ISEL: LD32
define i32 @va_i32(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}
