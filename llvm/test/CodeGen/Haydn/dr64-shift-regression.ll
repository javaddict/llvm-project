; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Updated for native DR64 shift (sll64/srl64/sra64)
;
; REGRESSION TEST: DR64 cross-type shift intrinsics.
;
; Purpose: Verify that cross-register-bank shift intrinsics (64-bit
; operand, 32-bit result, or 32-bit shift amount with 64-bit operand)
; correctly handle the register bank transition. These are the most
; common source of "wrong register class" verifier errors.
;
; Why this test: SLL64 takes two i32 operands (GPR32), while SRA64/SRL64
; take one i64 (DR64) and one i32 (GPR32). If the selector maps the
; wrong operand to the wrong register bank, the machine verifier will
; reject the output. SRAI64R uses an immediate shift amount from i32.
; A regression in any of these mappings causes a hard crash.
;
; Test design: Each function invokes one cross-type shift intrinsic and
; returns the result, ensuring the instruction survives to assembly.

;===----------------------------------------------------------------------===;
; SLL64: (i32, i32) -> i32 — 64-bit shift left producing 32-bit result
; Both operands are GPR32 (not DR64).
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.sll64(i32, i32)

define i32 @test_sll64(i32 %a, i32 %b) {
; CHECK-LABEL: test_sll64:
; CHECK: sll64
  %r = call i32 @llvm.haydn.sll64(i32 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; SRA64: (i64, i32) -> i32 — cross-bank shift
; The i64 operand is DR64, the i32 shift amount is GPR32.
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.sra64(i64, i32)
declare i32 @llvm.haydn.srl64(i64, i32)

define i32 @test_sra64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64:
; CHECK: sra64
  %r = call i32 @llvm.haydn.sra64(i64 %a, i32 %b)
  ret i32 %r
}

define i32 @test_srl64(i64 %a, i32 %b) {
; CHECK-LABEL: test_srl64:
; CHECK: srl64
  %r = call i32 @llvm.haydn.srl64(i64 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; SRAI64R: (i64, i32) -> i64 — shift with rounding, immediate amount
; The i32 operand encodes the immediate shift amount.
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.srai64r(i64, i32)

define i64 @test_srai64r(i64 %a) {
; CHECK-LABEL: test_srai64r:
; CHECK: srai64r
  %r = call i64 @llvm.haydn.srai64r(i64 %a, i32 5)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SRA64R: (i64, i32) -> i32 — register shift with rounding
; Cross-bank: DR64 accumulator, GPR32 shift amount, GPR32 result.
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.sra64r(i64, i32)

define i32 @test_sra64r(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64r:
; CHECK: sra64r
  %r = call i32 @llvm.haydn.sra64r(i64 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; SATSR64: (i64, i32) -> i32 — saturating shift-right
; Extracts 32-bit value from 64-bit accumulator with saturation.
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.satsr64(i64, i32)

define i32 @test_satsr64(i64 %a, i32 %b) {
; CHECK-LABEL: test_satsr64:
; CHECK: satsr64
  %r = call i32 @llvm.haydn.satsr64(i64 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; PACKSR32: (i64, i32) -> i32 — pack-shift-round
; Pack-shift-round: extract 32-bit value with rounding right shift.
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.packsr32(i64, i32)

define i32 @test_packsr32(i64 %a, i32 %b) {
; CHECK-LABEL: test_packsr32:
; CHECK: packsr32
  %r = call i32 @llvm.haydn.packsr32(i64 %a, i32 %b)
  ret i32 %r
}
