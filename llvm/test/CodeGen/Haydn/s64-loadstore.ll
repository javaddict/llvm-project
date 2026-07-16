; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Test 64-bit load

; REBASELINED (auto) dual-sched / AR logical-slot rebaseline;.file skipped

; CHECK: 	.text
; CHECK: 	.globl	load64                          // -- Begin function load64
; CHECK: 	.type	load64,@function
; CHECK: load64:                                 // @load64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r1, 4; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 	ld32	r2, r2, 0; 	subi32	sp, sp, 8; 	nop }
; CHECK: 	{ 	st32	r1, sp, 0 }
; CHECK: 	{ 	st32	r2, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	load64, .Lfunc_end0-load64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	store64                         // -- Begin function store64
; CHECK: 	.type	store64,@function
; CHECK: store64:                                // @store64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r1, 4; 	d_sw_l_with_imm	d0, r1, 0; 	nop }
; CHECK: 	{ 	d_sw_h_with_imm	d0, r2, 0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	store64, .Lfunc_end1-store64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	load64_array                    // -- Begin function load64_array
; CHECK: 	.type	load64_array,@function
; CHECK: load64_array:                           // @load64_array
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r0, 3; 	subi32	sp, sp, 8; 	nop }
; CHECK: 	{ 	sll32	r2, r2, r3 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r1, 4; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 	ld32	r2, r2, 0 }
; CHECK: 	{ 	st32	r1, sp, 0 }
; CHECK: 	{ 	st32	r2, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	load64_array, .Lfunc_end2-load64_array
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	store64_array                   // -- Begin function store64_array
; CHECK: 	.type	store64_array,@function
; CHECK: store64_array:                          // @store64_array
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK: 	{ 	sll32	r2, r2, r3 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r1, 4; 	d_sw_l_with_imm	d0, r1, 0; 	nop }
; CHECK: 	{ 	d_sw_h_with_imm	d0, r2, 0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	store64_array, .Lfunc_end3-store64_array
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	load64_volatile                 // -- Begin function load64_volatile
; CHECK: 	.type	load64_volatile,@function
; CHECK: load64_volatile:                        // @load64_volatile
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r1, 4; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 	ld32	r2, r2, 0; 	subi32	sp, sp, 8; 	nop }
; CHECK: 	{ 	st32	r1, sp, 0 }
; CHECK: 	{ 	st32	r2, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	load64_volatile, .Lfunc_end4-load64_volatile
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	store64_volatile                // -- Begin function store64_volatile
; CHECK: 	.type	store64_volatile,@function
; CHECK: store64_volatile:                       // @store64_volatile
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 	move32_dr_l	r2, d0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r1, 4 }
; CHECK: 	{ 	st32	r2, r1, 0; 	move32_dr_h	r1, d0; 	nop }
; CHECK: 	{ 	st32	r1, r3, 0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	store64_volatile, .Lfunc_end5-store64_volatile
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i64 @load64(ptr %p) {
  %v = load i64, ptr %p
  ret i64 %v
}

; Test 64-bit store
define void @store64(ptr %p, i64 %v) {
  store i64 %v, ptr %p
  ret void
}

; Test 64-bit load from array
define i64 @load64_array(ptr %p, i32 %idx) {
  %ptr = getelementptr i64, ptr %p, i32 %idx
  %v = load i64, ptr %ptr
  ret i64 %v
}

; Test 64-bit store to array
define void @store64_array(ptr %p, i32 %idx, i64 %v) {
  %ptr = getelementptr i64, ptr %p, i32 %idx
  store i64 %v, ptr %ptr
  ret void
}

; Test 64-bit volatile load
define i64 @load64_volatile(ptr %p) {
  %v = load volatile i64, ptr %p
  ret i64 %v
}

; Test 64-bit volatile store
define void @store64_volatile(ptr %p, i64 %v) {
  store volatile i64 %v, ptr %p
  ret void
}
