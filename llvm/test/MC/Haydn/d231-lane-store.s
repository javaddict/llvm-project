# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>/dev/null \
# RUN:   | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o \
# RUN:   | FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target

# Role: object — lane-store D_SW_L/H_WITH_IMM must parse, encode distinct
# lo/hi Format E parcels, and disassemble (not idle/unknown).
#
# D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
# D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)

# ENC: d_sw_l_with_imm{{.*}}d3, r1, 1
# ENC-SAME: encoding: [0x87,0x43,0x38,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: d_sw_l_with_imm{{.*}}d4, r2, 2
# ENC-SAME: encoding: [0x87,0x43,0x48,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: d_sw_h_with_imm{{.*}}d5, r3, 1
# ENC-SAME: encoding: [0x87,0x43,0x59,0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]

# DIS-LABEL: <.text>:
# DIS: d_sw_l_with_imm
# DIS-SAME: r1, 1
# DIS: d_sw_l_with_imm
# DIS-SAME: r2, 2
# DIS: d_sw_h_with_imm
# DIS-SAME: r3, 1
# DIS-NOT: <unknown>

d_sw_l_with_imm d3, r1, 1
d_sw_l_with_imm d4, r2, 2
d_sw_h_with_imm d5, r3, 1
