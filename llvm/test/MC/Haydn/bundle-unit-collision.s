# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target
# XFAIL: *
#
# KNOWN DEFECT — the assertion is right and the assembler is wrong. Do not
# "fix" this by relaxing it; see FORMAT-E-SWITCH-PLAN.md § 5.12.
#
# Format E: "an entry maps to exactly one unit; no two entries in a bundle may
# name the same unit" (§ 3). BEQ, BNEZ and BEQZ have ALU0 members and nothing
# else — 16 logicals are ALU0-only, all of them branches, JAL/JALR and the
# SET_HWLOOP family. Three of them in one bundle is three control transfers on
# one branch unit, which no reading of the model permits.
#
# llvm-mc accepts it. It picks BEQ_P30_ALU0 / BNEZ_P31_ALU0 / BEQZ_P32_ALU0 —
# three positions, one unit — and emits a parcel.
#
# Why this triple and not `{ beq; bne; blt }`: those exist only at positions 0
# and 1, so a third one is rejected on POSITION, and reading that as the unit
# check working is exactly the mistake this test exists to prevent.
# bundle-canadd-reject.s holds the position cases. BNEZ and BEQZ have P32
# members, so here all three positions are available and the unit is the only
# thing left to reject on.
#
# Mechanism, for whoever fixes it: HaydnBundle.h's `add(I*, MCSlotKind
# HintSlot)` reads the unit with `unitBitsForMember(MII, Opcode)`, which
# derives it from the member NAME. On the hinted path the AsmParser has
# already folded the opcode to its LOGICAL (`getLogicalBaseOpcode`), and a
# logical name carries no `_P<form><pos>_<UNIT>` suffix, so the call returns 0
# and the hint is taken without claiming anything. OccupiedUnits therefore
# never accumulates and the `pickSlot` fallback — which does seed
# `Probe.OccupiedUnits` correctly — is never reached for well-formed
# positional text. The code says so: "for a logical the member is picked later,
# so nothing is claimed here and the axis stays permissive." That was right
# under Bundle128, where every unit bit was 0 anyway.
#
# NOT part of this test, because it needs an answer this repo does not have:
# NOP padding also always lands on ALU0, so `{ add32 r0, r1, r2 }` alone
# becomes three ALU0 entries. Whether a NOP occupies its unit at all is a
# hardware question, and the simulator is the only thing that can settle it.
# The case below has no NOP in it for exactly that reason.

# CHECK: error: incorrect bundle
{ beq r1, r2, 8; bnez r3, 16; beqz r4, 24 }
