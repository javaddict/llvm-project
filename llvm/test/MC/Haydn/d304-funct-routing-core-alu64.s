# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj < %s | \
# RUN:   llvm-objdump -d - | FileCheck %s
#
# REGRESSION TEST: — funct(10) routing for DR/SIMD ALU64 ops (Mode-0 s1/s2).
#
# Bug: the 346 DR/SIMD ALU64 ops (FmtALU64/FmtALU64Acc) have no flat opcode
# assignment in getEncOpcodeMap. Before they emitted as legacy 32-bit
# parcels and disassembled as <unknown> / wrong mnemonic. encoding_manual.md
# (§6.6.2) allocates them via funct(10) routing keyed on the.td FUNCT
# field — s1 opcode = 0x400 + funct (marker bits[11:10]=01); s2 opcode =
# 0x040 + funct. The funct is the FmtALU64<0x67, FUNCT,...> 2nd template
# parameter (zero.td changes).
#
# Test design: assemble each pure-R-type Core ALU64 op (§6.6.2) into a Mode-0
# bundle and verify objdump prints the correct mnemonic. If funct-routing
# regresses, the decoder returns 0 → <unknown>; if the encoder regresses
# the bytes won't match and the mnemonic will be wrong.
#
# PoC scope: Core ALU64 family — 7 pure-DR64 R-type ops exercised here
# (MAX64/MIN64/SUB64S/SUB64S_H/SUB64S_L/SUB64_H/SUB64_L). The 4 shift-by-GPR
# ops (SLL64/SRA64/SRA64R/SRL64, funct 0x10C/0x135/0x136/0x137) take
# GPR32:$rs2 but the s1/s2 ALU64 slot has no GPR32 field; their funct routing
# is wired but operand reconstruction for the mixed GPR+DR case is a
# follow-up (the funct table includes them so the encoder marker is correct).

# CHECK: max64
max64 d0, d1, d2

# CHECK: min64
min64 d0, d1, d2

# CHECK: sub64s
sub64s d0, d1, d2

# CHECK: sub64s_h
sub64s_h d0, d1, d2

# CHECK: sub64s_l
sub64s_l d0, d1, d2

# CHECK: sub64_h
sub64_h d0, d1, d2

# CHECK: sub64_l
sub64_l d0, d1, d2
