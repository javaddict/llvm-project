; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

;
; Test baremetal _start function and basic runtime support.
; This verifies that the CodeGen can handle the _start entry point
; and that basic operations work for baremetal programs.

;_start function (entry point)


; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	_start                          // -- Begin function _start
; CHECK: 	.type	_start,@function
; CHECK: _start:                                 // @_start
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal	lr, main }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	_start, .Lfunc_end0-_start
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	main                            // -- Begin function main
; CHECK: 	.type	main,@function
; CHECK: main:                                   // @main
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 42 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	main, .Lfunc_end1-main
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	use_global                      // -- Begin function use_global
; CHECK: 	.type	use_global,@function
; CHECK: use_global:                             // @use_global
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, global_val }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, global_val }
; CHECK: 	{ nop; ld32	r1, r1, 0; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	use_global, .Lfunc_end2-use_global
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	use_zero                        // -- Begin function use_zero
; CHECK: 	.type	use_zero,@function
; CHECK: use_zero:                               // @use_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, zero_val }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, zero_val }
; CHECK: 	{ nop; ld32	r1, r1, 0; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	use_zero, .Lfunc_end3-use_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.type	global_val,@object              // @global_val
; CHECK: 	.data
; CHECK: 	.globl	global_val
; CHECK: 	.p2align	2, 0x0
; CHECK: global_val:
; CHECK: 	.long	123                             // 0x7b
; CHECK: 	.size	global_val, 4
; CHECK: 	.type	zero_val,@object                // @zero_val
; CHECK: 	.section	.bss,"aw",@nobits
; CHECK: 	.globl	zero_val
; CHECK: 	.p2align	2, 0x0
; CHECK: zero_val:
; CHECK: 	.long	0                               // 0x0
; CHECK: 	.size	zero_val, 4
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @_start() {
entry:
  %retval = call i32 @main()
  ret void
}

;main function
define i32 @main() {
entry:
  ret i32 42
}

;Test that global data works
@global_val = global i32 123, align 4

define i32 @use_global() {
entry:
  %val = load i32, ptr @global_val, align 4
  ret i32 %val
}

;Test BSS-style zero-initialized data
@zero_val = global i32 0, align 4

define i32 @use_zero() {
entry:
  %val = load i32, ptr @zero_val, align 4
  ret i32 %val
}
