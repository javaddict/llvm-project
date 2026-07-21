; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Haydn uses 8-byte stack alignment. SP adjustments must be multiples of 8.

declare void @sink(ptr)
declare i32 @many_params(i32, i32, i32, i32, i32, i32, i32, i32, i32)

; 3 x i32 = 12 → pad to multiple of 8
define void @test_multi_alloca() {
; CHECK-LABEL: test_multi_alloca:
; CHECK: subi32{{.*}}sp, sp, {{16|24|32}}
; CHECK: addi32{{.*}}sp, sp, {{16|24|32}}
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  store i32 1, ptr %a
  store i32 2, ptr %b
  store i32 3, ptr %c
  ret void
}

define void @test_256_bytes() {
; CHECK-LABEL: test_256_bytes:
; CHECK: subi32{{.*}}sp, sp, {{2[4-9][0-9]|3[0-9][0-9]}}
  %arr = alloca [64 x i32], align 8
  %p = getelementptr [64 x i32], ptr %arr, i32 0, i32 0
  store i32 0, ptr %p
  ret void
}

define void @test_512_bytes() {
; CHECK-LABEL: test_512_bytes:
; CHECK: subi32{{.*}}sp, sp, {{5[0-9][0-9]|6[0-9][0-9]}}
  %arr = alloca [128 x i32], align 8
  %p = getelementptr [128 x i32], ptr %arr, i32 0, i32 0
  store i32 42, ptr %p
  ret void
}

define i32 @test_odd_alloca(i32 %a) {
; CHECK-LABEL: test_odd_alloca:
; 7 x i32 = 28 → pad to 32 or 40
; CHECK: subi32{{.*}}sp, sp, {{32|40|48}}
  %arr = alloca [7 x i32], align 8
  %p = getelementptr [7 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

define void @test_nested_stack() {
; CHECK-LABEL: test_nested_stack:
; CHECK: subi32{{.*}}sp, sp,
; CHECK: jal_w
  %arr = alloca [32 x i32], align 8
  %p = getelementptr [32 x i32], ptr %arr, i32 0, i32 0
  store i32 42, ptr %p
  call void @sink(ptr %p)
  ret void
}

define i32 @test_stack_outgoing(i32 %a) {
; CHECK-LABEL: test_stack_outgoing:
; CHECK: subi32{{.*}}sp, sp,
; CHECK: jal_w
  %r = call i32 @many_params(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 %a)
  ret i32 %r
}

define i32 @test_local_and_outgoing(i32 %a) {
; CHECK-LABEL: test_local_and_outgoing:
; CHECK: subi32{{.*}}sp, sp,
; CHECK: jal_w
  %arr = alloca [4 x i32], align 8
  %p = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @many_params(i32 %v, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret i32 %r
}

; 5 x i32 = 20 → pad to multiple of 8
define void @test_align_5xi32() {
; CHECK-LABEL: test_align_5xi32:
; CHECK: subi32{{.*}}sp, sp, {{24|32}}
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  %d = alloca i32
  %e = alloca i32
  store i32 1, ptr %a
  store i32 2, ptr %b
  store i32 3, ptr %c
  store i32 4, ptr %d
  store i32 5, ptr %e
  ret void
}
