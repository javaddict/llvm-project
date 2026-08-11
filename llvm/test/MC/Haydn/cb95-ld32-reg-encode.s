# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>&1 | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS
#
# Reg-offset LS (s_lw_with_reg/s_sw_with_reg/d_ldw_with_reg/d_sdw_with_reg) must NOT encode
# as 16×0x00 Bundle128 NOP. Pre-fix: Bundle::add left SlotMap empty for
# FlexMap-less `_S0` opcodes → composite of 3 NOPs → PEI epilogue LR reload
# vanished in.o/.elf.
#
# ENC-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: s_lw_with_reg
# ENC: s_sw_with_reg
# ENC: d_ldw_with_reg
# ENC: d_sdw_with_reg
#
# DIS-NOT: {{{[[:space:]]*}}nop; nop; nop}
# DIS: s_lw_with_reg
# DIS: s_sw_with_reg
# DIS: d_ldw_with_reg
# DIS: d_sdw_with_reg

.text
.balign 16
  { s_lw_with_reg r1, r2, r3; nop; nop }
  { s_sw_with_reg r1, r2, r3; nop; nop }
  { d_ldw_with_reg d0, r2, r3; nop; nop }
  { d_sdw_with_reg d0, r2, r3; nop; nop }
  { s_lw_with_reg lr, sp, r12; nop; nop }
