# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

# Plain ALU32 RR ops route to Mode-0 (encoding_manual.md §3.6 + §6).
#
# WHY THIS TEST EXISTS (regression pin for the tryEncodeGFormat retirement):
# Manual §3.6 defines the 32-bit G-format s0 sub-slot as a DESTRUCTIVE
# 2-register form: an RR op `OP rd, rs1, rs2` may only use G-format when
# `rd == rs1` (tied destination and first source), with `rs = rs2` and the
# 2-bit imm2 field holding an IMMEDIATE (unused for RR).
#
# The retired `HaydnMCCodeEmitter::tryEncodeGFormat` ignored this contract:
# it packed Rs1->rs, Rs2->imm2 (convention B) and fired whenever all three
# regs happened to land in r0-r7 with rs2 in r0-r3 — INCLUDING cases where
# rd != rs1 (not G-eligible at all). That emitted a parcel the matching
# decoder round-tripped, but real hardware (convention A) misexecuted: e.g.
# `sub32 r1, r2, r3` encoded rt=r1, rs=r2, imm2=r3, so hardware computed
# `r1 = r1 - r2` and silently dropped r3. tryEncodeGFormat was removed; ALL
# 32-bit ALU32 RR ops now route to `emitMode0S0Bundle` (64-bit Mode-0 §6)
# which encodes the full 3-register, non-destructive form correctly.
#
# This test pins that routing: `sub32 r1, r2, r3` and `or32 r0, r1, r2`
# (both rd != rs1 — NOT G-eligible) MUST emit a 16-byte Bundle128
# parcel (s0 ALU32, `` slot suffix), never a 4-byte G-format parcel
# (bits[3:0]=0001). If a future change revives MC-time G-format emission
# for these, the CHECK bytes will diverge and fail loudly here.
#
# The convention-A G-format path for the rd==rs1 case is covered by the
# dedicated `_g` variant-opcode encode test (gformat-variant-opcodes-encode.s)
# which pins the manual §3.6 opcode map directly.

.text
.globl _start
_start:
    sub32 r1, r2, r3     // rd!=rs1 -> Mode-0 (NOT G-eligible)
    or32  r0, r1, r2     // rd!=rs1 -> Mode-0 (NOT G-eligible)

# Both ops are r0-r7 and both take the ordinary path: 12-byte parcels sharing
# the format-indicator and entry-mapping prefix, differing in the opcode
# nibble (1a for sub32, 26 for or32) and the register fields. A G-format
# decision would have made the first one a shorter, differently shaped parcel
# and moved the second off 0xc.
# CHECK:      0: 8f 00 00 00 40 00 00 00 e0 1a 42 06 {{.*}}sub32{{.*}}r1, r2, r3
# CHECK:      c: 8f 00 00 00 40 00 00 00 e0 26 20 04 {{.*}}or32{{.*}}r0, r1, r2
