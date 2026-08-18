# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target
#
# Direct B/JAL targets must land on an exact 12-byte Format E record.
# Every cycle serializes as one EncodedBytes parcel, so a label two
# parcels ahead is +24 and the printed PC-relative field is that byte
# displacement (no extra scale). Text is high-first; real ops sit in e0.

.text
fwd_b:
  { nop; nop; bnez_w r1, .Lbtgt }
  { nop; nop; xor32 r0, r0, r0 }
.Lbtgt:
  { nop; nop; xor32 r0, r0, r0 }

fwd_jal:
  { nop; nop; jal lr, .Ljtgt }
  { nop; nop; xor32 r0, r0, r0 }
.Ljtgt:
  { nop; nop; jalr r0, lr, 0 }

# SEC: Name: .text
# SEC: Size: 72

# DIS-LABEL: <fwd_b>:
# DIS:        0: {{.*}}bnez{{.*}}r1, 24
# DIS:        c:
# DIS:       18:
# DIS-LABEL: <fwd_jal>:
# DIS:       24: {{.*}}jal{{.*}}lr, 24
# DIS:       30:
# DIS:       3c:
