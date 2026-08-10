; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Load/store short-form immediates must fit the golden *scaled* signed 6-bit
; field: EA = base + (simm6 << log2(width)). An out-of-range offset must rebase
; the address or use a register-offset form. Truncating the immediate is
; illegal, and is the defect FORMAT-E-SWITCH-PLAN.md § 5.6 describes as one
; that compiles, encodes, disassembles, round-trips — and addresses `width`
; times too far.
;
; **Every assertion in this file used to be vacuous, and it passed anyway.**
; The negatives named Bundle128 spellings (`st8`, `ldu8`, `ld64`) that the
; retarget removed, so they could not fire; and the positives — a bare
; positive check on the token `st8` — matched the FUNCTION LABEL
; `cb96_st8_off32:`, not an instruction. A test of the migration's most consequential semantic change
; was checking nothing at all. Every check below is therefore anchored on the
; bundle-open brace so it can only match an instruction line.

@cb96_byte_obj = global [33 x i8] zeroinitializer
@cb96_half_obj = global [33 x i16] zeroinitializer

; Byte offset 32 needs simm6 = 32, one past the +31 maximum. The address has to
; be rebased and the surviving immediate must be 0 — NOT 32, which is what
; truncation would leave behind.
define void @cb96_st8_off32() {
; CHECK-LABEL: cb96_st8_off32:
; CHECK: {{.*}}addi32{{.*}}, 32
; CHECK: {{.*}}s_sb_with_imm{{[^;}]*}}, 0
; CHECK-NOT: {{.*}}s_sb_{{[a-z_]*}}{{[^;}]*}}, 32
entry:
  %p = getelementptr inbounds [33 x i8], ptr @cb96_byte_obj, i32 0, i32 32
  store i8 126, ptr %p
  ret void
}

; Halfword offset 64 is element 32, also one past the maximum. Same rebase.
define void @cb96_st16_off64() {
; CHECK-LABEL: cb96_st16_off64:
; CHECK: {{.*}}s_shw_with_imm{{[^;}]*}}, 0
; CHECK-NOT: {{.*}}s_shw_{{[a-z_]*}}{{[^;}]*}}, 64
; CHECK-NOT: {{.*}}s_shw_{{[a-z_]*}}{{[^;}]*}}, 32
entry:
  %p = getelementptr inbounds [33 x i16], ptr @cb96_half_obj, i32 0, i32 32
  store i16 4660, ptr %p
  ret void
}

; Byte offset 31 is the boundary and folds. Scale is 1, so the printed
; immediate is 31.
define void @cb96_st8_off31(ptr %base) {
; CHECK-LABEL: cb96_st8_off31:
; CHECK: {{.*}}s_sb_{{[a-z_]*}}{{[^;}]*}}, 31
entry:
  %p = getelementptr inbounds i8, ptr %base, i32 31
  store i8 1, ptr %p
  ret void
}

; Halfword element 31 is byte offset 62 and also folds — and this is the case
; that pins the UNITS. The field holds ELEMENTS, so the assembly prints 31, not
; 62. An expectation that "corrects" this back to the byte offset is the defect
; (§ 5.4 makes the same point about `s_lw_pre_imm r3, r1, 1`).
define void @cb96_st16_off62(ptr %base) {
; CHECK-LABEL: cb96_st16_off62:
; CHECK: {{.*}}s_shw_{{[a-z_]*}}{{[^;}]*}}, 31
; CHECK-NOT: {{.*}}s_shw_{{[a-z_]*}}{{[^;}]*}}, 62
entry:
  %p = getelementptr inbounds i16, ptr %base, i32 31
  store i16 1, ptr %p
  ret void
}

; Loads take the same gate as stores.
define i32 @cb96_ldu8_off32(ptr %base) {
; CHECK-LABEL: cb96_ldu8_off32:
; CHECK: {{.*}}s_lbu_with_imm{{[^;}]*}}, 0
; CHECK-NOT: {{.*}}s_lbu_{{[a-z_]*}}{{[^;}]*}}, 32
entry:
  %p = getelementptr inbounds i8, ptr %base, i32 32
  %v = load i8, ptr %p
  %z = zext i8 %v to i32
  ret i32 %z
}

; A large frame index is the case § 5.6 says stops being an addressing mode at
; all: byte 1168 is element 146 for a doubleword, far past +31, so PEI has to
; materialize the address and the access becomes a REGISTER-offset form.
define i64 @cb96_ld64_large_frame() {
; CHECK-LABEL: cb96_ld64_large_frame:
; CHECK: {{.*}}d_ldw_with_reg{{.*}}
; CHECK-NOT: {{.*}}d_ldw_{{[a-z_]*}}{{[^;}]*}}, 1168
; CHECK-NOT: {{.*}}d_ldw_{{[a-z_]*}}{{[^;}]*}}, 146
entry:
  %buf = alloca [200 x i64], align 8
  %p = getelementptr inbounds [200 x i64], ptr %buf, i32 0, i32 146
  %v = load i64, ptr %p, align 8
  ret i64 %v
}
