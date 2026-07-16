# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn %t.o | FileCheck %s
#
# residual : conditional branch and JAL immediates printed
# by the disassembler are PC-relative **byte** offsets (decode
# Shift=1 / call target bytes), not word offsets. BundleSim direct-ELF
# must NOT ×4 them (legacy word contract). This locks the printer unit.
#
# Layout: each Bundle128 parcel is 16 bytes. From the first bundle at 0
# a forward bnez of +32 bytes targets the third parcel (addr 0x20).

.text
.balign 16
# CHECK-LABEL: <fwd_branch>:
fwd_branch:
  # CHECK: bnez_w{{.*}}, 32
  { bnez_w r1, .Ltarget; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Ltarget:
  { xor32 r0, r0, r0; nop; nop }
  # CHECK: jal{{.*}}, 32
  { jal lr, .Lcall; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Lcall:
  { jalr r0, lr, 0; nop; nop }
