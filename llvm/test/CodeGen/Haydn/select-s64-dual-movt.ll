; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s64 G_SELECT lowers to dual MOVT32 on lo/hi (not NEG/AND/NOT/OR chain).

; s64 G_SELECT lowers to dual MOVT32 on lo/hi (not NEG/AND/NOT/OR chain).
; Enables full GenMux retirement (Pattern 1 no longer needed).


define i64 @select_s64(i32 %c, i64 %a, i64 %b) nounwind {
entry:
  %tobool = icmp ne i32 %c, 0
  %r = select i1 %tobool, i64 %a, i64 %b
  ret i64 %r
}

; Two half-selects via movt (lo + hi). Bitwise chain must not appear.
; CHECK-DAG: movt32
; CHECK-DAG: movt32
; CHECK-NOT: neg32
; CHECK-NOT: not32
