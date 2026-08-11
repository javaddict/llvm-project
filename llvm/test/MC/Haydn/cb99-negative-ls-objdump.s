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
    { ld32 r1, r10, -4; nop; nop }
    { ld64 d1, r10, -8; nop; nop }
    { st32 r1, r10, -4; nop; nop }
    { ldu8 r1, r10, -1; nop; nop }
    { st8 r1, r10, -1; nop; nop }
    { jalr r0, lr, 0; nop; nop }
    .size cb99_negative_ls_objdump, .-cb99_negative_ls_objdump

# CHECK: s_lw_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-4
# CHECK: d_ldw_{{[a-z_]*}}{{.*}}d1,{{.*}}r10,{{.*}}-8
# CHECK: s_sw_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-4
# CHECK: s_lbu_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# CHECK: s_sb_{{[a-z_]*}}{{.*}}r1,{{.*}}r10,{{.*}}-1
# CHECK-NOT: 65532
# CHECK-NOT: 65528
# CHECK-NOT: 65535
