; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; Role: semantic — a register-only call does not force a frame pointer.
;
; REGRESSION: hasFP must not treat MFI.adjustsStack() as sufficient.
; That rewrite reserved R14 on every caller, turned `{ nop; st32 lr, sp, 3 }`
; into `addi32 tmp, sp, 8` + `st32 lr, tmp, 0`, and switched CFA to fp.
; ADJCALLSTACK still moves SP around outgoing stack args; post-RA pack/scratch
; addressing adds the live call-frame delta instead of keeping FP.
;
; Peer: AIEBaseFrameLowering.cpp:40-43, RISCVFrameLowering.cpp:464-470.

declare void @callee()

define void @reg_only_call() {
; CHECK-LABEL: reg_only_call:
; CHECK:       { nop; xor32 r0, r0, r0 }
; CHECK-NEXT:  { nop; subi32 sp, sp, 16 }
; CHECK-NEXT:  { nop; st32 lr, sp, 3 }
; CHECK-NEXT:  .cfi_def_cfa_offset 16
; CHECK-NEXT:  .cfi_offset lr, -4
; CHECK-NOT:   addi32{{.*}}fp
; CHECK-NOT:   .cfi_def_cfa fp
; CHECK:       { nop; jal lr, callee }
; CHECK:       { nop; ld32 lr, sp, 3 }
; CHECK-NEXT:  { nop; addi32 sp, sp, 16 }
; CHECK-NEXT:  .cfi_def_cfa sp, 0
; CHECK-NEXT:  { nop; jalr r0, lr, 0 }
  call void @callee()
  ret void
}
