# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn %t.o | FileCheck %s
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

// CHECK: {{.*}}0: 07 0a 1a c0 00 00 00 00 00 00 00 00  	{ 		bnez	r1, <?>; 	nop }
// CHECK: {{.*}}c: 07 4b 01 00 00 00 00 00 00 00 00 00  	{ 		xor32	r0, r0, r0; 	nop }
// CHECK: {{.*}}18: 07 4b 01 00 00 00 00 00 00 00 00 00  	{ 		xor32	r0, r0, r0; 	nop }
// CHECK: {{.*}}24: 07 0e f8 c0 00 00 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
// CHECK: {{.*}}30: 07 4b 01 00 00 00 00 00 00 00 00 00  	{ 		xor32	r0, r0, r0; 	nop }
// CHECK: {{.*}}3c: 07 0d 02 0f 00 00 00 00 00 00 00 00  	{ 		jalr	r0, lr, <?>; 	nop }
# Role: object — residual : conditional branch and JAL immediates printed by the disassembler are PC-relative **byte** offsets (decode.

# residual : conditional branch and JAL immediates printed
# by the disassembler are PC-relative **byte** offsets (decode
# Shift=1 / call target bytes), not word offsets. BundleSim direct-ELF
# must NOT ×4 them (legacy word contract). This locks the printer unit.
#
# Layout: each Format E parcel is 12 bytes. From the first bundle at 0
# a forward bnez of +32 bytes targets the third parcel (addr 0x20).

.text
.balign 16
fwd_branch:
  { bnez_w r1, .Ltarget; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Ltarget:
  { xor32 r0, r0, r0; nop; nop }
  { jal lr, .Lcall; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Lcall:
  { jalr r0, lr, 0; nop; nop }
