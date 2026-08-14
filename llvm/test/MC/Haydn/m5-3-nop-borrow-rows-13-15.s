# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

// CHECK: {{.*}}0: 07 8b 00 21 00 00 48 10 10 32 00 00 { 	x2mul32	d0, d1, d2, d3; 	add32	r0, r1, r2 }
// CHECK: {{.*}}c: 07 8b 30 54 00 00 00 00 00 00 00 00 { 	nop; 	add32	r3, r4, r5 }
// CHECK: {{.*}}18: 47 02 31 54 06 00 00 00 00 00 00 00 { 	nop; 	x2mul32	d3, d4, d5, d6 }
# Role: object — Format E encode of { add32; x2mul32 } plus standalone
# controls. Both ops must round-trip with original operands.

# REGRESSION TEST: Format E packing of a 2-child { ALU32; MAC } bundle.
#
# Product is one 12-byte Format E parcel. Historical Mode-0 NOP-borrow rows
# and HaydnDClassOpcodes.cpp are deleted (never-reintroduce). This file
# guards encode/disasm: no crash, no operand corruption (distinct MAC
# destinations stay distinct).
#
# What this test verifies:
# 1. { add32; x2mul32 } encodes as Format E and disassembles both ops.
# 2. Standalone add32 / x2mul32 controls still decode.
# 3. The MAC form stays 4-operand (no destructive rtd=rsd1 rewrite).

#===----------------------------------------------------------------------===#
# Case 1: 2-child { add32; x2mul32 } Format E bundle. Both ops must
# round-trip with original operands (no silent drop, no MAC aliasing).
# Print order is MAC then ALU32 in the current E2 packing.
#===----------------------------------------------------------------------===#

        { add32 r0, r1, r2 ; x2mul32 d0, d1, d2, d3 }

#===----------------------------------------------------------------------===#
# Case 2: standalone add32 / x2mul32 as controls.
#===----------------------------------------------------------------------===#
        add32 r3, r4, r5
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
        x2mul32 d3, d4, d5, d6

#===----------------------------------------------------------------------===#
# Distinct MAC operands must stay distinct (no rtd=rsd1 rewrite).
#===----------------------------------------------------------------------===#
