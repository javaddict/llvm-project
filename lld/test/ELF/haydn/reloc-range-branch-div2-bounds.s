# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# Pin lld inBranchRange / relocate for WIDE branch byte scale (ValueShift=0)
# against HaydnRelocLayout::computeRelocValue. Signed-12 window is
# [-2048, +2046]; parcel-grid pins are +2040 / +2052 and -2048 / -2064.
# In-range links patch a direct branch; one past range is a D1.57 fail-closed
# link error (the R0-borrowing veneer is retired; no thunk is inserted).

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

# Positive one-past (+2052): no thunk exists — D1.57 fail-closed link error.
# RUN: not ld.lld %t.o -o %t.posoor -T %S/reloc-range-branch-div2-pos-oor.ld 2>&1 | FileCheck --check-prefix=POS-OOR %s

# POS-OOR: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# POS-OOR-NOT: __haydn_thunk

# Negative in-range (-2048): direct branch, no thunk.
# RUN: ld.lld %t.o -o %t.negin -T %S/reloc-range-branch-div2-neg-in.ld
# RUN: llvm-nm %t.negin | FileCheck --check-prefix=NEG-IN-NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.negin | FileCheck --check-prefix=NEG-IN %s

# NEG-IN-NM-NOT: __haydn_thunk_
# NEG-IN-NM-DAG: {{0+}}10000 T far_target
# NEG-IN-NM-DAG: {{0+}}10800 T _start
# NEG-IN-LABEL: <_start>:
# NEG-IN: beq{{(_w)?}}{{.*}}-2048

# Negative one-past (-2064): no thunk exists — D1.57 fail-closed link error.
# RUN: not ld.lld %t.o -o %t.negoor -T %S/reloc-range-branch-div2-neg-oor.ld 2>&1 | FileCheck --check-prefix=NEG-OOR %s

# NEG-OOR: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# NEG-OOR-NOT: __haydn_thunk

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
