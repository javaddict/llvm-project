; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — GEP constant offsets fold into word load/store (ld32/s_lw or st32/s_sw), not a separate address materialize + zero-offset load.

; GEP constant offsets fold into word load/store (ld32/s_lw or st32/s_sw),
; not a separate address materialize + zero-offset load.

define i32 @test_gep_const_offset(ptr %p) {
; CHECK-LABEL: test_gep_const_offset:
; CHECK: {{ld32|s_lw}}
; CHECK-NOT: addi32{{.*}}, 16
  %ptr = getelementptr i32, ptr %p, i32 4
  %v = load i32, ptr %ptr
  ret i32 %v
}

define i32 @test_gep_chained(ptr %p) {
; CHECK-LABEL: test_gep_chained:
; Combined offset = 5 words → s_lw_pre_imm …, 5 or ld32 …, 20
; CHECK: {{ld32|s_lw}}
  %p1 = getelementptr i32, ptr %p, i32 2
  %p2 = getelementptr i32, ptr %p1, i32 3
  %v = load i32, ptr %p2
  ret i32 %v
}

define void @test_store_gep(ptr %p, i32 %val) {
; CHECK-LABEL: test_store_gep:
; CHECK: {{st32|s_sw}}
  %ptr = getelementptr i32, ptr %p, i32 8
  store i32 %val, ptr %ptr
  ret void
}

define i32 @test_array_known_index(ptr %arr) {
; CHECK-LABEL: test_array_known_index:
; CHECK: {{ld32|s_lw}}
  %ptr = getelementptr [10 x i32], ptr %arr, i32 0, i32 5
  %v = load i32, ptr %ptr
  ret i32 %v
}

define i32 @test_neg_offset(ptr %p) {
; CHECK-LABEL: test_neg_offset:
; CHECK: {{ld32|s_lw}}
  %ptr = getelementptr i32, ptr %p, i32 -1
  %v = load i32, ptr %ptr
  ret i32 %v
}
