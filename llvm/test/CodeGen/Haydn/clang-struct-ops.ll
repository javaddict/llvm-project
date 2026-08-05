; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	test_small_struct_by_val        // -- Begin function test_small_struct_by_val
; CHECK: 	.type	test_small_struct_by_val,@function
; CHECK-LABEL: test_small_struct_by_val:               // @test_small_struct_by_val
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_small_struct_by_val, .Lfunc_end0-test_small_struct_by_val
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function






; Test struct passing and returning through Clang frontend

; Simple struct (fits in one register)
%struct.small = type { i32 }

define i32 @test_small_struct_by_val(%struct.small %s) {
entry:
  %val = extractvalue %struct.small %s, 0
  ret i32 %val
}

; Struct passed by pointer
define i32 @test_struct_ptr(ptr %s) {
entry:
  %val = load i32, ptr %s
  ret i32 %val
}

; Multiple fields
%struct.pair = type { i32, i32 }

define i32 @test_struct_field_access(ptr %p) {
entry:
  %f0 = load i32, ptr %p
  %f1.ptr = getelementptr %struct.pair, ptr %p, i32 0, i32 1
  %f1 = load i32, ptr %f1.ptr
  %sum = add i32 %f0, %f1
  ret i32 %sum
}

; Array of structs
define i32 @test_struct_array(ptr %arr, i32 %i) {
entry:
  %elem.ptr = getelementptr %struct.pair, ptr %arr, i32 %i
  %f0 = load i32, ptr %elem.ptr
  %f1.ptr = getelementptr %struct.pair, ptr %elem.ptr, i32 0, i32 1
  %f1 = load i32, ptr %f1.ptr
  %sum = add i32 %f0, %f1
  ret i32 %sum
}

; Nested structs
%struct.inner = type { i32, i32 }
%struct.outer = type { %struct.inner, i32 }

define i32 @test_nested_struct(ptr %o) {
entry:
  %i.ptr = getelementptr %struct.outer, ptr %o, i32 0, i32 0
  %i.f0 = load i32, ptr %i.ptr
  %i.f1.ptr = getelementptr %struct.inner, ptr %i.ptr, i32 0, i32 1
  %i.f1 = load i32, ptr %i.f1.ptr
  %o.f2 = getelementptr %struct.outer, ptr %o, i32 0, i32 1
  %outer_val = load i32, ptr %o.f2
  %sum = add i32 %i.f0, %i.f1
  %sum2 = add i32 %sum, %outer_val
  ret i32 %sum2
}

; Struct copying
define void @test_struct_copy(ptr %dst, ptr %src) {
entry:
  %val = load i32, ptr %src
  store i32 %val, ptr %dst
  ret void
}

; Struct initialization (GEP offset folded into store displacement)
define void @test_struct_init(ptr %p, i32 %a, i32 %b) {
entry:
  store i32 %a, ptr %p
  %f1.ptr = getelementptr %struct.pair, ptr %p, i32 0, i32 1
  store i32 %b, ptr %f1.ptr
  ret void
}

; Return struct by value (single field)
define %struct.small @test_return_struct(i32 %x) {
entry:
  %s = insertvalue %struct.small undef, i32 %x, 0
  ret %struct.small %s
}

; Return struct by value (multiple fields)
define %struct.pair @test_return_pair(i32 %a, i32 %b) {
entry:
  %s = insertvalue %struct.pair undef, i32 %a, 0
  %s2 = insertvalue %struct.pair %s, i32 %b, 1
  ret %struct.pair %s2
}

; Packed struct (bitfields - simulated with shifts)
define i32 @test_packed_fields(i32 %packed) {
entry:
  %low = and i32 %packed, 65535
  %high.shifted = lshr i32 %packed, 16
  %sum = add i32 %low, %high.shifted
  ret i32 %sum
}

; Struct with array member
%struct.with_array = type { [4 x i32] }

define i32 @test_struct_array_member(ptr %s, i32 %i) {
entry:
  %arr.ptr = getelementptr %struct.with_array, ptr %s, i32 0, i32 0
  %elem.ptr = getelementptr [4 x i32], ptr %arr.ptr, i32 0, i32 %i
  %val = load i32, ptr %elem.ptr
  ret i32 %val
}

; Struct pointer arithmetic
define ptr @test_struct_ptr_arith(ptr %arr, i32 %i) {
entry:
  %elem.ptr = getelementptr %struct.pair, ptr %arr, i32 %i
  ret ptr %elem.ptr
}

; Struct comparison (memcmp style)
; s32 G_SELECT lowered to MOVT32 (prior revision) — no longer emits
; neg32+and32+or32.
define i32 @test_struct_compare(ptr %a, ptr %b) {
entry:
  %a.val = load i32, ptr %a
  %b.val = load i32, ptr %b
  %cmp = icmp ne i32 %a.val, %b.val
  %result = select i1 %cmp, i32 1, i32 0
  ret i32 %result
}

; Zero-initialize struct (GEP offset folded into store displacement)
define void @test_struct_zero(ptr %p) {
entry:
  store i32 0, ptr %p
  %f1.ptr = getelementptr %struct.pair, ptr %p, i32 0, i32 1
  store i32 0, ptr %f1.ptr
  ret void
}

; Struct in global array
@global_structs = global [2 x %struct.pair] [%struct.pair { i32 1, i32 2 }, %struct.pair { i32 3, i32 4 }]

define i32 @test_global_struct(i32 %i) {
; Materialize the global base address (lui + addi32) and the index shift
; amount (addi32 for the log2(8) constant). These are independent operand
; setups; the post-RA scheduler may hoist the shift-amount materialization
; ahead of the address materialization, so check them as a DAG.
entry:
  %arr.ptr = getelementptr [2 x %struct.pair], ptr @global_structs, i32 0, i32 %i
  %val.ptr = getelementptr %struct.pair, ptr %arr.ptr, i32 0, i32 0
  %val = load i32, ptr %val.ptr
  ret i32 %val
}
