# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
// CHECK: {{.*}}0: 07 cb 80 a9 00 00 00 00 00 00 00 00  	{ nop; sub32	r8, r9, r10 }
// CHECK: {{.*}}c: 07 8b b0 9a 00 00 00 00 00 00 00 00  	{ nop; add32	r11, r10, r9 }
// CHECK: {{.*}}18: 07 8b 80 a9 00 00 00 00 00 00 00 00  	{ nop; add32	r8, r9, r10 }

# Role: object — ALU32 RR ops whose GPRs fall outside the r0-r7 window (r8-r11) MUST still assemble as bare mnemonics and emit a Format E parcel.

# REGRESSION TEST: ALU32 RR ops whose GPRs fall outside the r0-r7 window
# (r8-r11) MUST still assemble as bare mnemonics and emit a Format E parcel
# parcel with the op in s0 (post- / tryEncodeGFormat retirement).
#
# HISTORY: this file previously XFAIL'd on `add32 r11, r12, r13` for two
# independent reasons that were conflated:
# (1) R13 is the stack pointer and is named `sp` in asm (not `r13`);
# R14=`fp`, R15=`lr`. The bare form `add32 r11, r12, r13` is a
# register-name error, not an r8-r15 encoding gap.
# (2) Pre-R10 the encoder auto-wrapped bare r8-r15 ALU32 into an 8-byte
# Format E singleton parcel. Post- the live path is Format E (12 bytes)
# via encodeFormatE — same routing for r0-r7 and r8-r11 (see
# gformat-r0r7-vs-r8r15-routing.s).
#
# This rewrite pins the Format E truth for r8-r11 bare ALU32 (sub32 +
# add32). Reserved SP/FP/LR names are not used as general operands here.

.text
.globl _start
_start:

// sub32 r8, r9, r10 — all outside r0-r7, still Format E entry-0.
sub32 r8, r9, r10

// add32 r11, r10, r9 — r11 dest, high GPRs.
add32 r11, r10, r9

// add32 r8, r9, r10 — symmetric high-bank triple.
add32 r8, r9, r10
