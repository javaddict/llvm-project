# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=BUNDLE=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=BUNDLE
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=BARE=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=BARE

# Residual FieldSlots are not public match results. Public hand-asm uses
# catalog logicals; do not recover a logical name from an `_S*` suffix.

.ifdef BUNDLE
# BUNDLE: error: failed to match instruction in bundle
{ nop_s0 }
.endif

.ifdef BARE
# BARE: error: invalid instruction mnemonic
nop_s0
x2slt32_s1 d0, d1
.endif
