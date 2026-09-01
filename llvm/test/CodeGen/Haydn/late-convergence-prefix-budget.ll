; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence -o /dev/null < %s 2>&1 \
; RUN:     | FileCheck %s
;
; W68.3R: HaydnLateConvergence records per source/target-pair prefix
; budgets (encoded bytes + alignment pad), not a whole-function total.
; Range-relevant pairs are dumped at entry and after the bounded loop;
; no-growth is reported against those pair budgets.

define i32 @prefix_chain(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %l1, label %exit
l1:
  %x = load i32, ptr %a, align 4
  %v1 = add i32 %x, 1
  %d1 = icmp sgt i32 %v1, 10
  br i1 %d1, label %l2, label %exit
l2:
  %v2 = mul i32 %v1, 3
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %v1, %l1 ], [ %v2, %l2 ]
  ret i32 %r
}

; CHECK: HaydnLateConvergence: prefix_chain bound={{[0-9]+}}
; CHECK: HaydnLateConvergence: entry prefix_chain prefix bb.{{[0-9]+}}->bb.{{[0-9]+}} encoded={{[0-9]+}} pad={{[0-9]+}} maxalign={{[0-9]+}} budget={{[0-9]+}}
; CHECK: HaydnLateConvergence: fixed point after {{[0-9]+}} iteration(s)
; CHECK: HaydnLateConvergence: final prefix_chain prefix bb.{{[0-9]+}}->bb.{{[0-9]+}} encoded={{[0-9]+}} pad={{[0-9]+}} maxalign={{[0-9]+}} budget={{[0-9]+}}
; CHECK: HaydnLateConvergence: prefix_chain prefixes={{[0-9]+}} no-growth={{[01]}} jalr-sites={{[0-9]+}} hwloop-setups=0
