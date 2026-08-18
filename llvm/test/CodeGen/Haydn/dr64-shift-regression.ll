; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — Updated for golden DR64 shifts (sll64/srl64/sra64/sra64r): i64,i32 -> i64.

; Updated for golden DR64 shifts (sll64/srl64/sra64/sra64r): i64,i32 -> i64
;
; REGRESSION TEST: DR64 reg-form shift intrinsics.
;
; Purpose: Verify that DR64 shift intrinsics with a GPR32 shift amount
; (SLL64/SRA64/SRL64/SRA64R) select without wrong-register-class errors.
; Golden shape is rtd,rsd (DR64) + rs (GPR32) -> rtd (DR64).
; satsr64/packsr32 are not IR intrinsics (composites in haydn_dsp.h).

;===----------------------------------------------------------------------===;
; SLL64: (i64, i32) -> i64 — DR64 data + GPR32 amount
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sll64(i64, i32)

define i64 @test_sll64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sll64:
; CHECK: sll64
  %r = call i64 @llvm.haydn.sll64(i64 %a, i32 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SRA64 / SRL64: (i64, i32) -> i64
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sra64(i64, i32)
declare i64 @llvm.haydn.srl64(i64, i32)

define i64 @test_sra64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64:
; CHECK: sra64
  %r = call i64 @llvm.haydn.sra64(i64 %a, i32 %b)
  ret i64 %r
}

define i64 @test_srl64(i64 %a, i32 %b) {
; CHECK-LABEL: test_srl64:
; CHECK: srl64
  %r = call i64 @llvm.haydn.srl64(i64 %a, i32 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SRAI64R: (i64, i32) -> i64 — shift with rounding, immediate amount
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.srai64r(i64, i32)

define i64 @test_srai64r(i64 %a) {
; CHECK-LABEL: test_srai64r:
; CHECK: srai64r
  %r = call i64 @llvm.haydn.srai64r(i64 %a, i32 5)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SRA64R: (i64, i32) -> i64 — register shift with rounding
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sra64r(i64, i32)

define i64 @test_sra64r(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64r:
; CHECK: sra64r
  %r = call i64 @llvm.haydn.sra64r(i64 %a, i32 %b)
  ret i64 %r
}

; (Removed invented satsr64/packsr32 IR — see haydn_dsp.h composites.)
