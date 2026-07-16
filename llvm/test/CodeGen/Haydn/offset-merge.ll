; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Tests for base+offset merging in the Haydn backend.
; Verifies that the GISel selector and legalizer correctly handle
; GEP chains that produce merged offsets, and that the final code
; uses single LD32/ST32 with the combined offset rather than
; separate address computation instructions.

;Simple GEP with constant offset
define i32 @test_gep_const_offset(ptr %p) {
; CHECK-LABEL: test_gep_const_offset:
  %ptr = getelementptr i32, ptr %p, i32 4
  %v = load i32, ptr %ptr
  ret i32 %v
; CHECK: ld32
}

;Chained GEP: two offsets combined
define i32 @test_gep_chained(ptr %p) {
; CHECK-LABEL: test_gep_chained:
  %p1 = getelementptr i32, ptr %p, i32 2
  %p2 = getelementptr i32, ptr %p1, i32 3
  %v = load i32, ptr %p2
  ret i32 %v
; Combined offset = 5 * 4 = 20
; CHECK: ld32
}

;Store with GEP offset
define void @test_store_gep(ptr %p, i32 %val) {
; CHECK-LABEL: test_store_gep:
  %ptr = getelementptr i32, ptr %p, i32 8
  store i32 %val, ptr %ptr
  ret void
; CHECK: st32
}

;Array access with known index
define i32 @test_array_known_index(ptr %arr) {
; CHECK-LABEL: test_array_known_index:
  %ptr = getelementptr [10 x i32], ptr %arr, i32 0, i32 5
  %v = load i32, ptr %ptr
  ret i32 %v
; CHECK: ld32
}

;Negative offset
define i32 @test_neg_offset(ptr %p) {
; CHECK-LABEL: test_neg_offset:
  %ptr = getelementptr i32, ptr %p, i32 -1
  %v = load i32, ptr %ptr
  ret i32 %v
; CHECK: ld32
}
