; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — DR64 ALU intrinsics end-to-end codegen.

; REGRESSION TEST: DR64 ALU intrinsics end-to-end codegen.
;
; Purpose: Verify that every DR64 ALU intrinsic (add/sub, saturating
; abs/neg, min/max, bitwise) survives the full GlobalISel pipeline and
; emits the correct target instruction mnemonic. If any intrinsic's
; selector mapping breaks, the corresponding CHECK line will fail.
;
; Why this test: The DR64 register bank is separate from GPR32 (hard
; constraint #2). Bugs in register bank selection, operand class mapping
; or instruction selector patterns can silently drop DR64 operations or
; mis-select them to GPR32 instructions. This test catches regressions.
;
; Test design: Each function invokes one DR64 ALU intrinsic and returns
; the result. The intrinsic's return value is used (returned from the
; function), ensuring the instruction survives to assembly output.

;===----------------------------------------------------------------------===;
; 64-bit add/sub half-variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64.h(i64, i64)
declare i64 @llvm.haydn.add64.l(i64, i64)
declare i64 @llvm.haydn.sub64.h(i64, i64)
declare i64 @llvm.haydn.sub64.l(i64, i64)

define i64 @test_add64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_h:
; CHECK: add64_h
  %r = call i64 @llvm.haydn.add64.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_add64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_l:
; CHECK: add64_l
  %r = call i64 @llvm.haydn.add64.l(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_h:
; CHECK: sub64_h
  %r = call i64 @llvm.haydn.sub64.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_l:
; CHECK: sub64_l
  %r = call i64 @llvm.haydn.sub64.l(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit saturating add/sub
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64s(i64, i64)
declare i64 @llvm.haydn.sub64s(i64, i64)

define i64 @test_add64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64s:
; CHECK: add64s
  %r = call i64 @llvm.haydn.add64s(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64s:
; CHECK: sub64s
  %r = call i64 @llvm.haydn.sub64s(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit saturating abs/neg
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.abs64s(i64)
declare i64 @llvm.haydn.neg64s(i64)

define i64 @test_abs64s(i64 %a) {
; CHECK-LABEL: test_abs64s:
; CHECK: abs64s
  %r = call i64 @llvm.haydn.abs64s(i64 %a)
  ret i64 %r
}

define i64 @test_neg64s(i64 %a) {
; CHECK-LABEL: test_neg64s:
; CHECK: neg64s
  %r = call i64 @llvm.haydn.neg64s(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit non-saturating abs/neg
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.abs64(i64)
declare i64 @llvm.haydn.neg64(i64)

define i64 @test_abs64(i64 %a) {
; CHECK-LABEL: test_abs64:
; CHECK: abs64
  %r = call i64 @llvm.haydn.abs64(i64 %a)
  ret i64 %r
}

define i64 @test_neg64(i64 %a) {
; CHECK-LABEL: test_neg64:
; CHECK: neg64
  %r = call i64 @llvm.haydn.neg64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit min/max
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.max64(i64, i64)
declare i64 @llvm.haydn.min64(i64, i64)

define i64 @test_max64(i64 %a, i64 %b) {
; CHECK-LABEL: test_max64:
; CHECK: max64
  %r = call i64 @llvm.haydn.max64(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_min64(i64 %a, i64 %b) {
; CHECK-LABEL: test_min64:
; CHECK: min64
  %r = call i64 @llvm.haydn.min64(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit bitwise NOT
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.not64(i64)

define i64 @test_not64(i64 %a) {
; CHECK-LABEL: test_not64:
; CHECK: not64
  %r = call i64 @llvm.haydn.not64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULSA32 / MULSS32 dual 32-bit multiply-add/subtract
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulsa32.hhll(i64, i64)
declare i64 @llvm.haydn.mulsa32.hllh(i64, i64)
declare i64 @llvm.haydn.mulss32.hhll(i64, i64)
declare i64 @llvm.haydn.mulss32.hllh(i64, i64)

define i64 @test_mulsa32_hhll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulsa32_hhll:
; CHECK: mulsa32_hhll
  %r = call i64 @llvm.haydn.mulsa32.hhll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulsa32_hllh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulsa32_hllh:
; CHECK: mulsa32_hllh
  %r = call i64 @llvm.haydn.mulsa32.hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss32_hhll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss32_hhll:
; CHECK: mulss32_hhll
  %r = call i64 @llvm.haydn.mulss32.hhll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss32_hllh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss32_hllh:
; CHECK: mulss32_hllh
  %r = call i64 @llvm.haydn.mulss32.hllh(i64 %a, i64 %b)
  ret i64 %r
}
