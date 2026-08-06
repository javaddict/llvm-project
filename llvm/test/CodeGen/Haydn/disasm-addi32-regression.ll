; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t.o && llvm-objdump -d %t.o | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: object — Disassembler must correctly decode ADDI32/SUBI32 with high register numbers (r12-r15) as FmtI instructions, not as DSP.


;
; REGRESSION TEST: Disassembler must correctly decode ADDI32/SUBI32 with
; high register numbers (r12-r15) as FmtI instructions, not as DSP
; FmtALU32 instructions.
;
; Bug: FmtI (6-bit opcode in [29:24]) and FmtALU32 DSP (8-bit opcode in
; [29:22]) share the same Inst[31:24] byte space. When rd >= 12, bits
; [23:22]=0b11 overlap with FmtALU32 opcode bits [1:0]=0b11, causing
; mis-decodes like:
; addi32 fp, sp, 0 -> d_sdw_with_reg r11, r4, r0
; subi32 sp, sp, 8 -> d_sw_l_pre_reg r7, r4, r0
;
; Fix: Marked 23 auto-generated DSP FmtALU32 instructions as
; isAsmParserOnly=1 to exclude them from the disassembler decoder table.
; See ISA-01 in ~/haydn-plans/isa-improve/ for the root cause analysis.
;
; This test uses -O0 to force frame setup with SP/FP/LR (R13/R14/R15)
; which triggers the high-register-number code paths. If the disassembler
; regresses, you will see DSP instruction names (d_sdw_*, d_shw_*, etc.)
; instead of addi32/subi32.

define i32 @addi32_high_reg_test(i32 %a, i32 %b) {
; CHECK-LABEL: <addi32_high_reg_test>:
; CHECK-NOT: d_sdw
; CHECK-NOT: d_shw
; CHECK-NOT: d_sw_
; CHECK-NOT: d_lw_
; CHECK-NOT: s_sw_
; CHECK-NOT: s_lw_
  %sum = add i32 %a, %b
  ret i32 %sum
}
