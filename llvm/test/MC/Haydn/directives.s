# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=TEXT
# RUN: llvm-objdump -s -j .data %t.o | FileCheck %s --check-prefix=DATA
# REQUIRES: haydn-registered-target

# Role: object — Section / data / symbol directives encode to a real object
# (encode→obj→readobj/objdump), not assembler-print-only.
# Load-bearing contract is section presence + text parcel for ADD32 and data
# payload bytes.

.text
.globl global_func
.global global_func
.type global_func, @function
global_func:
  ADD32 R0, R0, R0
.size global_func, .-global_func

.local local_func
local_func:
  ADD32 R1, R1, R1

.p2align 2

.data
.byte 0x42
.byte 0x10, 0x20, 0x30
.short 0x1234
.2byte 0xABCD
.long 0x12345678
.4byte 0xDEADBEEF

.section .bss,"aw",@nobits
.zero 4

# SEC: Name: .text
# SEC: Name: .data
# SEC: Name: .bss

# TEXT-LABEL: <global_func>:
# TEXT: add32
# TEXT-LABEL: <local_func>:
# TEXT: add32

# DATA: Contents of section .data:
# DATA: 42102030 3412cdab 78563412 efbeadde
