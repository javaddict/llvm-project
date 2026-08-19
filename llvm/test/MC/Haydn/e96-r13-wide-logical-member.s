# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=SUFFIX=1 -o /dev/null \
# RUN:   2>&1 | FileCheck %s --check-prefix=SUFFIX
# REQUIRES: haydn-registered-target

# Reloc `_W` logicals share the compact catalog member (ADDI32_W → ADDI32,
# BEQ_W → BEQ, JAL_W → JAL). Public asm is the logical mnemonic; suffix
# FieldSlot peers are not matcher results.

.ifndef SUFFIX
# CHECK-LABEL: <.text>:
# CHECK: addi32
# CHECK: ori32
# CHECK: beq
# CHECK: jal
# CHECK: nop
.text
  { addi32_w r1, r2, 1 }
  { ori32_w r3, r4, 1 }
  { beq_w r1, r2, 8 }
  { jal_w lr, 16 }
  { nop }
.endif

.ifdef SUFFIX
# SUFFIX: error: assembler matched a private placement opcode
addi32_w_s0 r1, r2, 1
ori32_w_s0 r3, r4, 1
beq_w_s0 r1, r2, 8
jal_w_s0 lr, 16
nop_s0
wfi_s0
.endif
