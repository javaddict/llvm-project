# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s
#
# Negative load/store offsets must print as signed values, not
# zero-extended u16 (e.g. -4 not 65532). BundleSim golden range checks reject
# the unsigned spellings.
    .text
    .globl cb99_negative_ls_objdump
    .type cb99_negative_ls_objdump,@function
cb99_negative_ls_objdump:
    { s_lw_with_imm r1, r10, -1; nop; nop }
    { d_ldw_with_imm d1, r10, -1; nop; nop }
    { s_sw_with_imm r1, r10, -1; nop; nop }
    { s_lbu_with_imm r1, r10, -1; nop; nop }
    { s_sb_with_imm r1, r10, -1; nop; nop }
    { jalr r0, lr, 0; nop; nop }
    .size cb99_negative_ls_objdump, .-cb99_negative_ls_objdump

# The immediate is a SCALED element index now, so the byte offsets in the
# source (-4, -8, -1) print as -1 in every case: -4/4, -8/8 and -1/1.
# CHECK: s_lw_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# CHECK: d_ldw_{{[a-z_]*}}{{.*}}d1,{{.*}}r10,{{.*}}-1
# CHECK: s_sw_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# CHECK: s_lbu_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# CHECK: s_sb_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# The point of the test: a negative offset must print SIGNED. The unsigned
# renderings to forbid moved with the field width -- they were 65532 / 65528 /
# 65535 for a 16-bit field, and the field is 6 bits now, so -1 misprinted as
# unsigned is 63. Guarding the old values would have guarded nothing.
# CHECK-NOT: {{, 63 }}
# CHECK-NOT: {{, 6[0-3] }}
