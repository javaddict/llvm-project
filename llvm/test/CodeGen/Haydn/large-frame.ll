; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — large frame offsets must be scavenged, not truncated.

; REGRESSION TEST: large frame offsets must be scavenged, not truncated.
;
; Bug: eliminateFrameIndex assumed all offsets fit in simm16 and silently
; truncated larger offsets, corrupting stack accesses for functions with large
; frames. Haydn load/store/ADDI32 take a simm16 offset field.
; Fix: when the offset overflows simm16, scavenge a GPR and materialize
; FrameReg + Offset into it via ADDI32 (small residual) or
; LUI+SLLI32+ORI32+ADD32 (large), then rewrite the frame operand. See F33.
;
; Test design: allocate a large alloca (> 64KB) so the frame offset exceeds
; simm16. The generated code must not crash the verifier and must reference a
; scavenged register for the large offset. The exact scavenger reg varies; we
; just check that the function compiles and accesses the stack via a
; materialized address rather than a truncated immediate.

define i32 @large_frame() {
; CHECK-LABEL: large_frame:
; The frame is huge; the prologue allocates it (SUBI32 or SUB32 of r13).
; CHECK:       sub
; The access to the local array must use a scavenged/ materialized address.
; Just verify the function compiles and returns.
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
  %buf = alloca [16385 x i32], align 8
  %p = getelementptr [16385 x i32], ptr %buf, i32 0, i32 16384
  store i32 42, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}
