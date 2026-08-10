; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %s 2>&1 | FileCheck %s
;
; Role: semantic — musttail is unsupported; fail closed before CALLSEQ/call
; mutation (no silent ordinary-call lowering).

declare void @callee(i32)
define void @caller(i32 %x) {
  ; CHECK: unable to translate instruction: call
  musttail call void @callee(i32 %x)
  ret void
}
