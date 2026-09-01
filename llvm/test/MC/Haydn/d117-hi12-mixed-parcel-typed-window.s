# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0xa2004
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=ELF %s

# REGRESSION TEST: D1.17 — HI12/branch mixed parcels must patch the LUI
# window, never the branch imm12.
#
# Bug: resolveFieldLsb matched HI12 E3 e1 ALU0 on (map,type) only, and I12
# there hosts LUI(1) ALONGSIDE BEQZ(4)/BNEZ(5)/BGEZ(6)/BLTZ(7). In
# `{ nop; beqz; lui }` the committed parcel is beqz@e1 ALU0 (opc=4) +
# lui@e0 ALU2 — the sniff matched the e1 BEQZ member and returned 54, so
# both MC applyFixup and lld wrote hi12 bits into the branch imm12 window
# bits[65:54] and left the real LUI window (bits[32:21]) zero.
#
# Fix: producer emission routes through resolveFieldLsbForMember with the
# committed (Mode, EntryIdx, Unit) and emits entry-qualified kinds. Rows
# pinned below (verified against the committed parcels):
#   lui@e0 ALU2 (parcel 1, beqz@e1 ALU0 opc=4) → R_HAYDN_HI12_E3E0_ALU2 (21)
#   lui@e0 ALU2 (parcel 2, beqz@e1 again)      → R_HAYDN_HI12_E3E0_ALU2 (21)
#   lui@e1      (parcel 3, csrr@e0 ALU2)       → R_HAYDN_HI12_E3E1 (54)
# Base R_HAYDN_HI12 stays the E2-e0-default-window kind only.
# high_sym @ 0xa2004 → HI12 = (0xa2004 + 0x80000) >> 20 = 1.

# RELOCS:      Relocations [
# RELOCS:        0x0 R_HAYDN_HI12_E3E0_ALU2 high_sym 0x0
# RELOCS:        0xC R_HAYDN_HI12_E3E0_ALU2 high_sym 0x0
# RELOCS:        0x18 R_HAYDN_HI12_E3E1 high_sym 0x0
# RELOCS:      ]

# ELF: <_start>:
# Branch immediate (12 bytes → target 2:) must survive the neighboring
# HI12 patches; pre-fix the hi12 bits landed in the beqz imm12 window.
# ELF-NOT: beqz{{.*}} 2048
# ELF: lui{{.*}} r9, 1
# ELF: lui{{.*}} r5, 1
# ELF: lui{{.*}} r9, 1

    .section .text
    .globl _start
    .type _start, @function
_start:
    # The D1.17 defect parcel: beqz@e1 ALU0 (opc=4) + lui@e0 ALU2.
    # Pre-fix the sniff returned 54 (the BEQZ member matched map/type).
    { nop; beqz r1, 2f; lui r9, %hi12(high_sym) }
    # Same defect shape with the branch textual-first: beqz@e1 + lui@e0.
    { beqz r2, 2f; lui r5, %hi12(high_sym); nop }
    # Mixed-kind neighbor: csrr@e0 ALU2 + lui@e1 (window 54). The CSR
    # address uses a low symbol so the uimm8 range holds after link.
    { lui r9, %hi12(high_sym); csrr r1, low_csr; nop }
2:
    .size _start, .-_start

    .section .data
    .globl low_csr
low_csr = 0x40

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0
    .size high_sym, 4
