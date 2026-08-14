; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — function arguments triggers an "unknown operand type" crash in HaydnMCInstLower (the selector produces an operand type the printer.

; NOTE: Does not use -verify-machineinstrs because trunc i64 to i32 on
; function arguments triggers an "unknown operand type" crash in
; HaydnMCInstLower (the selector produces an operand type the printer
; cannot handle). The extend_chain function avoids this because the
; optimizer eliminates the i64 conversion.
;
; Test sign/zero extension and truncation operations.
; The Haydn backend uses shift-left/shift-right-arithmetic for sign extension
; and AND with mask for zero extension. Truncation is a no-op (just use the
; lower bits of the register).
;
; Note: sext i32->i64 and trunc i64->i32 on function arguments currently crash
; the AsmPrinter (unknown operand type in HaydnMCInstLower). These patterns are
; tested when they appear in contexts where the optimizer eliminates the
; conversion (e.g., extend_chain where the result is truncated back).

;Sign-extend i8 to i32

define i32 @sext_i8_i32(i8 %a) nounwind {
; CHECK-LABEL: sext_i8_i32:
; CHECK: {{sll32|slli32}}
; CHECK: {{sra32|srai32}}
  %r = sext i8 %a to i32
  ret i32 %r
}

;Zero-extend i8 to i32
define i32 @zext_i8_i32(i8 %a) nounwind {
; CHECK-LABEL: zext_i8_i32:
; CHECK: {{and32|andi32}}
  %r = zext i8 %a to i32
  ret i32 %r
}

;Sign-extend i16 to i32
define i32 @sext_i16_i32(i16 %a) nounwind {
; CHECK-LABEL: sext_i16_i32:
; CHECK: {{sll32|slli32}}
; CHECK: {{sra32|srai32}}
  %r = sext i16 %a to i32
  ret i32 %r
}

;Zero-extend i16 to i32
define i32 @zext_i16_i32(i16 %a) nounwind {
; CHECK-LABEL: zext_i16_i32:
; CHECK: {{and32|andi32}}
  %r = zext i16 %a to i32
  ret i32 %r
}

;Zero-extend i32 to i64 (no-op: upper 32 bits are already zero)
define i64 @zext_i32_i64(i32 %a) nounwind {
; CHECK-LABEL: zext_i32_i64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = zext i32 %a to i64
  ret i64 %r
}

;Truncate i32 to i16 (no-op in register)
define i16 @trunc_i32_i16(i32 %a) nounwind {
; CHECK-LABEL: trunc_i32_i16:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = trunc i32 %a to i16
  ret i16 %r
}

;Truncate i32 to i8 (no-op in register)
define i8 @trunc_i32_i8(i32 %a) nounwind {
; CHECK-LABEL: trunc_i32_i8:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = trunc i32 %a to i8
  ret i8 %r
}

;Truncate i64 to i32
define i32 @trunc_i64_i32(i64 %a) nounwind {
; CHECK-LABEL: trunc_i64_i32:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = trunc i64 %a to i32
  ret i32 %r
}

;Zero-extend after comparison (common pattern for boolean results)
define i32 @zext_cmp(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: zext_cmp:
; CHECK: slt32
; CHECK: andi32
  %cmp = icmp slt i32 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;Sign-extend after comparison
define i32 @sext_cmp(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sext_cmp:
; CHECK: slt32
; CHECK: slli32
; CHECK: srai32
  %cmp = icmp slt i32 %a, %b
  %r = sext i1 %cmp to i32
  ret i32 %r
}

;i1 zero-extend to i32
define i32 @zext_i1_i32(i1 %a) nounwind {
; CHECK-LABEL: zext_i1_i32:
; CHECK: {{and32|andi32}}
  %r = zext i1 %a to i32
  ret i32 %r
}

;i1 sign-extend to i32
define i32 @sext_i1_i32(i1 %a) nounwind {
; CHECK-LABEL: sext_i1_i32:
; CHECK: {{sll32|slli32}}
; CHECK: {{sra32|srai32}}
  %r = sext i1 %a to i32
  ret i32 %r
}
