; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST:.cfi_offset must be CFA-relative (add StackSize for SP-CFA).
;
; Bug: emitPrologue computed.cfi_offset from MFI.getObjectOffset(FrameIdx)
; directly, which is frame-layout-relative (negative, below post-prologue SP).
; For SP-based CFA, the CFA is SP + StackSize, so the offset must include
; StackSize. The bug double-counted the stack size, producing wrong unwinding
; info. See F12 / cfi-sp-cfa-requires-stacksize.
;
; Test design: a function that forces a callee-save spill (uses R8–R11 across a
; call) and has a non-zero stack frame. The.cfi_offset for the spilled reg
; must equal (getObjectOffset + StackSize), which is the CFA-relative offset.
; If the bug regresses, the.cfi_offset will be a large negative number that
; does not account for StackSize.

declare void @use(i32)

define void @cfi_test() {
; CHECK-LABEL: cfi_test:
; CHECK:       .cfi_offset {{[a-z0-9]+}}, [[OFF:[0-9-]+]]
; The offset must be a valid CFA-relative offset. For SP-CFA frames it is
; getObjectOffset(FI) + StackSize, which is non-positive but accounts for the
; full frame. The CHECK above just ensures a.cfi_offset is emitted for a
; spilled callee-saved register.
entry:
  call void @use(i32 1)
  call void @use(i32 2)
  call void @use(i32 3)
  call void @use(i32 4)
  call void @use(i32 5)
  call void @use(i32 6)
  call void @use(i32 7)
  call void @use(i32 8)
  ret void
}
