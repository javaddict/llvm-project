; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; REGRESSION TEST: R0 is reserved soft-zero, not a generic RegScavenger temp.
;
; Generic RegScavenger::FindUnusedReg / findSurvivorBackwards skip reserved
; registers (RegisterScavenging.cpp). Haydn reserves R0 as soft-zero
; (HaydnRegisterInfo::getReservedRegs). The obvious port — un-reserve R0 so
; the scavenger can pick the "free zero register" — makes FindUnusedReg
; return R0 first (GPR32 order is R0, R1, …). LOADI64 NeedsZeroBase then
; MatInts into r0 and the next ADDI rd, r0, imm reads a dirty zero.
;
; Pin: i64 materialization (HaydnPostRAScratch NeedsZeroBase) writes the
; MatInt dest into r1–r12, never r0. r0 appears only as the zero *source*,
; xor32 restore, and JALR link discard. A naive RegScavenger swap that
; un-reserves R0 fails the CHECK-NOT.
;
; PEI nested emergency FI is a different layer (always createVirtualRegister;
; hasNoVRegs is not post-PEI). See pei-scavenge-emergency-large-fi.ll.

define i64 @scavenge_needs_zero_base() {
; CHECK-LABEL: scavenge_needs_zero_base:
; CHECK:       xor32 r0, r0, r0
; CHECK:       addi32{{(_w)?}} r{{[1-9]|1[0-2]}}, r0,
; CHECK-NOT:   addi32{{(_w)?}} r0,
; CHECK-NOT:   lui{{(_w)?}} r0,
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 42
}

define i64 @scavenge_needs_zero_base_pressure(
    i32 %a1, i32 %a2, i32 %a3, i32 %a4,
    i32 %a5, i32 %a6, i32 %a7, i32 %a8,
    i32 %a9, i32 %a10, i32 %a11, i32 %a12) {
; CHECK-LABEL: scavenge_needs_zero_base_pressure:
; CHECK:       xor32 r0, r0, r0
; CHECK-NOT:   addi32{{(_w)?}} r0,
; CHECK-NOT:   lui{{(_w)?}} r0,
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
  %c = add i64 42, 0
  %s1 = add i32 %a1, %a2
  %s2 = add i32 %s1, %a3
  %s3 = add i32 %s2, %a4
  %s4 = add i32 %s3, %a5
  %s5 = add i32 %s4, %a6
  %s6 = add i32 %s5, %a7
  %s7 = add i32 %s6, %a8
  %s8 = add i32 %s7, %a9
  %s9 = add i32 %s8, %a10
  %s10 = add i32 %s9, %a11
  %s11 = add i32 %s10, %a12
  %z = zext i32 %s11 to i64
  %r = add i64 %c, %z
  ret i64 %r
}
