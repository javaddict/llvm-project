# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t --defsym=ext_csr=0x40 \
# RUN:   --section-start=.text=0x10000
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t \
# RUN:   | FileCheck --check-prefix=ELF %s

# REGRESSION TEST: D1.17 — CSR/ZERO_GPR mixed parcels must patch the CSR
# window, never the zero_gpr imm8 hole.
#
# Bug: resolveFieldLsb matched CSR_UImm8 on (map,type) only, and I8 hosts
# CSRR(4)/CSRW(5) ALONGSIDE ZERO_GPR(1)/ZERO_DR(2)/ZERO_SFR(3). In
# `{ nop; csrr r2, ext; zero_gpr r1 }` (csrr@e1, zero_gpr@e0) the sniff
# matched the e0 ZERO_GPR member (IsI8Alu0(6)) and returned 23, so the CSR
# address was written into zero_gpr's e0 window bits[30:23] and the real
# CSRR window bits[61:54] stayed 0.
#
# Fix: producer emission routes through resolveFieldLsbForMember and emits
# entry-qualified kinds. Rows pinned below (verified committed parcels):
#   csrr@e1      (parcel 1, zero_gpr@e0) → R_HAYDN_CSR_UImm8_E3E1 (54)
#   csrr@e0 ALU2 (parcel 2, xor32@e1)    → R_HAYDN_CSR_UImm8_E3E0_ALU2 (27)
#   csrr@e0 ALU2 (parcel 3, beqz@e1)     → R_HAYDN_CSR_UImm8_E3E0_ALU2 (27)
# Base R_HAYDN_CSR_UImm8 stays the E2-e0-default-window kind only
# (e.g. `{ csrr; nop; nop }` E2 parcels keep the base row — pinned in
# d15-dual-csr-uimm8-same-offset-fail.s UNIQUE).
#
# ext_csr is an undefined external resolved by --defsym=ext_csr=0x40 at
# link (uimm8 must stay in [0,255]).

# RELOCS:      Relocations [
# RELOCS:        0x0 R_HAYDN_CSR_UImm8_E3E1 ext_csr 0x0
# RELOCS:        0xC R_HAYDN_CSR_UImm8_E3E0_ALU2 ext_csr 0x0
# RELOCS:        0x18 R_HAYDN_CSR_UImm8_E3E0_ALU2 ext_csr 0x0
# RELOCS:      ]

# ELF: <_start>:
# The CSRR window gets 0x40 = 64; the zero_gpr e0 hole stays zero
# (pre-fix the address landed there).
# ELF: csrr{{.*}} r2, 64
# ELF: csrr{{.*}} r4, 64
# ELF: csrr{{.*}} r5, 64

    .section .text
    .globl _start
    .type _start, @function
_start:
    # The D1.17 defect parcel: csrr@e1 + zero_gpr@e0. Pre-fix returned 23.
    { nop; csrr r2, ext_csr; zero_gpr r1 }
    # csrr committed at e2 (xor32 takes e1) — typed window 85.
    { nop; xor32 r3, r3, r3; csrr r4, ext_csr }
    # csrr@e2 next to beqz@e1.
    { nop; beqz r1, 3f; csrr r5, ext_csr }
3:
    .size _start, .-_start
