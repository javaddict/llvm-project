# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST for § 5.12, FIXED. This was XFAIL until the AsmParser got an
# MCInstrInfo and the hinted path learned to resolve a LOGICAL's unit.
#
# Format E: "an entry maps to exactly one unit; no two entries in a bundle may
# name the same unit" (§ 3). BEQ, BNEZ and BEQZ have ALU0 members and nothing
# else — 16 logicals are ALU0-only, all of them branches, JAL/JALR and the
# SET_HWLOOP family. Three of them in one bundle is three control transfers on
# one branch unit, which no reading of the model permits.
#
# llvm-mc used to accept it, picking BEQ_P30_ALU0 / BNEZ_P31_ALU0 /
# BEQZ_P32_ALU0 — three positions, one unit — and emitting a parcel.
#
# Why this triple and not `{ beq; bne; blt }`: those exist only at positions 0
# and 1, so a third one is rejected on POSITION, and reading that as the unit
# check working is exactly the mistake this test exists to prevent.
# bundle-canadd-reject.s holds the position cases. BNEZ and BEQZ have P32
# members, so here all three positions are available and the unit is the only
# thing left to reject on.
#
# It took TWO fixes, and either alone leaves the axis silent:
#
#   * the AsmParser built a plain `HaydnMCFormats`, with no MCInstrInfo. A
#     unit is read off a member's NAME and the name comes from MII, so
#     without it nothing is ever claimed — whatever the rest of the code does.
#   * `add(I*, MCSlotKind HintSlot)` resolved the unit with
#     `unitBitsForMember`, which answers 0 for a LOGICAL because a logical
#     name has no `_P<form><pos>_<UNIT>` suffix. The hinted path holds
#     logicals, so the hint was taken while claiming nothing. It now asks the
#     placement alternatives AT THAT SLOT for a unit that is free.
#
# NOP is exempt by decision, not by omission: it occupies no unit, so
# `{ add32 r0, r1, r2 }` padding to three ALU0 entries is legal and the
# padding's encoding is unchanged by this fix. The case below contains no NOP
# anyway.

# CHECK: error: incorrect bundle
{ beq r1, r2, 8; bnez r3, 16; beqz r4, 24 }
