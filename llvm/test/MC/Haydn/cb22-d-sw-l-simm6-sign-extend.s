# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s

// CHECK: {{.*}}0: 87 43 08 e1 03 00 00 00 00 00 00 00 { d_sw_l_with_imm d0, r1, -2; nop }
// CHECK: {{.*}}c: 87 43 38 f1 03 00 00 00 00 00 00 00 { d_sw_l_with_imm d3, r1, -1; nop }
// CHECK: {{.*}}18: 87 43 09 e2 03 00 00 00 00 00 00 00 { d_sw_h_with_imm d0, r2, -2; nop }
// CHECK: {{.*}}24: 87 43 08 11 00 00 00 00 00 00 00 00 { d_sw_l_with_imm d0, r1, 1; nop }
// CHECK: {{.*}}30: 07 0d 02 0f 00 00 00 00 00 00 00 00 { jalr r0, lr, 0; nop }
# Role: object — residual (direct-ELF): D_SW_L/H_WITH_IMM use simm6 word-index offsets.

# residual (direct-ELF): D_SW_L/H_WITH_IMM use simm6 word-index offsets.
# Without a signed DecoderMethod, tblgen zero-extends the 6-bit field and
# objdump prints -2 as 62. BundleSim's ELF path feeds objdump text into the
# ISS, so the mul result store in sparse-switch VMs lands at base+248 instead
# of base-8 → host=35 / ELF=5.
#
# Mirror of (simm16 → signed decode). Assembly -S already prints the
# signed MC operand; this guards the objdump/ELF path.

    .text
    .globl cb22_d_sw_l_simm6
    .type cb22_d_sw_l_simm6,@function
cb22_d_sw_l_simm6:
    { d_sw_l_with_imm d0, r1, -2; nop; nop }
    { d_sw_l_with_imm d3, r1, -1; nop; nop }
    { d_sw_h_with_imm d0, r2, -2; nop; nop }
    { d_sw_l_with_imm d0, r1, 1; nop; nop }
    { jalr_w r0, lr, 0; nop; nop }
    .size cb22_d_sw_l_simm6, .-cb22_d_sw_l_simm6

