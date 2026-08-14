# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn %t.o | FileCheck %s
#
# REGRESSION TEST: GE96-03 — disassembler prints PC-relative **byte**
# offsets (field = PC+imm, ValueShift=0). BundleSim must not ×2/×4 them.
# Each Format E parcel is 12 bytes. bnez at 0 → .Ltarget at 0x18 is +24.
# jal at 0x24 → .Lcall at 0x3c is +24.

# CHECK: { {{.*}}bnez{{.*}}r1, 24
# CHECK: xor32
# CHECK: xor32
# CHECK: { {{.*}}jal{{.*}}lr, 24
# CHECK: xor32
# CHECK: { {{.*}}jalr{{.*}}r0, lr, 0

.text
fwd_branch:
  { bnez_w r1, .Ltarget; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Ltarget:
  { xor32 r0, r0, r0; nop; nop }
  { jal lr, .Lcall; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Lcall:
  { jalr r0, lr, 0; nop; nop }
