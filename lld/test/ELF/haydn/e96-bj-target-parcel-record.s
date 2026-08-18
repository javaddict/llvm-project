# REQUIRES: haydn
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: ld.lld %t.o -o %t.elf --section-start=.text=0x10000
# RUN: llvm-readobj -S %t.elf | FileCheck %s --check-prefix=SEC
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.elf | \
# RUN:   FileCheck %s --check-prefix=DIS
#
# Linked B/JAL displacements stay byte PC+imm onto the 12-byte parcel grid.
# Two parcels ahead is +24. .text size remains 0 mod EncodedBytes.

.text
.globl _start
_start:
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
# DIS:    10000: {{.*}}bnez{{.*}}r1, 24
# DIS:    1000c:
# DIS:    10018:
# DIS-LABEL: <fwd_jal>:
# DIS:    10024: {{.*}}jal{{.*}}lr, 24
# DIS:    10030:
# DIS:    1003c:
