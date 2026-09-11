; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s64 G_SELECT lowers to native MOVT64 (not NEG/AND/NOT/OR chain).

; s64 G_SELECT lowers to native MOVT64 (D1.62 v64:64). Bitwise chain
; must not appear. Enables full GenMux retirement (Pattern 1 no longer needed).


define i64 @select_s64(i32 %c, i64 %a, i64 %b) nounwind {
entry:
  %tobool = icmp ne i32 %c, 0
  %r = select i1 %tobool, i64 %a, i64 %b
  ret i64 %r
}

; Native 64-bit select. Bitwise chain must not appear.
; CHECK-DAG: movt64
; CHECK-NOT: neg32
; CHECK-NOT: not32
