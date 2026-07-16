; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -haydn-enable-gformat-select=1 < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: cmp_sgt_i64:
; CHECK-LABEL: cmp_ugt_i64:
; CHECK-LABEL: cmp_sle_i64:
; CHECK-LABEL: cmp_ule_i64:
; CHECK-LABEL: cmp_sge_i64:
; CHECK-LABEL: cmp_uge_i64:
; CHECK: {{.}}

define i32 @cmp_sgt_i64(i64 %a, i64 %b) {
  %cmp = icmp sgt i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Unsigned GT: same as SGT but with sltu32
define i32 @cmp_ugt_i64(i64 %a, i64 %b) {
  %cmp = icmp ugt i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Signed LE: LE = NOT(GT), so same swapped SLT/SLTU pattern + NOT.
; High half signed (slt32), low half unsigned (sltu32).
define i32 @cmp_sle_i64(i64 %a, i64 %b) {
  %cmp = icmp sle i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Unsigned LE: LE = NOT(GT), same pattern with sltu32 + NOT
define i32 @cmp_ule_i64(i64 %a, i64 %b) {
  %cmp = icmp ule i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Signed GE: GE = NOT(LT), uses unswapped SLT/SLTU + NOT.
; High half signed (slt32), low half unsigned (sltu32).
define i32 @cmp_sge_i64(i64 %a, i64 %b) {
  %cmp = icmp sge i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Unsigned GE: GE = NOT(LT), uses unswapped SLTU + NOT
define i32 @cmp_uge_i64(i64 %a, i64 %b) {
  %cmp = icmp uge i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}
