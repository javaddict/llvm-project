# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: MC standalone boundary — bare logical assembly still places (DFS/row
# retry allowed for hand-asm). Committed private wire re-place refuse is owned
# by CodeGen/Haydn/bundle-corruption-matrix.mir (PRIV-REPLACE).

.text

# CHECK-LABEL: <.text>:

# Bare logical singleton places as one Format E parcel.
# CHECK: {{.*}}0: {{.*}}add32
{ add32 r1, r2, r3 }

# Two-op bare logical pack (standalone placement, not private-member wire).
# CHECK: {{.*}}c: {{.*}}{
# CHECK: add32
# CHECK: xor32
{ add32 r4, r0, r1; xor32 r5, r2, r3 }
