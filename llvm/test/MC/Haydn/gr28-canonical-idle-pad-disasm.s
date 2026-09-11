# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o \
# RUN:   | FileCheck %s --check-prefix=DIS
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=HEX
# REQUIRES: haydn-registered-target

# Role: object — writeNopData canonical idle fill. .p2align 5 inside .text
# after one 12-byte parcel needs 7 parcels to reach 32-align; every fill
# parcel must be the generated complete architectural-NOP parcel
# (header 0x07 + zero entries, disasm `{ nop; nop }`), never zeros, a
# short pad, or an invented idle byte pattern. The MC-object disasm of
# each fill parcel matches the explicit `nop` parcel byte-for-byte.
# Companion pins: e96-text-pack-p2align8-idle.s (single-parcel remainder),
# e96-text-align-256-maxparcels.s (large Align), a6-bundle128-pad-only.s
# (non-multiple refusal), nop-format-e-not-all-zero.s (idle encode).

.section .text
.globl _start
_start:
    { add32 r1, r2, r3 }
    .p2align 5
    { xor32 r4, r5, r6 }
    .size _start, .-_start

# First real parcel, then 7 canonical idle fill parcels, then the second
# real parcel: 9 parcels = 108 bytes.
# DIS-LABEL: <_start>:
# DIS-NEXT:  0: 07 8b 10 32 00 00 00 00 00 00 00 00  	{ nop; 	add32	r1, r2, r3 }
# DIS-NEXT:  c: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 18: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 24: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 30: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 3c: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 48: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 54: 07 00 00 00 00 00 00 00 00 00 00 00  	{ nop; nop }
# DIS-NEXT: 60: 07 4b 41 65 00 00 00 00 00 00 00 00  	{ nop; 	xor32	r4, r5, r6 }
# DIS-NOT: {{[0-9a-f]+}}:

# HEX: Hex dump of section '.text':
# HEX: 0x00000000 078b1032 00000000 00000000 07000000
# HEX: 0x00000010 00000000 00000000 07000000 00000000
# HEX: 0x00000020 00000000 07000000 00000000 00000000
# HEX: 0x00000030 07000000 00000000 00000000 07000000
# HEX: 0x00000040 00000000 00000000 07000000 00000000
# HEX: 0x00000050 00000000 07000000 00000000 00000000
# HEX: 0x00000060 074b4165 00000000 00000000
