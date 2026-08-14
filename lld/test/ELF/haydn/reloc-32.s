# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-readobj -x .text %t | FileCheck %s
#
# R_HAYDN_32 absolute 32-bit data relocation after format E code.
#
# Layout (format E bundles are 12 bytes, not Bundle128's 16):
#   0x10000: format E ADD32 bundle  (12 B)
#   0x1000c: format E MOVE32 bundle (12 B)
#   0x10018: .long external_func  → LE word = VA(external_func)
#   0x1001c: external_func follows (nm reports its address).

# NM-DAG: {{[0-9a-f]+}} T _start
# NM-DAG: {{[0-9a-f]+}} T external_func

# Absolute word at VA 0x10018 within the .text hex dump. external_func is
# placed immediately after that 4-byte word, at 0x1001c, when no other padding
# is inserted — LE bytes of 0x0001001c are 1c 00 01 00. The address is
# derivable: 0x10000 + 2 * 12 (two bundles) + 4 (the .long).
# If linker padding changes, update this CHECK from nm + readobj together.
# CHECK: Hex dump of section '.text':
# CHECK: 1c000100

.globl _start
_start:
    ADD32 R0, R1, R2
    MOVE32 R2, R3
    .long external_func
    .size _start, .-_start

.globl external_func
external_func:
    SUB32 R0, R1, R2
    .size external_func, .-external_func
