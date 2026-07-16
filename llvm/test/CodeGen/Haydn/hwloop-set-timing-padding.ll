; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

;
; REGRESSION: SET_HWLOOP must be at or before body bundle t−3.
;
; Setup gap is filled preferentially with useful preheader work; HaydnFixupHwLoops
; inserts only the *deficit* idle NOPs when real work is short. A tight
; preheader (SET immediately before body) must still show ≥3 bundles of
; nop/precompute between set_hwloop and the body label.
;
define void @hwloop_t3(ptr nocapture %p, i32 %n) {
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

; CHECK-LABEL: hwloop_t3:
; CHECK:       set_hwloop_f2_w{{.*}}[[START:\.LLhwloop_start[0-9]+]], [[END:\.LLhwloop_end[0-9]+]]
; At least 3 bundles (nop or real precompute) before the body start label.
; CHECK-NEXT:  {
; CHECK-NEXT:  {
; CHECK-NEXT:  {
; CHECK:       [[START]]:
; Body work is between START and inclusive END (add32 in first body bundle).
; CHECK:       add32
; CHECK:       [[END]]:
; CHECK:       jalr_w
