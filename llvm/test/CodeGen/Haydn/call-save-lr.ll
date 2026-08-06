; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;
; REGRESSION TEST: non-leaf functions must save/restore R15 (LR).
;
; Bug: JAL writes the return address into R15 (LR), but non-leaf functions
; never forced R15 into the callee-saved set, so the caller's return path was
; clobbered by the call. The function would return to the wrong address.
; Fix: determineCalleeSaves adds R15 to SavedRegs when the function contains a
; call (MFI.hasCalls), and CSR_Haydn now lists R15 so PEI includes it in CSI.
; See F13 / CLAUDE.md register map (R15=LR).
;
; Test design: caller invokes a callee, so it is non-leaf and must save R15.
; The prologue must contain a store of R15, and the epilogue a load. Leaf
; functions (leaf below) must NOT save R15.



; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	caller                          // -- Begin function caller
; CHECK: 	.type	caller,@function
; CHECK: caller:                                 // @caller
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal	lr, callee }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	caller, .Lfunc_end0-caller
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	leaf                            // -- Begin function leaf
; CHECK: 	.type	leaf,@function
; CHECK: leaf:                                   // @leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	leaf, .Lfunc_end1-leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

declare void @callee()

define void @caller() {
; Prologue must spill R15 (LR). The exact slot varies, but st32 of LR (printed
; as "lr" or "r15") must appear in the FrameSetup region.
; Epilogue must reload R15.
  call void @callee()
  ret void
}

define i32 @leaf(i32 %a, i32 %b) {
; Leaf function — R15 must NOT be spilled.
  %sum = add i32 %a, %b
  ret i32 %sum
}
