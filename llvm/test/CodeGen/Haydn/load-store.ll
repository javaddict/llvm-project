; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Simple load/store.

; Simple load/store

define void @test_store(ptr %p, i32 %v) {
  store i32 %v, ptr %p
  ret void
}
; CHECK-LABEL: test_store:
; CHECK: st32

define i32 @test_load(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}
; CHECK-LABEL: test_load:
; CHECK: ld32

; Array access
define i32 @test_array(ptr %p, i32 %idx) {
  %ptr = getelementptr i32, ptr %p, i32 %idx
  %v = load i32, ptr %ptr
  ret i32 %v
}
; CHECK-LABEL: test_array:
; GEP+load may lower as sll + add + ld32, or fuse to s_lw_pre_reg (postinc AGU).
; CHECK: {{sll32|slli32}}
; CHECK: {{s_lw_pre_reg|add32}}
; CHECK: {{s_lw_pre_reg|ld32|move32}}
