; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Test extended loads: sextload, zextload, and extload patterns.
; These are common when loading smaller types into larger registers.
; NOTE: CHECKs reflect post- scheduled output (load + shift-amount materialization
; may pack in either slot order).
;
; ISA-43 bug 2: byte/half loads select LDU8/LDU16 (NOT LD32). i32 loads still
; use LD32. Sign/zero extension is done by the subsequent G_SEXT/G_ZEXT lowering
; (sll32+sra32 / and32) — LDU8/LDU16 zero-extend the narrow value into GPR32.

;Sign-extending load i8 -> i32
define i32 @sextload_i8_to_i32(ptr %ptr) {
; CHECK-LABEL: sextload_i8_to_i32:
; CHECK-DAG: ldu8
; CHECK-DAG: addi32
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load i8, ptr %ptr
  %r = sext i8 %v to i32
  ret i32 %r
}

;Zero-extending load i8 -> i32
define i32 @zextload_i8_to_i32(ptr %ptr) {
; CHECK-LABEL: zextload_i8_to_i32:
; CHECK-DAG: ldu8
; CHECK-DAG: addi32
; CHECK-DAG: and32
  %v = load i8, ptr %ptr
  %r = zext i8 %v to i32
  ret i32 %r
}

;Sign-extending load i16 -> i32
define i32 @sextload_i16_to_i32(ptr %ptr) {
; CHECK-LABEL: sextload_i16_to_i32:
; CHECK-DAG: ldu16
; CHECK-DAG: addi32
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load i16, ptr %ptr
  %r = sext i16 %v to i32
  ret i32 %r
}

;Zero-extending load i16 -> i32
define i32 @zextload_i16_to_i32(ptr %ptr) {
; CHECK-LABEL: zextload_i16_to_i32:
; CHECK-DAG: ldu16
; CHECK-DAG: addi32
; CHECK-DAG: and32
  %v = load i16, ptr %ptr
  %r = zext i16 %v to i32
  ret i32 %r
}

;Load i8 with sign extension to i64
define i64 @sextload_i8_to_i64(ptr %ptr) {
; CHECK-LABEL: sextload_i8_to_i64:
; CHECK-DAG: ldu8
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load i8, ptr %ptr
  %r = sext i8 %v to i64
  ret i64 %r
}

;Load i16 with sign extension to i64
define i64 @sextload_i16_to_i64(ptr %ptr) {
; CHECK-LABEL: sextload_i16_to_i64:
; CHECK-DAG: ldu16
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load i16, ptr %ptr
  %r = sext i16 %v to i64
  ret i64 %r
}

;Load i32 with sign extension to i64
define i64 @sextload_i32_to_i64(ptr %ptr) {
; CHECK-LABEL: sextload_i32_to_i64:
; CHECK-DAG: ld32
; Post-: sext i32->i64 selects directly to sext32t64 (was sra32-based lowering).
; CHECK-DAG: sext32t64
  %v = load i32, ptr %ptr
  %r = sext i32 %v to i64
  ret i64 %r
}

;Load i32 with zero extension to i64
define i64 @zextload_i32_to_i64(ptr %ptr) {
; CHECK-LABEL: zextload_i32_to_i64:
; CHECK-DAG: ld32
; CHECK-NOT: sxt
  %v = load i32, ptr %ptr
  %r = zext i32 %v to i64
  ret i64 %r
}

;Array element access with extload (common pattern)
define i32 @array_sext_i8(ptr %array, i32 %index) {
; CHECK-LABEL: array_sext_i8:
; Byte load may be fused s_lbu_pre_reg or split add32+ldu8.
; CHECK-DAG: {{s_lbu_pre_reg|ldu8|add32}}
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %ptr = getelementptr i8, ptr %array, i32 %index
  %v = load i8, ptr %ptr
  %r = sext i8 %v to i32
  ret i32 %r
}

;Multiple extloads in sequence
define i32 @multiple_extloads(ptr %p1, ptr %p2) {
; CHECK-LABEL: multiple_extloads:
; CHECK-DAG: ldu8
; CHECK-DAG: sll32
; CHECK-DAG: sra32
; CHECK-DAG: ldu16
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v1 = load i8, ptr %p1
  %e1 = sext i8 %v1 to i32
  %v2 = load i16, ptr %p2
  %e2 = sext i16 %v2 to i32
  %r = add i32 %e1, %e2
  ret i32 %r
}

;Volatile extload
define i32 @volatile_sextload(ptr %ptr) {
; CHECK-LABEL: volatile_sextload:
; CHECK-DAG: ldu8
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load volatile i8, ptr %ptr
  %r = sext i8 %v to i32
  ret i32 %r
}

;Aligned vs unaligned extload
; Align-1 i16 must expand to byte loads (ldu16 would ALIGNMENT-fault).
define i32 @unaligned_sextload(ptr %ptr) {
; CHECK-LABEL: unaligned_sextload:
; CHECK-NOT: ldu16
; CHECK-NOT: ld16
; CHECK-DAG: ldu8
; CHECK-DAG: sll32
; CHECK-DAG: sra32
  %v = load i16, ptr %ptr, align 1
  %r = sext i16 %v to i32
  ret i32 %r
}
