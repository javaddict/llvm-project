; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s
;
; Role: semantic — optional `tail` is an ordinary JAL_W + RET, not TCO.
; musttail stays fail-closed (musttail-reject.ll). JAL_W is not a
; terminator/return, so PEI would skip the epilogue of a fake tail.

declare i32 @sink(i32)

define i32 @soft_tail(i32 %x) nounwind {
; CHECK-LABEL: name: soft_tail
; CHECK: JAL{{.*}}@sink
; CHECK: RET
  %r = tail call i32 @sink(i32 %x)
  ret i32 %r
}

define void @soft_tail_void() nounwind {
; CHECK-LABEL: name: soft_tail_void
; CHECK: JAL{{.*}}@sink
; CHECK: RET
  tail call i32 @sink(i32 0)
  ret void
}
