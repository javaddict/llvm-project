# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -defsym=DEFINE_TARGET=1 %s -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 %t.o %t.target.o -o %t.elf
# RUN: llvm-objdump -s -j .text %t.elf | FileCheck %s --check-prefix=LINKED
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.elf | FileCheck %s --check-prefix=DISASM

# D1.12: pin the E2 HWLRIII (non-F2 SET_HWLOOP) branch of
# resolveFieldLsb (HaydnRelocLayout.cpp:359-373): parcel Type==0x10 →
# Off1 @ parcel bits[18:13], Off2 @ bits[47:36]. Golden:
# format_e_bit_layout_v2_2.json entry_num_0.entry0.ALU0.HWLRIII —
# type_code_bin 10000, imm1(uimm6_offset1) bit[18:13],
# imm2(uimm12_offset2) bit[47:36]. Both relocs share r_offset 0 (parcel
# origin); the Type sniff distinguishes their windows.
#
# Expected bytes derive from golden, not from a transcript:
#   header      07          indicator 111, entry_num 0 (E2)
#   Type@8:5    0x10        HWLRIII
#   Off1@13 = 3   → body 3*4 = 12 bytes after the SET parcel
#   Off2@36 = 6   → end  6*4 = 24 bytes after the SET parcel
# Word 0 (LE) = (0x10 << 8) | (3 << 13) | (6 << 36)
#             = 0x6000580007... → bytes 07 70 58 00 60 00 00 00 00 00 00 00
# (bits 13..17 = 3 → byte1 0x70; bit 36 → byte4 0x60.)
#
# Regression symptom: if this branch regressed to the F2 default window
# (Off1@32/Off2@38, covered by d486/cb90), lld would patch imm3/cnt bits
# instead — the linked parcel bytes and the disassembled distances both
# fail these pins.
#
# Unit-level twin: HaydnRelocLayoutTest.HWLoopOffFieldLsbE2HWLRIIIBranch
# pins resolveFieldLsb/resolveFieldLsbForMember on hand-built buffers.

# RELOC:       Relocations [
# RELOC-NEXT:    Section (3) .rela.text {
# RELOC-NEXT:      0x0 R_HAYDN_HWLoopOff1 xbody 0x0
# RELOC-NEXT:      0x0 R_HAYDN_HWLoopOff2 xend 0x0

# The cross-object arm links first (parcel 0): xbody/xend sit 48/60
# bytes ahead (fields 12/15). The local-label arm (parcel 1) resolves at
# assemble time to 12/24 — identical byte shape, different distance.
# LINKED: 07905900 f0000000 00000000 07705800
# LINKED: 60000000 00000000 078b1032 00000000
# LINKED: 00000000 074b0100 00000000 00000000

# DISASM pins both arms' distances: the cross-object arm (48/60) proves
# the lld-side Type==0x10 window + <<2 scale; the local-label arm
# (12/24, bytes 07 70 58 00 60 …) proves the assemble-time applyFixup
# wrote the SAME HWLRIII window (Off1@13=3, Off2@36=6).
# DISASM: set_hwloop{{.*}}0, 48, 60, 5
# DISASM: set_hwloop{{.*}}0, 12, 24, 5

.ifndef DEFINE_TARGET

.text
.globl _start
_start:
  set_hwloop_w 0, xbody, xend, 5

# Local-label arm: same-file symbols resolve via the MC AsmBackend
# applyFixup → resolveFieldLsb(Data) Type==0x10 branch at ASSEMBLE time
# (no relocation row survives). Identical patched bytes prove the MC and
# lld consumers share the window.
.locallabels:
  set_hwloop_w 0, lbody, lend, 5
lbody:
  { add32 r1, r2, r3 }
lend:
  { xor32 r0, r0, r0 }

.else

.text
.globl xbody, xend
xbody:
  { add32 r1, r2, r3 }
xend:
  { xor32 r0, r0, r0 }

.endif
