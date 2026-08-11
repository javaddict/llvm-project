# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn %t.o | FileCheck %s
#
# residual : conditional branch and JAL immediates printed
# by the disassembler are PC-relative **byte** offsets (decode
# Shift=1 / call target bytes), not word offsets. BundleSim direct-ELF
# must NOT ×4 them (legacy word contract). This locks the printer unit.
#
# Layout: each parcel is 12 bytes. From the first bundle at 0 a forward
# branch over two parcels targets 0x18, so the printed offset is 24.
# It was 32 when a parcel was 16 bytes — the CONTRACT is unchanged and only
# the parcel width moved, which is the whole point of asserting a byte offset
# rather than a parcel count.
#
# 24 is also what makes this test still say something: a printer that had
# gone back to word units would print 6, and one that counted parcels would
# print 2. Both are excluded by the number, not by its shape.

.text
# .balign 4, NOT 16 (§ 5.9). A 12-byte parcel is not a power of two, so
# padding a bundle stream up to 16 needs 4, 8 or 12 bytes and only 12 is a
# whole bundle — writeNopData refuses the rest and the assembly aborts,
# depending on the function's size. Every bundle boundary is 4-aligned
# already, so this request never pads at all.
.balign 4
# CHECK-LABEL: <fwd_branch>:
fwd_branch:
  # CHECK: bnez{{.*}}, 24
  { bnez r1, .Ltarget; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Ltarget:
  { xor32 r0, r0, r0; nop; nop }
  # CHECK: jal{{.*}}, 24
  { jal lr, .Lcall; nop; nop }
  { xor32 r0, r0, r0; nop; nop }
.Lcall:
  { jalr r0, lr, 0; nop; nop }
