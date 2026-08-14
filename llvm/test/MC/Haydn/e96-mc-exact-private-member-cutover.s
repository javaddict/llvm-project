# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: MC exact serialize boundary — bare logical assembly still places
# (standalone DFS/row retry for hand-asm). Residual FieldSlot (_S*) peers
# are CodeGen setDesc materialize only. Private MemberId residual cutover is
# owned by CodeGen mc-exact-private-member-residual-as-is.mir.

.text

# CHECK-LABEL: <.text>:

# CHECK: {{.*}}0: {{.*}}add32
{ add32 r1, r2, r3 }

# CHECK: {{.*}}c: {{.*}}{
# CHECK-DAG: add32
# CHECK-DAG: xor32
{ add32 r4, r0, r1; xor32 r5, r2, r3 }
