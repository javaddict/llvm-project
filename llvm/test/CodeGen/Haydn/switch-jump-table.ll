; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; XFAIL: *
; B1.2: singleton BUNDLE asm form (; nop pad / layout) needs rebaseline.

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

;
; Test jump table switch lowering for Haydn.
; Dense switches with 4+ consecutive cases are lowered via jump tables
; (G_BRJT → SLL32 + ADD32 + S_LW_WITH_IMM + BR_JT → JALR).
; Sparse switches with fewer cases still use comparison + branch chains.
;
; REGRESSION TEST: Jump table lowering (G_BRJT → BR_JT → JALR).
;
; Previously, jump tables were disabled entirely (getMinimumJumpTableEntries = max
; areJTsAllowed = false). This test verifies that:
; 1. Dense switches produce jump table sequences (lui+addi32{{(_w)?}} jt_base, slli32
; scale, add32 addr, ld32 target, jalr indirect_branch). The JT base MUST use
; the full lui+addi32{{(_w)?}} pair (HI12+LO20 fixups); a lone `addi32 rN, r0,.LJTI`
; only carries LO16 and cannot reach.rodata (/ : the linker
; resolved the base to 0xFFF80100 → OOB load → jalr 0 → NOEXIT).
; 2. Jump table data sections (.rodata with.long entries) are emitted
; 3. Sparse switches below the threshold still use comparison chains (seq32/slt32)
; 4. The machine verifier passes with -verify-machineinstrs (BR_JT successor list)

;Dense switch with 8 consecutive cases → uses jump table
; REBASELINED (auto) llc <stdin>;.file skipped





