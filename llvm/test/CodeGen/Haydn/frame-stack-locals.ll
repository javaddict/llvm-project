; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; NOTE: updated for VLIW slot-1 load promotion — independent loads now pack as ld32+ld32.
; NOTE: CHECKs reflect post- scheduled output (prologue/epilogue slot order varies per frame).
; NOTE: rebaselined for Stream B post-RA MachineScheduler — the scheduler reorders
; NOTE: instructions for ILP and forms denser bundles. Stack-local semantics (slots, offsets
; NOTE: load/store widths) are unchanged; only bundle layout / instruction order varies. CHECKs
; NOTE: use CHECK-DAG where order is scheduler-dependent.
;
; Tests for stack frame layout with locals, spills, and calls.
;
; Frame layout (stack grows down):
; [high address]
; incoming stack arguments
; saved R15 (LR) if needed
; saved callee-saved registers (R8-R11, D8-D15)
; local variables (allocas, spills)
; SP points here (low address)
;
; Prologue: decrement SP by frame size
; Epilogue: increment SP by frame size

; REBASELINED (auto) dual-sched / AR logical-slot rebaseline;.file skipped

; CHECK: 	.text
; CHECK: 	.globl	one_local                       // -- Begin function one_local
; CHECK: 	.type	one_local,@function
; CHECK: one_local:                              // @one_local
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 16 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 12 }
; CHECK: 	{ 	st32	r1, r2, 0 }
; CHECK: 	{ 	ld32	r1, r2, 0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	one_local, .Lfunc_end0-one_local
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	three_locals                    // -- Begin function three_locals
; CHECK: 	.type	three_locals,@function
; CHECK: three_locals:                           // @three_locals
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 24 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	{ 	addi32{{(_w)?}}	r4, sp, 20 }
; CHECK: 	{ 	addi32{{(_w)?}}	r5, sp, 16 }
; CHECK: 	{ 	st32	r1, r4, 0 }
; CHECK: 	{ 	st32	r2, r5, 0; 	ld32	r1, r4, 0; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r6, sp, 12; 	ld32	r2, r5, 0; 	nop }
; CHECK: 	{ 	st32	r3, r6, 0; 	add32	r1, r1, r2; 	nop }
; CHECK: 	{ 	ld32	r3, r6, 0 }
; CHECK: 	{ 	add32	r1, r1, r3 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	three_locals, .Lfunc_end1-three_locals
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	local_with_call                 // -- Begin function local_with_call
; CHECK: 	.type	local_with_call,@function
; CHECK: local_with_call:                        // @local_with_call
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 16 }
; CHECK: 	{ 	st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK: 	{ 	st32	r1, r2, 0 }
; CHECK: 	{ 	ld32	r1, r2, 0 }
; CHECK: 	{ 	jal_w	lr, use_i32 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	lr, sp, 12 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	local_with_call, .Lfunc_end2-local_with_call
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	mixed_locals                    // -- Begin function mixed_locals
; CHECK: 	.type	mixed_locals,@function
; CHECK: mixed_locals:                           // @mixed_locals
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 24 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	{ 	addi32{{(_w)?}}	r3, sp, 12 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 20; 	d_sw_l_with_imm	d0, r3, 0; 	subi32	sp, sp, 8 }
; CHECK: 	{ 	addi32{{(_w)?}}	r4, r3, 4 }
; CHECK: 	{ 	st32	r1, r2, 0; 	d_sw_h_with_imm	d0, r4, 0; 	nop }
; CHECK: 	{ 	ld32	r1, r2, 0; 	ld32	r2, r3, 0; 	nop }
; CHECK: 	{ 	ld32	r3, r4, 0; 	sext32t64	d1, r1; 	nop }
; CHECK: 	{ 	st32	r2, sp, 0 }
; CHECK: 	{ 	st32	r3, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	add64	d0, d1, d0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	mixed_locals, .Lfunc_end3-mixed_locals
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	array_local                     // -- Begin function array_local
; CHECK: 	.type	array_local,@function
; CHECK: array_local:                            // @array_local
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 24 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK: 	{ 	st32	r1, r2, 0 }
; CHECK: 	{ 	ld32	r1, r2, 0 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	array_local, .Lfunc_end4-array_local
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	full_frame                      // -- Begin function full_frame
; CHECK: 	.type	full_frame,@function
; CHECK: full_frame:                             // @full_frame
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 32 }
; CHECK: 	{ 	st32	lr, sp, 28 }
; CHECK: 	.cfi_def_cfa_offset 32
; CHECK: 	.cfi_offset lr, 28
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 24; 	subi32	sp, sp, 16; 	nop }
; CHECK: 	{ 	st32	r1, r2, 0 }
; CHECK: 	{ 	ld32	r1, r2, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK: 	{ 	addi32{{(_w)?}}	r4, r0, 4 }
; CHECK: 	{ 	addi32{{(_w)?}}	r5, r0, 5 }
; CHECK: 	{ 	addi32{{(_w)?}}	r6, r0, 6 }
; CHECK: 	{ 	addi32{{(_w)?}}	r7, r0, 7 }
; CHECK: 	{ 	addi32{{(_w)?}}	r12, r0, 8 }
; CHECK: 	{ 	addi32{{(_w)?}}	fp, r0, 9 }
; CHECK: 	{ 	st32	r12, sp, 0 }
; CHECK: 	{ 	st32	fp, sp, 8 }
; CHECK: 	{ 	jal_w	lr, many_params }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	lr, sp, 28 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	full_frame, .Lfunc_end5-full_frame
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	nested_call_chain               // -- Begin function nested_call_chain
; CHECK: 	.type	nested_call_chain,@function
; CHECK: nested_call_chain:                      // @nested_call_chain
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 24 }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, sp, 12 }
; CHECK: 	{ 	st32	lr, r3, 0 }
; CHECK: 	{ 	st32	r9, r3, 4 }
; CHECK: 	{ 	st32	r8, r3, 8 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	.cfi_offset r8, 20
; CHECK: 	.cfi_offset r9, 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ 	move32	r8, r2 }
; CHECK: 	{ 	jal_w	lr, use_i32 }
; CHECK: 	{ 	move32	r9, r1; 	move32	r1, r8; 	xor32	r0, r0, r0 }
; CHECK: 	{ 	jal_w	lr, use_i32 }
; CHECK: 	{ 	xor32	r0, r0, r0; 	add32	r1, r9, r1; 	nop }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	lr, sp, 12 }
; CHECK: 	{ 	ld32	r9, sp, 16 }
; CHECK: 	{ 	ld32	r8, sp, 20 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	nested_call_chain, .Lfunc_end6-nested_call_chain
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	pass_stack_ptr                  // -- Begin function pass_stack_ptr
; CHECK: 	.type	pass_stack_ptr,@function
; CHECK: pass_stack_ptr:                         // @pass_stack_ptr
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 16 }
; CHECK: 	{ 	st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK: 	{ 	st32	r1, r2, 0 }
; CHECK: 	{ 	move32	r1, r2 }
; CHECK: 	{ 	jal_w	lr, use_ptr }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	ld32	lr, sp, 12 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK: 	{ 	nop }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	pass_stack_ptr, .Lfunc_end7-pass_stack_ptr
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

declare void @use_ptr(ptr)
declare i32 @use_i32(i32)
declare i64 @use_i64(i64)

;Simple local variable (one i32 alloca)

define i32 @one_local(i32 %x) {
; Stack allocation for local
; (SFR-strip) changed bundle layout (denser packing) — the SP restore may
; be hoisted above the local st32/ld32 (anti-dep); use CHECK-DAG. Rebaselined.
; NOTE: lone load may be promoted to ld32 (slot-1 over-promotion,); both
; ld32 and ld32 are semantically identical loads.
  %p = alloca i32
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

;Multiple local variables (3 i32 allocas)

define i32 @three_locals(i32 %a, i32 %b, i32 %c) {
; 3 x i32 = 12 bytes, rounded to 16 (8-byte alignment)
; (post-RA scheduler) hoists the SP restore above the st32/ld32 (anti-dep on sp); the
; stores/loads use pre-computed address registers, so semantics are unchanged. CHECK-DAG.
  %p1 = alloca i32
  %p2 = alloca i32
  %p3 = alloca i32
  store i32 %a, ptr %p1
  store i32 %b, ptr %p2
  store i32 %c, ptr %p3
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %p2
  %v3 = load i32, ptr %p3
  %s = add i32 %v1, %v2
  %r = add i32 %s, %v3
  ret i32 %r
}

;Local with call (callee-save spill + local)

define i32 @local_with_call(i32 %a) {
; Stack for local + potential callee-save spills
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @use_i32(i32 %v)
  ret i32 %r
}

;Mixed i32 and i64 locals

define i64 @mixed_locals(i32 %a, i64 %b) {
; Both i32 and i64 allocas
; (SFR-strip) changed bundle layout (denser packing) — SP restore hoisted; CHECK-DAG. Rebaselined.
  %p32 = alloca i32
  %p64 = alloca i64
  store i32 %a, ptr %p32
  store i64 %b, ptr %p64
  %v32 = load i32, ptr %p32
  %v64 = load i64, ptr %p64
  %ext = sext i32 %v32 to i64
  %r = add i64 %ext, %v64
  ret i64 %r
}

;Array local: [4 x i32] = 16 bytes

define i32 @array_local(i32 %a) {
; (SFR-strip) changed bundle layout (denser packing) — SP restore hoisted; CHECK-DAG. Rebaselined.
  %arr = alloca [4 x i32], align 8
  %p = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

;Stack frame with locals, callee-save spills, AND outgoing args

declare i32 @many_params(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @full_frame(i32 %a) {
; Stack for: local + callee-save spills + outgoing stack arg
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @many_params(i32 %v, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret i32 %r
}

;Nested calls with parameter passing

define i32 @nested_call_chain(i32 %a, i32 %b) {
; First call result feeds into second call
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %b)
  %r = add i32 %v1, %v2
  ret i32 %r
}

;Pointer to local stack variable passed to callee

define void @pass_stack_ptr(i32 %a) {
  %p = alloca i32
  store i32 %a, ptr %p
  call void @use_ptr(ptr %p)
  ret void
}
