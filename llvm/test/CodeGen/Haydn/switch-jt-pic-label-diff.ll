; RUN: llc -mtriple=haydn-unknown-elf -relocation-model=pic -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; PIC/PIE jump tables use EK_LabelDifference32: each.long is (target - JTBase).
; G_BRJT must emit:
; load EntryVal from JTBase+index*4
; add Abs, EntryVal, JTBase; REQUIRED under PIC
; jalr Abs
; Without the add, BundleSim takes jalr of a signed relative (e.g. 0xffff5494)
; → BAD_PC. llvm-libc builds with -fpie; printf_core switches hit this path.

define i32 @switch_jt_pic(i32 %x) nounwind {
entry:
  switch i32 %x, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
    i32 4, label %bb4
    i32 5, label %bb5
    i32 6, label %bb6
    i32 7, label %bb7
  ]
bb0: ret i32 10
bb1: ret i32 20
bb2: ret i32 30
bb3: ret i32 40
bb4: ret i32 50
bb5: ret i32 60
bb6: ret i32 70
bb7: ret i32 80
default: ret i32 0
}

; CHECK-LABEL: switch_jt_pic:
; The jump-table base and the scaled index share one bundle; print is
; s2-s1-s0, so slli32 (s2) comes out before the lui's .LJTI0_0 operand (s0).
; CHECK:       slli32
; CHECK:       .LJTI0_0
; CHECK:       {{s_lw_pre_reg|ld32}}
; After load of a LabelDifference32 entry, ADD base back before jalr:
; CHECK:       add32
; CHECK:       jalr
; CHECK:       .LJTI0_0:
; CHECK:       .long	.LBB0_{{[0-9]+}}-.LJTI0_0
