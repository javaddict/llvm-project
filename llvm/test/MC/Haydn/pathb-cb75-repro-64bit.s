# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

# PATH B PROTOTYPE — repro MUST decode as 64-bit Mode0, NOT 32-bit.
#
# (encoding_manual.md §1.1): a 32-bit instruction whose low nibble
# happened to be 0011 was bit-identical to the lower half of a Mode-0 bundle
# over every discriminator field. The repro `addi32 r2,r0,3` = 0x60200003
# (bits[3:0]=0011) MUST decode as 64-bit Mode0 (Size=8) under the
# bits[1:0] parallel decode tree (§1.2). bits[1:0]=11 -> 64-bit; bits[31:30]
# are NEVER consulted for primary width.
#
# Bytes (LE): 03 00 20 60 00 00 00 00 = word 0x0000000060200003 (nibble=0011).
# If the OLD width tag (bits[31:30]=01) were still active, the decoder would
# consume only 4 bytes (Size=4) as `addi32 r2,r0,3` and then mis-decode the
# trailing 4 bytes. Under Path B it MUST be Size=8.

.text
.globl _start
_start:
    .byte 0x03, 0x00, 0x20, 0x60, 0x00, 0x00, 0x00, 0x00

# CHECK:      0: 03 00 20 60 00 00 00 00
# CHECK-NOT:  0: 03 00 20 60                addi32
# The disassembly must show 8 bytes consumed at offset 0 and the next parcel
# (if any) at offset 8, proving Size=8 (64-bit).
