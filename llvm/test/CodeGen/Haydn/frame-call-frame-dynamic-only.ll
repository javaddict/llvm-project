; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; REGRESSION TEST: F20 — one call-frame model (dynamic only).
;
; Bug: determineFrameLayout did `FrameSize += MaxCallFrameSize` while
; hasReservedCallFrame is always false and eliminateCallFramePseudoInstr
; already expands ADJCALLSTACKDOWN/UP to SUBI32/ADDI32. Both the reserved
; and dynamic models were active: every caller with outgoing stack args
; reserved MaxCallFrameSize in the prologue AND adjusted SP around the
; call. Inflated frames and scavenger thresholds. Guard was tautological
; (`!(false && …)`).
;
; Fix: drop FrameSize += MaxCallFrameSize. Keep the dynamic ADJCALLSTACK
; expansion. One mechanism, no per-function special case.
;
; Test design: two callers that differ only in outgoing stack-arg size.
; R1–R7 hold seven i32s; the 8th/9th force two 8-byte stack slots (16 B).
; Prologue CFA offsets must match (locals/CSR/scratch only). The
; stack-arg caller must still SUBI32/ADDI32 16 around the JAL. If the
; reserved add returns, [[SZ]] diverges by 16 and this fails.
;
; If this regresses, prologue of @stack_overflow_call grows by
; MaxCallFrameSize while the call-site bracket remains — dual model.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare i32 @seven(i32, i32, i32, i32, i32, i32, i32)
declare i32 @nine(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @reg_only_call() {
; CHECK-LABEL: reg_only_call:
; CHECK:       .cfi_def_cfa_offset 16
; CHECK-DAG:   lui{{.*}}seven
; CHECK-DAG:   addi32{{.*}}seven
; CHECK-DAG:   jalr{{.*}}lr
; CHECK:       { nop; jalr r0, lr, 0 }
  %r = call i32 @seven(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7)
  ret i32 %r
}

define i32 @stack_overflow_call() {
; CHECK-LABEL: stack_overflow_call:
; Extra CSR/scratch vs @reg_only_call is RA, not MaxCallFrameSize (16).
; Dual model would be 16+16=32.
; CHECK:       .cfi_def_cfa_offset 24
; CHECK-NOT:   .cfi_def_cfa_offset 32
; CHECK:       subi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; CHECK-DAG:   lui{{.*}}nine
; CHECK-DAG:   addi32{{.*}}nine
; CHECK:       { {{.*}}jalr{{.*}}lr{{.*}} }
; CHECK:       addi32{{(_w)?}}{{.*}}sp{{.*}}, 16
; CHECK:       { nop; jalr r0, lr, 0 }
  %r = call i32 @nine(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
                      i32 8, i32 9)
  ret i32 %r
}
