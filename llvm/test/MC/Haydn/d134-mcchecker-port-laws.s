# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=DEDUP=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=DEDUP
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=SOLO=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=SOLO
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=LS=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=LS
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=CSR=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=CSR
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s --defsym=OK=1 | \
# RUN:   FileCheck %s --check-prefix=OK

# D1.134/D1.144/D1.145: parse-time PortModel laws (Hexagon MCChecker analog).
# Per-operand RF ports, named HAYDN_*_PORTS ceilings, SIN_COS/ARCTAN
# issue-alone, unproven store/load overlap, CSRW 0x20-0x25 vs SET_HWLOOP.

.ifdef DEDUP
# Three ADD32 with a repeated src field are unit-legal on E3 ALU0/1/2 but
# charge 6 GPR reads (PortModel rule 3, no identity dedup) against 4R.
# DEDUP: error: incorrect bundle: cycle RF port demand
{ add32 r1, r2, r2; add32 r3, r4, r4; add32 r5, r6, r6 }
.endif

.ifdef SOLO
# HexagonMCChecker.cpp:692-703 checkSolo. SIN_COS is E3-only; ADD32 shares
# ALU cover, so unit injectivity would admit this without the solo law.
# SOLO: error: incorrect bundle: SIN_COS/ARCTAN must issue alone
{ sin_cos d0, r1, 1; add32 r2, r3, r4 }
.endif

.ifdef LS
# Same-base store+load at +0 overlaps. Distinct-base r2 vs r4 at +0 is
# GPR-disjoint (x4sel16-e3-mapping-canonical.s; see OK). Dual-load is
# not this law.
# LS: error: incorrect bundle: store/load pair is not proven disjoint
{ st32 r1, r2, 0; ld32 r3, r2, 0 }
.endif

.ifdef CSR
# HexagonMCChecker.cpp:326-338 checkHWLoop overlay; Haydn window is CSR
# 0x20-0x25 vs SET_HWLOOP, not SA0/SA1. CSRW ALU1/2 + SET ALU0 cover E3.
# CSR: error: incorrect bundle: CSRW HWLR
# CSR: SET_HWLOOP
{ csrw 32, r1; set_hwloop_f2_w 0, .L0, .L1, r2 }
.L0: nop
.L1: nop
.endif

.ifdef OK
# Distinct-src two-ALU is 4R/2W (at the GPR ceiling, not over). Dual-load
# is not the store/load overlap law (HexagonVLIWPacketizer.cpp:1559).
# Distinct-base store+load at +0 is GPR-disjoint (hand-asm has no AA).
# OK: add32
# OK: xor32
{ add32 r1, r2, r3; xor32 r4, r5, r6 }
# OK: ld32
{ ld32 r1, r2, 0; ld32 r3, r4, 8 }
# OK: st32
# OK: ld32
{ st32 r1, r2, 0; ld32 r3, r4, 0 }
.endif
