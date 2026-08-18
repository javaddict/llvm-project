; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Coverage for ctrl flow.

define i32 @cond_br(i32 %a, i32 %b) {
; CHECK-LABEL: cond_br:
; CHECK-DAG: seq32
; CHECK-DAG: b{{eq|ne}}z{{(\.s[012])?}}
; This shape (br i1 %cmp; then: ret; else: ret) has NO Join block, so
; HaydnGenMux Phase 2 (tryConvertBranchCMOV) correctly bails — it requires
; Join->pred_size==2. The backend emits seq32+beqz_w+branch, which is valid
; codegen and still exercises the branch-offset encoding. The movt32
; predication path is tested separately in cmov-formation.ll (phi-merge Join).
; See for the full verdict.
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 42
else:
  ret i32 0
}

define i32 @loop(i32 %n) {
; CHECK-LABEL: loop:
; NOTE: this countable loop now converts to a zero-overhead hardware loop
; (set_hwloop_f2) via HaydnHardwareLoops, so the slt32+bnez_w back-edge pair
; is no longer emitted. The HWLoop form is the desired lowering (M7 work).
; CHECK: set_hwloop_f2
entry:
  br label %loop_header
loop_header:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop_header ]
  %sum = phi i32 [ 0, %entry ], [ %new_sum, %loop_header ]
  %inc = add i32 %i, 1
  %new_sum = add i32 %sum, %i
  %cmp = icmp slt i32 %inc, %n
  br i1 %cmp, label %loop_header, label %exit
exit:
  ret i32 %new_sum
}
