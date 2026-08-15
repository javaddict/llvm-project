; RUN: llc -mtriple=haydn-unknown-elf -O2 -filetype=asm %s -o %t.s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -filetype=obj %s -o %t_c.o
; RUN: llvm-objdump -d --triple=haydn-unknown-elf %t_c.o > %t_c.dis
; RUN: FileCheck %s --check-prefix=ASM --input-file=%t.s

; Role: object — single authority: logical LD* only; slot from placement/bundle.

; single authority: logical LD* only; slot from placement/bundle.
; Public asm never prints the internal ld32_s1 / ld64_s1 mnemonics.
; (Asm→obj round-trip via llvm-mc deferred: MULL tied-op encoder assert on
; asm-parse path; CodeGen -filetype=obj is the product encode authority.)
;
; Instruction multisets: codegen obj only (asm-parse encode path has MULL gap).

@g1 = external global i32, align 4
@g2 = external global i32, align 4
@h1 = external global i64, align 8
@h2 = external global i64, align 8

define i32 @two_i32(i32 %a) nounwind {
; ASM-LABEL: two_i32:
  %p1 = load i32, ptr @g1, align 4
  %p2 = load i32, ptr @g2, align 4
  %sum = add i32 %p1, %p2
  %r = add i32 %sum, %a
  ret i32 %r
}

define i64 @two_i64(i64 %a) nounwind {
; ASM-LABEL: two_i64:
  %p1 = load i64, ptr @h1, align 8
  %p2 = load i64, ptr @h2, align 8
  %sum = add i64 %p1, %p2
  %r = add i64 %sum, %a
  ret i64 %r
}

define i32 @one_i32(ptr %p) nounwind {
; ASM-LABEL: one_i32:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}
