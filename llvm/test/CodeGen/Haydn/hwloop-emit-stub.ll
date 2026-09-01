; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | \
; RUN:   FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF

;; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; DEFAULT pins ON emission; HWOFF keeps the explicit-OFF residual.

; Role: smoke — SET_HWLOOP MCInst emission (not raw-text) under the
; product default (ON since 2026-08-22). DEFAULT must lower the retained
; LoopStart to a typed set_hwloop_f2 MCInst with start/end labels and a
; trip-count register (never a raw-text mnemonic dump). HWOFF is the
; software residual.

define i32 @hwloop_stub_simple(ptr %p) {
; DEFAULT-LABEL: hwloop_stub_simple:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: hwloop_stub_simple:
; HWOFF-NOT:   set_hwloop
; HWOFF-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWOFF:       bnez
; HWOFF:       jalr
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
