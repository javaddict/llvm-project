# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
# REQUIRES: haydn-registered-target
#
# SIMD ALU instruction round-trip.
# ROUNDTRIP note (post / cutover): the decoder now emits Bundle128
# `{ nop; <op>.sN...; nop }` form. ALU64 ops route to slot 1 (``). The
# earlier ADD64_H/ADD64_L 8-byte-window decoder gap (which emitted `<?>`
# placeholders) was fixed by the single Bundle128 FlexMap slot-authority
# cutover; both halves of this test now pass.
# SIMD ALU instruction test for Slot 1/2 DR64 datapath operations.
# Covers the full range of SIMD arithmetic from HaydnInstrInfoAuto.td:
# Saturating add/sub: ADD64S, SUB64S, X2ADD32S, X2SUB32S, X4ADD16S, X4SUB16S
# Halfword variants: ADD64_H, ADD64_L, SUB64_H, SUB64_L, etc.
# AddSub/SubAdd: X2ADDSUB32, X2SUBADD32
# Clamp: X2CLAMP32, X4CLAMP16
# Min/Max: X2MAX32, X2MIN32, X4MAX16, X4MIN16, MAX64, MIN64
# Dot product: X2DOT32, X4DOT16
# Complex multiply: X2FCMUL32RS, X4FCMUL16RS
# Fractional multiply: X2FMUL32RS, X4FMUL16RS
# Select: X2SEL32_HH/HL/LH/LL
# Pack high/low: X2MULAPH32, X2MULAPL32
# Round shift: X2FRSST32, X2FRST32, X4FRSST16, X4FRST16

#===----------------------------------------------------------------------===
# Saturating 64-bit Add/Sub
#===----------------------------------------------------------------------===

# CHECK: add64s d0, d1, d2
# ROUNDTRIP: add64s	d0, d1, d2
add64s d0, d1, d2

# CHECK: add64s_h d3, d4, d5
# ROUNDTRIP: add64s_h	d3, d4, d5
add64s_h d3, d4, d5

# CHECK: add64s_l d6, d7, d8
# ROUNDTRIP: add64s_l	d6, d7, d8
add64s_l d6, d7, d8

# CHECK: sub64s d0, d1, d2
# ROUNDTRIP: sub64s	d0, d1, d2
sub64s d0, d1, d2

# CHECK: sub64s_h d3, d4, d5
# ROUNDTRIP: sub64s_h	d3, d4, d5
sub64s_h d3, d4, d5

# CHECK: sub64s_l d6, d7, d8
# ROUNDTRIP: sub64s_l	d6, d7, d8
sub64s_l d6, d7, d8

#===----------------------------------------------------------------------===
# Non-saturating halfword Add/Sub
#===----------------------------------------------------------------------===

# CHECK: add64_h d0, d1, d2
# ROUNDTRIP: add64_h	d0, d1, d2
add64_h d0, d1, d2

# CHECK: add64_l d3, d4, d5
# ROUNDTRIP: add64_l	d3, d4, d5
add64_l d3, d4, d5

# CHECK: sub64_h d6, d7, d8
# ROUNDTRIP: sub64_h	d6, d7, d8
sub64_h d6, d7, d8

# CHECK: sub64_l d9, d10, d11
# ROUNDTRIP: sub64_l	d9, d10, d11
sub64_l d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X2 Saturating (dual 32-bit)
#===----------------------------------------------------------------------===

# CHECK: x2add32s d0, d1, d2
# ROUNDTRIP: x2add32s	d0, d1, d2
x2add32s d0, d1, d2

# CHECK: x2sub32s d3, d4, d5
# ROUNDTRIP: x2sub32s	d3, d4, d5
x2sub32s d3, d4, d5

# CHECK: x2add32s_hllh d6, d7, d8
# ROUNDTRIP: x2add32s_hllh	d6, d7, d8
x2add32s_hllh d6, d7, d8

# CHECK: x2sub32s_hllh d9, d10, d11
# ROUNDTRIP: x2sub32s_hllh	d9, d10, d11
x2sub32s_hllh d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X2 Non-saturating HLLH variants
#===----------------------------------------------------------------------===

# CHECK: x2add32_hllh d0, d1, d2
# ROUNDTRIP: x2add32_hllh	d0, d1, d2
x2add32_hllh d0, d1, d2

# CHECK: x2sub32_hllh d3, d4, d5
# ROUNDTRIP: x2sub32_hllh	d3, d4, d5
x2sub32_hllh d3, d4, d5

#===----------------------------------------------------------------------===
# AddSub / SubAdd
#===----------------------------------------------------------------------===

# CHECK: x2addsub32 d0, d1, d2
# ROUNDTRIP: x2addsub32	d0, d1, d2
x2addsub32 d0, d1, d2

# CHECK: x2addsub32s d3, d4, d5
# ROUNDTRIP: x2addsub32s	d3, d4, d5
x2addsub32s d3, d4, d5

# CHECK: x2subadd32 d6, d7, d8
# ROUNDTRIP: x2subadd32	d6, d7, d8
x2subadd32 d6, d7, d8

