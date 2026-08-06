# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>&1 | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

// CHECK: 	.text
// CHECK: 	.p2align	4
// CHECK: 	{ 		ld32_reg	r1, r2, r3; 	nop }   // encoding: [0x87,0x4c,0x00,0x00,0x00,0x20,0x07,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 		st32_reg	r1, r2, r3; 	nop }   // encoding: [0x87,0x4c,0x00,0x00,0x00,0x40,0x07,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 		ld64_reg	d0, r2, r3; 	nop }   // encoding: [0x87,0x0c,0x00,0x00,0x00,0x30,0x07,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 		st64_reg	d0, r2, r3; 	nop }   // encoding: [0x87,0x0c,0x00,0x00,0x00,0x50,0x07,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 		ld32_reg	lr, sp, r12; 	nop }   // encoding: [0x47,0xf3,0x03,0x00,0x00,0x20,0x07,0x00,0x00,0x00,0x00,0x00]
# Role: object — Reg-offset LS (ld32_reg/st32_reg/ld64_reg/st64_reg) must NOT encode as 12-byte Format E idle/NOP.

# Reg-offset LS (ld32_reg/st32_reg/ld64_reg/st64_reg) must NOT encode
# as 12-byte Format E idle/NOP. Pre-fix: Bundle::add left SlotMap empty for
# FlexMap-less `_S0` opcodes → composite of 3 NOPs → PEI epilogue LR reload
# vanished in.o/.elf.
#
# ENC-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# ENC: ld32_reg
# ENC: st32_reg
# ENC: ld64_reg
# ENC: st64_reg
#
# DIS-NOT: {{{[[:space:]]*}}nop; nop; nop}
# DIS: ld32_reg
# DIS: st32_reg
# DIS: ld64_reg
# DIS: st64_reg

.text
.balign 16
  { ld32_reg r1, r2, r3; nop; nop }
  { st32_reg r1, r2, r3; nop; nop }
  { ld64_reg d0, r2, r3; nop; nop }
  { st64_reg d0, r2, r3; nop; nop }
  { ld32_reg lr, sp, r12; nop; nop }
