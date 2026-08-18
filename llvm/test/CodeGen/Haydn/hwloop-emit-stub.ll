; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | \
; RUN:   FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON

; Role: smoke — SET_HWLOOP MCInst emission (not raw-text) while product
; default stays OFF. DEFAULT is the software residual. HWON must lower
; the retained LoopStart to a typed set_hwloop_f2 MCInst with start/end
; labels and a trip-count register (never a raw-text mnemonic dump).

define i32 @hwloop_stub_simple(ptr %p) {
; DEFAULT-LABEL: hwloop_stub_simple:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       bnez
; DEFAULT:       jalr
;
; HWON-LABEL: hwloop_stub_simple:
; HWON:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; HWON-NOT:   set_hwloop_f2 1,
; HWON-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; HWON:       .LLhwloop_start
; HWON:       .LLhwloop_end
; HWON:       jalr
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
