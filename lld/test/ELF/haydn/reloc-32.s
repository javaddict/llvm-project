# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-readobj -x .text %t | FileCheck %s
#
# R_HAYDN_32 absolute 32-bit data relocation after Format E code.
#
# Layout (EncodedBytes=12):
#   0x10000: Format E ADD32 (12 B)
#   0x1000c: Format E MOVE32 (12 B)
#   0x10018: .long external_func  → LE word = VA(external_func)
#   0x1001c: external_func (SUB32)

# NM-DAG: {{[0-9a-f]+}} T _start
# NM-DAG: {{[0-9a-f]+}} T external_func

# Absolute word at 0x10018 = VA(external_func)=0x0001001c → LE 1c 00 01 00.
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
