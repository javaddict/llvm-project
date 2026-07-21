# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>&1 | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS
#
# Reg-offset LS (ld32_reg/st32_reg/ld64_reg/st64_reg) must NOT encode
# as 16×0x00 Bundle128 NOP. Pre-fix: Bundle::add left SlotMap empty for
# FlexMap-less `_S0` opcodes → composite of 3 NOPs → PEI epilogue LR reload
# vanished in.o/.elf.
#
# ENC-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: ld32_reg
# ENC: st32_reg
# ENC: ld64_reg
# ENC: st64_reg
#
# DIS-NOT: {{{[[:space:]]*}}nop; nop; nop}
# DIS: ld32_reg
# DIS: st32_reg
# DIS: ld64_reg
# DIS: st64_reg

.text
.balign 16
  { ld32_reg r1, r2, r3; nop; nop }
  { st32_reg r1, r2, r3; nop; nop }
  { ld64_reg d0, r2, r3; nop; nop }
  { st64_reg d0, r2, r3; nop; nop }
  { ld32_reg lr, sp, r12; nop; nop }
