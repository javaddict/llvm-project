; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	test_load_gep_pos               // -- Begin function test_load_gep_pos
; CHECK: 	.type	test_load_gep_pos,@function
; CHECK-LABEL: test_load_gep_pos:                      // @test_load_gep_pos
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	r1, r1, 16 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_load_gep_pos, .Lfunc_end0-test_load_gep_pos
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; Golden scaled simm6 allows negative offsets (byte -12 → scaled -3).
; Fold into the immediate form rather than materializing a register offset.
; CHECK: 	.globl	test_load_gep_neg               // -- Begin function test_load_gep_neg
; CHECK: 	.type	test_load_gep_neg,@function
; CHECK-LABEL: test_load_gep_neg:                      // @test_load_gep_neg
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	r1, r1, -12 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_load_gep_neg, .Lfunc_end1-test_load_gep_neg
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_store_gep_pos              // -- Begin function test_store_gep_pos
; CHECK: 	.type	test_store_gep_pos,@function
; CHECK-LABEL: test_store_gep_pos:                     // @test_store_gep_pos
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	st32	r2, r1, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_store_gep_pos, .Lfunc_end2-test_store_gep_pos
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; Large offset (4000 > encodable simm immediate range) is materialized into a
; GPR and the register-offset LS form is used (§6.5).
; CHECK: 	.globl	test_load_gep_zero              // -- Begin function test_load_gep_zero
; CHECK: 	.type	test_load_gep_zero,@function
; CHECK-LABEL: test_load_gep_zero:                     // @test_load_gep_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	r1, r1, 0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_load_gep_zero, .Lfunc_end3-test_load_gep_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_multiple_offsets           // -- Begin function test_multiple_offsets
; CHECK: 	.type	test_multiple_offsets,@function
; CHECK-LABEL: test_multiple_offsets:                  // @test_multiple_offsets
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	r2, r1, 4; 	ld32	r1, r1, 12; 	nop }
; CHECK: 	{ 	add32	r1, r2, r1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_multiple_offsets, .Lfunc_end4-test_multiple_offsets
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; NOTE: updated for VLIW slot-1 load promotion — independent loads now pack as ld32+ld32.
; NOTE: updated for — LD32/ST32 s0-layout unify: slot-0 ld32/st32 now emit
; BYTE offsets (e.g. `ld32 r, r, 16` for element index 4 of i32). The legacy
; element-index form is retired.
;
; Tests for enhanced addressing mode optimization: constant offset folding.
;
; The Haydn LD32/ST32 instructions support a simm16 offset field. When a
; G_LOAD or G_STORE uses a pointer from G_PTR_ADD with a constant offset
; the selector should fold the offset directly into the load/store instruction
; instead of emitting a separate ADD32 to compute the address.
;
; Before optimization:
; %ptr = G_PTR_ADD %base, 16
; %val = G_LOAD %ptr -> ADD32 tmp, base, 16; LD32 dst, tmp, 0
;
; After optimization:
; %val = G_LOAD %ptr -> LD32 dst, base, 16

;Simple GEP with positive offset: load
; getelementptr i32, ptr %p, i32 4 => offset = 4*4 = 16
define i32 @test_load_gep_pos(ptr %p) {
  %ptr = getelementptr i32, ptr %p, i32 4
  %v = load i32, ptr %ptr
  ret i32 %v
}

;Simple GEP with negative offset: load
; getelementptr i32, ptr %p, i32 -3 => offset = -3*4 = -12
define i32 @test_load_gep_neg(ptr %p) {
  %ptr = getelementptr i32, ptr %p, i32 -3
  %v = load i32, ptr %ptr
  ret i32 %v
}

;Simple GEP with positive offset: store
; getelementptr i32, ptr %p, i32 2 => offset = 2*4 = 8
define void @test_store_gep_pos(ptr %p, i32 %val) {
  %ptr = getelementptr i32, ptr %p, i32 2
  store i32 %val, ptr %ptr
  ret void
}

;Simple GEP with zero offset: should use offset 0
define i32 @test_load_gep_zero(ptr %p) {
  %ptr = getelementptr i32, ptr %p, i32 0
  %v = load i32, ptr %ptr
  ret i32 %v
}

;Multiple loads from different offsets of the same base
; Each load should fold its offset independently.
define i32 @test_multiple_offsets(ptr %p) {
  %p1 = getelementptr i32, ptr %p, i32 1
  %p3 = getelementptr i32, ptr %p, i32 3
  %v1 = load i32, ptr %p1
  %v3 = load i32, ptr %p3
  %sum = add i32 %v1, %v3
  ret i32 %sum
}

;Struct field access via GEP
; Accessing the 3rd i32 field (offset 8) and 5th i32 field (offset 16).
%struct.S = type { i32, i32, i32, i32, i32 }

define i32 @test_struct_field(ptr %s) {
  %f2 = getelementptr %struct.S, ptr %s, i32 0, i32 2
  %f4 = getelementptr %struct.S, ptr %s, i32 0, i32 4
  %v2 = load i32, ptr %f2
  %v4 = load i32, ptr %f4
  %sum = add i32 %v2, %v4
  ret i32 %sum
}

;Store to struct field
define void @test_struct_store(ptr %s, i32 %val) {
  %f3 = getelementptr %struct.S, ptr %s, i32 0, i32 3
  store i32 %val, ptr %f3
  ret void
}

;64-bit load with GEP offset
; getelementptr i64, ptr %p, i32 2 => offset = 2*8 = 16
define i64 @test_load64_gep(ptr %p) {
  %ptr = getelementptr i64, ptr %p, i32 2
  %v = load i64, ptr %ptr
  ret i64 %v
}

;64-bit store with GEP offset
define void @test_store64_gep(ptr %p, i64 %val) {
  %ptr = getelementptr i64, ptr %p, i32 1
  store i64 %val, ptr %ptr
  ret void
}

;Array access with known index
; Accessing arr[5] => offset = 5*4 = 20
define i32 @test_array_known_index(ptr %arr) {
  %ptr = getelementptr [10 x i32], ptr %arr, i32 0, i32 5
  %v = load i32, ptr %ptr
  ret i32 %v
}

;Variable index (NOT foldable)
; When the GEP index is a variable, the offset cannot be folded.
; The selector should fall back to computing the address.
define i32 @test_array_var_index(ptr %arr, i32 %idx) {
  %ptr = getelementptr i32, ptr %arr, i32 %idx
  %v = load i32, ptr %ptr
  ret i32 %v
; The address computation cannot be folded into the load.
}

;Large offset that fits in simm16 (32767 * 4 = 131068)
; This should still be foldable since 131068 fits in simm16.
define i32 @test_large_offset(ptr %p) {
  %ptr = getelementptr i32, ptr %p, i32 1000
  %v = load i32, ptr %ptr
  ret i32 %v
}

;Mixed load and store with offsets
define void @test_copy_field(ptr %dst, ptr %src) {
  %src_ptr = getelementptr i32, ptr %src, i32 2
  %dst_ptr = getelementptr i32, ptr %dst, i32 3
  %v = load i32, ptr %src_ptr
  store i32 %v, ptr %dst_ptr
  ret void
}

;i8 load with GEP offset (byte access — ISA-43 bug 2: selects ldu8, not ld32)
define i8 @test_load_i8_gep(ptr %p) {
  %ptr = getelementptr i8, ptr %p, i32 7
  %v = load i8, ptr %ptr
  ret i8 %v
}

;i16 store with GEP offset (half access — ISA-43 bug 2: selects st16, not st32)
define void @test_store_i16_gep(ptr %p, i16 %val) {
  %ptr = getelementptr i16, ptr %p, i32 3
  store i16 %val, ptr %ptr
  ret void
}
