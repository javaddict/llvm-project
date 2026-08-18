; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON

; Role: semantic — SCEV-proven constant-trip QUALIFY while product default
; stays OFF. DEFAULT pins the software counted residual (no SET, no HWLR
; CSR invent). HWON re-derives the ZOL contract: set_hwloop_f2 sel=0,
; START before END, two intervening size-bearing parcels, no free CSR.

define i32 @hwloop_basic(ptr %p) {
; DEFAULT-LABEL: hwloop_basic:
; DEFAULT-NOT:   set_hwloop
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       bnez
; DEFAULT:       jalr
;
; HWON-LABEL: hwloop_basic:
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
  %cmp = icmp ult i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
