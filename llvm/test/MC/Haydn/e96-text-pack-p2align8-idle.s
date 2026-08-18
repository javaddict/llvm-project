# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=HEX
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target
#
# .p2align 3 after one 12-byte parcel would need a 4-byte fill to reach
# 8-align. writeNopData refuses that remainder. Emit one idle parcel
# instead so the next label stays an exact Format E record (offset 24).

.text
f:
  { add32 r1, r2, r3 }
  .p2align 3
g:
  { jal lr, f }

# SEC: Name: .text
# SEC: Size: 36

# Idle parcel at offset 12 is the generated full-slot NOP, not zeros.
# llvm-readobj -x prints 16-byte rows, so the 12-byte idle straddles
# the first two dump lines (last word of 0x0 + first two of 0x10).
# HEX: 0x00000000 {{.*}} 07000000
# HEX-NEXT: 0x00000010 00000000 00000000

# DIS-LABEL: <f>:
# DIS:        0:
# DIS-LABEL: <g>:
# DIS:       18: {{.*}}jal{{.*}}lr, -24
