# REQUIRES: haydn-registered-target
// CHECK: 	.text
// CHECK: 	.globl	_start
// CHECK: 	.type	_start,@function
// CHECK: _start:
// CHECK: 	{ 	lui	r1, glob_val }          // encoding: [0x07,0x0a,0x12,0x00,0x00,0bAAAA0000,A,0x00,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 5, value: glob_val, kind: FIXUP_HAYDN_HI12
// CHECK: 	{ 	addi32	r1, r1, glob_val }      // encoding: [0x07,0x0f,0x12,0x01,0x00,0bAAAAAA00,A,0b00AAAAAA,0x00,0x00,0x00,0x00]
// CHECK:                                         //   fixup A - offset: 3, value: glob_val, kind: FIXUP_HAYDN_LO20
// CHECK: .Ltmp0:
// CHECK: 	.size	_start, .Ltmp0-_start
// CHECK: 	.section	.rodata,"a",@progbits
// CHECK: 	.globl	glob_val
// CHECK: 	.type	glob_val,@object
// CHECK: glob_val:
// CHECK: 	.long	305419896
// CHECK: .Ltmp1:
// CHECK: 	.size	glob_val, .Ltmp1-glob_val
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck --check-prefix=ASM %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0x11000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=ELF %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s

# Role: object — lld must patch ONLY the imm field of HI12/LO20 relocations, preserving the lui/addi32 OPCODE field.

# REGRESSION TEST : lld must patch ONLY the imm field of HI12/LO20
# relocations, preserving the lui/addi32 OPCODE field.
#
# Historical bug: lld corrupted R_HAYDN_HI20 / R_HAYDN_LO16 (legacy 32-bit
# parcel geometry) by writing the resolved immediate at the wrong bit offset
# overwriting the lui/addi32 OPCODE. That destroyed every
# lui r1, %hi(sym) + addi32 r1, r1, %lo(sym)
# global-address materialization in direct-ELF programs.
#
# Format E ISA (/): LUI is HI12, ADDI32 RI20 is LO20 — not the
# retired HI20/LO16 parcel pair. A single-source reloc table
# (HaydnRelocLayout.h) drives both MC (applyFixup) and lld (relocate +
# getImplicitAddend). Imm-only geometric patch; opcodes must survive.
#
# Address math (--section-start.text=0x10000.rodata=0x11000):
# glob_val = 0x11000
# HI12 high field + LO20 low field reconstruct the VA.
# Linked pair must still disassemble as lui + addi32 (not <unknown>).

    .section .text
    .globl _start
    .type _start, @function
_start:
# ASM: { lui{{.*}}r1,
# ASM: addi32{{.*}}r1, r1,
    lui    R1, glob_val
    addi32 R1, R1, glob_val
    .size _start, .-_start

    .section .rodata
    .globl glob_val
    .type glob_val, @object
glob_val:
    .long 0x12345678
    .size glob_val, .-glob_val

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_HI12 glob_val 0x0
# RELOCS-NEXT:     0xC R_HAYDN_LO20 glob_val 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

# ELF: <_start>:
# ELF: {{.*}} lui{{.*}}r1,
# ELF: {{.*}} addi32{{.*}}r1, r1,
