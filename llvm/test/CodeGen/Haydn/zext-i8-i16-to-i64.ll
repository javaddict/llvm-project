; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Updated for native DR64 shift (sll64/srl64/sra64)
;
; REGRESSION TEST: G_ZEXT i8->i64 and i16->i64 must be selectable.
;
; Bug: The instruction selector had no pattern for zero-extending an i8 or i16
; value in GPR32 to an i64 value in DR64. The legalizer declared {s64, s8} and
; {s64, s16} as legal, so they passed through unmodified, but the selector's
; G_ZEXT case only handled s32->s64, s1->s32, s8->s32, and s16->s32. The
; missing s8->s64 and s16->s64 paths caused "cannot select" errors when
; compiling functions like crc32 that zero-extend byte values to uint64_t.
;
; Fix: Added s8->s64 and s16->s64 cases to the G_ZEXT handler. The approach:
; AND with 0xFF/0xFFFF to zero-extend to i32, then MOV_GPR_TO_DR64 with R0
; (always zero) as the upper half. The combiner may fold these into a single
; AND64 with a 64-bit mask constant.
;
; If this test fails with "cannot select", the s8/i16->s64 G_ZEXT path was
; removed or broken. Do NOT update CHECK lines without understanding the root
; cause.

; Test i8 -> i64 zero-extend
; CHECK-LABEL: test_zext_i8_to_i64:
; CHECK: and64
define i64 @test_zext_i8_to_i64(i8 %x) nounwind {
  %ext = zext i8 %x to i64
  ret i64 %ext
}

; Test i16 -> i64 zero-extend
; CHECK-LABEL: test_zext_i16_to_i64:
; CHECK: and64
define i64 @test_zext_i16_to_i64(i16 %x) nounwind {
  %ext = zext i16 %x to i64
  ret i64 %ext
}

; Test i8 -> i64 zero-extend used in arithmetic (realistic pattern from crc32)
; CHECK-LABEL: test_zext_i8_arith:
; CHECK: and64
; CHECK: add64
define i64 @test_zext_i8_arith(i8 %x, i64 %y) nounwind {
  %ext = zext i8 %x to i64
  %result = add i64 %ext, %y
  ret i64 %result
}

; Test i16 -> i64 zero-extend used in shift (common pattern).
; Shift amount only needs the low 32 bits, so GISel masks with and32
; (not and64) before sll64.
; CHECK-LABEL: test_zext_i16_shift:
; CHECK: and32
; CHECK: sll64
define i64 @test_zext_i16_shift(i16 %x, i64 %y) nounwind {
  %ext = zext i16 %x to i64
  %result = shl i64 %y, %ext
  ret i64 %result
}
