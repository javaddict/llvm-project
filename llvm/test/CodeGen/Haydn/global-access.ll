; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;
; Test global variable access patterns.
; Globals are accessed via LUI+ADDI32 to materialize the address, then
; LD32/ST32 for the data. This test covers load, store, and GEP on globals.










; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	load_global                     // -- Begin function load_global
; CHECK: 	.type	load_global,@function
; CHECK: load_global:                            // @load_global
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, g_int }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_int }
; CHECK: 	{ nop; ld32	r1, r1, 0; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	load_global, .Lfunc_end0-load_global
; CHECK:                                         // -- End function
; CHECK: 	.globl	store_global                    // -- Begin function store_global
; CHECK: 	.type	store_global,@function
; CHECK: store_global:                           // @store_global
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r2, g_int }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, g_int }
; CHECK: 	{ nop; nop; st32	r1, r2, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	store_global, .Lfunc_end1-store_global
; CHECK:                                         // -- End function
; CHECK: 	.globl	gep_global                      // -- Begin function gep_global
; CHECK: 	.type	gep_global,@function
; CHECK: gep_global:                             // @gep_global
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, g_int }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_int }
; CHECK: 	{ addi32	r1, r1, 20; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	gep_global, .Lfunc_end2-gep_global
; CHECK:                                         // -- End function
; CHECK: 	.globl	load_global_i64                 // -- Begin function load_global_i64
; CHECK: 	.type	load_global_i64,@function
; CHECK: load_global_i64:                        // @load_global_i64
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; lui	r1, g_long }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_long }
; CHECK: 	{ nop; ld32	r1, r1, 0; addi32_w	r2, r1, 4 }
; CHECK: 	{ nop; ld32	r2, r2, 0; nop }
; CHECK: 	{ nop; nop; st32	r1, sp, 0 }
; CHECK: 	{ nop; nop; st32	r2, sp, 4 }
; CHECK: 	{ nop; ld64	d0, sp, 0; addi32_w	sp, sp, 8 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	load_global_i64, .Lfunc_end3-load_global_i64
; CHECK:                                         // -- End function
; CHECK: 	.globl	store_global_i64                // -- Begin function store_global_i64
; CHECK: 	.type	store_global_i64,@function
; CHECK: store_global_i64:                       // @store_global_i64
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, g_long }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_long }
; CHECK: 	{ d_sw_l_with_imm	d0, r1, 0; nop; addi32_w	r2, r1, 4 }
; CHECK: 	{ d_sw_h_with_imm	d0, r2, 0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	store_global_i64, .Lfunc_end4-store_global_i64
; CHECK:                                         // -- End function
; CHECK: 	.globl	rmw_global                      // -- Begin function rmw_global
; CHECK: 	.type	rmw_global,@function
; CHECK: rmw_global:                             // @rmw_global
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, g_int }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_int }
; CHECK: 	{ nop; ld32	r2, r1, 0; nop }
; CHECK: 	{ nop; nop; nop }
; CHECK: 	{ addi32	r2, r2, 1; nop; nop }
; CHECK: 	{ nop; nop; st32	r2, r1, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	rmw_global, .Lfunc_end5-rmw_global
; CHECK:                                         // -- End function
; CHECK: 	.globl	two_globals                     // -- Begin function two_globals
; CHECK: 	.type	two_globals,@function
; CHECK: two_globals:                            // @two_globals
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, g_int }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, g_int }
; CHECK: 	{ nop; ld32	r1, r1, 0; lui	r2, g_int2 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, g_int2 }
; CHECK: 	{ nop; ld32	r2, r2, 0; nop }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	two_globals, .Lfunc_end6-two_globals
; CHECK:                                         // -- End function
; CHECK: 	.globl	load_local_const                // -- Begin function load_local_const
; CHECK: 	.type	load_local_const,@function
; CHECK: load_local_const:                       // @load_local_const
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r1, local_const }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, local_const }
; CHECK: 	{ nop; ld32	r1, r1, 0; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	load_local_const, .Lfunc_end7-load_local_const
; CHECK:                                         // -- End function
; CHECK: 	.globl	load_global_array               // -- Begin function load_global_array
; CHECK: 	.type	load_global_array,@function
; CHECK: load_global_array:                      // @load_global_array
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ slli32	r1, r1, 2; nop; lui	r2, g_array }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, g_array }
; CHECK: 	{ s_lw_pre_reg	r1, r2, r1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	load_global_array, .Lfunc_end8-load_global_array
; CHECK:                                         // -- End function
; CHECK: 	.type	local_const,@object             // @local_const
; CHECK: 	.section	.rodata,"a",@progbits
; CHECK: 	.globl	local_const
; CHECK: 	.p2align	2, 0x0
; CHECK: local_const:
; CHECK: 	.long	42                              // 0x2a
; CHECK: 	.size	local_const, 4
; CHECK: 	.section	".note.GNU-stack","",@progbits

@g_int = external global i32
@g_int2 = external global i32
@g_long = external global i64

declare void @use_i32(i32)

;Load from global
define i32 @load_global() nounwind {
  %v = load i32, ptr @g_int
  ret i32 %v
}

;Store to global
define void @store_global(i32 %v) nounwind {
  store i32 %v, ptr @g_int
  ret void
}

;GEP on global (pointer arithmetic)
define ptr @gep_global() nounwind {
  %p = getelementptr i32, ptr @g_int, i32 5
  ret ptr %p
}

;Load i64 from global
define i64 @load_global_i64() nounwind {
  %v = load i64, ptr @g_long
  ret i64 %v
}

;Store i64 to global
define void @store_global_i64(i64 %v) nounwind {
  store i64 %v, ptr @g_long
  ret void
}

;Load-modify-store on global
define void @rmw_global() nounwind {
  %old = load i32, ptr @g_int
  %new = add i32 %old, 1
  store i32 %new, ptr @g_int
  ret void
}

;Access two globals in same function
define i32 @two_globals() nounwind {
  %v1 = load i32, ptr @g_int
  %v2 = load i32, ptr @g_int2
  %sum = add i32 %v1, %v2
  ret i32 %sum
}

;Local constant global
@local_const = constant i32 42

define i32 @load_local_const() nounwind {
  %v = load i32, ptr @local_const
  ret i32 %v
}

;Array global
@g_array = external global [10 x i32]

define i32 @load_global_array(i32 %idx) nounwind {
  %p = getelementptr [10 x i32], ptr @g_array, i32 0, i32 %idx
  %v = load i32, ptr %p
  ret i32 %v
}
