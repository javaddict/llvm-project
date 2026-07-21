; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Indirect calls lower to jalr_w.

@funcs = external global [4 x ptr]
declare i32 @direct_callee(i32)

define i32 @test_basic_indirect(ptr %fp, i32 %x) {
; CHECK-LABEL: test_basic_indirect:
; CHECK: jalr_w
  %r = call i32 %fp(i32 %x)
  ret i32 %r
}

;Indirect call with two arguments

define i32 @test_indirect_2arg(ptr %fp, i32 %a, i32 %b) {
; CHECK-LABEL: test_indirect_2arg:
; CHECK: jalr_w
  %r = call i32 %fp(i32 %a, i32 %b)
  ret i32 %r
}

;Void function pointer call

define void @test_void_fp(ptr %fp) {
; CHECK-LABEL: test_void_fp:
; CHECK: jalr_w
  call void %fp()
  ret void
}

;Chained indirect calls (same fp called twice)

define i32 @test_chained_indirect(ptr %fp, i32 %x) {
; CHECK-LABEL: test_chained_indirect:
; CHECK: jalr_w
  %r1 = call i32 %fp(i32 %x)
  %r2 = call i32 %fp(i32 %r1)
  ret i32 %r2
}

;Function pointer loaded from global array

define i32 @test_fp_from_array(i32 %idx) {
; CHECK-LABEL: test_fp_from_array:
; CHECK: jalr_w
  %p = getelementptr [4 x ptr], ptr @funcs, i32 0, i32 %idx
  %fp = load ptr, ptr %p
  %r = call i32 %fp(i32 %idx)
  ret i32 %r
}

;Indirect call with many arguments (forces stack spilling)

define i32 @test_indirect_many_args(ptr %fp, i32 %a) {
; CHECK-LABEL: test_indirect_many_args:
; CHECK: jalr_w
  %r = call i32 %fp(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 %a)
  ret i32 %r
}

;Mix of direct and indirect calls in the same function

define i32 @test_mixed_calls(ptr %fp, i32 %x) {
; CHECK-LABEL: test_mixed_calls:
; CHECK: jalr_w
  %r1 = call i32 @direct_callee(i32 %x)
  %r2 = call i32 %fp(i32 %r1)
  %result = add i32 %r1, %r2
  ret i32 %result
}
