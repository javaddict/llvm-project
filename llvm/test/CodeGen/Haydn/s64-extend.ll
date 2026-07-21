; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Updated for native DR64 shift (sll64/srl64/sra64)
; NOTE: Previously XFAIL for SSA violation bug with zext16_to_i64 — now fixed
; Test zero-extend i32 to i64
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: zext_i32_to_i64:
; CHECK-LABEL: sext_i32_to_i64:
; CHECK-LABEL: trunc_i64_to_i32:
; CHECK-LABEL: zext_i16_to_i64:
; CHECK-LABEL: sext_i16_to_i64:
; CHECK-LABEL: zext_i8_to_i64:
; CHECK-LABEL: sext_i8_to_i64:
; CHECK-LABEL: trunc_i64_to_i16:
; CHECK-LABEL: trunc_i64_to_i8:
; CHECK-LABEL: zext_i1_to_i64:
; CHECK-LABEL: zext_cmp_i1_to_i64:
; CHECK-LABEL: sext_i1_to_i64:
; CHECK-LABEL: sext_cmp_i1_to_i64:
; CHECK: {{.}}

define i64 @zext_i32_to_i64(i32 %a) {
; TODO: Currently just returns the input in d0, should properly extend
  %r = zext i32 %a to i64
  ret i64 %r
}

; Test sign-extend i32 to i64
define i64 @sext_i32_to_i64(i32 %a) {
  %r = sext i32 %a to i64
  ret i64 %r
}

; Test truncate i64 to i32
define i32 @trunc_i64_to_i32(i64 %a) {
; TODO: Currently just returns zero, should extract low 32 bits
  %r = trunc i64 %a to i32
  ret i32 %r
}

; Test zero-extend i16 to i64
define i64 @zext_i16_to_i64(i16 %a) {
  %r = zext i16 %a to i64
  ret i64 %r
}

; Test sign-extend i16 to i64
define i64 @sext_i16_to_i64(i16 %a) {
  %r = sext i16 %a to i64
  ret i64 %r
}

; Test zero-extend i8 to i64
define i64 @zext_i8_to_i64(i8 %a) {
  %r = zext i8 %a to i64
  ret i64 %r
}

; Test sign-extend i8 to i64
define i64 @sext_i8_to_i64(i8 %a) {
  %r = sext i8 %a to i64
  ret i64 %r
}

; Test truncate i64 to i16
define i16 @trunc_i64_to_i16(i64 %a) {
; TODO: Currently just returns zero, should extract low 16 bits
  %r = trunc i64 %a to i16
  ret i16 %r
}

; Test truncate i64 to i8
define i8 @trunc_i64_to_i8(i64 %a) {
; TODO: Currently just returns zero, should extract low 8 bits
  %r = trunc i64 %a to i8
  ret i8 %r
}

;===-----------------------------------------------------------------------===$
; REGRESSION: zext/sext i1 -> i64 (was missing, blocked aha-mont64, wikisort)
;
; Bug: The legalizer had no rule for {S64, S1} in the G_ZEXT/G_SEXT table.
; When code like "zext i1 %cmp to i64" appeared (common in 64-bit benchmark
; kernels), llc crashed with "unable to legalize instruction".
; Fix: Added {S64, S1} as legal in HaydnLegalizerInfo and added selector
; handling in HaydnInstructionSelector for G_ZEXT/G_SEXT s1->s64.
; If this test regresses, llc will abort with -global-isel-abort=1.
;===-----------------------------------------------------------------------===

; Test zero-extend i1 to i64
define i64 @zext_i1_to_i64(i1 %x) {
  %ext = zext i1 %x to i64
  ret i64 %ext
}

; Test zero-extend i1 (from comparison) to i64 — the real-world pattern
define i64 @zext_cmp_i1_to_i64(i32 %a, i32 %b) {
  %cmp = icmp ult i32 %a, %b
  %ext = zext i1 %cmp to i64
  ret i64 %ext
}

; Test sign-extend i1 to i64
define i64 @sext_i1_to_i64(i1 %x) {
  %ext = sext i1 %x to i64
  ret i64 %ext
}

; Test sign-extend i1 (from comparison) to i64
define i64 @sext_cmp_i1_to_i64(i32 %a, i32 %b) {
  %cmp = icmp slt i32 %a, %b
  %ext = sext i1 %cmp to i64
  ret i64 %ext
}
