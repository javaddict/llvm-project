# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-readobj -x .text %t | FileCheck %s
#
# R_HAYDN_32 absolute 32-bit data relocation after Bundle128 code.
#
# Layout:
#   0x10000: Bundle128 ADD32 (16 B)
#   0x10010: Bundle128 MOVE32 (16 B)
#   0x10020: .long external_func  → LE word = VA(external_func)
#   external_func follows (nm reports its address).

# NM-DAG: {{[0-9a-f]+}} T _start
# NM-DAG: {{[0-9a-f]+}} T external_func

# Absolute word at file offset corresponding to VA 0x10020 within .text hex
# dump. external_func is placed immediately after the 4-byte word at 0x10024
# when no other padding is inserted — LE bytes of 0x00010024 are 24 00 01 00.
# If linker padding changes, update this CHECK from nm + readobj together.
# CHECK: Hex dump of section '.text':
# CHECK: 24000100

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
