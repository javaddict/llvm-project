# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — positive pin that product LS/branch still encode after
# rowFor fail-closed residual (unknown/Invalid kinds Unresolved, not None).
# getRelocFieldInfo(Invalid).Trans == Unresolved so isRelocTransformReady is
# false; product BranchSImm12 / LS_IMM rows remain table-ready.

# CHECK: encoding
{ beqz r1, 0; nop }
{ ld32 r0, r1, 0; nop }
