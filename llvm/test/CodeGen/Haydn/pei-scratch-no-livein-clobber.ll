; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; PEI must not use entry live-ins (formal args R1..) as the CSR-stride scratch.
; Without this, exit-style prologues rewrote status into SP+N and GUEST_EXIT
; reported a constant stack address (~0x7fffff64) for every main return value.
;
; Force several GPR CSRs live across a call so PEI emits the stride-4 base
; materialisation (addi scratch, sp, off). The stride base must not be r1
; (live-in arg); the arg must still be readable after the CSR stores
; (move/spill from r1).

declare void @clobber(i32)

define void @preserve_arg0_with_csr_spills(i32 %status) {
; CHECK-LABEL: preserve_arg0_with_csr_spills:
; CHECK: // %bb.0:
; CHECK: xor32{{.*}}r0
; CHECK: subi32{{.*}}sp
;
; Stride base must not be r1 (the live-in arg). Accept r2+ or reserved r12.
; CHECK-NOT: addi32{{(_w)?}}{{.*}}r1,{{.*}}sp
; After CSR stores, the live-in arg is still consumed from r1 (copy or spill).
; CHECK: {{move32|st32}}{{.*}}r1
; CHECK: jal
; CHECK: jalr{{.*}}lr
entry:
  %a = add i32 %status, 1
  %b = add i32 %status, 2
  %c = add i32 %status, 3
  %d = add i32 %status, 4
  call void @clobber(i32 %status)
  call void @clobber(i32 %a)
  call void @clobber(i32 %b)
  call void @clobber(i32 %c)
  call void @clobber(i32 %d)
  call void @clobber(i32 %status)
  ret void
}

; Five formal args live-in — stride base must not be r1 (and should avoid
; clobbering any still-unread arg; we only hard-check r1 as the ABI return
; first-arg channel that exit depends on).
define void @preserve_many_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
; CHECK-LABEL: preserve_many_args:
; CHECK: subi32{{.*}}sp
; CHECK-NOT: addi32{{(_w)?}}{{.*}}r1,{{.*}}sp
; CHECK: {{move32|st32}}{{.*}}r1
; CHECK: jal
entry:
  %t0 = add i32 %a, %b
  %t1 = add i32 %c, %d
  %t2 = add i32 %e, %t0
  call void @clobber(i32 %t0)
  call void @clobber(i32 %t1)
  call void @clobber(i32 %t2)
  call void @clobber(i32 %a)
  call void @clobber(i32 %b)
  call void @clobber(i32 %c)
  call void @clobber(i32 %d)
  call void @clobber(i32 %e)
  ret void
}
