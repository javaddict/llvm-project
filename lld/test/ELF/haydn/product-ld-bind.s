# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld --nmagic -T %S/../../../../haydn-rt/haydn.ld %t.o -o %t.elf
# RUN: llvm-readobj --file-headers %t.elf | FileCheck %s --check-prefix=HDR
# RUN: llvm-readobj -S %t.elf | FileCheck %s --check-prefix=SEC
# RUN: llvm-size -A %t.elf | FileCheck %s --check-prefix=SIZE

# Same-artifact product linker script bind (compiler object -> LLD ->
# haydn-rt/haydn.ld). The test script next to this file is not the product
# authority. Do not invent e_machine; 0x103 is the experimental 259 value.
# Idle pad is a whole Format E parcel. Not BundleSim execution.

# HDR: Machine: 0x103
# HDR: Entry: 0x10000
# HDR: Flags [ (0x1)

# SEC: Name: .text
# SEC: Address: 0x10000

# SIZE: .text                12

        .section .text
        .globl _start
        .type _start, @function
_start:
        nop
        .size _start, .-_start
