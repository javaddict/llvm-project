; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/vla_realign.ll 2>&1 | FileCheck %s --check-prefix=VLA
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/vla_align8.ll
;
; Role: semantic — VLA + MaxAlign > StackAlign(8) stays fail-closed.
; No base pointer (BP folded into FP). Static overalign realigns
; (stack-realign-align64.ll); a VLA that also requests align > 8 would
; move the post-AND SP used as the local base
; (RISCVFrameLowering.cpp:494-505 hasBP). Do not invent a BP.

;--- vla_realign.ll
define ptr @vla_align16(i32 %n) {
; VLA: {{stack object alignment|unable to legalize instruction|base pointer|G_DYN_STACKALLOC}}
  %p = alloca i32, i32 %n, align 16
  ret ptr %p
}

;--- vla_align8.ll
define ptr @vla_align8(i32 %n) {
  %p = alloca i32, i32 %n
  ret ptr %p
}
