# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.good.o
# RUN: llvm-readobj --file-headers %t.good.o | FileCheck %s --check-prefix=GOOD
# Product MC stamps EF_HAYDN_E96=0x1.
# GOOD: Flags [ (0x1)
#
# Zero e_flags must fail closed — no silent upgrade to product profile.
# RUN: cp %t.good.o %t.zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" %t.zero.o
# RUN: not ld.lld %t.zero.o -o %t.out 2>&1 | FileCheck %s --check-prefix=REJECT0
# REJECT0: incompatible e_flags 0x0
# REJECT0: expected Format E ABI flag 0x1
#
# Wrong nonzero flag also rejects.
# RUN: cp %t.good.o %t.bad.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',2));f.close()" %t.bad.o
# RUN: not ld.lld %t.bad.o -o %t.out2 2>&1 | FileCheck %s --check-prefix=REJECT2
# REJECT2: incompatible e_flags 0x2
# REJECT2: expected Format E ABI flag 0x1
#
# Positive: product-flag object links.
# RUN: ld.lld %t.good.o -o %t.ok --section-start=.text=0x10000
# RUN: llvm-readobj --file-headers %t.ok | FileCheck %s --check-prefix=LINKED
# LINKED: Flags [ (0x1)

.section .text
.globl _start
_start:
    nop
    .size _start, .-_start
