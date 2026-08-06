; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — various cast operations: trunc, sext, zext, fptoui, fptosi, uitofp, sitofp, bitcast, ptrtoint, inttoptr.

; Test various cast operations: trunc, sext, zext, fptoui, fptosi, uitofp, sitofp, bitcast, ptrtoint, inttoptr.

;Truncate i64 to i32

define i32 @trunc_i64_to_i32(i64 %val) {
; CHECK-LABEL: trunc_i64_to_i32:
  %r = trunc i64 %val to i32
  ret i32 %r
}

;Truncate i32 to i16
define i16 @trunc_i32_to_i16(i32 %val) {
; CHECK-LABEL: trunc_i32_to_i16:
  %r = trunc i32 %val to i16
  ret i16 %r
}

;Truncate i32 to i8
define i8 @trunc_i32_to_i8(i32 %val) {
; CHECK-LABEL: trunc_i32_to_i8:
  %r = trunc i32 %val to i8
  ret i8 %r
}

;Sign extend i8 to i32
define i32 @sext_i8_to_i32(i8 %val) {
; CHECK-LABEL: sext_i8_to_i32:
; CHECK: sll32
; CHECK: sra32
  %r = sext i8 %val to i32
  ret i32 %r
}

;Sign extend i16 to i32
define i32 @sext_i16_to_i32(i16 %val) {
; CHECK-LABEL: sext_i16_to_i32:
; CHECK: sll32
; CHECK: sra32
  %r = sext i16 %val to i32
  ret i32 %r
}

;Sign extend i32 to i64
define i64 @sext_i32_to_i64(i32 %val) {
; CHECK-LABEL: sext_i32_to_i64:
; Post-: sext i32→i64 selects directly to sext32t64 (was sra32-based lowering).
; CHECK: sext32t64
  %r = sext i32 %val to i64
  ret i64 %r
}

;Zero extend i8 to i32
define i32 @zext_i8_to_i32(i8 %val) {
; CHECK-LABEL: zext_i8_to_i32:
; CHECK: and32
  %r = zext i8 %val to i32
  ret i32 %r
}

;Zero extend i16 to i32
define i32 @zext_i16_to_i32(i16 %val) {
; CHECK-LABEL: zext_i16_to_i32:
; CHECK: and32
  %r = zext i16 %val to i32
  ret i32 %r
}

;Zero extend i32 to i64
define i64 @zext_i32_to_i64(i32 %val) {
; CHECK-LABEL: zext_i32_to_i64:
  %r = zext i32 %val to i64
  ret i64 %r
}

;Bitcast i32 to float (soft float, so this is just a bitcast)
define float @bitcast_i32_to_float(i32 %val) {
; CHECK-LABEL: bitcast_i32_to_float:
; Should be a no-op (just move bits)
  %r = bitcast i32 %val to float
  ret float %r
}

;Bitcast float to i32
define i32 @bitcast_float_to_i32(float %val) {
; CHECK-LABEL: bitcast_float_to_i32:
  %r = bitcast float %val to i32
  ret i32 %r
}

;Pointer to int
define i32 @ptrtoint_i32(ptr %ptr) {
; CHECK-LABEL: ptrtoint_i32:
  %r = ptrtoint ptr %ptr to i32
  ret i32 %r
}

;Pointer to i64
define i64 @ptrtoint_i64(ptr %ptr) {
; CHECK-LABEL: ptrtoint_i64:
  %r = ptrtoint ptr %ptr to i64
  ret i64 %r
}

;Int to pointer
define ptr @inttoptr(i32 %val) {
; CHECK-LABEL: inttoptr:
  %r = inttoptr i32 %val to ptr
  ret ptr %r
}

;Int to pointer from i64
define ptr @inttoptr_i64(i64 %val) {
; CHECK-LABEL: inttoptr_i64:
  %r = inttoptr i64 %val to ptr
  ret ptr %r
}

;Multiple casts in sequence
define i32 @cast_chain(i64 %val) {
; CHECK-LABEL: cast_chain:
; i64 -> i32 -> i64 -> i32
  %t1 = trunc i64 %val to i32
  %e1 = zext i32 %t1 to i64
  %t2 = trunc i64 %e1 to i32
  ret i32 %t2
}

;Cast between different pointer types
define ptr @bitcast_ptr(ptr %ptr) {
; CHECK-LABEL: bitcast_ptr:
  %r = bitcast ptr %ptr to ptr
  ret ptr %r
}

;Array decay to pointer
define ptr @array_decay(ptr %arr) {
; CHECK-LABEL: array_decay:
  %r = bitcast ptr %arr to ptr
  ret ptr %r
}
