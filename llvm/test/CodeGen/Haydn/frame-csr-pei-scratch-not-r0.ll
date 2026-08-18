; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; REGRESSION TEST: F21 — out-of-imm CSR offsets use a PEI scratch, not R0.
;
; Bug: emitCSRStore/emitCSRLoad materialized offsets outside the scaled
; imm4/simm6 window into soft-zero R0 (ADDI32_W/LOADI32 R0), then
; ST64_REG/LD64_REG read R0, then XOR32 re-zeroed. Safety depended on
; intra-cycle RAW keeping the R0-def and its use out of one bundle.
; Historical JALR-writes-R0 already corrupted CSR restores this way.
;
; Fix: one mechanism — requirePEIScratchReg (ABI call-clobbered, never
; R0). ADDI32_W/LOADI32 write the scratch; R0 is only read as soft-zero
; for the add. No XOR32 R0 on this path.
;
; Test design: 128-byte alloca + a call that keeps %a live in a CSR.
; Frame >60 B so the LR/R8 slots miss ST32 imm4 [0,60]. The REG-offset
; form must use r1-r12, never write r0 for the offset.
;
; If this regresses, CHECK-NOT addi32_w r0 / loadi32 r0 fail.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare void @clobber_all()

define i32 @large_csr_offset(i32 %a) {
; CHECK-LABEL: large_csr_offset:
; CHECK:       { nop; xor32 r0, r0, r0 }
; CHECK:       subi32{{(_w)?}}{{.*}}sp
; CHECK-NOT:   addi32{{(_w)?}} r0,
; CHECK-NOT:   loadi32 r0,
; CHECK:       st32{{(_reg)?}}{{.*}}lr
; CHECK:       { nop; jal lr, clobber_all }
; CHECK-NOT:   addi32{{(_w)?}} r0,
; CHECK-NOT:   loadi32 r0,
; CHECK:       ld32{{(_reg)?}}{{.*}}lr
; CHECK:       { nop; jalr r0, lr, 0 }
  %buf = alloca [32 x i32], align 4
  call void @clobber_all()
  %p = getelementptr [32 x i32], ptr %buf, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}
