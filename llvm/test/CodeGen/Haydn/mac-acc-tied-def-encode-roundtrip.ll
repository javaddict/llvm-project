; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -filetype=obj < %s | llvm-objdump -d - | \
; RUN:     FileCheck %s --check-prefix=OBJ

; Role: object — tied-def accumulator MAC encode/decode round-trip; no OR64 seed.

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)

; ASM-LABEL: test_mula64_ll:
; ASM-NOT: or64
; ASM: mula64.ll
; ASM-LABEL: test_muls64_ll:
; ASM-NOT: or64
; ASM: muls64.ll
; ASM-LABEL: test_ff2mula32rs_lh:
; ASM-NOT: or64
; ASM: ff2mula32rs.lh
; ASM-LABEL: test_x2fcmula32rs:
; ASM-NOT: or64
; ASM: x2fcmula32rs

; OBJ-LABEL: <test_mula64_ll>:
; OBJ: mula64{{[._]}}ll
; OBJ-LABEL: <test_muls64_ll>:
; OBJ: muls64{{[._]}}ll
; OBJ-LABEL: <test_ff2mula32rs_lh>:
; OBJ: ff2mula32rs{{[._]}}lh
; OBJ-LABEL: <test_x2fcmula32rs>:
; OBJ: x2fcmula32rs

define i64 @test_mula64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}
define i64 @test_muls64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}
define i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}
define <2 x i32> @test_x2fcmula32rs(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
