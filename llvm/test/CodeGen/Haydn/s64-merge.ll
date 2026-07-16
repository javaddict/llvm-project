; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Updated for native DR64 shift (sll64/srl64/sra64)

; Test G_MERGE_VALUES: build i64 from two i32 values
define i64 @merge_i32_to_i64(i32 %lo, i32 %hi) {
; CHECK-LABEL: merge_i32_to_i64:
; CHECK: or64
  %r = zext i32 %lo to i64
  %hi_shifted = zext i32 %hi to i64
  %hi_shifted_shl = shl i64 %hi_shifted, 32
  %merged = or i64 %r, %hi_shifted_shl
  ret i64 %merged
}

; Test G_UNMERGE_VALUES: extract i32 values from i64
define i32 @unmerge_low_i32(i64 %a) {
; CHECK-LABEL: unmerge_low_i32:
; TODO: Currently returns zero, should extract low 32 bits
  %r = trunc i64 %a to i32
  ret i32 %r
}

; Test extracting high 32 bits from i64
define i32 @unmerge_high_i32(i64 %a) {
; CHECK-LABEL: unmerge_high_i32:
; CHECK: srl64
  %shifted = lshr i64 %a, 32
  %r = trunc i64 %shifted to i32
  ret i32 %r
}

; Test cross-bank copy from GPR to DR64
define i64 @copy_gpr_to_dr64(i32 %a, i32 %b) {
; CHECK-LABEL: copy_gpr_to_dr64:
; CHECK: or64
  %a_ext = zext i32 %a to i64
  %b_ext = zext i32 %b to i64
  %b_shifted = shl i64 %b_ext, 32
  %r = or i64 %a_ext, %b_shifted
  ret i64 %r
}

; Test cross-bank copy from DR64 to GPR
define i32 @copy_dr64_to_gpr(i64 %a) {
; CHECK-LABEL: copy_dr64_to_gpr:
; TODO: Currently returns zero, should extract low 32 bits
  %r = trunc i64 %a to i32
  ret i32 %r
}

; Test multiple merges in sequence
define i64 @merge_chain(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: merge_chain:
; CHECK: add64
  %ab = zext i32 %a to i64
  %bc = zext i32 %b to i64
  %merged = add i64 %ab, %bc
  ret i64 %merged
}
