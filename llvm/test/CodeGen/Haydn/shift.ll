; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Coverage for shift.

define i32 @shl(i32 %a, i32 %b) {
; CHECK-LABEL: shl:
; CHECK: sll32
  %result = shl i32 %a, %b
  ret i32 %result
}

define i32 @lshr(i32 %a, i32 %b) {
; CHECK-LABEL: lshr:
; CHECK: srl32
  %result = lshr i32 %a, %b
  ret i32 %result
}

define i32 @ashr(i32 %a, i32 %b) {
; CHECK-LABEL: ashr:
; CHECK: sra32
  %result = ashr i32 %a, %b
  ret i32 %result
}
