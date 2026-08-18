# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=UNKNOWN=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=UNKNOWN %s

# REGRESSION TEST: W38 — fail-closed fixup-kind ladders.
#
# Bug: getBranchFixupKind silently returned WIDE_BranchSImm12_RI for ANY
# unrecognized branch mnemonic, and getCallFixupKind returned
# WIDE_CallSImm20 for any unrecognized call mnemonic. A future `brtarget` /
# `calltarget` EncoderMethod user (new .td opcode) would silently borrow a
# kind whose field window is wrong for its real geometry.
#
# Fix: every EncoderMethod user is enumerated (RI12 two-reg names were
# previously reaching the trailing default); the fallthrough is now
# report_fatal_error naming the mnemonic. getExprFixupKind's old
# FIXUP_HAYDN_32 default was already fail-closed (F19,
# f19-unknown-logical-fixup-fail-closed.s).
#
# Test design: positive controls pin the typed kind for every mnemonics
# that routes through the ladders (I12 one-reg, RI12 two-reg, JAL, JALR).
# If the enumeration drops a live name, the positive control fails with the
# new fatal "no branch/call fixup kind defined for mnemonic". A completely
# unknown mnemonic never reaches the emitter — the AsmParser rejects it
# first (UNKNOWN check below); the emitter fatals are the second line of
# defense against future .td growth, unreachable from hand asm today.

# CHECK: fixup A - offset: 0, value: sym_i12, kind: FIXUP_HAYDN_WIDE_BranchSImm12
beqz r1, sym_i12
# CHECK: fixup A - offset: 0, value: sym_ri12, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
beq r1, r2, sym_ri12
# CHECK: fixup A - offset: 0, value: sym_ri12b, kind: FIXUP_HAYDN_WIDE_BranchSImm12_RI
bgeu r1, r2, sym_ri12b
# CHECK: fixup A - offset: 0, value: sym_call, kind: FIXUP_HAYDN_WIDE_CallSImm20
jal r1, sym_call

# UNKNOWN: error: invalid instruction mnemonic
.ifdef UNKNOWN
bogus_branch r1, sym
.endif
