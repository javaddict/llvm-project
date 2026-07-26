; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Test that the VLIW list scheduler produces valid code for independent
; arithmetic. IR constant-folds add-immediates (1+2+3 -> 6), so the body is
; a short dependent add chain plus one folded addi32; pin that shape rather
; than a pre-fold three-addi32 layout.

define i32 @ilp_independent_ops(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: ilp_independent_ops:
; CHECK-DAG: add32
; CHECK-DAG: add32
; CHECK-DAG: addi32{{(_w)?}} {{.*}}, 6
; CHECK-DAG: add32
  %r1 = add i32 %a, 1
  %r2 = add i32 %b, 2
  %r3 = add i32 %c, 3
  %r4 = add i32 %r1, %r2
  %result = add i32 %r4, %r3
  ret i32 %result
}

; Test that independent loads are scheduled close together for memory-level
; parallelism. The scheduler should prefer scheduling loads early to overlap
; their latency with subsequent computation.
define i32 @ilp_independent_loads(ptr %p1, ptr %p2, ptr %p3) {
; CHECK-LABEL: ilp_independent_loads:
; CHECK: ld32
; CHECK: ld32
; CHECK: ld32
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  %r1 = add i32 %v1, %v2
  %r2 = add i32 %r1, %v3
  ret i32 %r2
}
