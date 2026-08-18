# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.good.o
# RUN: llvm-readobj --file-headers %t.good.o | FileCheck %s --check-prefix=GOOD
# Product MC stamps EM_HAYDN=259 + EF_HAYDN_E96=0x1. 259 is experimental and
# collides with a published Kalray KVX allocation; the flag is the distinguisher.
# A 259 object without this flag is refused so a KVX-like file cannot link.
# Do not invent a replacement e_machine. Generic LLD already rejects a
# non-259 object as incompatible with elf32haydn; the Haydn gate is e_flags.
# GOOD: Machine: 0x103
# GOOD: Flags [ (0x1)
#
# Zero e_flags must fail closed — no silent upgrade to product profile.
# A 259 object without EF_HAYDN_E96 looks like a KVX-like reuse of 259.
# RUN: cp %t.good.o %t.zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" %t.zero.o
# RUN: not ld.lld %t.zero.o -o %t.out 2>&1 | FileCheck %s --check-prefix=REJECT0
# REJECT0: incompatible e_flags 0x0
# REJECT0: expected Format E ABI flag 0x1
# REJECT0: EM_HAYDN=259 experimental
# REJECT0: reuse 259 without this flag
#
# Patch e_machine to 0. This is not a replacement Haydn number. Generic LLD
# rejects an incompatible machine; do not invent an interim e_machine here.
# Elf32_Ehdr.e_machine is at offset 18.
# RUN: cp %t.good.o %t.nomachine.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(18);f.write(struct.pack('<H',0));f.close()" %t.nomachine.o
# RUN: not ld.lld %t.nomachine.o -o %t.nomachine.out 2>&1 | FileCheck %s --check-prefix=REJECTM
# REJECTM: unsupported e_machine
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
