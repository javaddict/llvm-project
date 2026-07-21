; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; REGRESSION TEST: byte/half load/store selection (ISA-43 bug 2).
;
; Bug: the selector collapsed every load/store to LD32/ST32, so a char*/short*
; access emitted a 32-bit memory op against a byte pointer. LD32 reads 4 bytes
; and once bundled into the word-indexed S_LW_WITH_IMM slot a byte offset is
; rescaled to a word index, corrupting the address (observed wild store to
; 0x1F9C). Fix: select LDU8/LDU16/ST8/ST16 for byte/half access (these already
; existed in the ISA but were never selected), and only fold element-aligned
; offsets. If this regresses, char access emits `ld32`/`st32` again.

; byte load -> ldu8 / s_lbu_* (NOT ld32 / s_lw)
define i32 @load_char(ptr %p) {
; CHECK-LABEL: load_char:
; CHECK-NOT: {{(^|[^_])ld32|s_lw}}
; CHECK: {{ldu8|s_lbu}}
  %q = getelementptr i8, ptr %p, i32 1
  %c = load i8, ptr %q
  %z = zext i8 %c to i32
  ret i32 %z
}

; byte store -> st8 / s_sb_* (NOT st32 / s_sw)
define void @store_char(ptr %p, i8 %v) {
; CHECK-LABEL: store_char:
; CHECK-NOT: {{(^|[^_])st32|s_sw}}
; CHECK: {{st8|s_sb}}
  %q = getelementptr i8, ptr %p, i32 2
  store i8 %v, ptr %q
  ret void
}

; half load -> ldu16 / s_lhwu_* (NOT ld32 / s_lw)
define i32 @load_short(ptr %p) {
; CHECK-LABEL: load_short:
; CHECK-NOT: {{(^|[^_])ld32|s_lw}}
; CHECK: {{ldu16|s_lhwu}}
  %q = getelementptr i16, ptr %p, i32 1
  %s = load i16, ptr %q
  %z = zext i16 %s to i32
  ret i32 %z
}

; i32 load with align 1 must NOT use word load
; Without explicit align 1, i32 ABI align is 4 and ld32 is legal even at GEP+1.
define i32 @load_i32_unaligned(ptr %p) {
; CHECK-LABEL: load_i32_unaligned:
; CHECK-NOT: {{(^|[^_])ld32|s_lw}}
; CHECK: {{ldu8|s_lbu}}
  %q = getelementptr i8, ptr %p, i32 1
  %v = load i32, ptr %q, align 1
  ret i32 %v
}

; i32 load with an ALIGNED offset (multiple of 4) MAY fold
define i32 @load_i32_aligned(ptr %p) {
; CHECK-LABEL: load_i32_aligned:
; CHECK: {{ld32|s_lw}}
  %q = getelementptr i32, ptr %p, i32 1
  %v = load i32, ptr %q
  ret i32 %v
}
