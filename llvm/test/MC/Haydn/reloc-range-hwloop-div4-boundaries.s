# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -s --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=MISALIGN=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=ALIGN %s
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: object — Pin SET_HWLOOP_W / SET_HWLOOP_F2_W offset geometry: unsigned fields after ÷4 (ValueShift=2, Align=4).

# Pin SET_HWLOOP_W / SET_HWLOOP_F2_W offset geometry: unsigned fields after ÷4
# (ValueShift=2, Align=4). HWLoopOff1 is uimm6 → byte window [0, 252];
# HWLoopOff2 is uimm12 → [0, 16380]. applyFixup and lld inBranchRange both
# consult HaydnRelocLayout::computeRelocValue — no parallel isInt tables.
#

# OOR-POS: relocation offset out of range
# OOR-NEG: relocation offset out of range
# ALIGN: mis-aligned relocation target

.text
.globl test_hwloop_div4_bounds
.balign 16

# ---- Positive Off1 boundary: body at +240 (field = 60) is in uimm6÷4. ----
# Off2 at +256 (field = 64) is well inside uimm12. Raw LoWord byte0 carries
# (off1 << 1) | sel; off1=60 → 0x78 (sel=0).
pos_in_range:
    set_hwloop_f2_w 0, body_ok, end_ok, r1
    .space 224
.balign 16
body_ok:
    { add32 r1, r2, r3 }
.balign 16
end_ok:
    { add32 r4, r5, r6 }

.ifdef OOR_POS
# Positive just past Off1 range: body at +256 → field = 64 > uimm6 max 63.
pos_out_of_range:
    set_hwloop_f2_w 0, body_bad, end_bad, r1
    .space 240
.balign 16
body_bad:
    { add32 r1, r2, r3 }
.balign 16
end_bad:
    { add32 r4, r5, r6 }
.endif

.ifdef OOR_NEG
# Negative / reverse layout is out of unsigned range (Off1 cannot encode
# a body that starts before the SET_HWLOOP parcel).
.balign 16
body_before:
    { add32 r1, r2, r3 }
.balign 16
end_before:
    { add32 r4, r5, r6 }
    .space 32
neg_out_of_range:
    set_hwloop_f2_w 0, body_before, end_before, r1
.endif

.ifdef MISALIGN
# Off1/Off2 require 4-byte alignment (Align=4). A 2-byte-only gap fails closed.
misaligned:
    set_hwloop_f2_w 0, body_mis, end_mis, r1
    .space 2
body_mis:
    { add32 r1, r2, r3 }
    .space 2
end_mis:
    { add32 r4, r5, r6 }
.endif
