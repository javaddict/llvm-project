# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck --check-prefix=ASM %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck --check-prefix=OBJ %s
#
# LD16 / LD8 Bundle128 encode + disasm.
# Public mnemonics are logical ld16/ld8; private encode peers LD16_S0/LD8_S0
# (HaydnFormatsLS.td) pair via FlexMap at MC materialize. Disassembler MaxOpcode
# for LS FU must cover 0x7A/0x7B (not stop at LDU16=0x79).

# ASM: { s_lhws_{{[a-z_]*}}{{.*}} r3, r4, 0
# ASM: { s_lhws_{{[a-z_]*}}{{.*}} r5, r6, 2
# ASM: { s_lbs_{{[a-z_]*}}{{.*}} r7, r8, 0
# ASM: { s_lbs_{{[a-z_]*}}{{.*}} r9, r10, 1
s_lhws_with_imm r3, r4, 0
s_lhws_with_imm r5, r6, 1
s_lbs_with_imm  r7, r8, 0
s_lbs_with_imm  r9, r10, 1

# OBJ: {{.*}} s_lhws_{{[a-z_]*}}{{.*}} r3, r4, 0
# OBJ: {{.*}} s_lhws_{{[a-z_]*}}{{.*}} r5, r6, 2
# OBJ: {{.*}} s_lbs_{{[a-z_]*}}{{.*}} r7, r8, 0
# OBJ: {{.*}} s_lbs_{{[a-z_]*}}{{.*}} r9, r10, 1
