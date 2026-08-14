# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# Pin lld inBranchRange / relocate for the WIDE branch byte window against
# HaydnRelocLayout::computeRelocValue. GE96-03: branch fields are PC+imm with
# NO extra scale (ValueShift=0), so the signed-12 window is [-2048, +2046]
# BYTES — half the retired halfword-scaled (÷2) window these pins used to
# encode. In-range links patch a direct branch; one past range inserts a
# long-branch thunk (needsThunk), not a second isInt table.

# RELOCS: R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} far_target

# Positive in-range (+2040): direct branch, no thunk.
# RUN: ld.lld %t.o -o %t.posin -T %S/reloc-range-branch-div2-pos-in.ld
# RUN: llvm-nm %t.posin | FileCheck --check-prefix=POS-IN-NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.posin | FileCheck --check-prefix=POS-IN %s

# POS-IN-NM-NOT: __haydn_thunk_
# POS-IN-NM-DAG: {{0+}}10000 T _start
# POS-IN-NM-DAG: {{0+}}107f8 T far_target
# POS-IN-LABEL: <_start>:
# Printer may render beq_w as beq; pin family + in-range displacement.
# POS-IN: beq{{(_w)?}}{{.*}}2040

# Positive one-past (+2048): thunk required.
# RUN: ld.lld %t.o -o %t.posoor -T %S/reloc-range-branch-div2-pos-oor.ld
# RUN: llvm-nm %t.posoor | FileCheck --check-prefix=POS-OOR-NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.posoor | FileCheck --check-prefix=POS-OOR %s

# POS-OOR-NM: __haydn_thunk_far_target
# POS-OOR-LABEL: <_start>:
# POS-OOR: beq{{(_w)?}}
# POS-OOR-LABEL: <__haydn_thunk_far_target>:

# Negative in-range (-2048): direct branch, no thunk.
# RUN: ld.lld %t.o -o %t.negin -T %S/reloc-range-branch-div2-neg-in.ld
# RUN: llvm-nm %t.negin | FileCheck --check-prefix=NEG-IN-NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.negin | FileCheck --check-prefix=NEG-IN %s

# NEG-IN-NM-NOT: __haydn_thunk_
# NEG-IN-NM-DAG: {{0+}}10000 T far_target
# NEG-IN-NM-DAG: {{0+}}10800 T _start
# NEG-IN-LABEL: <_start>:
# NEG-IN: beq{{(_w)?}}{{.*}}-2048

# Negative one-past (-2056): thunk required.
# RUN: ld.lld %t.o -o %t.negoor -T %S/reloc-range-branch-div2-neg-oor.ld
# RUN: llvm-nm %t.negoor | FileCheck --check-prefix=NEG-OOR-NM %s

# NEG-OOR-NM: __haydn_thunk_far_target

.section .text,"ax"
.globl _start
_start:
    beq_w r1, r2, far_target
    { add32 r0, r0, r0 }
    .size _start, .-_start

.section .text.tgt,"ax"
.globl far_target
far_target:
    { add32 r3, r3, r3 }
    .size far_target, .-far_target
