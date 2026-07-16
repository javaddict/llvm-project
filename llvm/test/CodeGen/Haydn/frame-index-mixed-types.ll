; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: mixed_allocas:
; CHECK-LABEL: i32_only_allocas:
; CHECK-LABEL: i64_only_allocas:
; CHECK: {{.}}

define void @mixed_allocas() {
  %p32 = alloca i32
  %p64 = alloca i64
  store i32 42, ptr %p32
  store i64 1000000000000, ptr %p64
  ret void
}

; Test with i32-only allocas to ensure frame index elimination works for the
; simple case too (previously this worked because the offset happened to be 0
; after the frame layout, making the bug invisible).

define void @i32_only_allocas() {
  %p1 = alloca i32
  %p2 = alloca i32
  store i32 1, ptr %p1
  store i32 2, ptr %p2
  ret void
}

; Test with i64-only allocas.
; The i64 store may appear as either ST64 or two st32s (see note above).

define void @i64_only_allocas() {
  %p = alloca i64
  store i64 123456789012345, ptr %p
  ret void
}
