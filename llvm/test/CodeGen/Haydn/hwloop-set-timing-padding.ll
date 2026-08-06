; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION: SET_HWLOOP must be at or before body bundle t−3.
; Fixup inserts deficit NOPs when preheader work is short.

define void @hwloop_t3(ptr nocapture %p, i32 %n) {
; CHECK-LABEL: hwloop_t3:
; CHECK: set_hwloop_f2_w
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: { nop; nop; nop }
; CHECK: .LLhwloop_start{{[0-9]+}}:
; CHECK: addi32
; CHECK: jalr
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
