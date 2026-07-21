; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Test that the VLIW list scheduler orders independent instructions for
; maximum ILP. Independent arithmetic operations on different registers
; should be scheduled close together so the post-RA packetizer can bundle
; them into the same VLIW packet.
;
; The scheduler should place ADDI32 instructions on R1,R2,R3 (independent)
; before the dependent ADDI32 on R4 (which uses R1+R2), maximizing the
; chance that the packetizer bundles the three independent ops together.

define i32 @ilp_independent_ops(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: ilp_independent_ops
; CHECK: addi32
; CHECK: addi32
; CHECK: addi32
; The three independent adds should appear before the dependent add.
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
; CHECK-LABEL: ilp_independent_loads
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
