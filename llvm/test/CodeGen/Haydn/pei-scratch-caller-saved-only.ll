; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; PEI CSR-stride scratch must be caller-saved (R1–R7), never unsaved R8–R11.
; Using R8 as stride base without saving it clobbers the caller's R8 (e.g. a
; live pointer kept across the call) → silent wrong loads after return.
;
; Force many DR64 CSRs live across a call so PEI emits stride-4/8 CSR stores
; with a scratch base. The base materialisation must not target r8–r11.

declare void @clobber()

define void @callee_with_dr_pressure(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: callee_with_dr_pressure:
; CHECK: subi32{{.*}}sp
; Stride / CSR base must not be a callee-saved GPR. [^;] rather than .*
; because a bundle prints several instructions on one line: .* spans the
; separator and matched `r8` from one member against `sp` from another.
; CHECK-NOT: addi32{{(_w)?}}{{[^;]*}}r8,{{[^;]*}}sp
; CHECK-NOT: addi32{{(_w)?}}{{[^;]*}}r9,{{[^;]*}}sp
; CHECK-NOT: addi32{{(_w)?}}{{[^;]*}}r10,{{[^;]*}}sp
; CHECK-NOT: addi32{{(_w)?}}{{[^;]*}}r11,{{[^;]*}}sp
; CHECK: jal
entry:
  ; Keep several i64 values live across the call → D8–D15 pressure.
  %t0 = add i64 %a, 1
  %t1 = add i64 %b, 2
  %t2 = add i64 %c, 3
  %t3 = add i64 %d, 4
  %t4 = xor i64 %t0, %t1
  %t5 = xor i64 %t2, %t3
  call void @clobber()
  call void @clobber()
  ; Sink uses so values stay live across calls.
  %u0 = add i64 %t4, %t5
  %u1 = add i64 %t0, %t2
  %u2 = add i64 %t1, %t3
  %u3 = add i64 %u0, %u1
  %u4 = add i64 %u2, %u3
  store volatile i64 %u4, ptr null
  ret void
}
