# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
# REQUIRES: haydn-registered-target
#
# MAC (Multiply-Accumulate) instruction test.
# Covers FmtALU64 multiply-family and FmtMAC ternary instructions from
# HaydnInstrInfoAuto.td. These execute on Slot 1/2 MAC datapaths.
#
# Instruction categories tested:
# 32-bit multiply: MULL, MULSSH, MULSUH, MULUUH
# 64-bit multiply: MUL64_HH, MUL64_LL, MUL64_UHH, MUL64_ULUL,...
# 64-bit multiply-accumulate: MULA64_HH, MULA64_LL,...
# 64-bit multiply-subtract: MULS64_HH, MULSS64_LL,...
# 16-bit sum-of-products: SMULA16S_00, SMULA16_33, SMULS16S_00,...
# Fractional multiply: FMUL16_HS00, FMUL32S_HH, FMULA16_HS00,...
# Complex MAC: X2CMUL32, X2MULA32, X4MULA16, X4MULS16, X4SEL16
# Accumulator shift/extract: SRA64, SRL64, SRA64R, MUL16AQ, MUL16ZAQ

#===----------------------------------------------------------------------===
# 32-bit Multiply (GPR32 result)
#===----------------------------------------------------------------------===

# MULL family is tied-destructive ("$rd = $rs2") — the asm writes
# rs2 == rd explicitly. (Pre- this used 3 distinct regs, which no longer
# parses under the tied constraint.)
# CHECK: mull r0, r1, r0
# ROUNDTRIP: mull	r0, r1, r0
mull r0, r1, r0

# CHECK: mulssh r3, r4, r3
# ROUNDTRIP: mulssh	r3, r4, r3
mulssh r3, r4, r3

# CHECK: mulsuh r6, r7, r6
# ROUNDTRIP: mulsuh	r6, r7, r6
mulsuh r6, r7, r6

# CHECK: muluuh r9, r10, r9
# ROUNDTRIP: muluuh	r9, r10, r9
muluuh r9, r10, r9

#===----------------------------------------------------------------------===
# 64-bit Multiply (DR64 result) — halfword select variants
#===----------------------------------------------------------------------===

# CHECK: mul64_hh d0, d1, d2
# ROUNDTRIP: mul64_hh	d0, d1, d2
mul64_hh d0, d1, d2

# CHECK: mul64_hl d3, d4, d5
# ROUNDTRIP: mul64_hl	d3, d4, d5
mul64_hl d3, d4, d5

# CHECK: mul64_lh d6, d7, d8
# ROUNDTRIP: mul64_lh	d6, d7, d8
mul64_lh d6, d7, d8

# CHECK: mul64_ll d9, d10, d11
# ROUNDTRIP: mul64_ll	d9, d10, d11
mul64_ll d9, d10, d11

# CHECK: mul64_uhh d12, d13, d14
# ROUNDTRIP: mul64_uhh	d12, d13, d14
mul64_uhh d12, d13, d14

# CHECK: mul64_uhl d15, d0, d1
# ROUNDTRIP: mul64_uhl	d15, d0, d1
mul64_uhl d15, d0, d1

# CHECK: mul64_ulh d2, d3, d4
# ROUNDTRIP: mul64_ulh	d2, d3, d4
mul64_ulh d2, d3, d4

# CHECK: mul64_ull d5, d6, d7
# ROUNDTRIP: mul64_ull	d5, d6, d7
mul64_ull d5, d6, d7

#===----------------------------------------------------------------------===
# 64-bit Multiply-Accumulate (MULA64)
#===----------------------------------------------------------------------===

# CHECK: mula64_hh d0, d1, d2
# ROUNDTRIP: mula64_hh	d0, d1, d2
mula64_hh d0, d1, d2

# CHECK: mula64_ll d3, d4, d5
# ROUNDTRIP: mula64_ll	d3, d4, d5
mula64_ll d3, d4, d5