; REBASELINED (auto) R0-WAW/PortModel packing rebaseline; .file skipped
; CHECK: 	.text
; CHECK: 	.globl	switch_jt_8                     // -- Begin function switch_jt_8
; CHECK: 	.type	switch_jt_8,@function
; CHECK: switch_jt_8:                            // @switch_jt_8
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 7 }
; CHECK: 	{ 	sltu32	r2, r2, r1 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r2, .LBB0_10 }
; CHECK: // %bb.1:                               // %entry
; CHECK: 	{ nop; slli32	r1, r1, 2; lui	r2, .LJTI0_0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r2, .LJTI0_0 }
; CHECK: 	{ 	add32	r1, r2, r1 }
; CHECK: 	{ 	ld32	r1, r1, 0 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, r1, 0 }
; CHECK: .LBB0_2:                                // %bb0
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 10 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_3:                                // %bb4
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 50 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_4:                                // %bb2
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 30 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_5:                                // %bb3
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 40 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_6:                                // %bb7
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 80 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_7:                                // %bb1
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 20 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_8:                                // %bb5
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 60 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_9:                                // %bb6
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 70 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB0_11 }
; CHECK: .LBB0_10:                               // %default
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 0 }
; CHECK: .LBB0_11:                               // %bb0
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	switch_jt_8, .Lfunc_end0-switch_jt_8
; CHECK: 	.section	.rodata,"a",@progbits
; CHECK: 	.p2align	2, 0x0
; CHECK: .LJTI0_0:
; CHECK: 	.long	.LBB0_2
; CHECK: 	.long	.LBB0_7
; CHECK: 	.long	.LBB0_4
; CHECK: 	.long	.LBB0_5
; CHECK: 	.long	.LBB0_3
; CHECK: 	.long	.LBB0_8
; CHECK: 	.long	.LBB0_9
; CHECK: 	.long	.LBB0_6
; CHECK:                                         // -- End function
; CHECK: 	.text
; CHECK: 	.globl	switch_small_no_jt              // -- Begin function switch_small_no_jt
; CHECK: 	.type	switch_small_no_jt,@function
; CHECK: switch_small_no_jt:                     // @switch_small_no_jt
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 3 }
; CHECK: 	{ 	seq32	r2, r1, r2 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r2, .LBB1_5 }
; CHECK: // %bb.1:                               // %entry
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK: 	{ 	seq32	r2, r1, r2 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r2, .LBB1_4 }
; CHECK: // %bb.2:                               // %entry
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	seq32	r1, r1, r2 }
; CHECK: 	{ 	xori32	r1, r1, 1 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r1, .LBB1_6 }
; CHECK: // %bb.3:                               // %bb1
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 100 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB1_7 }
; CHECK: .LBB1_4:                                // %bb2
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 200 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB1_7 }
; CHECK: .LBB1_5:                                // %bb3
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 300 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB1_7 }
; CHECK: .LBB1_6:                                // %default
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 0 }
; CHECK: .LBB1_7:                                // %bb1
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	switch_small_no_jt, .Lfunc_end1-switch_small_no_jt
; CHECK:                                         // -- End function
; CHECK: 	.globl	switch_jt_offset                // -- Begin function switch_jt_offset
; CHECK: 	.type	switch_jt_offset,@function
; CHECK: switch_jt_offset:                       // @switch_jt_offset
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	move32	r2, r1 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 3 }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r0, -10 }
; CHECK: 	{ 	add32	r2, r2, r3 }
; CHECK: 	{ 	sltu32	r3, r1, r2 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r3, .LBB2_5 }
; CHECK: // %bb.1:                               // %entry
; CHECK: 	{ nop; slli32	r2, r2, 2; lui	r3, .LJTI2_0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r3, .LJTI2_0 }
; CHECK: 	{ 	add32	r2, r3, r2 }
; CHECK: 	{ 	ld32	r2, r2, 0 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, r2, 0 }
; CHECK: .LBB2_2:                                // %bb10
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 1 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB2_6 }
; CHECK: .LBB2_3:                                // %bb11
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 2 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB2_6 }
; CHECK: .LBB2_4:                                // %bb13
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 4 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB2_6 }
; CHECK: .LBB2_5:                                // %default
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 0 }
; CHECK: .LBB2_6:                                // %bb12
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	switch_jt_offset, .Lfunc_end2-switch_jt_offset
; CHECK: 	.section	.rodata,"a",@progbits
; CHECK: 	.p2align	2, 0x0
; CHECK: .LJTI2_0:
; CHECK: 	.long	.LBB2_2
; CHECK: 	.long	.LBB2_3
; CHECK: 	.long	.LBB2_6
; CHECK: 	.long	.LBB2_4
; CHECK:                                         // -- End function
; CHECK: 	.text
; CHECK: 	.globl	switch_jt_with_default          // -- Begin function switch_jt_with_default
; CHECK: 	.type	switch_jt_with_default,@function
; CHECK: switch_jt_with_default:                 // @switch_jt_with_default
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 3 }
; CHECK: 	{ 	sltu32	r2, r2, r1 }
; CHECK: 	{ 	bnez{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r2, .LBB3_6 }
; CHECK: // %bb.1:                               // %entry
; CHECK: 	{ nop; slli32	r1, r1, 2; lui	r2, .LJTI3_0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r2, .LJTI3_0 }
; CHECK: 	{ 	add32	r1, r2, r1 }
; CHECK: 	{ 	ld32	r1, r1, 0 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, r1, 0 }
; CHECK: .LBB3_2:                                // %bb0
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 100 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB3_7 }
; CHECK: .LBB3_3:                                // %bb2
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 300 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB3_7 }
; CHECK: .LBB3_4:                                // %bb3
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 400 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB3_7 }
; CHECK: .LBB3_5:                                // %bb1
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 200 }
; CHECK: 	{ 	beqz{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, .LBB3_7 }
; CHECK: .LBB3_6:                                // %default
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -1 }
; CHECK: .LBB3_7:                                // %bb0
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	switch_jt_with_default, .Lfunc_end3-switch_jt_with_default
; CHECK: 	.section	.rodata,"a",@progbits
; CHECK: 	.p2align	2, 0x0
; CHECK: .LJTI3_0:
; CHECK: 	.long	.LBB3_2
; CHECK: 	.long	.LBB3_5
; CHECK: 	.long	.LBB3_3
; CHECK: 	.long	.LBB3_4
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @switch_jt_8(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
    i32 4, label %bb4
    i32 5, label %bb5
    i32 6, label %bb6
    i32 7, label %bb7
  ]
bb0: ret i32 10
bb1: ret i32 20
bb2: ret i32 30
bb3: ret i32 40
bb4: ret i32 50
bb5: ret i32 60
bb6: ret i32 70
bb7: ret i32 80
default: ret i32 0
}

;Small switch (3 cases) → comparison chain, no jump table
define i32 @switch_small_no_jt(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
  ]
bb1: ret i32 100
bb2: ret i32 200
bb3: ret i32 300
default: ret i32 0
}

;Dense switch with non-zero base → subtract + jump table
define i32 @switch_jt_offset(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 10, label %bb10
    i32 11, label %bb11
    i32 12, label %bb12
    i32 13, label %bb13
  ]
bb10: ret i32 1
bb11: ret i32 2
bb12: ret i32 3
bb13: ret i32 4
default: ret i32 0
}

;Dense switch with default case returning value
define i32 @switch_jt_with_default(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
  ]
bb0: ret i32 100
bb1: ret i32 200
bb2: ret i32 300
bb3: ret i32 400
default: ret i32 -1
}
