# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -defsym=DEFINE_TARGET=1 %s -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 %t.o %t.target.o -o %t.elf
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.elf | FileCheck %s --check-prefix=DISASM

# D1.42: E3 F2 hwloop windows are generated data, not sniff literals.
# The generated HwLoopSniffSites rows (HaydnGenRelocFieldLsb.inc, golden
# format_e_bit_layout_v2_2.json entry_num_1.entry{0,1}.ALU0.HWLRIIR):
#   E3 e0: Off1(uimm6_offset1)  bit[23:18] → LSB 18
#          Off2(uimm12_offset2) bit[35:24] → LSB 24
#   E3 e1: Off1 bit[54:49] → LSB 49; Off2 bit[66:55] → LSB 55
# d112 covers E2 HWLRIII; d486/cb90 cover the E2 F2 decode only — no E3
# F2 linked-symbolic coverage existed before D1.42.
#
# Parcel construction (mirrors d117 mixed-parcel shapes):
#   parcel 0: { nop; set_hwloop_f2_w } → SET at E3 e1 (window 49/55),
#             cross-object symbols (lld arm).
#   parcel 1: { nop; set_hwloop_f2_w } with LOCAL labels → assemble-time
#             applyFixup writes the SAME generated e1 window (MC arm).
# $sel is uimm1 (product domain {0,1}); sel=1 on both arms. The
# disassembled byte distances pin the generated windows end-to-end (a
# wrong window writes other members' bits and the distances corrupt or
# the parcel fails to decode).
#
# Regression symptom: pre-D1.42 the E3 F2 windows were hand literals in
# HaydnRelocLayout.cpp; now they are the generator-law-checked table and
# unknown hwloop sites fail closed (lld/test/ELF/haydn/
# hwloop-unknown-site-reject.test, the lld arm of the shared
# tryResolveFieldLsb authority).

# RELOC:       Relocations [
# RELOC-NEXT:    Section (3) .rela.text {
# RELOC-NEXT:      0x0 R_HAYDN_HWLoopOff1 xbody 0x0
# RELOC-NEXT:      0x0 R_HAYDN_HWLoopOff2 xend 0x0

# DISASM pins both arms' distances: the cross-object arm proves the
# lld-side generated window + <<2 scale; the local-label arm proves the
# assemble-time applyFixup wrote the identical E3 e1 window.
# DISASM: set_hwloop_f2{{.*}}1, 48, 60, r1
# DISASM: set_hwloop_f2{{.*}}1, 12, 24, r1

.ifndef DEFINE_TARGET

.text
.globl _start
_start:
  { nop; set_hwloop_f2_w 1, xbody, xend, r1 }
  # Local-label arm: same-file symbols resolve via MC AsmBackend
  # applyFixup at ASSEMBLE time (no relocation row survives).
.locallabels:
  { nop; set_hwloop_f2_w 1, lbody, lend, r1 }
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
