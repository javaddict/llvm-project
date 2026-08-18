; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %s 2>&1 | FileCheck %s
;
; Role: semantic — musttail is unsupported; fail closed before CALLSEQ/call
; mutation (no silent ordinary-call lowering). lowerTailCall is the AIE-shaped
; seat (AIECallLowering.cpp:622) and returns false until a tail opcode is
; isReturn+isCall+isTerminator. Soft tail stays ordinary JAL+RET.

declare void @callee(i32)
define void @caller(i32 %x) {
  ; CHECK: unable to translate instruction: call
  musttail call void @callee(i32 %x)
  ret void
}
