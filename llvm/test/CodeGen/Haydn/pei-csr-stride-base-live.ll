; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — PEI CSR-stride materialisation (ADDI scratch, SP, off)
; must remain live. HaydnPEIPeephole is deleted; FrameLowering emits FP
; setup only when hasFP(). The deleted pass used to drop FrameSetup
; ADDI R14,R13,* when !hasFP as "dead FP setup", even when R14 was the
; live base for CSR ST64 — leaving ST64 d*, fp, * with an uninitialized
; FP → MEMORY_FAULT.
;
; Force several DR64 CSRs live across a call so PEI emits stride ST64s.
; The first CSR store's base must be defined from SP (addi/or), never a bare
; st64 …, fp/r14 without a prior def of that base from sp.

declare void @ext()

define void @csr_stride_base_must_be_live(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: csr_stride_base_must_be_live:
; CHECK: subi32{{.*}}sp
; Materialise stride base from SP before any CSR ST64.
; CHECK: addi32{{(_w)?}}{{.*}}sp
; CHECK: st64{{.*}}d1{{[0-5]}}
; Must not store through fp/r14 without the addi above (base is call-clobbered
; scratch, not architectural FP).
; CHECK-NOT: st64{{.*}}fp,{{.*}}0{{$}}
entry:
  %t0 = add i64 %a, 1
  %t1 = add i64 %b, 2
  %t2 = add i64 %c, 3
  %t3 = add i64 %d, 4
  %t4 = xor i64 %t0, %t1
  %t5 = xor i64 %t2, %t3
  %t6 = add i64 %t4, %a
  %t7 = add i64 %t5, %b
  call void @ext()
  ; Keep values live so D8–D15 are CSRs.
  store volatile i64 %t0, ptr null
  store volatile i64 %t1, ptr null
  store volatile i64 %t2, ptr null
  store volatile i64 %t3, ptr null
  store volatile i64 %t4, ptr null
  store volatile i64 %t5, ptr null
  store volatile i64 %t6, ptr null
  store volatile i64 %t7, ptr null
  ret void
}
