; RUN: llc -mtriple=haydn-unknown-elf -O0 -global-isel -global-isel-abort=1 %s -o - | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — Empty side-effecting inline asm with "r" / "+r" register constraints must lower (not crash IRTranslator with "unable to translate instruction: call").

; Empty side-effecting inline asm with "r" / "+r" register constraints must
; lower (not crash IRTranslator with "unable to translate instruction: call").
; libc memset_explicit uses asm("" : : "r"(dst) : "memory") as a DSE barrier.

define ptr @barrier_r(ptr %dst) {
; CHECK-LABEL: barrier_r:
; CHECK: {{//|#}}APP
; CHECK: {{//|#}}NO_APP
  call void asm sideeffect "", "r,~{memory}"(ptr %dst)
  ret ptr %dst
}

define ptr @barrier_plus_r(ptr %dst) {
; CHECK-LABEL: barrier_plus_r:
; CHECK: {{//|#}}APP
; CHECK: {{//|#}}NO_APP
  %ret = call ptr asm sideeffect "", "=r,0,~{memory}"(ptr %dst)
  ret ptr %ret
}

define i32 @barrier_r_i32(i32 %x) {
; CHECK-LABEL: barrier_r_i32:
; CHECK: {{//|#}}APP
; CHECK: {{//|#}}NO_APP
  call void asm sideeffect "", "r,~{memory}"(i32 %x)
  ret i32 %x
}
