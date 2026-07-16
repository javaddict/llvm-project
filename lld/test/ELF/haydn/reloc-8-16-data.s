# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --image-base=0 --section-start=.data=0x10
# RUN: llvm-readobj -x .data %t | FileCheck %s

# REGRESSION TEST: R_HAYDN_8 / R_HAYDN_16 narrow DATA relocations must not
# clobber neighboring bytes.
#
# Bug (D176 / L144 / review F5): `.byte sym` and `.short sym` previously
# mapped to R_HAYDN_SImm16, an *instruction-field* relocation. LLD's SImm16
# handler does a 4-byte read32le/write32le with a `& 0xFFFF0000` mask. For a
# 1-byte data value this overwrote the following 3 bytes; for a 2-byte value
# near a section end it read/wrote out of bounds.
#   .data
#   p: .byte target ; .byte 0xaa ; .short target ; .byte 0xbb
# with target = 0x10 linked to bytes `10 00 10 00 bb` -- 0xaa was clobbered --
# instead of the correct `10 aa 10 00 bb`.
#
# Fix: dedicated R_HAYDN_8 / R_HAYDN_16 absolute-data relocations whose LLD
# handlers do byte-width read/write (write8 / write16le) with NO 4-byte access
# and NO high-byte mask.
#
# Test design: place a 1-byte reloc immediately before sentinel 0xaa and a
# 2-byte reloc immediately before sentinel 0xbb, then dump .data and assert
# the exact byte layout. If the SImm16 clobber regresses, the 0xaa byte
# becomes 0x00 and this CHECK fails.
#
# Layout note: `target` is a section-relative label at the start of .data, so
# its value is the .data address. The link places .data at 0x10
# (--image-base=0 --section-start=.data=0x10) so `target` resolves to 0x10 --
# inside the R_HAYDN_8 [-128,255] and R_HAYDN_16 [-32768,65535] ranges and
# matching the CHECK's expected `10` bytes. (R_HAYDN_8/16 are absolute-data
# relocs: the symbol must be link-time-resolvable and in range; the low
# section address keeps it small.)
#
# AsmParser note: Haydn's AsmParser rejects trailing '#' comments on the same
# line as a directive (it does not treat '#' as a line comment there), so all
# '#' annotations are on their own lines.

# CHECK:      Hex dump of section '.data':
# CHECK-NEXT: 0x{{[0-9a-f]+}} 10aa1000 bb000000 00000000 00000000
#
# Byte layout (readobj -x prints bytes in address order):
#   byte[0] = 0x10  <- R_HAYDN_8 applied to `target` (=0x10)
#   byte[1] = 0xaa  <- sentinel, MUST survive (was 0x00 in the bug)
#   byte[2] = 0x10  <- low byte of R_HAYDN_16 applied to `target`
#   byte[3] = 0x00  <- high byte of R_HAYDN_16 (target fits in 8 bits)
#   byte[4] = 0xbb  <- sentinel, MUST survive
#
# The first 8 bytes are therefore `10 aa 10 00 bb 00 00 00`. If the SImm16
# clobber regresses, byte[1] becomes 0x00 and the dump reads `10100000...`,
# failing the CHECK.

        .data
        .balign 4
        .globl  target
target:
        .size   target, 0
        .globl  _start
p:
        .byte   target
        # R_HAYDN_8  -> 0x10
        .byte   0xaa
        # sentinel, must NOT be clobbered
        .short  target
        # R_HAYDN_16 -> 0x0010
        .byte   0xbb
        # sentinel, must NOT be clobbered
        .balign 16
        # pad .data out to 16 bytes for a stable dump
