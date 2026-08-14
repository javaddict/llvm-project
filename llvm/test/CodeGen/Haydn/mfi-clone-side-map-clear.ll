; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -enable-machine-outliner=always -outliner-benefit-threshold=0 -O2 < %s \
; RUN:     -o - | FileCheck %s

; Role: phase-firewall pin (CG-25 / PIPE-20): clone clears AltDescs and remaps SMS MBBs.

; CHECK-LABEL: clone_src_a:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
; CHECK-LABEL: clone_src_b:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0

define i32 @clone_src_a(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = mul i32 %a, 7
  %c = add i32 %b, %x
  %d = mul i32 %c, 3
  %e = add i32 %d, %y
  ret i32 %e
}

define i32 @clone_src_b(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = mul i32 %a, 7
  %c = add i32 %b, %x
  %d = mul i32 %c, 3
  %e = add i32 %d, %y
  ret i32 %e
}
