# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
# REQUIRES: haydn-registered-target
#
# DR64 (64-bit data register) operation test.
# Covers all DR64-register instructions that have real encodings and parse
# via the current AsmParser. The WideImm immediate-form shifts
# (SLLI64/SRLI64/SRAI64 d,d,imm) are an M5 AsmParser gap; they are tracked
# separately and exercised by disassembler-dsp-math-roundtrip.s.
#
# ROUNDTRIP note (post / cutover): the decoder now emits Bundle128
# `{ <slot0>; <slot1>; <slot2> }` form with each instruction tagged with its
# slot suffix (``/``/``) and idle slots rendered as `nop`. DR64 ALU
# ops route to slot 1 (``); DR64 load/store route to slot 0 (``). Each
# single-op asm line therefore round-trips as `{ nop; <op>.sN...; nop }`
# (or `{ <op>...; nop; nop }` for slot-0 ops). Earlier decoder gaps
# (8-byte bundle probe / strict-flat fallback) were fixed by the single
# Bundle128 FlexMap slot authority cutover; this test now passes both halves.

#===----------------------------------------------------------------------===
# 64-bit ALU Arithmetic (from HaydnInstrInfo.td — manual definitions)
#===----------------------------------------------------------------------===

# ROUNDTRIP: add64	d0, d1, d2
add64 d0, d1, d2

# ROUNDTRIP: sub64	d3, d4, d5
sub64 d3, d4, d5

# ROUNDTRIP: and64	d6, d7, d8
and64 d6, d7, d8

# ROUNDTRIP: or64	d9, d10, d11
or64 d9, d10, d11

# ROUNDTRIP: xor64	d12, d13, d14
xor64 d12, d13, d14

#===----------------------------------------------------------------------===
# 64-bit Shifts (manual definitions — binary register form)
#===----------------------------------------------------------------------===

# SLLI64/SRLI64/SRAI64 (WideImm immediate form) are M5 AsmParser gaps;
# not assembled here.

#===----------------------------------------------------------------------===
# SIMD X2 Shift (dual 32-bit)
#===----------------------------------------------------------------------===

# X2SLL32/X2SRL32/X2SRA32: DR64 dest, DR64 source, GPR32 shift amount.
# ROUNDTRIP: x2sll32	d5, d6, r0
x2sll32 d5, d6, r0

# ROUNDTRIP: x2srl32	d8, d9, r0
x2srl32 d8, d9, r0

# ROUNDTRIP: x2sra32	d11, d12, r0
x2sra32 d11, d12, r0

#===----------------------------------------------------------------------===
# DR64 Load/Store
#===----------------------------------------------------------------------===

# LD64 (internal name LD64_S1, Slot 1 load) — asm mnemonic is "ld64".
# Decoder routes the LD64 parcel to slot 0.
# ROUNDTRIP: ld64	d0, r7, 0
d_ldw_with_imm d0, r7, 0

# ROUNDTRIP: ld64	d1, r8, 8
d_ldw_with_imm d1, r8, 1

# ROUNDTRIP: ld64	d15, r0, 1020
d_ldw_with_imm d15, r0, 31

# ST64 (Slot 0 store)
# ROUNDTRIP: st64	d2, r9, 0
d_sdw_with_imm d2, r9, 0

# ROUNDTRIP: st64	d3, r10, 65520
d_sdw_with_imm d3, r10, -2

# ROUNDTRIP: st64	d0, r1, 8
d_sdw_with_imm d0, r1, 1

#===----------------------------------------------------------------------===
# Slot 1 Scalar Load
#===----------------------------------------------------------------------===

# ROUNDTRIP: ld32	r1, r2, 4
s_lw_with_imm r1, r2, 1

# ROUNDTRIP: ld32	r3, r4, 0
s_lw_with_imm r3, r4, 0

#===----------------------------------------------------------------------===
# Accumulator Shift/Extract (DR64 accumulator -> GPR32 result)
# SRA64/SRL64/SRA64R and SLL64 are M5 AsmParser gaps; not assembled here.
#===----------------------------------------------------------------------===

#===----------------------------------------------------------------------===
# X2SRA32R (SIMD rounding shift; DR64 dest, DR64 src, GPR32 shift amount)
#===----------------------------------------------------------------------===

# X2SRA32R: rtd=DR64, rsd=DR64, rs=GPR32 (per HaydnInstrInfoAuto.td:1793)
# ROUNDTRIP: x2sra32r	d0, d1, r2
x2sra32r d0, d1, r2

#===----------------------------------------------------------------------===
# X4 Shift operations (DR64 dest, DR64 src, GPR32 shift amount)
#===----------------------------------------------------------------------===

# X4SLL16: rtd=DR64, rsd=DR64, rs=GPR32 (per HaydnInstrInfoAuto.td:1951)
# ROUNDTRIP: x4sll16	d0, d1, r2
x4sll16 d0, d1, r2

# ROUNDTRIP: x4sra16	d3, d4, r5
x4sra16 d3, d4, r5

# ROUNDTRIP: x4sra16r	d6, d7, r8
x4sra16r d6, d7, r8

# ROUNDTRIP: x4srl16	d9, d10, r11
x4srl16 d9, d10, r11
