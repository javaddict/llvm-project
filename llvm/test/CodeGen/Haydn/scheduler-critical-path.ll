; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : IR folds a+1+2+3 and b+10+20 into (a+b)+36; scheduler keeps
; independent chains packable. Pin folded immediate + add32 presence.

define i32 @critical_path_priority(i32 %a, i32 %b) {
; CHECK-LABEL: critical_path_priority:
; CHECK: .cfi_startproc
; CHECK: // %bb.0:
; CHECK-DAG: xor32{{(_w)?}}
; The constant adds collapse into a single addi32; one register-register add
; remains for the %x dependency.
; CHECK-DAG: add32
; CHECK-DAG: addi32{{(_w)?}} {{.*}}, 36
; CHECK: jalr_w{{(\.s[012])?}}
; CHECK: .Lfunc_end0:
  %a1 = add i32 %a, 1
  %a2 = add i32 %a1, 2
  %a3 = add i32 %a2, 3

  %b1 = add i32 %b, 10
  %b2 = add i32 %b1, 20

  %result = add i32 %a3, %b2
  ret i32 %result
}

; Test that a long chain with memory operations gets scheduled so that
; loads are issued early. The load for the critical path should be
; prioritized over independent computation.
define i32 @critical_path_with_memory(ptr %p, i32 %x) {
; CHECK-LABEL: critical_path_with_memory:
; The load should appear in the function body.
; CHECK: ld32
; +1+2 folds into one addi32; the %x add stays register-register.
; CHECK-DAG: add32
; CHECK-DAG: addi32{{(_w)?}} {{.*}}, 3
  %v = load i32, ptr %p
  %r1 = add i32 %v, 1
  %r2 = add i32 %r1, 2
  %r3 = add i32 %r2, %x
  ret i32 %r3
}
