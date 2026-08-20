# REQUIRES: haydn
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf start.s -o start.o
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf helper.s -o helper.o
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf idle.s -o idle.o
# RUN: llvm-readobj --file-headers start.o | FileCheck %s --check-prefix=OBJ
#
# Archive-member / startup-object mixed e_flags matrix.
# Product MC stamps EM_HAYDN=259 + production ELFFlagsValue (0x1).
# 259 is experimental and collides with Kalray KVX; the flag is the
# distinguisher. Do not invent a replacement e_machine. calcEFlags walks
# ctx.objectFiles after archive extraction (Driver.cpp), so extracted
# members and startup objects are in the same equality gate as a
# standalone .o. Peer: AIE.cpp:66-70 copies the first object's flags;
# Haydn overlay requires exact production equality.
#
# OBJ: Machine: 0x103
# OBJ: Flags [ (0x1)
#
# Positive: extracted archive member with product flags links.
# RUN: llvm-ar rcs libgood.a helper.o
# RUN: ld.lld start.o libgood.a -o good --section-start=.text=0x10000
# RUN: llvm-readobj --file-headers good | FileCheck %s --check-prefix=LINKED
# LINKED: Machine: 0x103
# LINKED: Flags [ (0x1)
#
# Extracted archive member with zero e_flags rejects (no silent upgrade).
# A 259 member without the production flag looks like a KVX-like reuse.
# RUN: cp helper.o helper-zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" helper-zero.o
# RUN: llvm-ar rcs libzero.a helper-zero.o
# RUN: not ld.lld start.o libzero.a -o zero-out --section-start=.text=0x10000 2>&1 | FileCheck %s --check-prefix=AR0
# AR0: incompatible e_flags 0x0
# AR0: expected Format E ABI flag 0x1
#
# Unreferenced zero-flag member is not extracted — link stays product.
# RUN: cp idle.o idle-zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" idle-zero.o
# RUN: llvm-ar rcs libidle.a idle-zero.o
# RUN: ld.lld start.o helper.o libidle.a -o idle-out --section-start=.text=0x10000
# RUN: llvm-readobj --file-headers idle-out | FileCheck %s --check-prefix=LINKED
#
# --whole-archive extracts the unreferenced zero-flag member and must reject.
# RUN: not ld.lld start.o helper.o --whole-archive libidle.a --no-whole-archive \
# RUN:     -o whole-out --section-start=.text=0x10000 2>&1 | FileCheck %s --check-prefix=AR0
#
# Startup mixed-input: product user + zero-flag "crt" object rejects.
# RUN: cp start.o crt-zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" crt-zero.o
# RUN: not ld.lld crt-zero.o helper.o -o crt-out --section-start=.text=0x10000 2>&1 | FileCheck %s --check-prefix=AR0
#
# Startup mixed-input: both product objects link.
# RUN: ld.lld start.o helper.o -o both --section-start=.text=0x10000
# RUN: llvm-readobj --file-headers both | FileCheck %s --check-prefix=LINKED

#--- start.s
.section .text
.globl _start
_start:
    nop
    .long helper
    .size _start, .-_start

#--- helper.s
.section .text
.globl helper
helper:
    nop
    .size helper, .-helper

#--- idle.s
.section .text
.globl idle_sym
idle_sym:
    nop
    .size idle_sym, .-idle_sym
