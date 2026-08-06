# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — LD32 must print as "ld32", not "jal".

# REGRESSION TEST: LD32 must print as "ld32", not "jal".
#
# Bug : HaydnInstPrinter::printInst had a hardcoded opcode check
# `MI->getOpcode == 978` to special-case JAL printing. Opcode 978 was JAL
# when written, but tablegen renumbers opcodes when instructions are added to
# td files. By, opcode 978 = LD32 (not JAL). So LD32 was
# intercepted and printed as JAL, causing assembly round-trip failures.
#
# Fix: Changed to `MI->getOpcode == Haydn::JAL` using the enum constant.
#
# This test verifies that LD32 round-trips correctly through the assembler.
# If the opcode numbering changes again and someone re-introduces a magic number
# this test will fail because LD32 will print as the wrong instruction.
#
# Do NOT update CHECK lines without understanding why. If LD32 output changes
# to something other than "ld32", there is a bug in the instruction printer.

# CHECK: ld32	r0, r1, 0

ld32 r0, r1, 0

# CHECK: ld32	r2, r3, 16
ld32 r2, r3, 16

# CHECK: ld32	r4, r5, -8
ld32 r4, r5, -8

# CHECK: ld32	r12, r0, 100
ld32 r12, r0, 100
