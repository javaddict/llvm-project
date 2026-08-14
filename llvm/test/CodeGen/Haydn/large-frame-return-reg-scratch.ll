; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -O2 < %s | FileCheck %s

; Role: semantic — large-frame PEI scratch must not clobber RetCC R1/R2.
; >32 KiB fixed frame forces MatInt + ADD32/SUB32 via getPEIScratchReg.
; getPEIScratchReg permanently excludes R1/R2 (RetCC_Haydn).

; CHECK-LABEL: large_scalar_ret:
; Large adjust: MatInt into a non-RetCC temp, then sub/add sp.
; CHECK:       addi32{{[^,]*}}, r0, {{[0-9]+}}
; The MatInt dest must not be r1/r2 (return regs). Accept r3-r7 or r12.
; CHECK-NOT:   addi32{{.*}}r1, r0, {{[0-9][0-9][0-9][0-9]+}}
; CHECK-NOT:   addi32{{.*}}r2, r0, {{[0-9][0-9][0-9][0-9]+}}
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
define i32 @large_scalar_ret(i32 %a, i32 %b) {
  %buf = alloca [20000 x i32], align 8
  %p = getelementptr [20000 x i32], ptr %buf, i32 0, i32 19999
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %sum = add i32 %v, %b
  ret i32 %sum
}

; CHECK-LABEL: large_two_i32_ret:
; CHECK-NOT:   addi32{{.*}}r1, r0, {{[0-9][0-9][0-9][0-9]+}}
; CHECK-NOT:   addi32{{.*}}r2, r0, {{[0-9][0-9][0-9][0-9]+}}
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
define { i32, i32 } @large_two_i32_ret(i32 %a, i32 %b) {
  %buf = alloca [20000 x i32], align 8
  %p = getelementptr [20000 x i32], ptr %buf, i32 0, i32 19999
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r0 = insertvalue { i32, i32 } undef, i32 %v, 0
  %r1 = insertvalue { i32, i32 } %r0, i32 %b, 1
  ret { i32, i32 } %r1
}
