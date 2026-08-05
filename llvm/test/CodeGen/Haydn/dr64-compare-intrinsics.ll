; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Status : R_CMP operand-flag XFAIL stale; CHECKs use DAG for bundle order.
; REGRESSION TEST: DR64 comparison intrinsics — SFR/compare/conditional-move.
;
; Bug (fixed,): SFR/compare/conditional-move instructions
; (SLT64, SLE64, SEQ64, MOVT64, MOVF64, MOVESFR2GPR, MOVEGPR2SFR, ZERO_SFR
; X2SEQ32/SLT32/SLE32/MOVF32/MOVT32, X4SEQ16/SLT16/SLE16/MOVF16/MOVT16)
; were defined in HaydnInstrInfoAuto.td as HaydnInst<4,...> blanket pseudos
; with no Inst{...}=... fields. TableGen marked them MCID::Pseudo and the
; AsmPrinter silently dropped them — the instructions survived ISel but
; never reached assembly.
;
; Fix: real 32-bit R-type encodings assigned in HaydnInstrInfo.td under
; opcode 0x67, funct 0x187..0x196. Each mnemonic now reaches
; assembly. If a regression reintroduces the blanket pseudo (no Inst{}
; fields), the corresponding CHECK line fails because the mnemonic
; disappears from the output.

