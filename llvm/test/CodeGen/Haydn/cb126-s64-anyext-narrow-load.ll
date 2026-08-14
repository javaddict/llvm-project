; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — CB-126: G_LOAD s64 from sub-word mem must legalize (not ICE).

; CB-126: G_LOAD s64 from sub-word mem must legalize (not ICE).
; Expect anyext path: narrow load + widen, no backend crash.

define i64 @load_s8_to_s64(ptr %p) {
; CHECK-LABEL: load_s8_to_s64:
; CHECK: {{ldu8|ld8|s_lbu}}
; CHECK: jalr
  %v = load i8, ptr %p, align 1
  %e = zext i8 %v to i64
  ret i64 %e
}

define i64 @load_s16_to_s64(ptr %p) {
; CHECK-LABEL: load_s16_to_s64:
; CHECK: {{ldu16|ld16|s_lhw}}
; CHECK: jalr
  %v = load i16, ptr %p, align 2
  %e = zext i16 %v to i64
  ret i64 %e
}

; Packed-struct style: i64 anyext from unaligned i8 field offset.
%struct.pack = type <{ i8, i8, i8, i8, i8, i8, i8, i8 }>
define i64 @packed_byte7(ptr %s) {
; CHECK-LABEL: packed_byte7:
; CHECK: jalr
  %p = getelementptr inbounds %struct.pack, ptr %s, i32 0, i32 7
  %v = load i8, ptr %p, align 1
  %e = zext i8 %v to i64
  ret i64 %e
}
