# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s

# REGRESSION TEST: F17 — MC has no PseudoLongB* / expandLongBranch.
#
# Bug: HaydnMCCodeEmitter::expandLongBranch plus HaydnLongBranchPseudo
# (long_beq / long_b / …) was a second long-branch mechanism beside
# CodeGen BranchRelaxation. AsmBackend mayNeedRelaxation is always false,
# so the MC path was dead, but the TD comment still claimed assembler
# relaxation produced inverted-cond + JAL, and the expander had a latent
# double-offset bug.
#
# Fix: delete PseudoLongB* and expandLongBranch. Long-branch stays in
# CodeGen BranchRelaxation only. If this test starts matching, an MC-layer
# second mechanism was reintroduced.

	long_beq r1, r2, far
# CHECK: error: invalid instruction mnemonic

	long_bne r1, r2, far
# CHECK: error: invalid instruction mnemonic

	long_b far
# CHECK: error: invalid instruction mnemonic
