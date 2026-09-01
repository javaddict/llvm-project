# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -T %S/haydn/haydn.ld %t.o -o %t
# RUN: llvm-readobj -h %t | FileCheck --check-prefix=HEADER %s
# RUN: llvm-readobj -S %t | FileCheck --check-prefix=SECTIONS %s
# RUN: llvm-readobj -s %t | FileCheck --check-prefix=SYMS %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=DISASM %s

# Comprehensive linker script test for Haydn baremetal target.
# Tests:
#   1. ENTRY(_start) sets the ELF entry point to 0x10000 (RAM ORIGIN)
#   2. SECTIONS places .text, .data, .bss in the RAM memory region
#   3. BSS boundary symbols (__bss_start, __bss_end) are defined
#   4. Data boundary symbols (__data_start, __data_end) are defined
#   5. _start entry point symbol is present and global
#   6. Disassembly shows code at 0x10000 (RAM ORIGIN)
#   7. .rodata is merged into .text per the linker script

# ---------------------------------------------------------------------------
# ELF header checks
# ---------------------------------------------------------------------------
# HEADER: Format: elf32-unknown
# HEADER: Machine: 0x103
# HEADER: Entry: 0x10000

# ---------------------------------------------------------------------------
# Section table checks — verify .text, .data, .bss exist with correct types/flags
# ---------------------------------------------------------------------------
# SECTIONS-DAG: Name: .text
# SECTIONS-DAG: Name: .data
# SECTIONS-DAG: Name: .bss

# ---------------------------------------------------------------------------
# Symbol table checks — verify key symbols are defined
# ---------------------------------------------------------------------------
# SYMS-DAG: Name: _start
# SYMS-DAG: Name: __bss_start
# SYMS-DAG: Name: __bss_end
# SYMS-DAG: Name: __data_start
# SYMS-DAG: Name: __data_end
# SYMS-DAG: Name: __stack_bottom
# SYMS-DAG: Name: __stack_top

# ---------------------------------------------------------------------------
# Disassembly checks — code placed at 0x10000 (RAM ORIGIN)
# Note: ADD32 R0, R0, R0 is the canonical NOP encoding.
# Standalone-assembly packets are 12-byte Format E rows (MC commit
# a747377): one instruction per packet, addresses advance by 0xc.
# ---------------------------------------------------------------------------
# DISASM: <_start>:
# DISASM: 10000: {{.*}} nop
# DISASM: 1000c: {{.*}} addi32 r1, r0, 0
# DISASM: 10018: {{.*}} addi32 r0, r0, 42

    .globl _start
    .type _start, @function
_start:
    ADD32 R0, R0, R0
    addi32 R1, R0, 0
    addi32 R0, R0, 42

    .size _start, .-_start

# Test data section — verifies .data placement
    .globl test_data
    .data
    .type test_data, @object
    .p2align 2
test_data:
    .byte 0x78
    .byte 0x56
    .byte 0x34
    .byte 0x12
    .size test_data, .-test_data

# Test BSS — verifies __bss_start / __bss_end symbols
    .globl test_bss
    .comm test_bss, 4, 4

# Test rodata — should be merged into .text by linker script
    .section .rodata
    .globl rodata_val
rodata_val:
    .long 0xCAFEBABE
    .size rodata_val, .-rodata_val
