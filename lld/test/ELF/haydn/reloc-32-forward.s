# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: llvm-readobj -x .text %t | FileCheck %s
# RUN: llvm-readobj -s %t | FileCheck --check-prefix=SYMS %s

# Test R_HAYDN_32 relocation for forward references.
# When a .long directive references a symbol defined later in the same file,
# llvm-mc emits an R_HAYDN_32 relocation. The linker resolves it to the
# actual address of target_func.
#
# This test verifies:
# 1. The linker produces a valid ELF (no errors)
# 2. .text hex dump is non-empty (linking succeeded)
# 3. Both _start and target_func symbols appear in the output
#
# If R_HAYDN_32 resolution breaks, ld.lld will error or the .long field
# will contain zero instead of the resolved address.

# CHECK: Hex dump of section '.text':
# CHECK-NEXT: 0x

# SYMS: Name: _start
# SYMS: Name: target_func

    .globl _start
    .type _start, @function
_start:
    ADD32 R0, R1, R2
    # Forward reference produces R_HAYDN_32 relocation
    .long target_func
    ADD32 R3, R4, R5
    .size _start, .-_start

    .globl target_func
    .type target_func, @function
target_func:
    MOVE32 R6, R7
    .size target_func, .-target_func
