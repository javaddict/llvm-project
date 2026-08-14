; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: PEI nested scavenger + large emergency FI must not ICE.
;
; Bug: R11 fail-closed createVirtualRegister whenever hasNoVRegs() and RS
; was set. PEI sets NoVRegs before scavengeFrameVirtualRegs; a scavenger
; spill of an emergency FI with an out-of-range offset still needs a scratch
; vreg. gcc-c-torture multi-ix.c hit that nested path and aborted:
;   "cannot create a virtual register after PEI to materialize a frame-index
;    offset".
; Fix: fail-closed only for a true post-PEI caller (RS == nullptr) on an
; emergency FI. Nested PEI scavenger (RS != nullptr) still creates a vreg.
;
; Test design: many live i32 values plus a large frame so PEI scavenging
; spills while eliminating a far FI. llc must compile; CHECK the return.

define i32 @pei_nested_scavenge(
    i32 %a0, i32 %a1, i32 %a2, i32 %a3,
    i32 %a4, i32 %a5, i32 %a6, i32 %a7,
    i32 %a8, i32 %a9, i32 %a10, i32 %a11,
    i32 %a12, i32 %a13, i32 %a14, i32 %a15) {
; CHECK-LABEL: pei_nested_scavenge:
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
  %buf = alloca [8192 x i32], align 8
  %p = getelementptr [8192 x i32], ptr %buf, i32 0, i32 8000
  store i32 %a0, ptr %p
  %v = load i32, ptr %p
  %s1 = add i32 %v, %a1
  %s2 = add i32 %s1, %a2
  %s3 = add i32 %s2, %a3
  %s4 = add i32 %s3, %a4
  %s5 = add i32 %s4, %a5
  %s6 = add i32 %s5, %a6
  %s7 = add i32 %s6, %a7
  %s8 = add i32 %s7, %a8
  %s9 = add i32 %s8, %a9
  %s10 = add i32 %s9, %a10
  %s11 = add i32 %s10, %a11
  %s12 = add i32 %s11, %a12
  %s13 = add i32 %s12, %a13
  %s14 = add i32 %s13, %a14
  %s15 = add i32 %s14, %a15
  ret i32 %s15
}
