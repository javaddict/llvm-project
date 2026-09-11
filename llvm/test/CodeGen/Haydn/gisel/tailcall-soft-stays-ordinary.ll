; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s
;
; Role: semantic — optional `tail` is an ordinary general call + RET, not TCO.
; Legal musttail sibcall is JAL_TCO / JALR_W (musttail-reject.ll).

declare i32 @sink(i32)

define i32 @soft_tail(i32 %x) nounwind {
; CHECK-LABEL: name: soft_tail
; CHECK: LOAD_ADDR
; CHECK: JALR_CALL
; CHECK: RET
  %r = tail call i32 @sink(i32 %x)
  ret i32 %r
}

define void @soft_tail_void() nounwind {
; CHECK-LABEL: name: soft_tail_void
; CHECK: LOAD_ADDR
; CHECK: JALR_CALL
; CHECK: RET
  tail call i32 @sink(i32 0)
  ret void
}
