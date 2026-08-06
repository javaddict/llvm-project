# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o - | \
# RUN:   llvm-objdump -d - | FileCheck %s --check-prefix=DIS

# Golden Format E CBRI: rtd, rs, imm8, cbr_sel. Imm must appear on the wire
# (imm0 ≢ imm1) or BundleSim circular-buffer stride collapses to 0.

# ENC-LABEL: d_sdw_cb_imm
{ d_sdw_cb_imm 0, d0, r1, 0 }
# ENC: encoding: [0x87,0x01,0x0c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
{ d_sdw_cb_imm 0, d0, r1, 1 }
# ENC-NOT: encoding: [0x87,0x01,0x0c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: encoding: [{{.*}}]
{ d_sdw_cb_imm 1, d0, r1, 1 }
# ENC: encoding: [{{.*}}]

{ d_ldw_cb_imm 0, d0, r1, 0 }
{ d_ldw_cb_imm 0, d0, r1, 1 }

# DIS: d_sdw_cb_imm{{.*}}0{{.*}}d0{{.*}}r1{{.*}}0
# DIS: d_sdw_cb_imm{{.*}}0{{.*}}d0{{.*}}r1{{.*}}1
# DIS: d_sdw_cb_imm{{.*}}1{{.*}}d0{{.*}}r1{{.*}}1
# DIS: d_ldw_cb_imm{{.*}}0{{.*}}d0{{.*}}r1{{.*}}0
# DIS: d_ldw_cb_imm{{.*}}0{{.*}}d0{{.*}}r1{{.*}}1
