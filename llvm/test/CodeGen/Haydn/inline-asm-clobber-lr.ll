; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: inline-asm clobber of LR must force-save R15.
;
; Bug: R15 is reserved (getReservedRegs). Leaf `asm("" ::: "lr")` / `~{r15}`
; is not hasCalls(), so determineCalleeSaves left LR unsaved and the asm
; destroyed the return address. `{r15}` also failed to map (asm name is
; "lr") and the clobber was dropped.
; Fix: alias `{r15}`→`{lr}` in getRegForInlineAsmConstraint; PEI force-saves
; R15 when INLINEASM clobbers it. If this regresses, the leaf returns via
; a clobbered lr with no st32/ld32 of lr.
;
; Test design: leaf, no calls, only an lr / r15 clobber. Prologue must
; spill lr and epilogue must reload it.

define void @clobber_lr() {
; CHECK-LABEL: clobber_lr:
; CHECK:       { nop; st32 lr, sp,
; CHECK:       {{//|#}}APP
; CHECK:       {{//|#}}NO_APP
; CHECK:       { nop; ld32 lr, sp,
; CHECK:       { nop; jalr r0, lr, 0 }
  call void asm sideeffect "", "~{lr}"()
  ret void
}

define void @clobber_r15_alias() {
; CHECK-LABEL: clobber_r15_alias:
; CHECK:       { nop; st32 lr, sp,
; CHECK:       {{//|#}}APP
; CHECK:       {{//|#}}NO_APP
; CHECK:       { nop; ld32 lr, sp,
; CHECK:       { nop; jalr r0, lr, 0 }
  call void asm sideeffect "", "~{r15}"()
  ret void
}
