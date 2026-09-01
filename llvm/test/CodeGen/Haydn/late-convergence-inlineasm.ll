; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs -haydn-sms2 < %t/code_bearing.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=REJECT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs -haydn-sms2 < %t/meta_only.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
;
; W68.3R inline-asm shapes under the convergence driver (exit-row matrix
; item). RE-PINNED for the W68.4 admission law (this file's own prior
; note said to): pipeline.md "Finalization, freeze, inline assembly"
; now rejects code-bearing opaque asm fail-closed at Finalize — its
; bytes would emit after the claimed freeze via AsmPrinter::
; emitInlineAsm, outside the exact Format E layout model.
;
; Admission surface under the convergence driver:
;   * code-bearing opaque asm: Finalize fatal (typed admission or
;     reject; the diagnostic names the owning law);
;   * metadata-only asm (empty string, no side-effect text): emits no
;     executable bytes (empty APP/NO_APP pair), charges 0 layout bytes,
;     and does not perturb the loop.

; The reject fatal names the owning law (fail closed; metadata-only asm
; is the legal non-executable class).
; REJECT: Haydn Finalize: code-bearing inline asm is outside the exact
; REJECT: Format E layout model

;--- code_bearing.ll
define void @conv_inlineasm_code_bearing(ptr nocapture writeonly %out) {
entry:
  %p0 = getelementptr i32, ptr %out, i32 0
  store i32 1, ptr %p0, align 4
  call void asm sideeffect "nop; nop; nop", "~{memory}"()
  %p1 = getelementptr i32, ptr %out, i32 1
  store i32 2, ptr %p1, align 4
  ret void
}

;--- meta_only.ll
define void @conv_inlineasm_meta_only(ptr nocapture writeonly %out) {
entry:
  store i32 7, ptr %out, align 4
  call void asm "", "~{memory}"()
  ret void
}

; Metadata-only asm is not executable: empty APP/NO_APP pair, no bytes.
; LOOP-LABEL: conv_inlineasm_meta_only:
; LOOP: //APP
; LOOP-NEXT: //NO_APP
; LOOP: jalr
