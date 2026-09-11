; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s \
; RUN:   && llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; Role: object — Soft-float libcall symbol preservation end-to-end.

; REGRESSION TEST: Soft-float libcall symbol preservation end-to-end.
;
; Bug (scope m6-softfloat-scope.md BUG B): `clang -target haydn-unknown-elf -c`
; was reported to emit a call to `0` with relocation `*ABS*` (no symbol)
; for soft-float libcalls.
;
; ISel now emits the general call (LUI HI12 + ADDI32 LO20 + JALR). The ASM
; checks require the named libcall on the address parcels (not `0`). The
; RELOC checks require HI12/LO20 to name the symbol (not `*ABS*`). Short
; CallSImm20 JAL is LLD cycle-neutral relax, not the compiler object form.
;
; Reference: -softfloat-gfconstant-and-libcall-symbol.md
; m8-extern-call-symbol-registration-in-asmprinter-bundle-path.md
; m8-extern-call-null-reloc-needsRelocateWithSymbol.md

;fadd → __addsf3

define float @fadd_f32(float %a, float %b) {
; ASM-LABEL: fadd_f32:
; ASM:       lui{{.*}}__addsf3
; ASM:       addi32{{.*}}__addsf3
; ASM:       jalr{{.*}}lr
  %r = fadd float %a, %b
  ret float %r
}

;fsub → __subsf3
define float @fsub_f32(float %a, float %b) {
; ASM-LABEL: fsub_f32:
; ASM:       lui{{.*}}__subsf3
; ASM:       addi32{{.*}}__subsf3
; ASM:       jalr{{.*}}lr
  %r = fsub float %a, %b
  ret float %r
}

;fmul → __mulsf3
define float @fmul_f32(float %a, float %b) {
; ASM-LABEL: fmul_f32:
; ASM:       lui{{.*}}__mulsf3
; ASM:       addi32{{.*}}__mulsf3
; ASM:       jalr{{.*}}lr
  %r = fmul float %a, %b
  ret float %r
}

;fdiv → __divsf3
define float @fdiv_f32(float %a, float %b) {
; ASM-LABEL: fdiv_f32:
; ASM:       lui{{.*}}__divsf3
; ASM:       addi32{{.*}}__divsf3
; ASM:       jalr{{.*}}lr
  %r = fdiv float %a, %b
  ret float %r
}

;fptosi → __fixsfsi
define i32 @fptosi_f32_i32(float %a) {
; ASM-LABEL: fptosi_f32_i32:
; ASM:       lui{{.*}}__fixsfsi
; ASM:       addi32{{.*}}__fixsfsi
; ASM:       jalr{{.*}}lr
  %r = fptosi float %a to i32
  ret i32 %r
}

;sitofp → __floatsisf
define float @sitofp_i32_f32(i32 %a) {
; ASM-LABEL: sitofp_i32_f32:
; ASM:       lui{{.*}}__floatsisf
; ASM:       addi32{{.*}}__floatsisf
; ASM:       jalr{{.*}}lr
  %r = sitofp i32 %a to float
  ret float %r
}

;fcmp olt → __ltsf2
define i1 @fcmp_olt_f32(float %a, float %b) {
; ASM-LABEL: fcmp_olt_f32:
; ASM:       lui{{.*}}__ltsf2
; ASM:       addi32{{.*}}__ltsf2
; ASM:       jalr{{.*}}lr
  %r = fcmp olt float %a, %b
  ret i1 %r
}

;Relocations reference the named symbols (not `*ABS*`).
; RELOC:      Relocations [
; RELOC-DAG:    R_HAYDN_HI12 __addsf3
; RELOC-DAG:    R_HAYDN_LO20{{(_E1)?}} __addsf3
; RELOC-DAG:    R_HAYDN_HI12 __subsf3
; RELOC-DAG:    R_HAYDN_LO20{{(_E1)?}} __subsf3
; RELOC-DAG:    R_HAYDN_HI12 __mulsf3
; RELOC-DAG:    R_HAYDN_LO20{{(_E1)?}} __mulsf3
; RELOC-DAG:    R_HAYDN_HI12 __divsf3
; RELOC-DAG:    R_HAYDN_LO20{{(_E1)?}} __divsf3
; RELOC-NOT:    *ABS*
; RELOC:      ]
