# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: ALU32 RR ops whose GPRs fall outside the r0-r7 window
# (r8-r11) MUST still assemble as bare mnemonics and emit a Bundle128
# parcel with the op in s0 (post- / tryEncodeGFormat retirement).
#
# HISTORY: this file previously XFAIL'd on `add32 r11, r12, r13` for two
# independent reasons that were conflated:
# (1) R13 is the stack pointer and is named `sp` in asm (not `r13`);
# R14=`fp`, R15=`lr`. The bare form `add32 r11, r12, r13` is a
# register-name error, not an r8-r15 encoding gap.
# (2) Pre-R10 the encoder auto-wrapped bare r8-r15 ALU32 into an 8-byte
# Mode-0 s0 bundle. Post- the live path is Bundle128 (16 bytes)
# via encodeBundle128 — same routing for r0-r7 and r8-r11 (see
# gformat-r0r7-vs-r8r15-routing.s).
#
# This rewrite pins the Bundle128 truth for r8-r11 bare ALU32 (sub32 +
# add32). Reserved SP/FP/LR names are not used as general operands here.

.text
.globl _start
_start:

// sub32 r8, r9, r10 — all outside r0-r7, still Bundle128 s0.
// 12-byte parcel, same prefix an r0-r7 op gets (gformat-r0r7-vs-r8r15-routing
// pins that identity directly). Only the register fields carry the bank.
// CHECK: 0: 8f 00 00 00 40 00 00 00 e0 1a 30 15
// CHECK-SAME: sub32{{.*}}r8, r9, r10
sub32 r8, r9, r10

// add32 r11, r10, r9 — r11 dest, high GPRs.
// CHECK: c: 8f 00 00 00 40 00 00 00 e0 12 56 13
// CHECK-SAME: add32{{.*}}r11, r10, r9
add32 r11, r10, r9

// add32 r8, r9, r10 — symmetric high-bank triple.
// sub32 and add32 over the SAME registers differ only in the opcode nibble
// (1a against 12) — compare with the first check.
// CHECK: 18: 8f 00 00 00 40 00 00 00 e0 12 30 15
// CHECK-SAME: add32{{.*}}r8, r9, r10
add32 r8, r9, r10
