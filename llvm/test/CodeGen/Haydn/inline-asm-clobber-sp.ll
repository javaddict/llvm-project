; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 | FileCheck %s
;
; REGRESSION TEST: inline-asm clobber of SP is fail-closed.
;
; Bug: reserved R13 clobbers were silently ignored (empty `{r13}` map or
; PEI skip). Destroying SP is not restorable.
; Fix: `{r13}` maps to `{sp}`; determineCalleeSaves diagnoses
; DiagnosticInfoUnsupported. Do not accept a warning-only compile.
;
; Test design: one function so `not llc` cannot stop before the CHECK.

define void @clobber_sp() {
; CHECK: inline asm clobbers reserved register
  call void asm sideeffect "", "~{sp}"()
  ret void
}
