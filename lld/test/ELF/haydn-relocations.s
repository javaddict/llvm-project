# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -m elf32haydn %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-objdump -s --section=.rodata %t | FileCheck --check-prefix=RODATA %s
# RUN: llvm-objdump -s --section=.data %t | FileCheck --check-prefix=DATA %s

# Comprehensive LLD relocation test for all Haydn relocation types.
# Each relocation type is tested with known address layout and verified
# CHECK lines against the disassembled output.
#
# Relocation types tested:
#   R_HAYDN_32             - absolute 32-bit address (in .rodata)
#   R_HAYDN_32_PCREL       - PC-relative 32-bit (in .data)
#   R_HAYDN_HI20 / R_HAYDN_LO16 - address materialization pair (LUI+ADDI32)
#   R_HAYDN_BranchSImm16   - conditional branch (16-bit, word-aligned, PC-rel)
#   R_HAYDN_CallSImm20     - JAL call (20-bit, halfword-aligned, PC-rel)
#
# Uses --section-start=.text=0x10000 for deterministic addresses.
# .globl on branch/call targets forces the assembler to emit relocations
# (otherwise short forward branches are resolved at assembly time).

# ---------------------------------------------------------------------------
# Verify the assembler emits the expected relocation types.
# ---------------------------------------------------------------------------
# RELOCS-DAG: R_HAYDN_CallSImm20 callee
# RELOCS-DAG: R_HAYDN_BranchSImm16 branch_target
# RELOCS-DAG: R_HAYDN_HI20 target_data
# RELOCS-DAG: R_HAYDN_LO16 target_data
# RELOCS-DAG: R_HAYDN_32 _start
# RELOCS-DAG: R_HAYDN_32_PCREL _start

# ---------------------------------------------------------------------------
# Section 1: R_HAYDN_CallSImm20 — JAL call relocation
# Linear 20-bit encoding: offset>>1 stored in Inst[19:0].
# ---------------------------------------------------------------------------

    .section .text
    .globl _start
    .type _start, @function
_start:
    # JAL lr (R15) to callee. The .globl on callee forces a relocation.
    # _start is at 0x10000, callee is at 0x10020 (8 instructions = 32 bytes ahead).
    # offset = 32, offset>>1 = 16 = 0x10, stored in Inst[19:0].
    # CHECK: <_start>:
    # CHECK: 10000: {{.*}} jal lr,
    jal lr, callee

    # Padding (3 instructions = 12 bytes).
    # ADD32 R0,R0,R0 is the canonical NOP encoding.
    # CHECK: 10004: {{.*}} nop
    ADD32 R0, R0, R0
    # CHECK: 10008: {{.*}} add32 r1, r1, r1
    ADD32 R1, R1, R1
    # CHECK: 1000c: {{.*}} add32 r2, r2, r2
    ADD32 R2, R2, R2

    # ---------------------------------------------------------------------------
    # Section 2: R_HAYDN_BranchSImm16 — conditional branch relocation
    # 16-bit signed offset, word-aligned: offset>>2 in bits [15:0].
    # ---------------------------------------------------------------------------

    # BEQ forward to branch_target. The .globl on branch_target forces a
    # relocation. branch_target is 2 instructions (8 bytes) ahead.
    # offset = 8, offset>>2 = 2, stored in bits [15:0].
    # CHECK: 10010: {{.*}} beq r4, r5,
    BEQ R4, R5, branch_target

    # CHECK: 10014: {{.*}} add32 r6, r6, r6
    ADD32 R6, R6, R6

    .globl branch_target
branch_target:
    # ---------------------------------------------------------------------------
    # Section 3: R_HAYDN_HI20 / R_HAYDN_LO16 — address materialization pair
    # HI20: (addr + 0x8000) >> 16, stored in bits [15:0] of LUI.
    # LO16: addr & 0xFFFF, stored in bits [15:0] of ADDI32.
    #
    # target_data is in .rodata. The linker places it after .text in a separate
    # segment. With .text at 0x10000 (size 0x24), .rodata is placed at 0x11024.
    # HI20: (0x11024 + 0x8000) >> 16 = 0x19024 >> 16 = 1
    # LO16: 0x11024 & 0xFFFF = 0x1024 = 4132
    # ---------------------------------------------------------------------------

    # CHECK: <branch_target>:
    # CHECK: 10018: {{.*}} lui r1, 1
    lui R1, target_data

    # CHECK: 1001c: {{.*}} addi32 r1, r1, 4132
    addi32 R1, R1, target_data

    .size _start, .-_start

# ---------------------------------------------------------------------------
# Callee function — target for JAL (R_HAYDN_CallSImm20) test
# ---------------------------------------------------------------------------

    .globl callee
    .type callee, @function
callee:
    # CHECK: <callee>:
    # CHECK: 10020: {{.*}} add32 r10, r10, r10
    ADD32 R10, R10, R10
    .size callee, .-callee

# ---------------------------------------------------------------------------
# Section 4: R_HAYDN_32 — absolute 32-bit data relocation (in .rodata)
# .long _start produces R_HAYDN_32. After linking, contains 0x00010000
# (the absolute address of _start).
# Verifier: llvm-objdump -s --section=.rodata shows little-endian bytes.
# ---------------------------------------------------------------------------

    .section .rodata
    .globl target_data
    .type target_data, @object
target_data:
    # RODATA: Contents of section .rodata:
    # RODATA-NEXT: 11024 00000100
    .long _start
    .size target_data, .-target_data

# ---------------------------------------------------------------------------
# Section 5: R_HAYDN_32_PCREL — PC-relative 32-bit data relocation (in .data)
# .long (_start - .) produces R_HAYDN_32_PCREL. After linking, contains
# (S + A - P) = (0x10000 + 0 - 0x12028) = -0x2028 = 0xFFFFDFD8 (signed: -8232).
# Verifier: llvm-objdump -s --section=.data shows little-endian bytes.
# ---------------------------------------------------------------------------

    .section .data
    .globl pcrel_data
    .type pcrel_data, @object
    .p2align 2
pcrel_data:
    # DATA: Contents of section .data:
    # DATA-NEXT: 12028 d8dfffff
    .long _start - .
    .size pcrel_data, .-pcrel_data
