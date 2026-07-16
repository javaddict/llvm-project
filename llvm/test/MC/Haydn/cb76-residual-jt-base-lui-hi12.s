# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=ELF %s

# REGRESSION TEST (-residual): the s0 ALU32 _M0S0ALU slot-OR variants
# (LUI_M0S0ALU, ADDI32_M0S0ALU,...) MUST emit the same fixup kind as their
# generic parents. The original getExprFixupKind switch matched only the
# GENERIC opcodes (Haydn::LUI, Haydn::ADDI32,...), but emitMode0S0Bundle
# rewrites the MCInst opcode to the _M0S0ALU variant BEFORE calling
# getBinaryCodeForInstr, which calls getMachineOpValue -> getExprFixupKind on
# any expression operand. The _M0S0ALU opcodes fell through to the default
# FIXUP_HAYDN_32, so the pre-link.o carried R_HAYDN_32 (not R_HAYDN_HI12) on
# a JT-base `lui rN, %hi12(.LJTI*)`. lld then wrote the full 32-bit symbol
# address into the 4-byte LoWord of the 8-byte Mode-0 LUI bundle, clobbering
# the opcode/rd/rs bytes (e.g. `03 06 56 00` -> `00 00 08 00` for a symbol at
# 0x80000). The disassembler printed <unknown> and the simulator read a wild
# opcode -> switch/JT repros jumped wild (cb1_mod: host 2 /.elf -471141795;
# cb22_switch_jt: host 35 /.elf 0).
#
# Fix: getExprFixupKind now matches the _M0S0ALU variants too, so a JT-base
# lui carries R_HAYDN_HI12 and patches ONLY the LUI imm12 field
# (HaydnRelocLayout HI12 row {NBytes=4, FieldSize=12, FieldLsb=4} per
# Bundle128 LUI_S0_FLEX / HaydnFU_ALU32_S0_I12 bits[15:4]). The opcode bytes
# survive. (Pre- the row still said FieldSize=5 from Mode-0 uimm5.)
#
# Test design: a `lui r3, sym` + `addi32{{(_w)?}} r3, r3, sym` pair, where sym is a
# runtime jump-table base (a.rodata label) -- the canonical address-mat pattern
# that emitMode0S0Bundle routes through the _M0S0ALU rewrite. The RELOCS check
# pins that the lui carries R_HAYDN_HI12 (NOT R_HAYDN_32) after the MC emit;
# the ELF check pins that the lui opcode bytes survive linking (only the
# imm12 field at bits[15:4] is patched).

#===----------------------------------------------------------------------===
# Pre-link: MC must emit R_HAYDN_HI12 on the lui (the -residual regression
# invariant). A regression to R_HAYDN_32 means the _M0S0ALU fixup fell through
# to the default again.
#===----------------------------------------------------------------------===

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_HI12 jt_table 0x0
# RELOCS-NEXT:     0x10 R_HAYDN_LO20 jt_table 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

#===----------------------------------------------------------------------===
# Post-link: only the imm12 field (bits[15:4]) is patched. The opcode/rt
# bytes survive so the disassembly still reads `lui r3,...`. If -residual
# regresses (wrong reloc kind R_HAYDN_32), the opcode bytes are clobbered ->
# objdump prints <unknown> or a wrong mnemonic.
#
# Address math (--section-start.text=0x10000,.rodata begins after the 22-byte
# text: 16-byte lui Bundle128 + 6-byte addi32{{(_w)?}} WIDE = 22 bytes, padded to
# the next.rodata alignment):
# jt_table address = 0x11016.
# HI12 = (0x11016 + 0x80000) >> 20 = 0x91016 >> 20 = 0 (address < 1 MB)
# LO20 = 0x11016 - 0 = 0x11016 (= 69664)
# So the linked pair is `lui r3, 0` + `addi32{{(_w)?}} r3, r3, 69664`.
#===----------------------------------------------------------------------===

# ELF: <_start>:
# ELF: 10000: {{.*}} lui{{.*}} r3,
# ELF: 10010: {{.*}} addi32{{(_w)?}}{{.*}} r3, r3,

    .section .text
    .globl _start
    .type _start, @function
_start:
    lui    R3, jt_table
    addi32_w R3, R3, jt_table
    .size _start, .-_start

    .section .rodata
    .globl jt_table
    .type jt_table, @object
jt_table:
    .long 0x12345678
    .size jt_table, .-jt_table
