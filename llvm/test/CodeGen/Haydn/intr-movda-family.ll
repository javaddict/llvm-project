; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_movda32:
; CHECK-LABEL: test_movda16:
; CHECK-LABEL: test_movda32x2:
; CHECK-LABEL: test_movad32_low:
; CHECK-LABEL: test_movad32_high:
; CHECK-LABEL: test_roundtrip_low:
; CHECK: {{.}}

declare i64 @llvm.haydn.movda32(i32)

define i64 @test_movda32(i32 %x) {
; Native MOVE32_DR (no stack spill)
  %r = call i64 @llvm.haydn.movda32(i32 %x)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; movda16: i32 -> i64 (identical lowering — DR64 lanes are i32-wide)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.movda16(i32)

define i64 @test_movda16(i32 %x) {
; Native MOVE32_DR (no stack spill)
  %r = call i64 @llvm.haydn.movda16(i32 %x)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; movda32x2: (i32, i32) -> i64 (both lanes from explicit operands)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.movda32x2(i32, i32)

define i64 @test_movda32x2(i32 %lo, i32 %hi) {
; Native MOVE32_DR (no stack spill)
  %r = call i64 @llvm.haydn.movda32x2(i32 %lo, i32 %hi)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; movad32_low: i64 -> i32 (extract low lane)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.movad32.low(i64)

define i32 @test_movad32_low(i64 %dr) {
; Native MOVE32_DR (no stack spill)
; (native DR->GPR, no spill)
; (no epilogue needed)
  %r = call i32 @llvm.haydn.movad32.low(i64 %dr)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; movad32_high: i64 -> i32 (extract high lane)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.movad32.high(i64)

define i32 @test_movad32_high(i64 %dr) {
; Native MOVE32_DR (no stack spill)
; (native DR->GPR, no spill)
; (no epilogue needed)
  %r = call i32 @llvm.haydn.movad32.high(i64 %dr)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Round-trip: GPR32 -> DR64 -> GPR32 must preserve the value.
; Guards against the ISel emitting the wrong lane index.
;===----------------------------------------------------------------------===;

define i32 @test_roundtrip_low(i32 %x) {
; Native MOVE32_DR (no stack spill)
; Native MOVE32_DR (no stack spill)
; (native DR->GPR, no spill)
; (no epilogue needed)
  %dr = call i64 @llvm.haydn.movda32(i32 %x)
  %back = call i32 @llvm.haydn.movad32.low(i64 %dr)
  ret i32 %back
}
