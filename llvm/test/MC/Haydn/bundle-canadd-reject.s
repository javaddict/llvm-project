# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: verifier — fail-closed negative: Format E hard-rejects more than
# three co-issued entries. Product diagnostic is the entry-count gate.

# CHECK: error: Format E bundle supports at most three entries
{ add32 r0, r1, r2; add32 r3, r4, r5; add32 r6, r7, r8; add32 r9, r10, r11 }
