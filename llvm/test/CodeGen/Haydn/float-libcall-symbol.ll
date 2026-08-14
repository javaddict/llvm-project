; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s \
; RUN:   && llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; Role: object — Soft-float libcall symbol preservation end-to-end.

; REGRESSION TEST: Soft-float libcall symbol preservation end-to-end.
;
; Bug (scope m6-softfloat-scope.md BUG B): `clang -target haydn-unknown-elf -c`
; was reported to emit `jal_w lr, 0` with relocation
; `R_HAYDN_CallSImm20 *ABS*` (value 0, no symbol) for soft-float libcalls.
;
; Status as of this test's XFAIL : the defensive fix in
; (HaydnCallLowering::lowerCall adds the callee operand verbatim via
; `MIB.add(Info.Callee)`, mirroring RISCVCallLowering) only fixed the
; MachineInstr / asm-print stage. The ASM CHECKs below PASS; `llc` textual
; asm correctly shows `jal_w lr, __addsf3`. The RELOC CHECKs FAILED because
; the callee MCSymbol was dropped during MCInst lowering / object emission:
; HaydnAsmPrinter wraps every instruction in a BUNDLE MCInst whose children
; are MCOperand::createInst operands, and the default streamer path does not
; recurse into those children to register referenced symbols.
;
; Fixed by : HaydnAsmPrinter gained registerSymbolicOperands
; which walks an MCInst's operands (recursing into MCOperand::createInst
; children) and calls OutStreamer->visitUsedExpr on every Expr operand before
; the BUNDLE is emitted. This makes the extern/libcall MCSymbol land in
; symtab and the R_HAYDN_CallSimm20 reloc reference it by name/index. The
; XFAIL has been removed; all ASM and RELOC CHECKs now pass.
;
; Test design: each function performs a float operation that lowers to a
; specific compiler-rt libcall. The ASM checks verify the JAL target is the
; named libcall symbol (not `0`); the RELOC checks verify the relocation
; references the symbol (not `*ABS*`).
;
; Reference: -softfloat-gfconstant-and-libcall-symbol.md
; m8-extern-call-symbol-registration-in-asmprinter-bundle-path.md
; m8-extern-call-null-reloc-needsRelocateWithSymbol.md

;fadd → __addsf3

define float @fadd_f32(float %a, float %b) {
; ASM-LABEL: fadd_f32:
; ASM:       jal lr, __addsf3
  %r = fadd float %a, %b
  ret float %r
}

;fsub → __subsf3
define float @fsub_f32(float %a, float %b) {
; ASM-LABEL: fsub_f32:
; ASM:       jal lr, __subsf3
  %r = fsub float %a, %b
  ret float %r
}

;fmul → __mulsf3
define float @fmul_f32(float %a, float %b) {
; ASM-LABEL: fmul_f32:
; ASM:       jal lr, __mulsf3
  %r = fmul float %a, %b
  ret float %r
}

;fdiv → __divsf3
define float @fdiv_f32(float %a, float %b) {
; ASM-LABEL: fdiv_f32:
; ASM:       jal lr, __divsf3
  %r = fdiv float %a, %b
  ret float %r
}

;fptosi → __fixsfsi
define i32 @fptosi_f32_i32(float %a) {
; ASM-LABEL: fptosi_f32_i32:
; ASM:       jal lr, __fixsfsi
  %r = fptosi float %a to i32
  ret i32 %r
}

;sitofp → __floatsisf
define float @sitofp_i32_f32(i32 %a) {
; ASM-LABEL: sitofp_i32_f32:
; ASM:       jal lr, __floatsisf
  %r = sitofp i32 %a to float
  ret float %r
}

;fcmp olt → __ltsf2
define i1 @fcmp_olt_f32(float %a, float %b) {
; ASM-LABEL: fcmp_olt_f32:
; ASM:       jal lr, __ltsf2
  %r = fcmp olt float %a, %b
  ret i1 %r
}

;Relocations reference the named symbols (not `*ABS*`).
; RELOC:      Relocations [
; RELOC:        R_HAYDN_WIDE_CallSImm20 __addsf3
; RELOC:        R_HAYDN_WIDE_CallSImm20 __subsf3
; RELOC:        R_HAYDN_WIDE_CallSImm20 __mulsf3
; RELOC:        R_HAYDN_WIDE_CallSImm20 __divsf3
; RELOC-NOT:    R_HAYDN_WIDE_CallSImm20 *ABS*
; RELOC:      ]
