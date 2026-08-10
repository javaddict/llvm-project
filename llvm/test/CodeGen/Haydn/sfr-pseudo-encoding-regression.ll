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
; REGRESSION TEST: SFR/compare/conditional-move/sel-imm pseudo-drops.
;
; Bug: SLT64, SLE64, SEQ64, MOVT64, MOVF64, MOVESFR2GPR, MOVEGPR2SFR
; ZERO_SFR, X2SEQ32, X2SLT32, X2SLE32, X2MOVF32, X2MOVT32, X4SEQ16
; X4SLT16, X4SLE16, X4MOVF16, X4MOVT16, X2ABS32S, X4SELI16 were defined
; in HaydnInstrInfoAuto.td as HaydnInst<4,...> blanket pseudos with no
; Inst{...}=... fields. TableGen marked them MCID::Pseudo and AsmPrinter
; silently dropped them — the instructions survived ISel but never reached
; assembly. Function bodies emitted as { xor32 r0, r0, r0; nop; nop }.
;
; Fix : real 32-bit R-type encodings assigned in HaydnInstrInfo.td
; under opcode 0x67, funct 0x187..0x19A. Each mnemonic now reaches
; assembly. If a regression reintroduces the blanket pseudo (no Inst{}
; fields), the corresponding CHECK line fails because the mnemonic
; disappears from the output.
;
; Test design: each intrinsic is invoked in its own function so the
; mnemonic must appear in the assembly. The CHECK lines only assert
; presence of the mnemonic (not full bundle formatting), which is the
; minimal contract — emitting the right MCInst is the bug being guarded.

;===----------------------------------------------------------------------===;
; Scalar SFR compare (unary DR64 → sets SFR)
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	test_zero_sfr, .Lfunc_end7-test_zero_sfr
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	test_x2seq32, .Lfunc_end8-test_x2seq32
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	test_x2slt32, .Lfunc_end9-test_x2slt32
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	test_x2sle32, .Lfunc_end10-test_x2sle32
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	test_x2movf32, .Lfunc_end11-test_x2movf32
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	test_x2movt32, .Lfunc_end12-test_x2movt32
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	test_x4seq16, .Lfunc_end13-test_x4seq16
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	test_x4slt16, .Lfunc_end14-test_x4slt16
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	test_x4sle16, .Lfunc_end15-test_x4sle16
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end16:
; CHECK: 	.size	test_x4movf16, .Lfunc_end16-test_x4movf16
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
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end17:
; CHECK: 	.size	test_x4movt16, .Lfunc_end17-test_x4movt16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x2abs32s                   // -- Begin function test_x2abs32s
; CHECK: 	.type	test_x2abs32s,@function
; CHECK: test_x2abs32s:                          // @test_x2abs32s
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x2abs32s	d0, d0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end18:
; CHECK: 	.size	test_x2abs32s, .Lfunc_end18-test_x2abs32s
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4seli16_const5            // -- Begin function test_x4seli16_const5
; CHECK: 	.type	test_x4seli16_const5,@function
; CHECK: test_x4seli16_const5:                   // @test_x4seli16_const5
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4seli16	d0, d0, d1, 5; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end19:
; CHECK: 	.size	test_x4seli16_const5, .Lfunc_end19-test_x4seli16_const5
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4seli16_const0            // -- Begin function test_x4seli16_const0
; CHECK: 	.type	test_x4seli16_const0,@function
; CHECK: test_x4seli16_const0:                   // @test_x4seli16_const0
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4seli16	d0, d0, d1, 0; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end20:
; CHECK: 	.size	test_x4seli16_const0, .Lfunc_end20-test_x4seli16_const0
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_x4seli16_const15           // -- Begin function test_x4seli16_const15
; CHECK: 	.type	test_x4seli16_const15,@function
; CHECK: test_x4seli16_const15:                  // @test_x4seli16_const15
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ x4seli16	d0, d0, d1, 15; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end21:
; CHECK: 	.size	test_x4seli16_const15, .Lfunc_end21-test_x4seli16_const15
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

declare void @llvm.haydn.slt64(i64, i64)
declare void @llvm.haydn.sle64(i64, i64)
declare void @llvm.haydn.seq64(i64, i64)

define dso_local i64 @test_slt64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.slt64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}

define dso_local i64 @test_sle64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.sle64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}

define dso_local i64 @test_seq64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.seq64(i64 %a, i64 %cmp_rhs)
  ret i64 %a
}

;===----------------------------------------------------------------------===;
; Scalar SFR conditional move (unary DR64 → reads SFR)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

define dso_local i64 @test_movt64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.slt64(i64 %a, i64 %cmp_rhs)
  %r = call i64 @llvm.haydn.movt64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_movf64(i64 %a, i64 %cmp_rhs) {
  call void @llvm.haydn.sle64(i64 %a, i64 %cmp_rhs)
  %r = call i64 @llvm.haydn.movf64(i64 %a)
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

define dso_local void @test_movegpr2sfr(i32 %v) {
  call void @llvm.haydn.movegpr2sfr(i32 %v)
  ret void
}

define dso_local void @test_zero_sfr() {
  call void @llvm.haydn.zero.sfr()
  ret void
}

;===----------------------------------------------------------------------===;
; X2 SIMD SFR compare (binary DR64 → sets SFR)
;===----------------------------------------------------------------------===;

declare void @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare void @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare void @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2seq32(<2 x i32> %a, <2 x i32> %b) {
  call void @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %a
}

define dso_local <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
  call void @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %a
}

define dso_local <2 x i32> @test_x2sle32(<2 x i32> %a, <2 x i32> %b) {
  call void @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %a
}

;===----------------------------------------------------------------------===;
; X2 SIMD conditional move on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2movf32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2movt32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD SFR compare (binary DR64 → sets SFR)
;===----------------------------------------------------------------------===;

declare void @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare void @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare void @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4seq16(<4 x i16> %a, <4 x i16> %b) {
  call void @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %a
}

define dso_local <4 x i16> @test_x4slt16(<4 x i16> %a, <4 x i16> %b) {
  call void @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %a
}

define dso_local <4 x i16> @test_x4sle16(<4 x i16> %a, <4 x i16> %b) {
  call void @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %a
}

;===----------------------------------------------------------------------===;
; X4 SIMD conditional move on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4movf16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4movt16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2ABS32S — unary DR64 (per-lane saturating absolute value)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2abs32s(<2 x i32>)

define dso_local <2 x i32> @test_x2abs32s(<2 x i32> %a) {
  %r = call <2 x i32> @llvm.haydn.x2abs32s(<2 x i32> %a)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4SELI16 — select with 4-bit immediate lane mask.
; Per spec, the third operand is uimm4 (constant 0..15). The selector
; correctly rejects non-constant operands; this test exercises the
; constant-imm path.
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

define dso_local <4 x i16> @test_x4seli16_const5(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 5)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_const0(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 0)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_const15(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 15)
  ret <4 x i16> %r
}
