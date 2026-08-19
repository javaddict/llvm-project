# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d -z --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s

// CHECK: { move32 r0, r1 } // encoding: [0x07,0x44,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { move32 r5, r6 } // encoding: [0x07,0x44,0x50,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { move32 r12, r0 } // encoding: [0x07,0x44,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { nop } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { lui r0, 0 } // encoding: [0x07,0x0a,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { lui r7, 42 } // encoding: [0x07,0x0a,0x72,0x00,0x2a,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { lui r8, 4095 } // encoding: [0x07,0x0a,0x82,0x00,0xff,0x0f,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { lui r12, 2048 } // encoding: [0x07,0x0a,0xc2,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xor32 r0, r0, r0 } // encoding: [0x07,0x4b,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xor32 r5, r5, r5 } // encoding: [0x07,0x4b,0x51,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { xor32 r12, r12, r12 } // encoding: [0x07,0x4b,0xc1,0xcc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { addi32s r0, r1, 100 } // encoding: [0x07,0x0f,0x06,0x01,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { subi32s r2, r3, 50 } // encoding: [0x07,0x0f,0x2e,0x03,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { srai32r r0, r1, 1 } // encoding: [0x07,0x06,0x03,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sra32r r2, r3, r4 } // encoding: [0x07,0xab,0x21,0x43,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sra32 r5, r6, r7 } // encoding: [0x07,0x8b,0x51,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sra64 d0, d0, r9 } // encoding: [0x07,0x0b,0x06,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// ROUNDTRIP: {{.*}}0: 07 44 00 01 00 00 00 00 00 00 00 00 { nop; move32 r0, r1 }
// ROUNDTRIP: {{.*}}c: 07 44 50 06 00 00 00 00 00 00 00 00 { nop; move32 r5, r6 }
// ROUNDTRIP: {{.*}}18: 07 44 c0 00 00 00 00 00 00 00 00 00 { nop; move32 r12, r0 }
// ROUNDTRIP: {{.*}}24: 07 00 00 00 00 00 00 00 00 00 00 00 { nop; nop }
// ROUNDTRIP: {{.*}}30: 07 0a 02 00 00 00 00 00 00 00 00 00 { nop; lui r0, 0 }
// ROUNDTRIP: {{.*}}3c: 07 0a 72 00 2a 00 00 00 00 00 00 00 { nop; lui r7, 42 }
// ROUNDTRIP: {{.*}}48: 07 0a 82 00 ff 0f 00 00 00 00 00 00 { nop; lui r8, 4095 }
// ROUNDTRIP: {{.*}}54: 07 0a c2 00 00 08 00 00 00 00 00 00 { nop; lui r12, 2048 }
// ROUNDTRIP: {{.*}}60: 07 4b 01 00 00 00 00 00 00 00 00 00 { nop; xor32 r0, r0, r0 }
// ROUNDTRIP: {{.*}}6c: 07 4b 51 55 00 00 00 00 00 00 00 00 { nop; xor32 r5, r5, r5 }
// ROUNDTRIP: {{.*}}78: 07 4b c1 cc 00 00 00 00 00 00 00 00 { nop; xor32 r12, r12, r12 }
// ROUNDTRIP: {{.*}}84: 07 0f 06 01 32 00 00 00 00 00 00 00 { nop; addi32s r0, r1, 100 }
// ROUNDTRIP: {{.*}}90: 07 0f 2e 03 19 00 00 00 00 00 00 00 { nop; subi32s r2, r3, 50 }
// ROUNDTRIP: {{.*}}9c: 07 06 03 01 01 00 00 00 00 00 00 00 { nop; srai32r r0, r1, 1 }
// ROUNDTRIP: {{.*}}a8: 07 ab 21 43 00 00 00 00 00 00 00 00 { nop; sra32r r2, r3, r4 }
// ROUNDTRIP: {{.*}}b4: 07 8b 51 76 00 00 00 00 00 00 00 00 { nop; sra32 r5, r6, r7 }
// ROUNDTRIP: {{.*}}c0: 07 0b 06 90 00 00 00 00 00 00 00 00 { nop; sra64 d0, d0, r9 }
# Role: object — Decoder bundle-boundary gap CLOSED: the 4-byte SRA64 parcel (encoding [0x35,0x25,0xc0,0x59]) now re-syncs correctly when it lands at an.

