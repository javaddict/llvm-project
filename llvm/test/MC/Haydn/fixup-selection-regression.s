# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:   llvm-objdump -d -r --triple=haydn-unknown-elf - | FileCheck %s

# REGRESSION TEST: Fixup selection must use TableGen enum values, not encoding opcodes.
#
# Bug: HaydnMCCodeEmitter.cpp compared MI.getOpcode (TableGen enum values
# e.g., JAL=975, JALR=976) against ISA encoding opcodes (e.g., 0x37 for JAL
# 0x38 for JALR). These comparisons NEVER matched because the values come from
# completely different number spaces:
# TableGen enums: sequential from 0 through generic opcodes then target
# opcodes. Haydn instructions get values 934-1044.
# ISA encoding opcodes: small values 0x00-0x64 embedded in the instruction
# word.
# The old code: `if (Opcode == 0x37)` compared JAL's enum 975 against 0x37(55).
# Never matches. JAL always fell through to the default fixup.
#
# Fix: Replace all hardcoded numbers with TableGen enum names:
# `Opcode == 0x37` -> `Opcode == Haydn::JAL`
# `Opcode == 0x38` -> `Opcode == Haydn::JALR`
# Range checks `0x20-0x2C` and `0x3B-0x42` replaced with explicit enum lists.
#
# This test verifies that JAL produces the correct R_HAYDN_WIDE_CallSImm20 fixup
# (not R_HAYDN_BranchSImm16, which was the JALR path, and not some wrong type
# from the default path).
#
# Phase-2 decoder purge note (prior revision) + WIDE LS migration: the
# legacy 4-byte Haydn32 decoder probes were deleted, and asm-parse ld32/st32
# now route to the 48-bit WIDE LS parcel (emitWideLSParcel) which the Haydn48
# trie decodes cleanly. jal asm-parse now emits the 16-byte Bundle128
# JAL_S0_FLEX parcel (the fixup-restored cutover routes asm-parse jal through
# the Bundle128 path). The load-bearing assertion of THIS test is the
# RELOCATION TYPE on the `-r` output — that survives all the parcel-width
# changes (the fixup KIND is selected at MC-emit time, independent of the
# decoder). The CHECK lines below assert only the reloc types, using {{.*}}
# for the instruction-dependent offset so parcel-width drift does not break
# the test (the OFFSET is a function of the prior instructions' widths, not
# of the fixup selection logic under test).
#
# Do NOT update CHECK lines without understanding the root cause.

#===----------------------------------------------------------------------===#
# JAL must produce R_HAYDN_WIDE_CallSImm20 (20-bit call fixup)
# Before the fix, the comparison Opcode == 0x37 never matched Opcode == 975
# so JAL fell through to the default which happened to be CallSImm20 too
# but only by coincidence. The fix makes the intent explicit and correct.
#===----------------------------------------------------------------------===#

jal r0, external_func
# CHECK:      {{.*}}: R_HAYDN_WIDE_CallSImm20 external_func

#===----------------------------------------------------------------------===#
# Second JAL to verify consistency
#===----------------------------------------------------------------------===#

jal r0, another_func
# CHECK:      {{.*}}: R_HAYDN_WIDE_CallSImm20 another_func

#===----------------------------------------------------------------------===#
# LD32 with symbolic offset. routes asm-parse LD/ST to the 48-bit WIDE
# LS parcel (emitWideLSParcel), so the symbolic offset emits the WIDE-LS
# fixup FIXUP_HAYDN_WIDE_LSOff20 -> R_HAYDN_LO20 (20-bit, ±512KB). The legacy
# R_HAYDN_32 (FIXUP_HAYDN_32 from the deleted 4-byte FmtLS path) is gone.
#===----------------------------------------------------------------------===#

ld32 r1, r2, data_sym
# CHECK:      {{.*}}: R_HAYDN_LO20 data_sym

#===----------------------------------------------------------------------===#
# ST32 with symbolic offset -- same WIDE-LS path, R_HAYDN_LO20
#===----------------------------------------------------------------------===#

# asm-parse ST32 also routes through the 48-bit WIDE LS parcel. The
# load-bearing assertion remains the reloc TYPE (R_HAYDN_LO20, not the
# legacy R_HAYDN_32 nor a wrong BranchSImm16).
st32 r3, r4, store_sym
# CHECK:      {{.*}}: R_HAYDN_LO20 store_sym
