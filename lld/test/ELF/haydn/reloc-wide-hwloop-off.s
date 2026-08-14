# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 \
# RUN:   --section-start=.text.body=0x1000c
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION: cross-section SET_HWLOOP labels force external relocs that
# lld must accept (no "unrecognized relocation" Fatal).
#
# Format E EncodedBytes=12. Do not .balign 16 after SET (4-byte pad fails
# writeNopData). Product emits typed R_HAYDN_HWLoopOff1/Off2 for symbolic
# Off1/Off2 — pin live emission; LLD must accept without Fatal.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_HWLoopOff1 loop_body 0x0
# RELOCS-DAG:      0x0 R_HAYDN_HWLoopOff2 loop_end 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    set_hwloop_w 0, loop_body, loop_end, 3
    .size _start, .-_start

.section .text.body
.globl loop_body
loop_body:
    { add32 r1, r2, r3 }
.globl loop_end
loop_end:
    { add32 r4, r5, r6 }
