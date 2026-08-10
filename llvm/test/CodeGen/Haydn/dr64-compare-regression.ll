; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REGRESSION FILED : DR64 R_CMP instructions (SLT64_S1
; SEQ64_S1, SLE64_S1 and _S0/_S2 variants) have an operand-flag
; bug in HaydnFormatsALU64.td that aborts the MachineVerifier ("Explicit
; operand marked as def"). The.td marks operand 0 ($rd) as a def with
; hasSideEffects = 1 (implicit SFR write) — the verifier rejects this
; combination. Regressed in the Flex cutover. Selector is correct
; (MIR shows real emission); only the.td operand flags need adjusting.
; Track under / Flex cutover (DR64 R_CMP operand flags). Do NOT
; rebaseline CHECKs to silence the verifier abort.
;
; REGRESSION TEST: DR64 scalar compare and conditional move intrinsics.
;
; Purpose: Verify that 64-bit SFR compare (SLT64, SLE64, SEQ64) and
; conditional move (MOVT64, MOVF64) intrinsics emit the correct
; instructions. These intrinsics have IntrHasSideEffects because they
; interact with the SFR register.
;
; Why this test: The SFR compare/move intrinsics use a side-effect
; protocol: the compare writes SFR flags, and the move reads them.
; If hasSideEffects is incorrectly set (known bug #1: SFR writers with
; hasSideEffects=0 get eliminated as dead), the compare instruction
; gets DCE'd and the subsequent MOV reads stale SFR state. This test
; catches that regression by chaining compare→move in each function.
;
; Test design: Each function chains a compare intrinsic with a
; conditional move intrinsic, returning the move result. This forces
; both instructions to survive. If the compare is eliminated as dead
; the move will produce incorrect results and the CHECK lines will fail
; (or llc will crash with a verifier error).

;===----------------------------------------------------------------------===;
; SLT64 + MOVT64 chain
;===----------------------------------------------------------------------===;

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_slt64_movt64               // -- Begin function test_slt64_movt64
; CHECK: 	.type	test_slt64_movt64,@function
; CHECK: test_slt64_movt64:                      // @test_slt64_movt64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ slt64	d0, d0; nop; nop }
; CHECK: 	{ movt64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_slt64_movt64, .Lfunc_end0-test_slt64_movt64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_sle64_movf64               // -- Begin function test_sle64_movf64
; CHECK: 	.type	test_sle64_movf64,@function
; CHECK: test_sle64_movf64:                      // @test_sle64_movf64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ sle64	d0, d0; nop; nop }
; CHECK: 	{ movf64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_sle64_movf64, .Lfunc_end1-test_sle64_movf64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_seq64                      // -- Begin function test_seq64
; CHECK: 	.type	test_seq64,@function
; CHECK: test_seq64:                             // @test_seq64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ seq64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_seq64, .Lfunc_end2-test_seq64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_movesfr2gpr                // -- Begin function test_movesfr2gpr
; CHECK: 	.type	test_movesfr2gpr,@function
; CHECK: test_movesfr2gpr:                       // @test_movesfr2gpr
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ movesfr2gpr	r1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_movesfr2gpr, .Lfunc_end3-test_movesfr2gpr
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_movegpr2sfr                // -- Begin function test_movegpr2sfr
; CHECK: 	.type	test_movegpr2sfr,@function
; CHECK: test_movegpr2sfr:                       // @test_movegpr2sfr
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ movegpr2sfr	r1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_movegpr2sfr, .Lfunc_end4-test_movegpr2sfr
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_zero_sfr                   // -- Begin function test_zero_sfr
; CHECK: 	.type	test_zero_sfr,@function
; CHECK: test_zero_sfr:                          // @test_zero_sfr
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ zero_sfr; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	test_zero_sfr, .Lfunc_end5-test_zero_sfr
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

declare void @llvm.haydn.slt64(i64, i64)
declare i64 @llvm.haydn.movt64(i64)

define dso_local i64 @test_slt64_movt64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.slt64(i64 %a, i64 %cmp_rhs)
  %r = call i64 @llvm.haydn.movt64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SLE64 + MOVF64 chain
;===----------------------------------------------------------------------===;

declare void @llvm.haydn.sle64(i64, i64)
declare i64 @llvm.haydn.movf64(i64)

define dso_local i64 @test_sle64_movf64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.sle64(i64 %a, i64 %cmp_rhs)
  %r = call i64 @llvm.haydn.movf64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SEQ64 standalone (sets SFR, returns passthrough)
;===----------------------------------------------------------------------===;

declare void @llvm.haydn.seq64(i64, i64)

define dso_local i64 @test_seq64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.seq64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}

;===----------------------------------------------------------------------===;
; SFR register transfer: MOVESFR2GPR / MOVEGPR2SFR / ZERO_SFR
; These verify the SFR↔GPR32 transfer instructions survive codegen.
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.movesfr2gpr()
declare void @llvm.haydn.movegpr2sfr(i32)
declare void @llvm.haydn.zero.sfr()

define dso_local i32 @test_movesfr2gpr() {
  %r = call i32 @llvm.haydn.movesfr2gpr()
  ret i32 %r
}

define dso_local void @test_movegpr2sfr(i32 %a) {
  call void @llvm.haydn.movegpr2sfr(i32 %a)
  ret void
}

define dso_local void @test_zero_sfr() {
  call void @llvm.haydn.zero.sfr()
  ret void
}
