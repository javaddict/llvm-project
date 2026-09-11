; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s
;
; REGRESSION TEST: F24 — epilogue XOR32 R0 re-zero is gated.
;
; Bug: emitEpilogue emitted `XOR32 r0,r0,r0` in every epilogue, including
; leaf no-call frames with empty CSI. Caller-side re-zero after JAL/JALR
; is already HaydnExpandPseudos. The extra xor was dead on leaves and
; bloated every frameless/leaf function.
;
; Fix: one predicate — emit the epilogue xor iff hasCalls() || !CSI.empty().
; Leaf no-call empty-CSI skips it. A call, or a CSR spill (including leaf
; `~{lr}` force-save), keeps it so emitCSRLoad never sees a dirty R0.
;
; Test design:
;   @leaf_no_call  — prologue xor only; CHECK-NOT a second xor before ret.
;   @has_call      — post-call xor (ExpandPseudos) AND epilogue xor.
;   @leaf_csr      — empty hasCalls, non-empty CSI; epilogue xor stays.
;
; If the gate is dropped, @leaf_no_call grows a second xor32. If the CSI
; disjunct is dropped, @leaf_csr loses the xor before ld32 lr.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare i32 @extern(i32)

define i32 @leaf_no_call(i32 %x) {
; CHECK-LABEL: leaf_no_call:
; CHECK:       xor32 r0, r0, r0
; CHECK-NOT:   xor32 r0, r0, r0
; CHECK:       jalr r0, lr, 0
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @has_call(i32 %x) {
; CHECK-LABEL: has_call:
; CHECK:       xor32 r0, r0, r0
; CHECK:       lui r{{[0-9]+}}, extern
; CHECK:       addi32 r{{[0-9]+}}, r{{[0-9]+}}, extern
; CHECK:       jalr{{.*}}lr
; CHECK:       xor32 r0, r0, r0
; CHECK:       xor32 r0, r0, r0
; CHECK:       jalr r0, lr, 0
  %v = call i32 @extern(i32 %x)
  %r = add i32 %v, 1
  ret i32 %r
}

define void @leaf_csr() {
; CHECK-LABEL: leaf_csr:
; CHECK:       xor32 r0, r0, r0
; CHECK:       st32 lr, sp,
; CHECK:       {{//|#}}APP
; CHECK:       {{//|#}}NO_APP
; CHECK:       xor32 r0, r0, r0
; CHECK:       ld32 lr, sp,
; CHECK:       jalr r0, lr, 0
  call void asm sideeffect "", "~{lr}"()
  ret void
}
