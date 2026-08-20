# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Standalone MOVE32 is 2-op from AsmString ("move32 rd, rs1"). The
# generated member is 2-op. Placement fill is positional — it must not
# reconstruct a 3-op compiler bag (rd, rs1, rs2). Compiler extra-op
# cutover is Finalize keep-map. Closed extra-op (AR-UA POST / CB) is
# e96-fill-peel-one-parcel.s.

.text
  { move32 r0, r1 }
  { move32 r2, r3; nop }
  { add32 r4, r0, r1; xor32 r5, r2, r3 }

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}move32
# CHECK: {{.*}}c: {{.*}}move32
# CHECK: {{.*}}18: {{.*}}{
# CHECK: add32
# CHECK: xor32
# CHECK-NOT: <unknown>
# CHECK-NOT: one-parcel placement failed
# CHECK-NOT: sequential E2 singleton split

# Three parcels × 12 bytes.
# SEC: Name: .text
# SEC: Size: 36
