; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; Role: semantic — 38bd405 hasFP/adjustsStack pin.
;
; A call (ADJCALLSTACK / MachineFrameInfo::adjustsStack) must not force a
; frame pointer. Peer: AIEBaseFrameLowering.cpp:40-43 and
; RISCVFrameLowering.cpp:464-470 (DisableFramePointerElim / VLA /
; frameaddress / realign only). Forcing FP on every caller reserved R14
; and rewrote the LR slot. Locals stay SP-relative; post-RA FI users add
; getCallFrameSPAdj so a live ADJCALLSTACKDOWN cannot land a pack store
; on outgoing stack args (frame-call-frame-pack-safe.ll).

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare i32 @seven(i32, i32, i32, i32, i32, i32, i32)
declare i32 @nine(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @reg_only_caller() {
; CHECK-LABEL: reg_only_caller:
; CHECK:       xor32 r0, r0, r0
; CHECK:       st32 lr,
; CHECK-NOT:   .cfi_def_cfa fp
; CHECK-NOT:   addi32{{(_w)?}} fp, sp
; CHECK-DAG:   lui{{.*}}seven
; CHECK-DAG:   addi32{{.*}}seven
; CHECK-DAG:   jalr{{.*}}lr
; CHECK:       { nop; jalr r0, lr, 0 }
  %r = call i32 @seven(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7)
  ret i32 %r
}

define i32 @stack_arg_caller() {
; CHECK-LABEL: stack_arg_caller:
; CHECK:       xor32 r0, r0, r0
; CHECK:       st32 lr,
; CHECK-NOT:   .cfi_def_cfa fp
; CHECK-NOT:   addi32{{(_w)?}} fp, sp
; CHECK:       subi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; CHECK-DAG:   lui{{.*}}nine
; CHECK-DAG:   addi32{{.*}}nine
; CHECK-DAG:   jalr{{.*}}lr
; CHECK:       addi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; CHECK:       { nop; jalr r0, lr, 0 }
  %r = call i32 @nine(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret i32 %r
}
