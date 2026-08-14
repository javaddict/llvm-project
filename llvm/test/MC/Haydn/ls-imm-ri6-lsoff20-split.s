# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s 2>&1 | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=BYTES
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOCS
# REQUIRES: haydn-registered-target
#
# Role: object — T-MC2 / W18 LS_IMM silent-miscompile resolution.
#
# REGRESSION TEST: split narrow Format E LS RI6 vs wide LSOff20/LO20.
#
# Bug: getExprFixupKind mapped LD32/ST32/LD64/ST64 (and Format E placed
# S_LW_WITH_IMM members) to FIXUP_HAYDN_LO20, which patches 20 bits at
# parcel bits[31:50] — the ALU RI20 / retired WIDE LSOff20 field. The
# product LS form is LOADSTORE0/LOAD1 RI6: signed imm6 at bits[33:28].
# HaydnELFObjectWriter then masqueraded FIXUP_HAYDN_LS_IMM as
# R_HAYDN_SImm16 (16-bit field at bit 0) "so objects assemble".
#
# Fix: RI6 LS → FIXUP_HAYDN_LS_IMM / R_HAYDN_LS_IMM (AIE 1:1 fixup→ELF,
# AIEELFObjectWriter.cpp:60-63; RISCV splits mem-imm by format:
# RISCVMCCodeEmitter.cpp:640-643, RISCVELFObjectWriter.cpp:134-140).
# ALU RI20 and simm20_ls EncoderMethod keep LO20. No SImm16 alias.
#
# If this regresses to LO20/SImm16, RELOCS fail. If the 6-bit vs 20-bit
# fields swap, BYTES fail. Do not invent GE96-03 branch scale.

#--- Narrow RI6 constant encode (imm6 @ bits[33:28], FieldLsb=28) ---
# ASM: { ld32{{.*}}r0, r1, 0 }{{.*}}encoding: [0x87,0x43,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# BYTES: {{.*}}0: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32{{.*}}r0, r1, 0
ld32 r0, r1, 0

# ASM: { ld32{{.*}}r2, r3, 4 }{{.*}}encoding: [0x87,0x43,0x23,0x43,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# BYTES: {{.*}}c: 87 43 23 43 00 00 00 00 00 00 00 00{{.*}}ld32{{.*}}r2, r3, 4
ld32 r2, r3, 4

# ASM: { ld32{{.*}}r4, r5, -4 }{{.*}}encoding: [0x87,0x43,0x43,0xc5,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# BYTES: {{.*}}18: 87 43 43 c5 03 00 00 00 00 00 00 00{{.*}}ld32{{.*}}r4, r5, -4
ld32 r4, r5, -4

#--- Wide LSOff20 geometry: ALU RI20 / LO20 (imm20 @ bits[31:50]) ---
# Must stay LO20 — the over-broad "all LS → LS_IMM" path broke this.
# ASM: { addi32{{.*}}r3, r4, 0 }{{.*}}encoding: [0x07,0x0f,0x32,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# BYTES: {{.*}}24: 07 0f 32 04 00 00 00 00 00 00 00 00{{.*}}addi32{{.*}}r3, r4, 0
addi32 r3, r4, 0

# ASM: { addi32{{.*}}r5, r6, 32767 }{{.*}}encoding: [0x07,0x0f,0x52,0x86,0xff,0x3f,0x00,0x00,0x00,0x00,0x00,0x00]
# BYTES: {{.*}}30: 07 0f 52 86 ff 3f 00 00 00 00 00 00{{.*}}addi32{{.*}}r5, r6, 32767
addi32 r5, r6, 32767

#--- Symbolic narrow LS: R_HAYDN_LS_IMM, never SImm16 / LO20 ---
# ASM: { ld32{{.*}}r1, r2, nearby }
# ASM: fixup A - offset: 0, value: nearby, kind: FIXUP_HAYDN_LS_IMM
ld32 r1, r2, nearby

#--- Symbolic wide: still R_HAYDN_LO20 ---
# ASM: { addi32{{.*}}r7, r8, wide_sym }
# ASM: fixup A - offset: 0, value: wide_sym, kind: FIXUP_HAYDN_LO20
addi32 r7, r8, wide_sym

# RELOCS: Relocations [
# RELOCS: .rela.text
# RELOCS-DAG: R_HAYDN_LS_IMM nearby
# RELOCS-DAG: R_HAYDN_LO20 wide_sym
# RELOCS-NOT: R_HAYDN_SImm16
# RELOCS: ]
