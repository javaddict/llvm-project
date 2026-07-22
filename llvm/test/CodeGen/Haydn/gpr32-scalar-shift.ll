; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Golden slot0 scalar-32 shifts: imm RI5 + reg RR.

declare i32 @llvm.haydn.slli32(i32, i32)
declare i32 @llvm.haydn.srli32(i32, i32)
declare i32 @llvm.haydn.srai32(i32, i32)
declare i32 @llvm.haydn.srai32r(i32, i32)
declare i32 @llvm.haydn.sll32(i32, i32)
declare i32 @llvm.haydn.srl32(i32, i32)
declare i32 @llvm.haydn.sra32(i32, i32)
declare i32 @llvm.haydn.sra32r(i32, i32)

define i32 @test_slli32(i32 %a) {
; CHECK-LABEL: test_slli32:
; CHECK: slli32
  %r = call i32 @llvm.haydn.slli32(i32 %a, i32 3)
  ret i32 %r
}
define i32 @test_srli32(i32 %a) {
; CHECK-LABEL: test_srli32:
; CHECK: srli32
  %r = call i32 @llvm.haydn.srli32(i32 %a, i32 5)
  ret i32 %r
}
define i32 @test_srai32(i32 %a) {
; CHECK-LABEL: test_srai32:
; CHECK: srai32
  %r = call i32 @llvm.haydn.srai32(i32 %a, i32 7)
  ret i32 %r
}
define i32 @test_srai32r(i32 %a) {
; CHECK-LABEL: test_srai32r:
; CHECK: srai32r
  %r = call i32 @llvm.haydn.srai32r(i32 %a, i32 4)
  ret i32 %r
}
define i32 @test_sll32(i32 %a, i32 %sh) {
; CHECK-LABEL: test_sll32:
; CHECK: sll32
  %r = call i32 @llvm.haydn.sll32(i32 %a, i32 %sh)
  ret i32 %r
}
define i32 @test_srl32(i32 %a, i32 %sh) {
; CHECK-LABEL: test_srl32:
; CHECK: srl32
  %r = call i32 @llvm.haydn.srl32(i32 %a, i32 %sh)
  ret i32 %r
}
define i32 @test_sra32(i32 %a, i32 %sh) {
; CHECK-LABEL: test_sra32:
; CHECK: sra32
  %r = call i32 @llvm.haydn.sra32(i32 %a, i32 %sh)
  ret i32 %r
}
define i32 @test_sra32r(i32 %a, i32 %sh) {
; CHECK-LABEL: test_sra32r:
; CHECK: sra32r
  %r = call i32 @llvm.haydn.sra32r(i32 %a, i32 %sh)
  ret i32 %r
}
