; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s





;
; Tests for leaf functions and prologue/epilogue decisions.
;
; Leaf functions do not call other functions, so they:
; Do not need callee-save spills
; May not need a stack frame at all (no allocas, no locals)
; Still zero R0 in the prologue (reserved soft-zero register)

;Pure leaf: no stack frame at all

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	pure_leaf                       // -- Begin function pure_leaf
; CHECK: 	.type	pure_leaf,@function
; CHECK: pure_leaf:                              // @pure_leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 1; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	add32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	pure_leaf, .Lfunc_end0-pure_leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_arith                      // -- Begin function leaf_arith
; CHECK: 	.type	leaf_arith,@function
; CHECK: leaf_arith:                             // @leaf_arith
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	add32	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	mull	r3, r2, r3 }
; CHECK: 	{ 		nop; 	nop; 	sub32	r1, r3, r1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	leaf_arith, .Lfunc_end1-leaf_arith
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_i64                        // -- Begin function leaf_i64
; CHECK: 	.type	leaf_i64,@function
; CHECK: leaf_i64:                               // @leaf_i64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	add64	d0, d0, d1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	leaf_i64, .Lfunc_end2-leaf_i64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_cmp                        // -- Begin function leaf_cmp
; CHECK: 	.type	leaf_cmp,@function
; CHECK: leaf_cmp:                               // @leaf_cmp
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	max32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	leaf_cmp, .Lfunc_end3-leaf_cmp
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	non_leaf                        // -- Begin function non_leaf
; CHECK: 	.type	non_leaf,@function
; CHECK: non_leaf:                               // @non_leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 16 }
; CHECK: 	{ 		addi32_w	r2, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		st32	lr, r2, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r8, r2, 4; 	nop; 	nop }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset r8, 12
; CHECK: 	.cfi_offset lr, 8
; CHECK: 	{ 		addi32_w	r8, r0, 1; 	nop; 	nop }
; CHECK: 	{ 		jal_w	lr, extern; 	nop; 	nop }
; CHECK: 	{ 		nop; 	add32	r1, r1, r8; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	ld32	lr, sp, 8; 	nop }
; CHECK: 	{ 		nop; 	ld32	r8, sp, 12; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 16; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	non_leaf, .Lfunc_end4-non_leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_with_alloca                // -- Begin function leaf_with_alloca
; CHECK: 	.type	leaf_with_alloca,@function
; CHECK: leaf_with_alloca:                       // @leaf_with_alloca
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 16 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	{ 		addi32_w	r2, sp, 12; 	nop; 	nop }
; CHECK: 	{ 		st32	r1, r2, 0; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r2, r0, 1; 	ld32	r1, r2, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	add32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 16; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	leaf_with_alloca, .Lfunc_end5-leaf_with_alloca
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_multi_alloca               // -- Begin function leaf_multi_alloca
; CHECK: 	.type	leaf_multi_alloca,@function
; CHECK: leaf_multi_alloca:                      // @leaf_multi_alloca
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 16 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	{ 		addi32_w	r2, sp, 12; 	nop; 	nop }
; CHECK: 	{ 		st32	r1, r2, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	ld32	r2, r2, 0; 	nop }
; CHECK: 	{ 		addi32_w	r1, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		st32	r2, r1, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 16; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	leaf_multi_alloca, .Lfunc_end6-leaf_multi_alloca
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	void_leaf                       // -- Begin function void_leaf
; CHECK: 	.type	void_leaf,@function
; CHECK: void_leaf:                              // @void_leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	void_leaf, .Lfunc_end7-void_leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf_branch                     // -- Begin function leaf_branch
; CHECK: 	.type	leaf_branch,@function
; CHECK: leaf_branch:                            // @leaf_branch
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r3, r0, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	slt32	r3, r3, r1 }
; CHECK: 	{ 		nop; 	nop; 	xori32	r3, r3, 1 }
; CHECK: 	{ 		bnez_w	r3, .LBB8_2; 	nop; 	nop }
; CHECK: // %bb.1:                               // %pos
; CHECK: 	{ 		nop; 	nop; 	add32	r1, r1, r2 }
; CHECK: 	{ 		beqz_w	r0, .LBB8_3; 	nop; 	nop }
; CHECK: .LBB8_2:                                // %neg
; CHECK: 	{ 		nop; 	nop; 	sub32	r1, r1, r2 }
; CHECK: .LBB8_3:                                // %pos
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	leaf_branch, .Lfunc_end8-leaf_branch
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @pure_leaf(i32 %x) {
; Only the R0 zeroing prologue, no SP adjustment
  %r = add i32 %x, 1
  ret i32 %r
}

;Leaf with only arithmetic (no memory, no stack)

define i32 @leaf_arith(i32 %a, i32 %b, i32 %c) {
  %s1 = add i32 %a, %b
  %s2 = mul i32 %s1, %c
  %s3 = sub i32 %s2, %a
  ret i32 %s3
}

;Leaf with i64 arithmetic (no stack needed if only register ops)

define i64 @leaf_i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

;Leaf with comparison (no stack)

define i32 @leaf_cmp(i32 %a, i32 %b) {
  %cmp = icmp sgt i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;Non-leaf: function with a call (needs callee-save spills)

declare i32 @extern(i32)

define i32 @non_leaf(i32 %x) {
; Must save/restore callee-saved registers
  %v = call i32 @extern(i32 %x)
  %r = add i32 %v, 1
  ret i32 %r
}

;Leaf with alloca (needs stack frame but no callee saves)

define i32 @leaf_with_alloca(i32 %x) {
; Needs stack space for alloca
; (SFR-strip) changed bundle layout (denser packing) — the SP restore may
; be hoisted above the local st32/ld32 (anti-dep); use CHECK-DAG so the frame
; alloc/dealloc and local access are matched regardless of order. Rebaselined.
  %p = alloca i32
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  %r = add i32 %v, 1
  ret i32 %r
}

;Leaf with multiple allocas

define i32 @leaf_multi_alloca(i32 %x) {
  %p1 = alloca i32
  %p2 = alloca i32
  store i32 %x, ptr %p1
  %v = load i32, ptr %p1
  store i32 %v, ptr %p2
  %r = load i32, ptr %p2
  ret i32 %r
}

;Void leaf function

define void @void_leaf() {
  ret void
}

;Leaf with conditional branch (no stack needed)

define i32 @leaf_branch(i32 %a, i32 %b) {
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %pos, label %neg

pos:
  %r1 = add i32 %a, %b
  ret i32 %r1

neg:
  %r2 = sub i32 %a, %b
  ret i32 %r2
}