;===----------------------------------------------------------------------===;
; Scalar 64-bit SFR compare (unary DR64, IntrHasSideEffects)
;===----------------------------------------------------------------------===;

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_slt64                      // -- Begin function test_slt64
; CHECK: 	.type	test_slt64,@function
; CHECK: test_slt64:                             // @test_slt64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ slt64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_slt64, .Lfunc_end0-test_slt64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_sle64                      // -- Begin function test_sle64
; CHECK: 	.type	test_sle64,@function
; CHECK: test_sle64:                             // @test_sle64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ sle64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_sle64, .Lfunc_end1-test_sle64
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
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_seq64, .Lfunc_end2-test_seq64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_movt64                     // -- Begin function test_movt64
; CHECK: 	.type	test_movt64,@function
; CHECK: test_movt64:                            // @test_movt64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ slt64	d0, d0; nop; nop }
; CHECK: 	{ movt64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_movt64, .Lfunc_end3-test_movt64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_movf64                     // -- Begin function test_movf64
; CHECK: 	.type	test_movf64,@function
; CHECK: test_movf64:                            // @test_movf64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ sle64	d0, d0; nop; nop }
; CHECK: 	{ movf64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_movf64, .Lfunc_end4-test_movf64
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
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	test_movesfr2gpr, .Lfunc_end5-test_movesfr2gpr
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
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	test_movegpr2sfr, .Lfunc_end6-test_movegpr2sfr
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
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	test_zero_sfr, .Lfunc_end7-test_zero_sfr
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_scalar_predication         // -- Begin function test_scalar_predication
; CHECK: 	.type	test_scalar_predication,@function
; CHECK: test_scalar_predication:                // @test_scalar_predication
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ slt64	d0, d0; nop; nop }
; CHECK: 	{ movt64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	test_scalar_predication, .Lfunc_end8-test_scalar_predication
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_sfr_save_restore           // -- Begin function test_sfr_save_restore
; CHECK: 	.type	test_sfr_save_restore,@function
; CHECK: test_sfr_save_restore:                  // @test_sfr_save_restore
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ movegpr2sfr	r1; nop; nop }
; CHECK: 	{ slt64	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	test_sfr_save_restore, .Lfunc_end9-test_sfr_save_restore
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2seq32                    // -- Begin function test_x2seq32
; CHECK: 	.type	test_x2seq32,@function
; CHECK: test_x2seq32:                           // @test_x2seq32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2seq32	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	test_x2seq32, .Lfunc_end10-test_x2seq32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2slt32                    // -- Begin function test_x2slt32
; CHECK: 	.type	test_x2slt32,@function
; CHECK: test_x2slt32:                           // @test_x2slt32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2slt32	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	test_x2slt32, .Lfunc_end11-test_x2slt32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2sle32                    // -- Begin function test_x2sle32
; CHECK: 	.type	test_x2sle32,@function
; CHECK: test_x2sle32:                           // @test_x2sle32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2sle32	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	test_x2sle32, .Lfunc_end12-test_x2sle32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2movf32                   // -- Begin function test_x2movf32
; CHECK: 	.type	test_x2movf32,@function
; CHECK: test_x2movf32:                          // @test_x2movf32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2movf32	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	test_x2movf32, .Lfunc_end13-test_x2movf32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2movt32                   // -- Begin function test_x2movt32
; CHECK: 	.type	test_x2movt32,@function
; CHECK: test_x2movt32:                          // @test_x2movt32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2movt32	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	test_x2movt32, .Lfunc_end14-test_x2movt32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4seq16                    // -- Begin function test_x4seq16
; CHECK: 	.type	test_x4seq16,@function
; CHECK: test_x4seq16:                           // @test_x4seq16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4seq16	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	test_x4seq16, .Lfunc_end15-test_x4seq16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4slt16                    // -- Begin function test_x4slt16
; CHECK: 	.type	test_x4slt16,@function
; CHECK: test_x4slt16:                           // @test_x4slt16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4slt16	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end16:
; CHECK: 	.size	test_x4slt16, .Lfunc_end16-test_x4slt16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4sle16                    // -- Begin function test_x4sle16
; CHECK: 	.type	test_x4sle16,@function
; CHECK: test_x4sle16:                           // @test_x4sle16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4sle16	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end17:
; CHECK: 	.size	test_x4sle16, .Lfunc_end17-test_x4sle16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4movf16                   // -- Begin function test_x4movf16
; CHECK: 	.type	test_x4movf16,@function
; CHECK: test_x4movf16:                          // @test_x4movf16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4movf16	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end18:
; CHECK: 	.size	test_x4movf16, .Lfunc_end18-test_x4movf16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4movt16                   // -- Begin function test_x4movt16
; CHECK: 	.type	test_x4movt16,@function
; CHECK: test_x4movt16:                          // @test_x4movt16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4movt16	d0, d0, d1; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end19:
; CHECK: 	.size	test_x4movt16, .Lfunc_end19-test_x4movt16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

declare i64 @llvm.haydn.slt64(i64)
declare i64 @llvm.haydn.sle64(i64)
declare i64 @llvm.haydn.seq64(i64)

define dso_local i64 @test_slt64(i64 %a) {
  %r = call i64 @llvm.haydn.slt64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_sle64(i64 %a) {
  %r = call i64 @llvm.haydn.sle64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_seq64(i64 %a) {
  %r = call i64 @llvm.haydn.seq64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Scalar 64-bit SFR conditional move (unary DR64, IntrHasSideEffects)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

define dso_local i64 @test_movt64(i64 %a) {
  %cmp = call i64 @llvm.haydn.slt64(i64 %a)
  %r = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %r
}

define dso_local i64 @test_movf64(i64 %a) {
  %cmp = call i64 @llvm.haydn.sle64(i64 %a)
  %r = call i64 @llvm.haydn.movf64(i64 %cmp)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SFR register transfer
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.movesfr2gpr()
declare void @llvm.haydn.movegpr2sfr(i32)
declare void @llvm.haydn.zero.sfr()

define dso_local i32 @test_movesfr2gpr() {
  %r = call i32 @llvm.haydn.movesfr2gpr()
  ret i32 %r
}

define dso_local void @test_movegpr2sfr(i32 %val) {
  call void @llvm.haydn.movegpr2sfr(i32 %val)
  ret void
}

define dso_local void @test_zero_sfr() {
  call void @llvm.haydn.zero.sfr()
  ret void
}

;===----------------------------------------------------------------------===;
; Scalar predication pattern: compare -> SFR -> conditional select
;===----------------------------------------------------------------------===;

define dso_local i64 @test_scalar_predication(i64 %a) {
  %cmp = call i64 @llvm.haydn.slt64(i64 %a)
  %result = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %result
}

;===----------------------------------------------------------------------===;
; SFR save/restore via GPR
;===----------------------------------------------------------------------===;

define dso_local i64 @test_sfr_save_restore(i64 %a, i32 %saved_sfr) {
  call void @llvm.haydn.movegpr2sfr(i32 %saved_sfr)
  %r = call i64 @llvm.haydn.slt64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 SIMD compare -> SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2seq32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2sle32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X2 SIMD conditional move based on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2movf32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2movt32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD compare -> SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4seq16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4slt16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4sle16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD conditional move based on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4movf16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4movt16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}
