; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — F2MULZAA32RS binary/zero-acc dual-product MAC selects directly,
; and F2MULAA(acc=0) folds to ZAA. Packing-agnostic mnemonic contract.

declare i64 @llvm.haydn.f2mulzaa32rs.hhll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32rs.hllh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32r.hhll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32r.hllh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)

; CHECK-LABEL: test_f2mulzaa32rs_hhll:
; CHECK: f2mulzaa32rs.hhll
; CHECK-LABEL: test_f2mulzaa32rs_hllh:
; CHECK: f2mulzaa32rs.hllh
; CHECK-LABEL: test_f2mulzaa32r_hhll:
; CHECK: f2mulzaa32r.hhll
; CHECK-LABEL: test_f2mulzaa32r_hllh:
; CHECK: f2mulzaa32r.hllh
; CHECK-LABEL: test_fold_zero_acc:
; CHECK: f2mulzaa32rs.hhll
; CHECK-NOT: f2mulaa32rs.hhll

define i64 @test_f2mulzaa32rs_hhll(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32rs.hhll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}
define i64 @test_f2mulzaa32rs_hllh(i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32rs.hllh(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}
define i64 @test_f2mulzaa32r_hhll(i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32r.hhll(<2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}
define i64 @test_f2mulzaa32r_hllh(i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32r.hllh(<2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}
define i64 @test_fold_zero_acc(i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 0, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}
