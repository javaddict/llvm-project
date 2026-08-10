# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — DSP math SFR moves encode→obj→disasm.
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 00 0c 00 00 00 00 00 00 00 00 00{{.*}}movegpr2sfr
# CHECK: {{.*}}c: 07 00 18 00 00 00 00 00 00 00 00 00{{.*}}movesfr2gpr

movegpr2sfr	r0

movesfr2gpr	r1
