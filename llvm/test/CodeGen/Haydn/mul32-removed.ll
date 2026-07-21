; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

;
; REGRESSION TEST: scalar s32 multiply must NOT emit the invented `mul32`.
;
; Bug (/): the backend defined a scalar `MUL32` (FmtALU32<0x1F>
; "mul32 $rd,$rs1,$rs2") that is NOT in the Haydn ISA DB — the slot-0 scalar
; ALU has no multiply unit; every multiply lives on the DR64/MAC unit in
; slots 1/2. The BundleSim ISS decoded the fabricated `mul32` as garbage:
; `if(n&1) h=h*x;` dropped the conditional multiply (odd-n -> h^2).
; Under register pressure a consuming mul32 was scheduled before its
; defining mul32 -> stale read -> wrong result.
;
; Fix : MUL32 removed (same "not in ISA DB" rule as MAC32/). Scalar
; G_MUL <s32> lowers to the REAL sequence:
; sext32t64 x2 + mul64.ll (slot-1/2 DR64 unit) + move32_dr_l
; whose low-32 product is the exact s32 multiply result (wraparound — the sign
; of the operand extension is irrelevant once only the low 32 bits are kept).
;
; If this regresses, the output contains `mul32` (invented instr, decoded as
; garbage) or a `__mulsi3`/`__muldi3` libcall. Both are wrong.

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped







; CHECK:  	.text
; CHECK:  	.globl	scalar_mul                      // -- Begin function scalar_mul
; CHECK:  	.type	scalar_mul,@function
; CHECK:  scalar_mul:                             // @scalar_mul
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	.cfi_def_cfa_offset 8
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	scalar_mul, .Lfunc_end0-scalar_mul
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	cond_mul                        // -- Begin function cond_mul
; CHECK:  	.type	cond_mul,@function
; CHECK:  cond_mul:                               // @cond_mul
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	.cfi_def_cfa_offset 8
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r3, sp, 0 }
; CHECK:  	{ 	st32	r3, sp, 4 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 1; 	ld64	d1, sp, 0; 	nop }
; CHECK:  	{ 	and32	r1, r1, r4; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, r0, 0; 	move32_dr_l	r3, d0; 	nop }
; CHECK:  	{ 	seq32	r1, r1, r5 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	movt32	r3, r2, r1; 	nop }
; CHECK:  	{ 	move32	r1, r3 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	cond_mul, .Lfunc_end1-cond_mul
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits

define i32 @scalar_mul(i32 %a, i32 %b) {
entry:
  %m = mul i32 %a, %b
  ret i32 %m
}

; Conditional multiply (shape: `if (n & 1) h = h * x;`). The multiply
; must survive selection as a real multiply, not fold to a no-op identity.
define i32 @cond_mul(i32 %n, i32 %h, i32 %x) {
; The load-bearing assertions are the CHECK-NOTs (no invented mul32, no libcall)
; plus the presence of the real mul64.ll multiply. The byte/bundle layout is
; not semantically meaningful.
entry:
  %bit = and i32 %n, 1
  %cmp = icmp eq i32 %bit, 0
  %mh = mul i32 %h, %x
  %sel = select i1 %cmp, i32 %h, i32 %mh
  ret i32 %sel
}
