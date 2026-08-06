# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s

// CHECK: {{.*}}0: 87 43 13 ca 03 00 00 00 00 00 00 00 { s_lw_with_imm r1, r10, -4; nop }
// CHECK: {{.*}}c: 87 43 12 8a 03 00 00 00 00 00 00 00 { d_ldw_with_imm d1, r10, -8; nop }
// CHECK: {{.*}}18: 87 43 1b ca 03 00 00 00 00 00 00 00 { s_sw_with_imm r1, r10, -4; nop }
// CHECK: {{.*}}24: 87 43 17 fa 03 00 00 00 00 00 00 00 { s_lbu_with_imm r1, r10, -1; nop }
// CHECK: {{.*}}30: 87 43 1e fa 03 00 00 00 00 00 00 00 { s_sb_with_imm r1, r10, -1; nop }
// CHECK: {{.*}}3c: 07 0d 02 0f 00 00 00 00 00 00 00 00 { jalr r0, lr, 0; nop }
# Role: object — Negative load/store offsets must print as signed values, not zero-extended u16 (e.g.

# Negative load/store offsets must print as signed values, not
# zero-extended u16 (e.g. -4 not 65532). BundleSim golden range checks reject
# the unsigned spellings.

    .text
    .globl cb99_negative_ls_objdump
    .type cb99_negative_ls_objdump,@function
cb99_negative_ls_objdump:
    { ld32 r1, r10, -4; nop; nop }
    { ld64 d1, r10, -8; nop; nop }
    { st32 r1, r10, -4; nop; nop }
    { ldu8 r1, r10, -1; nop; nop }
    { st8 r1, r10, -1; nop; nop }
    { jalr_w r0, lr, 0; nop; nop }
    .size cb99_negative_ls_objdump, .-cb99_negative_ls_objdump

