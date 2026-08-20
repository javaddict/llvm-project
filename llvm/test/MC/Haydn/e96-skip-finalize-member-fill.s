# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: standalone hand-asm still places public logicals (one-parcel DFS).
# Compiler skip-Finalize bag-sort is isolated: AsmPrinter inverse-at-entry
# is positional only; keep-map is PublicHandAsm. Immediate CSRW_W places as
# the CSRW member (csrw print).

.text

# CHECK-LABEL: <.text>:

# CHECK: add32
{ add32 r1, r2, r3 }

# CHECK: csrw{{.*}}r1
{ csrw_w 44, r1 }
