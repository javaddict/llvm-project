// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-dwarfdump --debug-line %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// Every DWARF line-table address must be an exact bundle address.
//
// `minimum_instruction_length` is the unit MCDwarf DIVIDES address advances
// by, and `HaydnMCAsmInfo` set it to 16 for Bundle128. A format E parcel is
// 12, so after the switch every address was rounded DOWN to a multiple of 16
// and the line table pointed into the middle of bundles: the `.loc` below at
// 0x24 reported 0x20, the one at 0x48 reported 0x40.
//
// Nothing in the compiler's own gates reads the line table, so this stayed
// invisible until a debugger tried to use it: LLDB asked BundleSim for a
// breakpoint at a mid-bundle address and got "failed to set breakpoint site",
// with the error naming the breakpoint rather than the address that was
// wrong. Hence this test — it compares the numbers directly.
//
// The value is 1 rather than 12 on purpose. Advances are not all whole
// parcels: functions align to 4, so the gap between two functions need not
// divide by 12 and the truncation would come back for exactly those cases.

	.text
	.file 1 "bundle.c"
	.globl _start
_start:
	.loc 1 1 0
	{ nop; nop; nop }
	{ nop; nop; nop }
	{ nop; nop; nop }
	.loc 1 2 0
	{ nop; nop; nop }
	{ nop; nop; nop }
	{ nop; nop; nop }
	.loc 1 3 0
	{ nop; nop; nop }

// Bundles are 12 bytes, so the three `.loc` addresses are 0, 3*12 and 6*12.
// A multiple-of-16 answer here is the old defect: 0x0, 0x20, 0x40.
// CHECK:      Address {{.*}}Line
// CHECK:      0x0000000000000000 {{ *}}1
// CHECK-NEXT: 0x0000000000000024 {{ *}}2
// CHECK-NEXT: 0x0000000000000048 {{ *}}3
