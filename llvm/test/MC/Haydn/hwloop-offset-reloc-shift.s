# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -defsym=DEFINE_TARGET=1 %s -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 %t.o %t.target.o -o %t.elf
# RUN: llvm-objdump -d --no-show-raw-insn %t.elf | FileCheck %s --check-prefix=LINKED

# REGRESSION TEST: W37 — hwloop symbolic offsets must link with <<2 scale.
#
# Bug: HaydnELFObjectWriter aliased FIXUP_HAYDN_HWLoopOffset to
# R_HAYDN_BranchSImm16, but the two kinds have DIFFERENT ValueShift in the
# shared HaydnRelocLayout table (HWLoopOffset=2, BranchSImm16=0). A symbolic
# SET_HWLOOP offset that linked through the alias would apply Shift=0 —
# skipping the <<2 word-to-byte scale — and redirect the hardware loop to a
# wrong target (offset interpreted 4x too small).
#
# Fix: the alias is closed fail-closed in the writer. Product SET_HWLOOP_*W
# symbolic offsets emit the TYPED R_HAYDN_HWLoopOff1/R_HAYDN_HWLoopOff2 pair
# (hwloop_off1/hwloop_off2 operand classes, ValueShift=2 rows), which this
# test pins. The writer's HWLoopOffset case now reportErrors (it has no
# producer; nothing in this test may reach it).
#
# Test design: cross-object link forces the relocation through lld's shared
# layout row (a same-file local label would be resolved by the AsmBackend).
# Off1 target sits 12 bytes after the SET parcel (field 3), Off2 24 bytes
# after (field 6) — the printed byte distances prove the <<2 scale survived
# the link. If the shift regressed, objdump would print 3/6 (fields without
# byte scale) or wrong targets.
#
# What breaks on regression: RELOC lines show a BranchSImm16 alias (or none),
# and LINKED prints set_hwloop 0, 3, 6 instead of 0, 12, 24.

# RELOC: R_HAYDN_HWLoopOff1{{.*}} xbody
# RELOC: R_HAYDN_HWLoopOff2{{.*}} xend

# LINKED: set_hwloop{{.*}}0, 12, 24

.ifndef DEFINE_TARGET

.text
.globl _start
_start:
  set_hwloop_w 0, xbody, xend, 5

.else

.text
.globl xbody, xend
xbody:
  { add32 r1, r2, r3 }
xend:
  { xor32 r0, r0, r0 }

.endif
