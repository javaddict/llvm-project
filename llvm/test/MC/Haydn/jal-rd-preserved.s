# REQUIRES: haydn-registered-target
// CHECK: 	{ jal	lr, forward_target }            // encoding: [0x07,0x0e,0xf8,0bA0000000,A,A,0b00000AAA,0x00,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 0, value: forward_target, kind: FIXUP_HAYDN_WIDE_CallSImm20
// CHECK: forward_target:
// CHECK: 	{ 	add32	r0, r1, r2 }            // encoding: [0x07,0x8b,0x00,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// ROUNDTRIP: {{.*}}0: 07 0e f8 00 06 00 00 00 00 00 00 00  	{ 	jal	lr, 12; 	nop }
// ROUNDTRIP: {{.*}}c: 07 8b 00 21 00 00 00 00 00 00 00 00  	{ 		add32	r0, r1, r2; 	nop }
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=ROUNDTRIP

# Role: object — (F08 / G-FORMAT-E-96-CUTOVER): CallSImm20 fixup must NOT clobber lr.

# REGRESSION TEST (F08): CallSImm20 fixup must NOT clobber the link register.
#
# Format E JAL (E2 e0 ALU0 I20): rd/rt at absolute parcel bits[23:20], imm20 at
# bits[43:24] under dense entry packing. applyFixup writes only the displacement
# field at FieldLsb=24 with r_offset = parcel origin (byte 0); lr (R15) survives.
#
# Use the architectural name `lr` (asm rejects bare `r15` for this operand
# class — R15 is the named link register).
#
# If F08 regresses, ROUNDTRIP shows jal with a wrong destination register
# (often r0) instead of lr.


jal lr, forward_target

forward_target:
add32 r0, r1, r2
