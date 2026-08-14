# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# Format E SET_HWLOOP with cross-section body/end labels.
# EncodedBytes=12: no .balign 16 after SET (writeNopData rejects 4 B pads).
#
# Product emission restored the typed HWLoopOff relocs (FieldLsb/reloc fix):
# symbolic Off1/Off2 now carry R_HAYDN_HWLoopOff1/Off2, which lld range-checks
# and writes into the SET fields. Pin assemble + typed reloc presence +
# successful in-range link.

# RELOCS-DAG: R_HAYDN_HWLoopOff1 loop_body
# RELOCS-DAG: R_HAYDN_HWLoopOff2 loop_end

# Positive in-range link (body@0x100F0, end@0x100FC = body + one Format E
# 12-byte parcel via .ld script placement of .text.body).
# RUN: ld.lld %t.o -o %t.posin -T %S/reloc-range-hwloop-div4-pos-in.ld
# RUN: llvm-nm %t.posin | FileCheck --check-prefix=POS-IN-NM %s
# RUN: llvm-objdump -s --triple=haydn-unknown-elf %t.posin | FileCheck --check-prefix=POS-IN %s

# POS-IN-NM-DAG: {{0+}}10000 T _start
# POS-IN-NM-DAG: {{0+}}100f0 T loop_body
# POS-IN-NM-DAG: {{0+}}100fc T loop_end
# POS-IN: Contents of section .text:
# POS-IN-NEXT: 10000

.section .text,"ax"
.globl _start
_start:
    set_hwloop_w 0, loop_body, loop_end, 3
    .size _start, .-_start

.section .text.body,"ax"
.globl loop_body
loop_body:
    { add32 r1, r2, r3 }
.globl loop_end
loop_end:
    { add32 r4, r5, r6 }
