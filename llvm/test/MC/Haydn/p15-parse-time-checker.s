# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=UNIT=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=UNIT
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=WAW=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=WAW
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=PORTS=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=PORTS
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=HWLOOP=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=HWLOOP
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s --defsym=OK=1 | \
# RUN:   FileCheck %s --check-prefix=OK

# Parse-time bundle legality (Hexagon MCChecker analog). Public hand-asm
# rejects illegal co-issue before encode. Encoder DFS may still re-place a
# legal pack; this is not committed-entry inverse.

.ifdef UNIT
# Two stores share LOADSTORE0 — no injective unit cover under E2 or E3.
# UNIT: error: incorrect bundle
{ st32 r1, r2, 0; st32 r3, r4, 0 }
.endif

.ifdef WAW
# ADD32+XOR32 can co-issue under E3; same GPR dest is intra-cycle WAW.
# WAW: error: incorrect bundle: same-register WAW
{ add32 r1, r2, r3; xor32 r1, r4, r5 }
.endif

.ifdef PORTS
# Three ALU32 dests are unit-legal on E3 ALU0/1/2 but exceed GPR 2W.
# PORTS: error: incorrect bundle: cycle RF port demand
{ add32 r1, r0, r0; add32 r2, r0, r0; add32 r3, r0, r0 }
.endif

.ifdef HWLOOP
# Two SET_HWLOOP share the hwloop unit — no injective cover. Same-sel is
# a second law; unit injectivity fires first (one law, two entry points).
# HWLOOP: error: incorrect bundle
{ set_hwloop_f2_w 0, .L0, .L1, r1; set_hwloop_f2_w 0, .L2, .L3, r2 }
.L0: nop
.L1: nop
.L2: nop
.L3: nop
.endif

.ifdef OK
# Distinct dests, two ALU32s — legal hand-asm pack.
# OK: add32
# OK: xor32
{ add32 r1, r2, r3; xor32 r4, r5, r6 }
.endif
