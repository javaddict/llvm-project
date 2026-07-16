# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

// All ALU32 RR ops route to Mode-0 regardless of register bank
// (encoding_manual.md §3.6 + §6).
//
// HISTORY: this test previously pinned an MC-time G-format decision
// `sub32 r1, r2, r3` (all r0-r7) was expected to emit a 4-byte G-format parcel
// (pattern 0001) while `sub32 r8, r9, r10` (r8-r15) emitted an 8-byte Mode-0
// bundle. That MC-time split was performed by `HaydnMCCodeEmitter::
// tryEncodeGFormat`, which has been RETIRED because it violated manual §3.6:
// the G-format s0 sub-slot is a destructive 2-register form requiring
// `rd == rs1`, but tryEncodeGFormat fired for `sub32 r1, r2, r3` (rd != rs1)
// and packed Rs2 into the imm2 immediate field, producing a parcel hardware
// misexecuted. (See gformat-32bit-roundtrip.s for the full bug writeup.)
//
// CURRENT BEHAVIOR: there is NO MC-time G-format selection. Every 32-bit
// ALU32 RR op — whether its registers land in r0-r7 or r8-r15 — routes to
// `emitMode0S0Bundle` and emits an 8-byte Mode-0 bundle (bits[3:0]=0011) with
// the full 3-register, non-destructive form. This test pins that BOTH register
// banks now share the identical Mode-0 routing. The G-format size win for the
// rd==rs1 destructive case will return via a post-RA variant-select pass that
// emits the `_g` opcode family (gformat-variant-opcodes-encode.s).
//
// NOTE on Haydn asm comments: Haydn's AsmParser CommentString is "//"
// (HaydnMCAsmInfo.cpp). Comments inside the asm body MUST use "//" — "#"
// makes the parser treat the rest of the line as operands, failing with
// "expected comma between operands". (lit RUN/CHECK markers still use "#"
// that is lit's syntax, not the asm comment string.)

.text
.globl _start
_start:
    sub32 r1, r2, r3      // r0-r7 -> Mode-0
    sub32 r8, r9, r10     // r8-r15 -> Mode-0 (same routing)

// CHECK: 0: 32 01 00 00 c0 01 00 00 00 00 00 00 00 00 00 00 { sub32 r1, r2, r3
// CHECK: 10: a9 08 00 00 c0 01 00 00 00 00 00 00 00 00 00 00 { sub32 r8, r9, r10