# CHECK: x2subadd32s d9, d10, d11
# ROUNDTRIP: x2subadd32s	d9, d10, d11
x2subadd32s d9, d10, d11

#===----------------------------------------------------------------------===
# Clamp
#===----------------------------------------------------------------------===

# CHECK: x2clamp32 d0, d1, d2
# ROUNDTRIP: x2clamp32	d0, d1, d2
x2clamp32 d0, d1, d2

# CHECK: x4clamp16 d3, d4, d5
# ROUNDTRIP: x4clamp16	d3, d4, d5
x4clamp16 d3, d4, d5

#===----------------------------------------------------------------------===
# Min / Max (SIMD and 64-bit scalar)
#===----------------------------------------------------------------------===

# CHECK: max64 d0, d1, d2
# ROUNDTRIP: max64	d0, d1, d2
max64 d0, d1, d2

# CHECK: min64 d3, d4, d5
# ROUNDTRIP: min64	d3, d4, d5
min64 d3, d4, d5

# CHECK: x2max32 d0, d1, d2
# ROUNDTRIP: x2max32	d0, d1, d2
x2max32 d0, d1, d2

# CHECK: x2min32 d3, d4, d5
# ROUNDTRIP: x2min32	d3, d4, d5
x2min32 d3, d4, d5

# CHECK: x4max16 d6, d7, d8
# ROUNDTRIP: x4max16	d6, d7, d8
x4max16 d6, d7, d8

# CHECK: x4min16 d9, d10, d11
# ROUNDTRIP: x4min16	d9, d10, d11
x4min16 d9, d10, d11

#===----------------------------------------------------------------------===
# Dot Product
#===----------------------------------------------------------------------===

# CHECK: x2dot32 d0, d1, d2
# ROUNDTRIP: x2dot32	d0, d1, d2
x2dot32 d0, d1, d2

# CHECK: x4dot16 d3, d4, d5
# ROUNDTRIP: x4dot16	d3, d4, d5
x4dot16 d3, d4, d5

#===----------------------------------------------------------------------===
# Complex Multiply (non-ternary, FmtALU64 forms)
#===----------------------------------------------------------------------===

# CHECK: x2fcmul32rs d0, d1, d2
# ROUNDTRIP: x2fcmul32rs	d0, d1, d2
x2fcmul32rs d0, d1, d2

# CHECK: x2fcmul32rss d3, d4, d5
# ROUNDTRIP: x2fcmul32rss	d3, d4, d5
x2fcmul32rss d3, d4, d5

# CHECK: x4fcmul16rs d0, d1, d2
# ROUNDTRIP: x4fcmul16rs	d0, d1, d2
x4fcmul16rs d0, d1, d2

# CHECK: x4fcmul16rss d3, d4, d5
# ROUNDTRIP: x4fcmul16rss	d3, d4, d5
x4fcmul16rss d3, d4, d5

#===----------------------------------------------------------------------===
# Fractional Multiply (non-ternary, FmtALU64 forms)
#===----------------------------------------------------------------------===

# CHECK: x2fmul32rs d0, d1, d2
# ROUNDTRIP: x2fmul32rs	d0, d1, d2
x2fmul32rs d0, d1, d2

# CHECK: x2fmul32rss d3, d4, d5
# ROUNDTRIP: x2fmul32rss	d3, d4, d5
x2fmul32rss d3, d4, d5

# CHECK: x2fmul32ts d6, d7, d8
# ROUNDTRIP: x2fmul32ts	d6, d7, d8
x2fmul32ts d6, d7, d8

# CHECK: x4fmul16rs d0, d1, d2
# ROUNDTRIP: x4fmul16rs	d0, d1, d2
x4fmul16rs d0, d1, d2

# CHECK: x4fmul16rss d3, d4, d5
# ROUNDTRIP: x4fmul16rss	d3, d4, d5
x4fmul16rss d3, d4, d5

# CHECK: x4fmul16ts d6, d7, d8
# ROUNDTRIP: x4fmul16ts	d6, d7, d8
x4fmul16ts d6, d7, d8

#===----------------------------------------------------------------------===
# Select
#===----------------------------------------------------------------------===

# CHECK: x2sel32_hh d0, d1, d2
# ROUNDTRIP: x2sel32_hh	d0, d1, d2
x2sel32_hh d0, d1, d2

# CHECK: x2sel32_hl d3, d4, d5
# ROUNDTRIP: x2sel32_hl	d3, d4, d5
x2sel32_hl d3, d4, d5

# CHECK: x2sel32_lh d6, d7, d8
# ROUNDTRIP: x2sel32_lh	d6, d7, d8
x2sel32_lh d6, d7, d8

# CHECK: x2sel32_ll d9, d10, d11
# ROUNDTRIP: x2sel32_ll	d9, d10, d11
x2sel32_ll d9, d10, d11

#===----------------------------------------------------------------------===
# Pack High/Low
#===----------------------------------------------------------------------===

# CHECK: x2mulaph32 d0, d1, d2
# ROUNDTRIP: x2mulaph32	d0, d1, d2
x2mulaph32 d0, d1, d2

