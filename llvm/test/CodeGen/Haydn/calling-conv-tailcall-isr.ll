; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/musttail.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=MUSTTAIL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/isr.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ISR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - \
; RUN:     %t/soft_tail.ll | FileCheck %s --check-prefix=SOFT
;
; Role: semantic — musttail and ISR stay fail-closed; soft tail is JAL+RET.
;
; lowerTailCall is the AIE-shaped seat (AIECallLowering.cpp:622) and returns
; false: JAL_W is not isReturn+isTerminator, so a fake tail would skip the
; PEI epilogue. No product ISR vector / CC_ISR analog.

;--- musttail.ll
declare void @callee(i32)
define void @musttail_caller(i32 %x) {
  ; MUSTTAIL: unable to translate instruction: call
  musttail call void @callee(i32 %x)
  ret void
}

;--- isr.ll
define void @isr() "interrupt"="machine" {
  ; ISR: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- soft_tail.ll
declare i32 @sink(i32)
define i32 @soft_tail(i32 %x) nounwind {
; SOFT-LABEL: name: soft_tail
; SOFT: JAL{{.*}}@sink
; SOFT: RET
  %r = tail call i32 @sink(i32 %x)
  ret i32 %r
}
