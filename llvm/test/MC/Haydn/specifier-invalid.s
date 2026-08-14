# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s

# Role: parse — unknown %specifier is rejected.

# REGRESSION TEST: only %hi12/%lo20/%pc_lo20 are Haydn relocation specifiers.
# %hi is RISC-V; accepting it would emit the wrong fixup or a silent
# opcode-default. If this test starts passing, the parser grew a name that
# is not in Haydn::parseSpecifierName.

    lui r1, %hi(foo)
# CHECK: error: invalid relocation specifier
