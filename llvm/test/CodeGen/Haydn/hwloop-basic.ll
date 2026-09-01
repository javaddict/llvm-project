; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=0 < %s | FileCheck %s --check-prefix=HWOFF

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON
; (qualified independent + combined). DEFAULT pins the ZOL contract;
; HWOFF keeps the explicit-OFF software-residual coverage.

; Role: semantic — SCEV-proven constant-trip QUALIFY under the product
; default (ON since 2026-08-22). DEFAULT re-derives the ZOL contract:
; set_hwloop_f2 sel=0, START before END, two intervening size-bearing
; parcels, no free CSR. HWOFF pins the software counted residual (no SET,
; no HWLR CSR invent).

define i32 @hwloop_basic(ptr %p) {
; DEFAULT-LABEL: hwloop_basic:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
;
; HWOFF-LABEL: hwloop_basic:
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
  %cmp = icmp ult i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
