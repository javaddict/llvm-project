# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=ROUNDTRIP
#
# REGRESSION TEST (F08): CallSImm20 fixup must NOT clobber the link register.
#
# Bundle128 JAL (HaydnFU_ALU32_S0 / FlexMap): rd is encoded in the low
# window with the call target fixup. applyFixup must write only the
# displacement field; the link register (lr / R15) survives.
#
# Use the architectural name `lr` (asm rejects bare `r15` for this operand
# class — R15 is the named link register).
#
# If F08 regresses, ROUNDTRIP shows jal with a wrong destination register
# (often r0) instead of lr.

# CHECK: jal{{.*}}lr, forward_target
# CHECK: FIXUP_HAYDN_WIDE_CallSImm20
# ROUNDTRIP: jal{{.*}}lr,
jal lr, forward_target

forward_target:
# CHECK: add32{{.*}}r0, r1, r2
# ROUNDTRIP: add32{{.*}}r0, r1, r2
add32 r0, r1, r2