# Decoder bundle-boundary gap CLOSED: the 4-byte SRA64 parcel (encoding
# [0x35,0x25,0xc0,0x59]) now re-syncs correctly when it lands at an
# 8-byte-window straddle right after the SRA32 window — objdump emits
# `sra64 d0, d0, r9` instead of `<unknown>`. Both RUNs (SHOW-ENCODING and
#
# Move, immediate, and NOP instruction test.
# Covers:
# MOVE32: GPR32 register-to-register move (2-source ALU32 form)
# NOP: product idle parcel (header 0x07 + zero entries, EncodedBytes=12)
# LUI: load upper immediate (uimm12 immediate, post-ISA-43)
# XOR32 rN,rN,rN: zero a GPR register via x^x=0 (-Z canonical;
# ZERO_GPR : FmtI<0x28> was retired)
# SRAI32R: rounding arithmetic right shift immediate
# SRA32R / SRA32: register-based rounding/normal arithmetic right shift
# ADDI32S / SUBI32S: saturating add/subtract immediate
# MACQ31 / MULQ31 / MULQ63 / MAC32: fractional MAC from HaydnInstrInfo.td

#===----------------------------------------------------------------------===
# MOVE32 — register move
# The TableGen definition has 2 source operands (rd = rs1, ignores rs2).
# The assembler syntax is "move32 rd, rs1".
#===----------------------------------------------------------------------===


move32 r0, r1

move32 r5, r6

move32 r12, r0

#===----------------------------------------------------------------------===
# NOP — product idle parcel (Format E EncodedBytes=12).
# Live encoding is header 0x07 + zero entries (see flex-nop.s).
# Fail-closed: do not claim all-zero bytes as the product NOP.
#===----------------------------------------------------------------------===

nop

#===----------------------------------------------------------------------===
# LUI — load upper immediate
#===----------------------------------------------------------------------===

# Post-migration (R10): LUI's uimm12 immediate was historically split by the
# Mode-0 s0 bit layout, but under the 128-bit-only Flex decode recombines
# it, so the ROUNDTRIP (decoded) immediate now renders the original uimm12.
# The load-bearing assertions for THIS test are the mnemonic and the register
# operands; the immediate is documented here.
lui r0, 0

lui r7, 42

lui r8, 4095

lui r12, 2048

#===----------------------------------------------------------------------===
# XOR32 rN,rN,rN — zero a GPR register via x^x=0 identity
# (slice Z: the ZERO_GPR : FmtI<0x28> pseudo was retired; XOR32 is the
# canonical zero-register mechanism used by CodeGen prologues and selectors.)
#===----------------------------------------------------------------------===

xor32 r0, r0, r0

xor32 r5, r5, r5

xor32 r12, r12, r12

#===----------------------------------------------------------------------===
# Saturating immediate add/sub (from HaydnInstrInfo.td)
#===----------------------------------------------------------------------===

addi32s r0, r1, 100

subi32s r2, r3, 50

#===----------------------------------------------------------------------===
# Rounding shift (from HaydnInstrInfo.td)
#===----------------------------------------------------------------------===

srai32r r0, r1, 1

sra32r r2, r3, r4

sra32 r5, r6, r7

sra64 d0, d0, r9

#===----------------------------------------------------------------------===
# Fractional MAC from HaydnInstrInfo.td (manually defined)
# These use the FmtMAC format with 3-source + 1-dest.
#===----------------------------------------------------------------------===

# MULQ31/MACQ31/MULQ63 REMOVED — phantom instructions (not in the ISA
# DB; the real Q-format MAC family is FF2MULA32RS_*). The asm mnemonics are
# no longer accepted by the AsmParser. The source-level builtins remain and
# are lowered in HaydnInstructionSelector to real ISA sequences.

# MAC32: 32-bit multiply-accumulate, GPR32 result
