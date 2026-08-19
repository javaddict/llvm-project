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
; Role: semantic — ineligible musttail (byval) and ISR stay fail-closed;
; legal musttail sibcall is JAL_W_MSP (musttail-reject.ll). Soft tail is
; JAL+RET. No product ISR vector / CC_ISR analog.

;--- musttail.ll
%struct.by = type { [8 x i32] }
declare void @byval_callee(ptr byval(%struct.by) %p)
define void @musttail_byval(ptr byval(%struct.by) %p) {
  ; MUSTTAIL: unable to translate instruction: call
  musttail call void @byval_callee(ptr byval(%struct.by) %p)
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
