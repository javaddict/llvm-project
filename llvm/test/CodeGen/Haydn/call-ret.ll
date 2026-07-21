; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

define i32 @callee(i32 %a) {
; CHECK-LABEL: callee:
  ret i32 %a
}

define i32 @caller(i32 %x) {
; CHECK-LABEL: caller:
; CHECK: jal_w{{(\.s[012])?}}
  %result = call i32 @callee(i32 %x)
  ret i32 %result
}
