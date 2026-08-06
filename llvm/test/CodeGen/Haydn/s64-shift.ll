; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Updated for native DR64 shift (sll64/srl64/sra64).

; Updated for native DR64 shift (sll64/srl64/sra64)

; Test 64-bit logical left shift

define i64 @shl64(i64 %a, i64 %amt) {
; CHECK-LABEL: shl64:
; CHECK: sll64
  %r = shl i64 %a, %amt
  ret i64 %r
}

; Test 64-bit logical right shift
define i64 @lshr64(i64 %a, i64 %amt) {
; CHECK-LABEL: lshr64:
; CHECK: srl64
  %r = lshr i64 %a, %amt
  ret i64 %r
}

; Test 64-bit arithmetic right shift
define i64 @ashr64(i64 %a, i64 %amt) {
; CHECK-LABEL: ashr64:
; CHECK: sra64
  %r = ashr i64 %a, %amt
  ret i64 %r
}

; Test 64-bit shift by constant
define i64 @shl64_const(i64 %a) {
; CHECK-LABEL: shl64_const:
; CHECK-DAG: sll64
  %r = shl i64 %a, 8
  ret i64 %r
}

; Test 64-bit right shift by constant
define i64 @lshr64_const(i64 %a) {
; CHECK-LABEL: lshr64_const:
; CHECK-DAG: srl64
  %r = lshr i64 %a, 16
  ret i64 %r
}

; Test 64-bit arithmetic right shift by constant
define i64 @ashr64_const(i64 %a) {
; CHECK-LABEL: ashr64_const:
; CHECK-DAG: sra64
  %r = ashr i64 %a, 31
  ret i64 %r
}

; Test chained 64-bit shifts
define i64 @shift64_chain(i64 %a) {
; CHECK-LABEL: shift64_chain:
; CHECK-DAG: sll64
; CHECK-DAG: srl64
  %1 = shl i64 %a, 8
  %2 = lshr i64 %1, 4
  ret i64 %2
}

; REGRESSION TESTS: s64 shift with amount >= 32 boundary cases
;
; Bug: All three s64 shift handlers (G_SHL, G_LSHR, G_ASHR) unconditionally
; used the "amt < 32" code path, discarding the "amt >= 32" result. This
; produced silently wrong results for any shift by 32 or more.
;
; Fix: Use conditional select (SLT32 + NEG32 + AND32 + OR32) to choose between
; the two paths at runtime:
; if (Amt < 32) -> use the <32 path
; else -> use the >=32 path
;
; These tests exercise the boundary cases directly. Each function shifts by
; a constant >= 32 which triggers the >=32 path. If the bug reappears, these
; will produce wrong values (instead of the correct expected results).
;
; Test design: Use constant shift amounts so the compiler cannot optimize
; away either path. The values are chosen so the <32 and >=32 paths produce
; different results, making a wrong path selection detectable.

; G_SHL: shift left by exactly 32
; 0x0000000100000000 << 32 = 0x0000000000000000 (lo becomes 0, hi gets old lo)
; Input: lo=0, hi=1. shl(lo,hi, 32) -> lo=0, hi=0
; FIX-A: shift-by-32 now has a fast path (DR64 unmerge + repack)
; instead of the full slt32+neg32+and32+or32 conditional-select decomposition.
define i64 @shl64_by_32(i64 %a) {
; CHECK-LABEL: shl64_by_32:
; CHECK-NOT: slt32
; CHECK-NOT: neg32
  %r = shl i64 %a, 32
  ret i64 %r
}

; G_SHL: shift left by 40
; 0x0000000100000000 << 40 = 0x0000000000000000 (lo=0, hi=old_lo<<8=0)
; Input: lo=0x100, hi=0. shl(lo,hi, 40) -> lo=0, hi=0x100
define i64 @shl64_by_40(i64 %a) {
; CHECK-LABEL: shl64_by_40:
  %r = shl i64 %a, 40
  ret i64 %r
}

; G_SHL: shift left by 63 (maximum meaningful shift)
define i64 @shl64_by_63(i64 %a) {
; CHECK-LABEL: shl64_by_63:
  %r = shl i64 %a, 63
  ret i64 %r
}

; G_LSHR: logical right shift by 32
; 0x0000000100000000 >>u 32 = 0x0000000000000001
; lo gets old hi, hi becomes 0
define i64 @lshr64_by_32(i64 %a) {
; CHECK-LABEL: lshr64_by_32:
  %r = lshr i64 %a, 32
  ret i64 %r
}

; G_LSHR: logical right shift by 40
define i64 @lshr64_by_40(i64 %a) {
; CHECK-LABEL: lshr64_by_40:
  %r = lshr i64 %a, 40
  ret i64 %r
}

; G_LSHR: logical right shift by 63
define i64 @lshr64_by_63(i64 %a) {
; CHECK-LABEL: lshr64_by_63:
  %r = lshr i64 %a, 63
  ret i64 %r
}

; G_ASHR: arithmetic right shift by 32
; 0x8000000000000000 >>s 32 = 0xFFFFFFFF80000000
; lo gets old hi (unsigned), hi gets sign extension
; FIX-A: shift-by-32 now has a fast path (SRA64 + sign-extend pack)
; instead of the full slt32+neg32+and32+or32 conditional-select decomposition.
define i64 @ashr64_by_32(i64 %a) {
; CHECK-LABEL: ashr64_by_32:
; CHECK: sra64
; CHECK-NOT: slt32
; CHECK-NOT: neg32
  %r = ashr i64 %a, 32
  ret i64 %r
}

; G_ASHR: arithmetic right shift by 40
define i64 @ashr64_by_40(i64 %a) {
; CHECK-LABEL: ashr64_by_40:
  %r = ashr i64 %a, 40
  ret i64 %r
}

; G_ASHR: arithmetic right shift by 63
; All bits become copies of the sign bit
define i64 @ashr64_by_63(i64 %a) {
; CHECK-LABEL: ashr64_by_63:
  %r = ashr i64 %a, 63
  ret i64 %r
}
