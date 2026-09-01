; RUN: llc -mtriple=haydn-unknown-elf -O0 -global-isel -global-isel-abort=1 %s -o - | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — Empty side-effecting inline asm is metadata-only (0
; layout bytes): IRTranslator must lower it, and APP/NO_APP must emit no
; executable text. Exact typed admission: empty is the legal non-executable
; class (code-bearing mixed asm is rejected at Finalize).

; Empty side-effecting inline asm must not crash IRTranslator.
; Pre-fix: "Inline asm lowering is not supported for this target yet"
; → fatal "unable to translate instruction: call".

define void @inline_asm_only() {
; CHECK-LABEL: inline_asm_only:
; Haydn asm printer uses // line comments (not gas #).
; CHECK: {{//|#}}APP
; CHECK-NEXT: {{//|#}}NO_APP
  call void asm sideeffect "", "~{memory}"()
  ret void
}
