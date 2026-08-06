# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 \
# RUN:   --section-start=.text.body=0x10010
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION TEST (L228-class sibling): lld must DISPATCH the hwloop offset
# relocations through getRelExpr, not only relocate().
#
# Bundle128 SET_HWLOOP emits R_HAYDN_HWLoopOff1/Off2. Order in .rela.text is
# emission order (Off2 before Off1 is fine — both must be present and accepted
# by getRelExpr). Cross-section body labels force external relocs (MC cannot
# resolve them locally).
#
# ld.lld must exit 0 (getRelExpr accepts both). Decoder/objdump of SET_HWLOOP
# is out of scope here.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_HWLoopOff1 loop_body 0x0
# RELOCS-DAG:      0x0 R_HAYDN_HWLoopOff2 loop_end 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    set_hwloop 0, loop_body, loop_end, 3
    .balign 16

.section .text.body
.balign 16
.globl loop_body
loop_body:
    { add32 r1, r2, r3 }
.globl loop_end
loop_end:
    { add32 r4, r5, r6 }