# CHECK: x2mulapl32 d3, d4, d5
# ROUNDTRIP: x2mulapl32	d3, d4, d5
x2mulapl32 d3, d4, d5

# CHECK: x2mulsph32 d6, d7, d8
# ROUNDTRIP: x2mulsph32	d6, d7, d8
x2mulsph32 d6, d7, d8

# CHECK: x2mulspl32 d9, d10, d11
# ROUNDTRIP: x2mulspl32	d9, d10, d11
x2mulspl32 d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X4 Saturating (quad 16-bit)
#===----------------------------------------------------------------------===

# CHECK: x4add16s d0, d1, d2
# ROUNDTRIP: x4add16s	d0, d1, d2
x4add16s d0, d1, d2

# CHECK: x4sub16s d3, d4, d5
# ROUNDTRIP: x4sub16s	d3, d4, d5
x4sub16s d3, d4, d5

#===----------------------------------------------------------------------===
# Round Shift
#===----------------------------------------------------------------------===

# CHECK: x2frsst32 d0, d1, d2
# ROUNDTRIP: x2frsst32	d0, d1, d2
x2frsst32 d0, d1, d2

# CHECK: x2frst32 d3, d4, d5
# ROUNDTRIP: x2frst32	d3, d4, d5
x2frst32 d3, d4, d5

# CHECK: x4frsst16 d0, d1, d2
# ROUNDTRIP: x4frsst16	d0, d1, d2
x4frsst16 d0, d1, d2

# CHECK: x4frst16 d3, d4, d5
# ROUNDTRIP: x4frst16	d3, d4, d5
x4frst16 d3, d4, d5

#===----------------------------------------------------------------------===
# Saturation
#===----------------------------------------------------------------------===

# CHECK: x4sat32t16 d0, d1, d2
# ROUNDTRIP: x4sat32t16	d0, d1, d2
x4sat32t16 d0, d1, d2

#===----------------------------------------------------------------------===
# SIMD X4 Complex Multiply (non-ternary forms)
#===----------------------------------------------------------------------===

# CHECK: x4cjmul16s_h d0, d1, d2
# ROUNDTRIP: x4cjmul16s.h	d0, d1, d2
x4cjmul16s_h d0, d1, d2

# CHECK: x4cjmul16s_l d3, d4, d5
# ROUNDTRIP: x4cjmul16s.l	d3, d4, d5
x4cjmul16s_l d3, d4, d5

# CHECK: x4cmul16s_h d6, d7, d8
# ROUNDTRIP: x4cmul16s.h	d6, d7, d8
x4cmul16s_h d6, d7, d8

# CHECK: x4cmul16s_l d9, d10, d11
# ROUNDTRIP: x4cmul16s.l	d9, d10, d11
x4cmul16s_l d9, d10, d11

#===----------------------------------------------------------------------===
# X2 Fractional Multiply Accumulate/Subtract (FmtALU64 — 3-operand)
# These accumulate into rd with the product of rs1 and rs2.
#===----------------------------------------------------------------------===

# CHECK: x2fmula32rs d0, d1, d2
# ROUNDTRIP: x2fmula32rs	d0, d1, d2
x2fmula32rs d0, d1, d2

# CHECK: x2fmula32rss d3, d4, d5
# ROUNDTRIP: x2fmula32rss	d3, d4, d5
x2fmula32rss d3, d4, d5

# CHECK: x2fmuls32rs d6, d7, d8
# ROUNDTRIP: x2fmuls32rs	d6, d7, d8
x2fmuls32rs d6, d7, d8

# CHECK: x2fmuls32rss d9, d10, d11
# ROUNDTRIP: x2fmuls32rss	d9, d10, d11
x2fmuls32rss d9, d10, d11

#===----------------------------------------------------------------------===
# Complex MAC accumulate (FmtALU64 — 3-operand)
#===----------------------------------------------------------------------===

# CHECK: x2fcmula32rs d0, d1, d2
# ROUNDTRIP: x2fcmula32rs	d0, d1, d2
x2fcmula32rs d0, d1, d2

# CHECK: x2fcmula32rss d3, d4, d5
# ROUNDTRIP: x2fcmula32rss	d3, d4, d5
x2fcmula32rss d3, d4, d5

#===----------------------------------------------------------------------===
# Ternary MAC forms (FmtMAC — 4-operand)
# These use a separate accumulator register (ra).
#===----------------------------------------------------------------------===

# CHECK: x4ff2mul16s d4, d5, d6, d7
# ROUNDTRIP: x4ff2mul16s	d4, d5, d6, d7
x4ff2mul16s d4, d5, d6, d7

# CHECK: x4ff2mula16s d0, d1, d2, d3
# ROUNDTRIP: x4ff2mula16s	d0, d1, d2, d3
x4ff2mula16s d0, d1, d2, d3

# CHECK: x4ff2muls16s d4, d5, d6, d7
# ROUNDTRIP: x4ff2muls16s	d4, d5, d6, d7
x4ff2muls16s d4, d5, d6, d7
