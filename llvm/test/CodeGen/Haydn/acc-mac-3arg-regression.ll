; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — accumulator-form MAC builtins are 3-arg end-to-end.
; Soft packing-agnostic contract: each 3-arg MAC intrinsic selects its mnemonic.

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mulas64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)

; CHECK-LABEL: test_mula64_ss_ll:
; CHECK: mula64.ll
; CHECK-LABEL: test_muls64_ss_ll:
; CHECK: muls64.ll
; CHECK-LABEL: test_mulas64_ss_ll:
; CHECK: mulas64.ll
; CHECK-LABEL: test_mulss64_ss_ll:
; CHECK: mulss64.ll
; CHECK-LABEL: test_fmula32s_ll:
; CHECK: fmula32s.ll
; CHECK-LABEL: test_ff2mula32rs_ll:
; CHECK: ff2mula32rs.ll
; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK: f2mulaa32rs.hhll
; CHECK-LABEL: test_x2fcmula32rs:
; CHECK: x2fcmula32rs

define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}
define i64 @test_muls64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}
define i64 @test_mulas64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}
define i64 @test_mulss64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}
define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}
define i64 @test_ff2mula32rs_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.ll(i64 %acc, <2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}
define i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}
define <2 x i32> @test_x2fcmula32rs(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
