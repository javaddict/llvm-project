// RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
// RUN: llvm-dwarfdump --debug-line %t.o | FileCheck %s
// REQUIRES: haydn-registered-target
//
// Every DWARF line-table address must be an exact bundle address.
//
// `minimum_instruction_length` is the unit MCDwarf DIVIDES address advances
// by. Setting it to the parcel size (12) is only exact while every advance is
// a whole number of parcels; functions align to 4 (the largest power of two
// dividing 12), so a perturbed layout yields advances that truncate and every
// later address drifts. On the pre-merge line the same defect wore a
// different constant: Bundle128's 16 against 12-byte parcels rounded the
// `.loc` at 0x24 to 0x20 and the one at 0x48 to 0x40, and LLDB asked
// BundleSim for a breakpoint at a mid-bundle address.
//
// This base sets the value to the golden 2-byte min bundle-address
// alignment (MinBundleAddressAlignBytes) — exact for every even address,
// which the golden guarantees. The 12 and 16 constants of earlier lines
// both drifted; this test pins the numbers directly either way.
//
// The filler bundles are nop+ALU pairs — this assembler fail-closes on
// all-nop idle bundles ("no approved Format E completion").

	.text
	.file 1 "bundle.c"
	.globl _start
_start:
	.loc 1 1 0
	{ nop; add32 r1, r1, r0 }
	{ nop; add32 r1, r1, r0 }
	{ nop; add32 r1, r1, r0 }
	.loc 1 2 0
	{ nop; add32 r1, r1, r0 }
	{ nop; add32 r1, r1, r0 }
	{ nop; add32 r1, r1, r0 }
	.loc 1 3 0
	{ nop; add32 r1, r1, r0 }

// Bundles are 12 bytes, so the three `.loc` addresses are 0, 3*12 and 6*12.
// A multiple-of-16 answer here is the Bundle128-era defect (0x0, 0x20, 0x40);
// truncated multiples of 12 are the ProductBytes-era one.
// CHECK:      Address {{.*}}Line
// CHECK:      0x0000000000000000 {{ *}}1
// CHECK-NEXT: 0x0000000000000024 {{ *}}2
// CHECK-NEXT: 0x0000000000000048 {{ *}}3
