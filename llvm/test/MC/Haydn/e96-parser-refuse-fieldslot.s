# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=BUNDLE=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=BUNDLE
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=BARE=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=BARE
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=ABS64STAR=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=ABS64STAR

# Residual FieldSlots are not public match results. Public hand-asm uses
# catalog logicals; do not recover a logical name from an `_S*` suffix.
# ABS64 is matcher-visible unsuffixed; abs64_s1 must not peel to ABS64.

.ifdef BUNDLE
# BUNDLE: error: assembler matched a private placement opcode
{ nop_s0 }
.endif

.ifdef BARE
# BARE: error: assembler matched a private placement opcode
nop_s0
x2slt32_s1 d0, d1
.endif

.ifdef ABS64STAR
# ABS64STAR: error: assembler matched a private placement opcode
abs64_s1 d0, d1
{ abs64_s2 d0, d1 }
.endif
