# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Round-trip test for ALU instructions: asm → parse → print.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 0f 32 04 15 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}18: 07 cb 50 76 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}24: 07 eb 80 a9 00 00 00 00 00 00 00 00{{.*}}sub32s
# CHECK: {{.*}}30: 07 0b b1 0c 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}3c: 07 2b 11 32 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}48: 07 4b 41 65 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}54: 07 24 70 08 00 00 00 00 00 00 00 00{{.*}}not32
# CHECK: {{.*}}60: 07 0f 94 8a 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}6c: 07 0f b8 8c 07 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}78: 07 0f 0c 81 03 00 00 00 00 00 00 00{{.*}}xori32
# CHECK: {{.*}}84: 07 06 21 03 04 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}90: 07 06 42 05 08 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}9c: 07 06 64 07 10 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}a8: 07 cb 81 a9 00 00 00 00 00 00 00 00{{.*}}srl32
# CHECK: {{.*}}b4: 07 8b b1 0c 00 00 00 00 00 00 00 00{{.*}}sra32
# CHECK: {{.*}}c0: 07 eb 11 32 00 00 00 00 00 00 00 00{{.*}}sll32
# CHECK: {{.*}}cc: 07 44 40 05 00 00 00 00 00 00 00 00{{.*}}move32
# CHECK: {{.*}}d8: 07 0a 62 00 2a 00 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}e4: 07 24 71 08 00 00 00 00 00 00 00 00{{.*}}abs32s
# CHECK: {{.*}}f0: 07 0b 92 ba 00 00 00 00 00 00 00 00{{.*}}max32
# CHECK: {{.*}}fc: 07 4b c2 10 00 00 00 00 00 00 00 00{{.*}}min32
# CHECK: {{.*}}108: 07 44 21 03 00 00 00 00 00 00 00 00{{.*}}neg32
# CHECK: {{.*}}114: 07 8b 42 65 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}120: 07 ab 72 98 00 00 00 00 00 00 00 00{{.*}}sltu32
# CHECK: {{.*}}12c: 07 eb a2 cb 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK-NOT: <unknown>

# Round-trip test for ALU instructions: asm → parse → print.
# NOTE: The full encode→decode roundtrip (llvm-mc -filetype=obj | llvm-objdump)
# is not yet tested because the Haydn disassembler is not fully implemented
# (many instructions decode as <unknown>). When the disassembler is complete
# add objdump-based roundtrip tests.

# Arithmetic instructions

add32 r0, r1, r2

addi32 r3, r4, 42

sub32 r5, r6, r7

sub32s r8, r9, r10

# Logical instructions
and32 r11, r12, r0

or32 r1, r2, r3

xor32 r4, r5, r6

not32 r7, r8

# Logical immediate instructions
andi32 r9, r10, 255

ori32 r11, r12, 15

xori32 r0, r1, 7

# Shift instructions (immediate)
srli32 r2, r3, 4

srai32 r4, r5, 8

slli32 r6, r7, 16

# Shift instructions (register)
srl32 r8, r9, r10

sra32 r11, r12, r0

sll32 r1, r2, r3

# Move and load immediate
move32 r4, r5

lui r6, 42

# DSP/miscellaneous instructions
abs32s r7, r8

max32 r9, r10, r11

min32 r12, r0, r1

neg32 r2, r3

# Compare instructions
slt32 r4, r5, r6

sltu32 r7, r8, r9

seq32 r10, r11, r12
