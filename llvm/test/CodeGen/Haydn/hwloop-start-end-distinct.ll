; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Single-BB ZOL: START/END labels must both be present; body has work.

define void @tiny_body(ptr nocapture %p, i32 %n) {
; CHECK-LABEL: tiny_body:
; CHECK: set_hwloop_f2_w{{.*}}[[START:\.LLhwloop_start[0-9]+]], [[END:\.LLhwloop_end[0-9]+]]
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: [[START]]:
; CHECK: [[END]]:
; CHECK: add32
; CHECK: jalr_w
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %gep = getelementptr inbounds i32, ptr %p, i32 %i
  store i32 %i, ptr %gep, align 4
  %i2 = add nuw i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
