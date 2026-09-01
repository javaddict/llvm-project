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
#   R_HAYDN_WIDE_BranchSImm12_RI - conditional branch (12-bit, byte, PC-rel)
#   R_HAYDN_WIDE_CallSImm20 - JAL call (20-bit, byte, PC-rel)
#
# Entry-qualified reloc kinds (MC commit a747377): wide packet members carry
# the WIDE_ prefix and LUI/ADDI32 split is HI12 (addr>>20) + LO20 (low 20).
# Uses --section-start=.text=0x10000 for deterministic addresses.
# .globl on branch/call targets forces the assembler to emit relocations
# (otherwise short forward branches are resolved at assembly time).

# ---------------------------------------------------------------------------
# Verify the assembler emits the expected relocation types.
# ---------------------------------------------------------------------------
# RELOCS-DAG: R_HAYDN_WIDE_CallSImm20 callee
# RELOCS-DAG: R_HAYDN_WIDE_BranchSImm12_RI branch_target
# RELOCS-DAG: R_HAYDN_HI12 target_data
# RELOCS-DAG: R_HAYDN_LO20 target_data
# RELOCS-DAG: R_HAYDN_32 _start
# RELOCS-DAG: R_HAYDN_32_PCREL _start

# ---------------------------------------------------------------------------
# Standalone-assembly packets are 12-byte Format E rows (one instruction
# per packet; MC commit a747377). Addresses advance by 0xc per packet, not
# 4. target placement below is computed from the real 12-byte strides.
# ---------------------------------------------------------------------------

    .section .text
    .globl _start
    .type _start, @function
_start:
    # JAL lr (R15) to callee. The .globl on callee forces a relocation.
    # _start spans 6 packets (0x10000..0x10053); callee is at 0x10060.
    # Byte offset = 0x60 = 96 (branch/call offsets are unscaled byte PC+imm).
    # CHECK: <_start>:
    # CHECK: 10000: {{.*}} jal lr, 96
    jal lr, callee

    # CHECK: 1000c: {{.*}} add32 r0, r0, r0
    ADD32 R0, R0, R0
    # CHECK: 10018: {{.*}} add32 r1, r1, r1
    ADD32 R1, R1, R1
    # CHECK: 10024: {{.*}} add32 r2, r2, r2
    ADD32 R2, R2, R2

    # BEQ forward to branch_target. The .globl on branch_target forces a
    # relocation. branch_target is at 0x10048; this packet is at 0x10030.
    # Byte offset = 0x18 = 24.
    # CHECK: 10030: {{.*}} beq r4, r5, 24
    BEQ R4, R5, branch_target

    # CHECK: 1003c: {{.*}} add32 r6, r6, r6
    ADD32 R6, R6, R6

    .globl branch_target
branch_target:
    # ---------------------------------------------------------------------------
    # Section 3: R_HAYDN_HI20 / R_HAYDN_LO16 — address materialization pair
    # HI20: (addr + 0x8000) >> 16, stored in bits [15:0] of LUI.
    # LO16: addr & 0xFFFF, stored in bits [15:0] of ADDI32.
    #
    # .text is 7 packets = 0x54 bytes; .rodata lands at 0x1106c.
    # Entry-qualified HI20/LO16 split (MC commit a747377): the ADDI32
    # carries the low 20 bits and LUI carries addr >> 20.
    # HI20: 0x1106c >> 20 = 0
    # LO16 field: 0x1106c & 0xFFFFF = 0x1106c = 69740
    # Pair materializes exactly target_data: (0 << 16) + 69740 = 0x1106c.
    # ---------------------------------------------------------------------------

    # CHECK: <branch_target>:
    # CHECK: 10048: {{.*}} lui r1, 0
    lui R1, target_data

    # CHECK: 10054: {{.*}} addi32 r1, r1, 69740
    addi32 R1, R1, target_data

    .size _start, .-_start

# ---------------------------------------------------------------------------
# Callee function — target for JAL (R_HAYDN_CallSImm20) test
# ---------------------------------------------------------------------------

    .globl callee
    .type callee, @function
callee:
    # CHECK: <callee>:
    # CHECK: 10060: {{.*}} add32 r10, r10, r10
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
    # RODATA-NEXT: 1106c 00000100
    .long _start
    .size target_data, .-target_data

# ---------------------------------------------------------------------------
# Section 5: R_HAYDN_32_PCREL — PC-relative 32-bit data relocation (in .data)
# .long (_start - .) produces R_HAYDN_32_PCREL. After linking, contains
# (S + A - P) = (0x10000 + 0 - 0x12070) = -0x2070 = 0xFFFFDF90 (signed: -8336).
# Verifier: llvm-objdump -s --section=.data shows little-endian bytes.
# ---------------------------------------------------------------------------

    .section .data
    .globl pcrel_data
    .type pcrel_data, @object
    .p2align 2
pcrel_data:
    # DATA: Contents of section .data:
    # DATA-NEXT: 12070 90dfffff
    .long _start - .
    .size pcrel_data, .-pcrel_data
