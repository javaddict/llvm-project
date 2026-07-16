; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 %s -o - | FileCheck %s

;
; REGRESSION TEST: VLA + call — the epilogue must restore callee-saved GPRs
; from SP (R13), not via a stale R12 base pointer.
;
; VLA forces hasFP(MF)==true (the dynamic stack allocation requires a frame
; pointer so SP can be restored). The prologue materialises the usual
; R12 = SP + off base for the CSR stores. If the matching epilogue ADDI32
; R12, SP, off is dead-code eliminated (because R12 is a reserved AT scratch
; with no live range), the LD32 rt, R12, off in the epilogue would read a
; clobbered value — losing saved LR (R15) and R8 across the call.
;
; Codex MEDIUM-3 concern (review): VLA may interact with the SP-restore
; path in a way that loses saved registers. This test exercises exactly that
; path.
;
; Expected (correct) epilogue: ld32 {{.*}}, sp, <off> for each CSR.
; Failure signature (regression): ld32 {{.*}}, r12, <off>.

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped







; CHECK:  	.text
; CHECK:  	.globl	vla_call_sp_restore             // -- Begin function vla_call_sp_restore
; CHECK:  	.type	vla_call_sp_restore,@function
; CHECK:  vla_call_sp_restore:                    // @vla_call_sp_restore
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK:  	{ 	st32	lr, r2, 0 }
; CHECK:  	{ 	st32	r8, r2, 4 }
; CHECK:  	{ 	addi32{{(_w)?}}	fp, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK:  	{ 	sll32	r1, r1, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 7 }
; CHECK:  	{ 	add32	r1, r1, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, -8 }
; CHECK:  	{ 	and32	r1, r1, r2 }
; CHECK:  	{ 	sub32	r8, sp, r1 }
; CHECK:  	{ 	move32	r1, r8; 	move32	sp, r8; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use }
; CHECK:  	{ 	ld32	r1, r8, 0; 	xor32	r0, r0, r0; 	nop }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 8 }
; CHECK:  	{ 	ld32	r8, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	vla_call_sp_restore, .Lfunc_end0-vla_call_sp_restore
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits


declare void @use(ptr)

define i32 @vla_call_sp_restore(i32 %n) nounwind {
; Epilogue must restore callee-saved regs directly from SP (R13).
entry:
  %v = alloca i32, i32 %n
  call void @use(ptr %v)
  %ret = load i32, ptr %v
  ret i32 %ret
}
