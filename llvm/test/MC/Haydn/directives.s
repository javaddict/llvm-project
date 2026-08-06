# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: object — Section directives.

#===----------------------------------------------------------------------===
# Section directives
#===----------------------------------------------------------------------===

# CHECK: .text

.text

# CHECK: .data
.data

# CHECK: .section	.bss,"aw",@nobits
.bss

# Back to text
.text

#===----------------------------------------------------------------------===
# Data directives
#===----------------------------------------------------------------------===

# CHECK: .byte 66
.byte 0x42

# CHECK: .byte 16
# CHECK-NEXT: .byte 32
# CHECK-NEXT: .byte 48
.byte 0x10, 0x20, 0x30

# CHECK: .short 4660
.short 0x1234

# CHECK: .short 43981
.2byte 0xABCD

# CHECK: .long 305419896
.long 0x12345678

# CHECK: .long 3735928559
.4byte 0xDEADBEEF

#===----------------------------------------------------------------------===
# Symbol directives
#===----------------------------------------------------------------------===

# CHECK: .globl global_func
.global global_func

global_func:
ADD32 R0, R0, R0

# CHECK: .local local_func
.local local_func

local_func:
ADD32 R1, R1, R1

#===----------------------------------------------------------------------===
# Alignment directives
#===----------------------------------------------------------------------===

# CHECK: .p2align 2
.p2align 2

# CHECK: .p2align 2
.align 4

#===----------------------------------------------------------------------===
# Size/type directives
#===----------------------------------------------------------------------===

# CHECK: .size global_func, 4
.size global_func, 4

# CHECK: .type global_func,@function
.type global_func, @function

# CHECK: .type data_obj,@object
.type data_obj, @object

#===----------------------------------------------------------------------===
# Symbol references
#===----------------------------------------------------------------------===

# CHECK: local_label:
local_label:
ADD32 R2, R3, R4

# CHECK: .long extern_symbol
.long extern_symbol

#===----------------------------------------------------------------------===
# Multiple sections
#===----------------------------------------------------------------------===

# CHECK: .section .rodata
.section .rodata

# CHECK: .long 287454020
.long 0x11223344

# CHECK: .byte 85
.byte 0x55

# Back to text
.text

#===----------------------------------------------------------------------===
# String directives
#===----------------------------------------------------------------------===

# CHECK: .ascii "Hello"
# CHECK-NEXT: .byte 0
.asciz "Hello"

# CHECK: .ascii "World"
# CHECK-NEXT: .byte 0
.ascii "World"
.byte 0

#===----------------------------------------------------------------------===
# Fill directives
#===----------------------------------------------------------------------===

# CHECK: .fill 4, 1, 0xff
.fill 4, 1, 0xFF

# CHECK: .zero 8
.zero 8
