; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; Role: semantic — f64 pack during a live call-frame does not clobber
; outgoing stack args, and does not force a frame pointer.
;
; REGRESSION: SP-relative LOADI64 pack after ADJCALLSTACKDOWN used the
; post-prologue offset (no SPAdj). The 2nd stack f64 became the last DR
; value. hasFP-on-adjustsStack hid that by switching locals to FP; it also
; rewrote every caller's LR slot. Address the pack with getFrameIndexReferenceAt
; (PEI SPAdj) instead.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare void @sink(double, double, double, double, double)

define void @stack_f64_const() {
; CHECK-LABEL: stack_f64_const:
; CHECK:       { nop; xor32 r0, r0, r0 }
; CHECK:       { nop; st32 lr, sp,
; CHECK-NOT:   .cfi_def_cfa fp
; Distinctive high word of 0x400921fb54442d18 (pi bits).
; CHECK:       lui{{.*}}1025
; Outgoing 8-byte slot after four DR args. Pack must not land here.
; CHECK:       subi32{{(_w)?}}{{.*}}sp{{.*}}, 8
; CHECK:       st64{{.*}}sp
; CHECK:       { nop; jal lr, sink }
; CHECK:       addi32{{(_w)?}}{{.*}}sp{{.*}}, 8
; CHECK:       { nop; jalr r0, lr, 0 }
  call void @sink(double 1.0, double 2.0, double 3.0, double 4.0,
                  double 0x400921FB54442D18)
  ret void
}
