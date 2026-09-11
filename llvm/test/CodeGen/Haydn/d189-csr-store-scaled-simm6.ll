; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s

; Role: semantic — D1.89 CSR ST32/LD32 share scaled simm6, not store-uimm4.

; D1.89: emitCSRStore used s0 uimm4 ([0,15] elements → ST32 bytes [0,60]).
; emitCSRLoad and eliminateFrameIndex already used golden Format E RI6
; scaled simm6 ([-32,31] elements → ST32 bytes [-128,124]). Same opcode.
; Store over-refused offsets 64..124 and burned a PEI scratch + ST32_REG.
;
; Pin: one GPR CSR (LR) + a frame whose LR slot sits in (60,124] bytes
; from SP after the prologue sub. Store and restore must be immediate
; ST32/LD32 with scaled field 16..31, never ST32_REG/LD32_REG.
; Load after the call keeps the frame and blocks a tail-call (no LR save).
;
; If this regresses to uimm4, the prologue becomes st32_reg.
; PEI may fold a legal SP+simm6 store into addi+st32 field 0; that is
; still immediate ST32, not REG.

declare void @use(ptr)

define i32 @d189_csr_store_simm6() nounwind {
; CHECK-LABEL: d189_csr_store_simm6:
; CHECK:       subi32{{(_w)?}}{{.*}}sp
; CHECK-NOT:   st32_reg
; CHECK:       st32 {{lr|r15}},
; CHECK:       jal
; CHECK-NOT:   ld32_reg
; CHECK:       ld32 {{lr|r15}},
  %buf = alloca [20 x i32], align 4
  call void @use(ptr %buf)
  %v = load i32, ptr %buf
  ret i32 %v
}
