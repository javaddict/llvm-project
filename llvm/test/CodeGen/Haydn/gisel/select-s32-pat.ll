; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — T7.4 G_SELECT contract: - i1 cond (s1 typed) → C++ residual MOVT32 (Pat is Cond s32 only).

; T7.4 G_SELECT contract:
;   - i1 cond (s1 typed) → C++ residual MOVT32 (Pat is Cond s32 only)
;   - s64 → dual MOVT multi-instr C++ residual
;   - both must select without abort

; CHECK-LABEL: sel_s32:
; CHECK: movt32

define i32 @sel_s32(i1 %c, i32 %t, i32 %f) {
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

; CHECK-LABEL: sel_s64:
; CHECK: movt64
define i64 @sel_s64(i1 %c, i64 %t, i64 %f) {
  %r = select i1 %c, i64 %t, i64 %f
  ret i64 %r
}
