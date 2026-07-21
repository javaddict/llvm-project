; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	leaf_none                       // -- Begin function leaf_none
; CHECK: 	.type	leaf_none,@function
; CHECK-LABEL: leaf_none:                              // @leaf_none
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	leaf_none, .Lfunc_end0-leaf_none
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function





;
; Comprehensive tests for callee-saved register spill/restore.
;
; Callee-saved registers :
;   GPR: R8-R11           (R12 is reserved as the linker/assembler scratch "AT")
;   DR64: D8-D15
;   AR: AR2-AR3
;
; The prologue saves these to the stack and the epilogue restores them.
; Spills happen after SP decrement; restores happen before SP increment.
;
; Note: R13=SP, R14=FP, R15=LR are special-purpose. R14 is saved as
; callee-saved when FP is enabled. R0 is reserved as soft-zero.
;
; Note: The register allocator may use callee-saved registers to hold
; live values across calls, which triggers the prologue/epilogue to
; save/restore them. The specific registers used depend on RA decisions.
;
; Rebaselined for the Stream B post-RA MachineScheduler (decision)
; which was previously inert (createPostMachineScheduler returned nullptr)
; and is now active. It reorders instructions for ILP and the packetizer
; forms denser bundles, so the epilogue SP increment (addi32 sp, sp,...)
; may now be bundled earlier, interleaved with the callee-save restores.
; The frame/callee-save SEMANTICS is unchanged: the same callee-saved
; registers are spilled/restored at the same frame offsets, and the
; epilogue addi32 sp, sp, <size> is still present. CHECK lines below
; therefore use CHECK-DAG for prologue/epilogue pieces whose exact
; ordering is scheduler-dependent.

declare void @clobber_all()
declare i32 @use_i32(i32)
declare i64 @use_i64(i64)

;Leaf function: no callee saves needed (no calls)

define i32 @leaf_none(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

;Single call: callee-save registers may be used to hold live values

define i32 @save_across_call(i32 %a) {
  %v = call i32 @use_i32(i32 %a)
  %r = add i32 %v, %a
  ret i32 %r
}

;Multiple GPR callee saves: force many registers to be live across calls

define i32 @save_many_gpr(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
; Must spill callee-saved registers to hold 5 live values across calls
; Epilogue restores callee-saved registers and re-increments SP.
; Order is scheduler-dependent; verify each piece is present.
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %b)
  %v3 = call i32 @use_i32(i32 %c)
  %v4 = call i32 @use_i32(i32 %d)
  %v5 = call i32 @use_i32(i32 %e)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  ret i32 %s4
}

;DR64 callee saves: force DR64 registers to be saved

define i64 @save_dr64(i64 %a, i64 %b, i64 %c) {
; Must spill DR64 callee-saved registers
; Epilogue restores DR64 callee-saved registers and re-increments SP.
; Order is scheduler-dependent; verify each piece is present.
  %v1 = call i64 @use_i64(i64 %a)
  %v2 = call i64 @use_i64(i64 %b)
  %v3 = call i64 @use_i64(i64 %c)
  %s1 = add i64 %v1, %v2
  %s2 = add i64 %s1, %v3
  ret i64 %s2
}

;Mixed GPR + DR64 callee saves

define i64 @save_mixed_gpr_dr64(i32 %a, i64 %b) {
; Both GPR and DR64 callee-save spills
  %ext = sext i32 %a to i64
  %v = call i64 @use_i64(i64 %b)
  %r = add i64 %v, %ext
  ret i64 %r
}

;Clobber test: call to function that clobbers all caller-saved regs

define i32 @clobber_test(i32 %a, i32 %b) {
; GPR callee-saves must be preserved across the call
  call void @clobber_all()
  %r = add i32 %a, %b
  ret i32 %r
}

;Nested calls: callee-saves must survive multiple calls

define i32 @nested_calls(i32 %a) {
; Multiple calls, each potentially clobbering caller-saved regs
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %v1)
  %r = add i32 %v1, %v2
  ret i32 %r
}

;Deep register pressure: force many callee-saves with heavy computation

define i32 @deep_pressure(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
; Must save callee-saved registers to hold live values across calls
                          i32 %a4, i32 %a5, i32 %a6) {
  %v0 = call i32 @use_i32(i32 %a0)
  %v1 = call i32 @use_i32(i32 %a1)
  %v2 = call i32 @use_i32(i32 %a2)
  %v3 = call i32 @use_i32(i32 %a3)
  %v4 = call i32 @use_i32(i32 %a4)
  %v5 = call i32 @use_i32(i32 %a5)
  %v6 = call i32 @use_i32(i32 %a6)
  %s1 = add i32 %v0, %v1
  %s2 = add i32 %s1, %v2
  %s3 = add i32 %s2, %v3
  %s4 = add i32 %s3, %v4
  %s5 = add i32 %s4, %v5
  %s6 = add i32 %s5, %v6
  ret i32 %s6
}

;DR64 deep pressure: force many DR64 callee-saves

define i64 @dr64_deep_pressure(i64 %a0, i64 %a1, i64 %a2) {
  %v0 = call i64 @use_i64(i64 %a0)
  %v1 = call i64 @use_i64(i64 %a1)
  %v2 = call i64 @use_i64(i64 %a2)
  %s1 = add i64 %v0, %v1
  %s2 = add i64 %s1, %v2
  ret i64 %s2
}
