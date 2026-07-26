; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; T1.1/T1.4: 1:1 Pats for G_MUL s32/v2i32 and s64 shifts.
; G_SMULH/UMULH covered by scalar-mulh-isel.mir (Pat → MULSSH/MULUUH).
; Logical opcodes only (mull / x2mulpl32 / sll64 / srl64 / sra64).

define i32 @mul32(i32 %a, i32 %b) {
; CHECK-LABEL: mul32:
; CHECK: // %bb.0:
; CHECK: mull
; CHECK: jalr{{.*}}lr
entry:
  %r = mul i32 %a, %b
  ret i32 %r
}

define <2 x i32> @mul_v2i32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: mul_v2i32:
; CHECK: // %bb.0:
; CHECK: x2mulpl32
; CHECK: jalr{{.*}}lr
entry:
  %r = mul <2 x i32> %a, %b
  ret <2 x i32> %r
}

define i64 @shl64(i64 %a, i32 %s) {
; CHECK-LABEL: shl64:
; CHECK: // %bb.0:
; CHECK: sll64
; CHECK: jalr{{.*}}lr
entry:
  %z = zext i32 %s to i64
  %r = shl i64 %a, %z
  ret i64 %r
}

define i64 @lshr64(i64 %a, i32 %s) {
; CHECK-LABEL: lshr64:
; CHECK: // %bb.0:
; CHECK: srl64
; CHECK: jalr{{.*}}lr
entry:
  %z = zext i32 %s to i64
  %r = lshr i64 %a, %z
  ret i64 %r
}

define i64 @ashr64(i64 %a, i32 %s) {
; CHECK-LABEL: ashr64:
; CHECK: // %bb.0:
; CHECK: sra64
; CHECK: jalr{{.*}}lr
entry:
  %z = zext i32 %s to i64
  %r = ashr i64 %a, %z
  ret i64 %r
}