# CHECK: mula64_uhh d6, d7, d8
# ROUNDTRIP: mula64_uhh	d6, d7, d8
mula64_uhh d6, d7, d8

# CHECK: mula64_ull d9, d10, d11
# ROUNDTRIP: mula64_ull	d9, d10, d11
mula64_ull d9, d10, d11

#===----------------------------------------------------------------------===
# 64-bit Multiply-Subtract (MULS64 / MULSS64)
#===----------------------------------------------------------------------===

# CHECK: muls64_hh d0, d1, d2
# ROUNDTRIP: muls64_hh	d0, d1, d2
muls64_hh d0, d1, d2

# CHECK: muls64_ll d3, d4, d5
# ROUNDTRIP: muls64_ll	d3, d4, d5
muls64_ll d3, d4, d5

# CHECK: mulss64_hh d6, d7, d8
# ROUNDTRIP: mulss64_hh	d6, d7, d8
mulss64_hh d6, d7, d8

# CHECK: mulss64_ll d9, d10, d11
# ROUNDTRIP: mulss64_ll	d9, d10, d11
mulss64_ll d9, d10, d11

#===----------------------------------------------------------------------===
# 32x32→64 Accumulator operations (MULAS64 / MULAA32 / MULSS32)
#===----------------------------------------------------------------------===

# CHECK: mulas64_hh d0, d1, d2
# ROUNDTRIP: mulas64_hh	d0, d1, d2
mulas64_hh d0, d1, d2

# CHECK: mulas32_hhll d3, d4, d5
# ROUNDTRIP: mulas32_hhll	d3, d4, d5
mulas32_hhll d3, d4, d5

# CHECK: mulaa32_hhll d6, d7, d8
# ROUNDTRIP: mulaa32_hhll	d6, d7, d8
mulaa32_hhll d6, d7, d8

# CHECK: mulss32_hhll d9, d10, d11
# ROUNDTRIP: mulss32_hhll	d9, d10, d11
mulss32_hhll d9, d10, d11

#===----------------------------------------------------------------------===
# 16-bit Sum-of-Products (SMULA / SMULS)
#===----------------------------------------------------------------------===

# CHECK: smula16s_00 d0, d1, d2
# ROUNDTRIP: smula16s_00	d0, d1, d2
smula16s_00 d0, d1, d2

# CHECK: smula16s_33 d3, d4, d5
# ROUNDTRIP: smula16s_33	d3, d4, d5
smula16s_33 d3, d4, d5

# CHECK: smula16_00 d6, d7, d8
# ROUNDTRIP: smula16_00	d6, d7, d8
smula16_00 d6, d7, d8

# CHECK: smuls16s_00 d9, d10, d11
# ROUNDTRIP: smuls16s_00	d9, d10, d11
smuls16s_00 d9, d10, d11

# CHECK: smuls16_33 d12, d13, d14
# ROUNDTRIP: smuls16_33	d12, d13, d14
smuls16_33 d12, d13, d14

#===----------------------------------------------------------------------===
# Fractional Multiply (FMUL / FMULA / FMULS)
#===----------------------------------------------------------------------===

# CHECK: fmul16_hs00 d0, d1, d2
# ROUNDTRIP: fmul16_hs00	d0, d1, d2
fmul16_hs00 d0, d1, d2

# CHECK: fmul16_ls33 d3, d4, d5
# ROUNDTRIP: fmul16_ls33	d3, d4, d5
fmul16_ls33 d3, d4, d5

# CHECK: fmul32s_hh d6, d7, d8
# ROUNDTRIP: fmul32s_hh	d6, d7, d8
fmul32s_hh d6, d7, d8

# CHECK: fmul32s_ll d9, d10, d11
# ROUNDTRIP: fmul32s_ll	d9, d10, d11
fmul32s_ll d9, d10, d11

# CHECK: fmula16_hs00 d0, d1, d2
# ROUNDTRIP: fmula16_hs00	d0, d1, d2
fmula16_hs00 d0, d1, d2

