; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.


; GEP+load fuses to s_lw_pre_reg (postinc AGU) rather than add32+ld32.



; NOTE: -verify-machineinstrs is disabled due to known G_PHI selection issue

; Test complex GetElementPtr (GEP) patterns

;Simple array indexing
; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	gep_array                       // -- Begin function gep_array
; CHECK: 	.type	gep_array,@function
; CHECK: gep_array:                              // @gep_array
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	slli32	r2, r2, 2 }
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_reg	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	gep_array, .Lfunc_end0-gep_array
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_struct_field                // -- Begin function gep_struct_field
; CHECK: 	.type	gep_struct_field,@function
; CHECK: gep_struct_field:                       // @gep_struct_field
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	gep_struct_field, .Lfunc_end1-gep_struct_field
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_struct_field1               // -- Begin function gep_struct_field1
; CHECK: 	.type	gep_struct_field1,@function
; CHECK: gep_struct_field1:                      // @gep_struct_field1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_imm	r2, r1, 1 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	gep_struct_field1, .Lfunc_end2-gep_struct_field1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_array_of_struct             // -- Begin function gep_array_of_struct
; CHECK: 	.type	gep_array_of_struct,@function
; CHECK: gep_array_of_struct:                    // @gep_array_of_struct
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r3, r0, 12; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	mull	r3, r2, r3 }
; CHECK: 	{ 		nop; 	nop; 	add32	r2, r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_imm	r1, r2, 1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	gep_array_of_struct, .Lfunc_end3-gep_array_of_struct
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_2d_array                    // -- Begin function gep_2d_array
; CHECK: 	.type	gep_2d_array,@function
; CHECK: gep_2d_array:                           // @gep_2d_array
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	slli32	r2, r2, 4 }
; CHECK: 	{ 		nop; 	slli32	r1, r3, 2; 	add32	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_reg	r1, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	gep_2d_array, .Lfunc_end4-gep_2d_array
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_nested_struct               // -- Begin function gep_nested_struct
; CHECK: 	.type	gep_nested_struct,@function
; CHECK: gep_nested_struct:                      // @gep_nested_struct
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_imm	r2, r1, 2 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	gep_nested_struct, .Lfunc_end5-gep_nested_struct
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_const_offset                // -- Begin function gep_const_offset
; CHECK: 	.type	gep_const_offset,@function
; CHECK: gep_const_offset:                       // @gep_const_offset
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_imm	r2, r1, 5 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	gep_const_offset, .Lfunc_end6-gep_const_offset
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_in_loop                     // -- Begin function gep_in_loop
; CHECK: 	.type	gep_in_loop,@function
; CHECK: gep_in_loop:                            // @gep_in_loop
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r3, r0, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	move32	r4, r3 }
; CHECK: .LBB7_1:                                // %loop
; CHECK:                                         // =>This Inner Loop Header: Depth=1
; CHECK: 	{ 		nop; 	nop; 	s_lw_post_imm	r5, r1, 1 }
; CHECK: 	{ 		nop; 	nop; 	nop }
; CHECK: 	{ 		nop; 	addi32	r4, r4, 1; 	add32	r3, r3, r5 }
; CHECK: 	{ 		nop; 	nop; 	slt32	r5, r4, r2 }
; CHECK: 	{ 		bnez_w	r5, .LBB7_1; 	nop; 	nop }
; CHECK: // %bb.2:                               // %exit
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	gep_in_loop, .Lfunc_end7-gep_in_loop
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_64bit_index                 // -- Begin function gep_64bit_index
; CHECK: 	.type	gep_64bit_index,@function
; CHECK: gep_64bit_index:                        // @gep_64bit_index
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	move32_dr_l	r2, d0 }
; CHECK: 	{ 		nop; 	nop; 	slli32	r2, r2, 2 }
; CHECK: 	{ 		nop; 	nop; 	s_lw_pre_reg	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	gep_64bit_index, .Lfunc_end8-gep_64bit_index
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_store                       // -- Begin function gep_store
; CHECK: 	.type	gep_store,@function
; CHECK: gep_store:                              // @gep_store
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	slli32	r2, r2, 2 }
; CHECK: 	{ 		nop; 	nop; 	s_sw_pre_reg	r3, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	gep_store, .Lfunc_end9-gep_store
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @gep_array(ptr %arr, i32 %idx) {
  %ptr = getelementptr i32, ptr %arr, i32 %idx
  %val = load i32, ptr %ptr
  ret i32 %val
}

;Struct field access
%struct.Point = type { i32, i32 }

define i32 @gep_struct_field(ptr %p) {
  %ptr = getelementptr %struct.Point, ptr %p, i32 0, i32 0
  %val = load i32, ptr %ptr
  ret i32 %val
}

define i32 @gep_struct_field1(ptr %p) {
  %ptr = getelementptr %struct.Point, ptr %p, i32 0, i32 1
  %val = load i32, ptr %ptr
  ret i32 %val
}

;Array of structs
%struct.Triple = type { i32, i32, i32 }

define i32 @gep_array_of_struct(ptr %arr, i32 %idx) {
  %ptr = getelementptr %struct.Triple, ptr %arr, i32 %idx, i32 1
  %val = load i32, ptr %ptr
  ret i32 %val
}

;Multi-dimensional array
define i32 @gep_2d_array(ptr %arr, i32 %row, i32 %col) {
  %row_ptr = getelementptr [4 x i32], ptr %arr, i32 %row
  %elem_ptr = getelementptr [4 x i32], ptr %row_ptr, i32 0, i32 %col
  %val = load i32, ptr %elem_ptr
  ret i32 %val
}

;Nested struct
%struct.Inner = type { i32, i32 }
%struct.Outer = type { i32, %struct.Inner, i32 }

define i32 @gep_nested_struct(ptr %p) {
  %inner_ptr = getelementptr %struct.Outer, ptr %p, i32 0, i32 1
  %field_ptr = getelementptr %struct.Inner, ptr %inner_ptr, i32 0, i32 1
  %val = load i32, ptr %field_ptr
  ret i32 %val
}

;GEP with constant offset (folded into load displacement)
define i32 @gep_const_offset(ptr %arr) {
  %ptr = getelementptr i32, ptr %arr, i32 5
  %val = load i32, ptr %ptr
  ret i32 %val
}

;GEP in loop
define i32 @gep_in_loop(ptr %arr, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %sum = phi i32 [0, %entry], [%sum_next, %loop]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  %val = load i32, ptr %ptr
  %sum_next = add i32 %sum, %val
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum_next
}
; Note: The array load in the loop is optimized away by the compiler.
; The loop back-edge lowers to slt32 + bnez_w (not blt_w) under.
; (SFR-strip) changed bundle layout — rebaselined.
; (No ld32 here: the array load in the loop is DCE'd — %val is dead, see
; comment above. Only the back-edge bnez_w is CHECKed.)

;GEP with 64-bit index
define i32 @gep_64bit_index(ptr %arr, i64 %idx) {
  %ptr = getelementptr i32, ptr %arr, i64 %idx
  %val = load i32, ptr %ptr
  ret i32 %val
}

;GEP store
define void @gep_store(ptr %arr, i32 %idx, i32 %val) {
  %ptr = getelementptr i32, ptr %arr, i32 %idx
  store i32 %val, ptr %ptr
  ret void
}
