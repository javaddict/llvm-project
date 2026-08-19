# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --image-base=0 --section-start=.text=0x0
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t | FileCheck %s
#
# Reloc CSRW I8: R_HAYDN_CSR_UImm8 patches unsigned uimm8 at parcel
# bits[39:32] (E2 e0). nearby @ VA 12 fits [0, 255]. Never R_HAYDN_8.
#
# RELOCS: R_HAYDN_CSR_UImm8 nearby
# RELOCS-NOT: R_HAYDN_8
#
# Constant `csrw 12, r3` encodes 07 03 3a 00 0c ... (imm @ byte 4).
# CHECK: {{.*}}0: 07 03 3a 00 0c 00 00 00 00 00 00 00{{.*}}csrw{{.*}}12

.globl _start
_start:
    csrw nearby, r3
nearby:
    nop
    .size _start, .-_start