# CHECK: fmula32s_hh d3, d4, d5
# ROUNDTRIP: fmula32s_hh	d3, d4, d5
fmula32s_hh d3, d4, d5

# CHECK: fmuls16_hs00 d6, d7, d8
# ROUNDTRIP: fmuls16_hs00	d6, d7, d8
fmuls16_hs00 d6, d7, d8

# CHECK: fmuls32s_hh d9, d10, d11
# ROUNDTRIP: fmuls32s_hh	d9, d10, d11
fmuls32s_hh d9, d10, d11

#===----------------------------------------------------------------------===
# Accumulator operations (MUL16AQ / MUL16ZAQ)
#===----------------------------------------------------------------------===

# CHECK: mul16aq d0, d1, d2
# ROUNDTRIP: mul16aq	d0, d1, d2
mul16aq d0, d1, d2

# CHECK: mul16zaq d3, d4, d5
# ROUNDTRIP: mul16zaq	d3, d4, d5
mul16zaq d3, d4, d5

#===----------------------------------------------------------------------===
# Accumulator shift/extract (SRA64 / SRL64 / SRA64R)
# These produce GPR32 result from DR64 accumulator + GPR32 shift amount.
#===----------------------------------------------------------------------===

# CHECK: sra64 d0, d0, r1
# ROUNDTRIP: sra64	d0, d0, r1
sra64 d0, d0, r1

# CHECK: srl64 d1, d1, r3
# ROUNDTRIP: srl64	d1, d1, r3
srl64 d1, d1, r3

# CHECK: sra64r d2, d2, r5
# ROUNDTRIP: sra64r	d2, d2, r5
sra64r d2, d2, r5

#===----------------------------------------------------------------------===
# FmtMAC — Ternary register (rd, rs1, rs2, ra)
# These are the 4-operand MAC instructions from HaydnInstrInfoAuto.td.
#===----------------------------------------------------------------------===

# Complex multiply
# CHECK: x2cmul32 d0, d1, d2, d3
# ROUNDTRIP: x2cmul32	d0, d1, d2, d3
x2cmul32 d0, d1, d2, d3

# CHECK: x2cmul32s d4, d5, d6, d7
# ROUNDTRIP: x2cmul32s	d4, d5, d6, d7
x2cmul32s d4, d5, d6, d7

# Multiply-accumulate SIMD
# CHECK: x2mula32 d0, d1, d2, d3
# ROUNDTRIP: x2mula32	d0, d1, d2, d3
x2mula32 d0, d1, d2, d3

# CHECK: x2muls32 d4, d5, d6, d7
# ROUNDTRIP: x2muls32	d4, d5, d6, d7
x2muls32 d4, d5, d6, d7

# 4-way MAC
# CHECK: x4mula16 d0, d1, d2, d3
# ROUNDTRIP: x4mula16	d0, d1, d2, d3
x4mula16 d0, d1, d2, d3

# CHECK: x4mula16s d4, d5, d6, d7
# ROUNDTRIP: x4mula16s	d4, d5, d6, d7
x4mula16s d4, d5, d6, d7

# CHECK: x4muls16 d0, d1, d2, d3
# ROUNDTRIP: x4muls16	d0, d1, d2, d3
x4muls16 d0, d1, d2, d3

# CHECK: x4muls16s d4, d5, d6, d7
# ROUNDTRIP: x4muls16s	d4, d5, d6, d7
x4muls16s d4, d5, d6, d7

# Select
# DB: X4SEL16 syntax = "X4SEL16 rtd, rsd1, rsd2, rs" — rtd=DR64 out, rsd1/rsd2=DR64
# in, rs=GPR32 in (3xDR64 + 1xGPR32). The earlier all-GPR form was wrong per the
# authoritative DB (~/haydn-plans/Database/haydn_instruction_db.json).
# CHECK: x4sel16 d0, d1, d2, r3
# ROUNDTRIP: x4sel16	d0, d1, d2, r3
x4sel16 d0, d1, d2, r3
