; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s
;
; HiFi-competitive constant-trip loop: materialize count once, then ZOL.
; Haydn form: set_hwloop_f2_w with trip in a GPR (imm 10 loaded preheader).
;
; CHECKs are intentionally shape-level (not full schedule) so packetizer
; post-RA densify can move body ops without false failures. Core contract:
; 1) set_hwloop_f2 emitted (not silent drop / raw-text only)
; 2) start/end labels present (inclusive END on body)
; 3) soft back-edge gone

; CHECK-LABEL: hwloop_basic:
; CHECK: set_hwloop_f2_w
; CHECK: .LLhwloop_start{{[0-9]+}}:
; CHECK: .LLhwloop_end{{[0-9]+}}:
; CHECK-NOT: beqz_w
define i32 @hwloop_basic(ptr %p) {
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
