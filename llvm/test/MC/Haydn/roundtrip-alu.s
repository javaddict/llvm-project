# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Round-trip test for ALU instructions: asm → parse → print.
# NOTE: The full encode→decode roundtrip (llvm-mc -filetype=obj | llvm-objdump)
# is not yet tested because the Haydn disassembler is not fully implemented
# (many instructions decode as <unknown>). When the disassembler is complete
# add objdump-based roundtrip tests.

# Arithmetic instructions
# CHECK: add32 r0, r1, r2
add32 r0, r1, r2

# CHECK: addi32 r3, r4, 42
addi32 r3, r4, 42

# CHECK: sub32 r5, r6, r7
sub32 r5, r6, r7

# CHECK: sub32s r8, r9, r10
sub32s r8, r9, r10

# Logical instructions
# CHECK: and32 r11, r12, r0
and32 r11, r12, r0

# CHECK: or32 r1, r2, r3
or32 r1, r2, r3

# CHECK: xor32 r4, r5, r6
xor32 r4, r5, r6

# CHECK: not32 r7, r8
not32 r7, r8

# Logical immediate instructions
# CHECK: andi32 r9, r10, 255
andi32 r9, r10, 255

# CHECK: ori32 r11, r12, 15
ori32 r11, r12, 15

# CHECK: xori32 r0, r1, 7
xori32 r0, r1, 7

# Shift instructions (immediate)
# CHECK: srli32 r2, r3, 4
srli32 r2, r3, 4

# CHECK: srai32 r4, r5, 8
srai32 r4, r5, 8

# CHECK: slli32 r6, r7, 16
slli32 r6, r7, 16

# Shift instructions (register)
# CHECK: srl32 r8, r9, r10
srl32 r8, r9, r10

# CHECK: sra32 r11, r12, r0
sra32 r11, r12, r0

# CHECK: sll32 r1, r2, r3
sll32 r1, r2, r3

# Move and load immediate
# CHECK: move32 r4, r5
move32 r4, r5

# CHECK: lui r6, 42
lui r6, 42

# DSP/miscellaneous instructions
# CHECK: abs32s r7, r8
abs32s r7, r8

# CHECK: max32 r9, r10, r11
max32 r9, r10, r11

# CHECK: min32 r12, r0, r1
min32 r12, r0, r1

# CHECK: neg32 r2, r3
neg32 r2, r3

# Compare instructions
# CHECK: slt32 r4, r5, r6
slt32 r4, r5, r6

# CHECK: sltu32 r7, r8, r9
sltu32 r7, r8, r9

# CHECK: seq32 r10, r11, r12
seq32 r10, r11, r12
