; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Load/store short-form immediates must fit the golden *scaled* signed
; 6-bit field (EA = base + (simm6 << log2(width))). Out-of-range offsets must
; rebase the address (ADDI/materialize) or use a register-offset form. Truncating
; the immediate is illegal.
;
; Focused symptom (byte/half):
; st8..., 32 -> scaled +32 (illegal; simm6 max +31)
; st16..., 64 -> scaled +32 (illegal)
; Legal boundary cases (offset 31 / 62) must still fold into the imm form.

@cb96_byte_obj = global [33 x i8] zeroinitializer
@cb96_half_obj = global [33 x i16] zeroinitializer

; ST8 offset 32: must NOT print st8 with imm 32
define void @cb96_st8_off32() {
; CHECK-LABEL: cb96_st8_off32:
; CHECK-NOT: st8{{.*}}, 32
; CHECK: st8
entry:
  %p = getelementptr inbounds [33 x i8], ptr @cb96_byte_obj, i32 0, i32 32
  store i8 126, ptr %p
  ret void
}

; ST16 offset 64: must NOT print st16 with imm 64
define void @cb96_st16_off64() {
; CHECK-LABEL: cb96_st16_off64:
; CHECK-NOT: st16{{.*}}, 64
; CHECK: st16
entry:
  %p = getelementptr inbounds [33 x i16], ptr @cb96_half_obj, i32 0, i32 32
  store i16 4660, ptr %p
  ret void
}

; Legal ST8 offset 31: may fold into imm
define void @cb96_st8_off31(ptr %base) {
; CHECK-LABEL: cb96_st8_off31:
; CHECK: st8{{.*}}, 31
entry:
  %p = getelementptr inbounds i8, ptr %base, i32 31
  store i8 1, ptr %p
  ret void
}

; Legal ST16 offset 62: may fold into imm (62/2 = 31)
define void @cb96_st16_off62(ptr %base) {
; CHECK-LABEL: cb96_st16_off62:
; CHECK: st16{{.*}}, 62
entry:
  %p = getelementptr inbounds i16, ptr %base, i32 31
  store i16 1, ptr %p
  ret void
}

; LDU8 offset 32: must NOT print ldu8 with imm 32
define i32 @cb96_ldu8_off32(ptr %base) {
; CHECK-LABEL: cb96_ldu8_off32:
; CHECK-NOT: ldu8{{.*}}, 32
; CHECK: ldu8
entry:
  %p = getelementptr inbounds i8, ptr %base, i32 32
  %v = load i8, ptr %p
  %z = zext i8 %v to i32
  ret i32 %z
}

; Large frame-index LD64: must not emit ld64/ld64 with out-of-range imm
; Stack object at a high offset forces PEI/eliminateFrameIndex through the
; scaled-simm6 gate (yarpgen path: ld64..., sp, 1168).
define i64 @cb96_ld64_large_frame() {
; CHECK-LABEL: cb96_ld64_large_frame:
; CHECK-NOT: ld64{{.*}}, 1168
; CHECK-NOT: ld64{{.*}}, 1168
entry:
  %buf = alloca [200 x i64], align 8
  %p = getelementptr inbounds [200 x i64], ptr %buf, i32 0, i32 146
  %v = load i64, ptr %p, align 8
  ret i64 %v
}
