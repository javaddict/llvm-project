# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s \
# RUN:   | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o \
# RUN:   | FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target

# Role: object — Path B X2MUL32 2-dest FLEX encoder round-trip.
# Historical encoder OOB on 3-op form is fixed; pin 4-op encode+disasm.

# ENC: x2mul32{{.*}}d0, d1, d2, d3
# ENC-SAME: encoding: [0x47,0x02,0x01,0x21,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00]

# DIS-LABEL: <.text>:
# DIS: x2mul32
# DIS-NOT: <unknown>

x2mul32 d0, d1, d2, d3
