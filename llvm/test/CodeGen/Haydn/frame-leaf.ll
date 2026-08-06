; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	void_leaf                       // -- Begin function void_leaf
; CHECK: 	.type	void_leaf,@function
; CHECK-LABEL: void_leaf:                              // @void_leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; jalr{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	void_leaf, .Lfunc_end0-void_leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

;
; REGRESSION TEST: Leaf function frame lowering must NOT emit a redundant
; `.cfi_def_cfa_offset 0` directive when the stack frame is empty.
;
; Bug: Before the fix in, `emitPrologue` unconditionally emitted a
; `.cfi_def_cfa_offset <StackSize>` CFI directive for every function, even
; when StackSize == 0. Since `getInitialCFAOffset` already returns 0, the
; emitted `.cfi_def_cfa_offset 0` is a no-op that wastes code bytes on every
; function in the binary.
;
; Why this test exists: To guard against regression. If someone reverts the
; `if (AlignedStackSize != 0)` guard around the cfiDefCfaOffset emission in
; HaydnFrameLowering.cpp, this test will fail because the redundant
; `.cfi_def_cfa_offset 0` directive will reappear.
;
; Test design: We use four canonical leaf-function shapes (no-args void
; single-arg i32, multi-arg i32 arithmetic, i64 arithmetic) that require no
; stack allocation, no callee-saved spills, and no FP. Each must:
; 1. Emit ONLY `xor32 r0, r0, r0` as the prologue (no SP adjustment, no ST32)
; 2. NOT emit `.cfi_def_cfa_offset 0` — that's the load-bearing CHECK
; 3. Still emit the return sequence correctly

define void @void_leaf() {
; The load-bearing check: no redundant CFI directive
  ret void
}

define i32 @i32_leaf(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @i32_leaf_multi(i32 %a, i32 %b, i32 %c) {
  %s1 = add i32 %a, %b
  %s2 = mul i32 %s1, %c
  %s3 = sub i32 %s2, %a
  ret i32 %s3
}

define i64 @i64_leaf(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}
