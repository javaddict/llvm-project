; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s
;
; Role: semantic — backend `'d'` is a DR64 register-class constraint.
; Empty template: pin IRTranslator + getConstraintType, not mnemonic
; operand printing (Haydn inline-asm operand rewrite is residual).

define i64 @d_constraint_ok(i64 %x) nounwind {
; CHECK-LABEL: name: d_constraint_ok
; CHECK: INLINEASM
  %r = call i64 asm sideeffect "", "=d,d"(i64 %x)
  ret i64 %r
}

define i64 @r_i64_ok(i64 %x) nounwind {
; CHECK-LABEL: name: r_i64_ok
; CHECK: INLINEASM
  %r = call i64 asm sideeffect "", "=r,r"(i64 %x)
  ret i64 %r
}
