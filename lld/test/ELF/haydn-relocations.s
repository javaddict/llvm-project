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
#   R_HAYDN_HI12 / R_HAYDN_LO20 - address materialization pair (LUI+ADDI32)
#   R_HAYDN_WIDE_BranchSImm12_RI - conditional branch (Bundle128, PC-rel)
#   R_HAYDN_CallSImm20     - JAL call (20-bit, halfword-aligned, PC-rel)
#
# Uses --section-start=.text=0x10000 for deterministic addresses.
# .globl on branch/call targets forces the assembler to emit relocations
# (otherwise short forward branches are resolved at assembly time).

# ---------------------------------------------------------------------------
# Verify the assembler emits the expected relocation types.
# ---------------------------------------------------------------------------
# RELOCS-DAG: R_HAYDN_CallSImm20 callee
# RELOCS-DAG: R_HAYDN_WIDE_BranchSImm12_RI branch_target
# RELOCS-DAG: R_HAYDN_HI12 target_data
# RELOCS-DAG: R_HAYDN_LO20 target_data
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
    # Each instruction is emitted as a 16-byte Bundle128, so callee (the 9th
    # bundle from _start) lands at 0x10080: offset = 0x80 = 128 bytes ahead.
    # CHECK: <_start>:
    # CHECK: 10000: {{.*}} jal lr,
    jal lr, callee

    # Padding (3 bundles). ADD32 R0,R0,R0 disassembles as add32 r0, r0, r0.
    # CHECK: 10010: {{.*}} add32 r0, r0, r0
    ADD32 R0, R0, R0
    # CHECK: 10020: {{.*}} add32 r1, r1, r1
    ADD32 R1, R1, R1
    # CHECK: 10030: {{.*}} add32 r2, r2, r2
    ADD32 R2, R2, R2

    # ---------------------------------------------------------------------------
    # Section 2: R_HAYDN_BranchSImm16 — conditional branch relocation
    # 16-bit signed offset, word-aligned: offset>>2 in bits [15:0].
    # ---------------------------------------------------------------------------

    # BEQ forward to branch_target. The .globl on branch_target forces a
    # relocation. branch_target is 2 bundles (32 bytes) ahead.
    # CHECK: 10040: {{.*}} beq r4, r5,
    BEQ R4, R5, branch_target

    # CHECK: 10050: {{.*}} add32 r6, r6, r6
    ADD32 R6, R6, R6

    .globl branch_target
branch_target:
    # ---------------------------------------------------------------------------
    # Section 3: R_HAYDN_HI20 / R_HAYDN_LO16 — address materialization pair
    # HI20: (addr + 0x8000) >> 16, stored in bits [15:0] of LUI.
    # LO16: addr & 0xFFFF, stored in bits [15:0] of ADDI32.
    #
    # target_data is in .rodata. The linker places it after .text in a separate
    # segment. With .text at 0x10000 (9 Bundle128s = size 0x90), .rodata is
    # placed at 0x11090 (= 69776). The Bundle128 addi32 immediate is wide enough
    # to hold the full low value, so the HI20 part resolves to 0.
    # ---------------------------------------------------------------------------

    # CHECK: <branch_target>:
    # CHECK: 10060: {{.*}} lui r1, 0
    lui R1, target_data

    # CHECK: 10070: {{.*}} addi32 r1, r1, 69776
    addi32 R1, R1, target_data

    .size _start, .-_start

# ---------------------------------------------------------------------------
# Callee function — target for JAL (R_HAYDN_CallSImm20) test
# ---------------------------------------------------------------------------

    .globl callee
    .type callee, @function
callee:
    # CHECK: <callee>:
    # CHECK: 10080: {{.*}} add32 r10, r10, r10
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
    # RODATA-NEXT: 11090 00000100
    .long _start
    .size target_data, .-target_data

# ---------------------------------------------------------------------------
# Section 5: R_HAYDN_32_PCREL — PC-relative 32-bit data relocation (in .data)
# .long (_start - .) produces R_HAYDN_32_PCREL. After linking, contains
# (S + A - P) = (0x10000 + 0 - 0x12094) = -0x2094 = 0xFFFFDF6C.
# Verifier: llvm-objdump -s --section=.data shows little-endian bytes.
# ---------------------------------------------------------------------------

    .section .data
    .globl pcrel_data
    .type pcrel_data, @object
    .p2align 2
pcrel_data:
    # DATA: Contents of section .data:
    # DATA-NEXT: 12094 6cdfffff
    .long _start - .
    .size pcrel_data, .-pcrel_data
