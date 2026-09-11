# REQUIRES: haydn
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s
#
# ISA-69: symbolic JALR on generated E3 members is fail-closed. A 3-entry
# bundle is high-first (`{ a; b; c }` = e2,e1,e0). Neither the E3 e0 nor
# E3 e1 site may emit R_HAYDN_JALRSImm12_E3E{0,1}. Crafted objects are
# reloc-jalr-simm12-reject.test.

# CHECK: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# CHECK: refusing silent PC-relative R_HAYDN_JALRSImm12
# CHECK-NOT: kind: FIXUP_HAYDN_JALRSImm12
# CHECK-NOT: R_HAYDN_JALRSImm12{{_E3}}

.section .text
.globl _start
_start:

.globl e0_site
e0_site:
    { xor32 r3, r3, r3; nop; jalr r1, r2, tgt_e0 }
    { xor32 r0, r0, r0 }

.globl tgt_e0
tgt_e0:
    { add32 r0, r0, r0 }

.globl e1_site
e1_site:
    { xor32 r3, r3, r3; jalr r1, r2, tgt_e1; st32 r4, r5, 0 }
    { xor32 r0, r0, r0 }

.globl tgt_e1
tgt_e1:
    { add32 r0, r0, r0 }

.size _start, .-_start
