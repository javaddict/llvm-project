# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s \
# RUN:   | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o \
# RUN:   | FileCheck %s --check-prefix=DEC
#
# REGRESSION TEST: (Path B) — X2MUL32 _FLEX encoder round-trip.
#
# Bug: `x2mul32 d0, d1, d2` (3-operand asm under) aborted in
# HaydnMCCodeEmitter::getBinaryCodeForInstr (SmallVector.h:301
# MCOperand idx < size). The D_RR2 format class's `let s1` concat
# referenced the orphan `rtd2` bits field; the generated encoder table
# tried to read an operand for rtd2 but the def's dag bound only 3
# operands ($rtd1, $rsd1, $rsd2) -> SmallVector OOB -> SIGABRT.
#
# Fix (Path B): bind rtd2 to a dag operand (2 defs + 2 ins). The asm
# is now `x2mul32 d0, d1, d2, d3` (4 operands). The encoder reads all 4
# bound operands — no orphan field, no OOB.
#
# Test design: assemble a 2-dest X2MUL32 and verify (a) no SIGABRT during
# encoding, (b) the objdump round-trip decodes back. Before the fix
# `llvm-mc -show-encoding` exits with SIGABRT (signal 6 / exit 134).

# ENC:  x2mul32
# ENC-NOT: SIGABRT
x2mul32 d0, d1, d2, d3

# DEC-LABEL: <.text>:
# DEC:  x2mul32
