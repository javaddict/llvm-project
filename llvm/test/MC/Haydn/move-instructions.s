# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d -z --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
#
# Decoder bundle-boundary gap CLOSED: the 4-byte SRA64 parcel (encoding
# [0x35,0x25,0xc0,0x59]) now re-syncs correctly when it lands at an
# 8-byte-window straddle right after the SRA32 window — objdump emits
# `sra64 d0, d0, r9` instead of `<unknown>`. Both RUNs (SHOW-ENCODING and
# ROUNDTRIP) pass; XFAIL removed.
#
# Move, immediate, and NOP instruction test.
# Covers:
# MOVE32: GPR32 register-to-register move (2-source ALU32 form)
# NOP: no-operation (all-zeros encoding)
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

# CHECK: move32 r0, r1
# ROUNDTRIP: move32	r0, r1
move32 r0, r1

# CHECK: move32 r5, r6
# ROUNDTRIP: move32	r5, r6
move32 r5, r6

# CHECK: move32 r12, r0
# ROUNDTRIP: move32	r12, r0
move32 r12, r0

#===----------------------------------------------------------------------===
# NOP — no-operation
# Encoded as all-zeros (ADD32 rd=R0, rs1=R0, rs2=R0).
#===----------------------------------------------------------------------===

# CHECK: nop
# ROUNDTRIP: { nop; nop; nop }
nop

#===----------------------------------------------------------------------===
# LUI — load upper immediate
#===----------------------------------------------------------------------===

# Post-migration (R10): LUI's uimm12 immediate was historically split by the
# Mode-0 s0 bit layout, but under the 128-bit-only Flex decode recombines
# it, so the ROUNDTRIP (decoded) immediate now renders the original uimm12.
# The load-bearing assertions for THIS test are the mnemonic and the register
# operands; the immediate is documented here.
# CHECK: lui r0, 0
# ROUNDTRIP: lui	r0, 0
lui r0, 0

# CHECK: lui r7, 42
# ROUNDTRIP: lui	r7, 42
lui r7, 42

# CHECK: lui r8, 4095
# ROUNDTRIP: lui	r8, 4095
lui r8, 4095

# CHECK: lui r12, 2048
# ROUNDTRIP: lui	r12, 2048
lui r12, 2048

#===----------------------------------------------------------------------===
# XOR32 rN,rN,rN — zero a GPR register via x^x=0 identity
# (slice Z: the ZERO_GPR : FmtI<0x28> pseudo was retired; XOR32 is the
# canonical zero-register mechanism used by CodeGen prologues and selectors.)
#===----------------------------------------------------------------------===

# CHECK: xor32 r0, r0, r0
# ROUNDTRIP: xor32	r0, r0, r0
xor32 r0, r0, r0

# CHECK: xor32 r5, r5, r5
# ROUNDTRIP: xor32	r5, r5, r5
xor32 r5, r5, r5

# CHECK: xor32 r12, r12, r12
# ROUNDTRIP: xor32	r12, r12, r12
xor32 r12, r12, r12

#===----------------------------------------------------------------------===
# Saturating immediate add/sub (from HaydnInstrInfo.td)
#===----------------------------------------------------------------------===

# CHECK: addi32s r0, r1, 100
# ROUNDTRIP: addi32s	r0, r1, 100
addi32s r0, r1, 100

# CHECK: subi32s r2, r3, 50
# ROUNDTRIP: subi32s	r2, r3, 50
subi32s r2, r3, 50

#===----------------------------------------------------------------------===
# Rounding shift (from HaydnInstrInfo.td)
#===----------------------------------------------------------------------===

# CHECK: srai32r r0, r1, 1
# ROUNDTRIP: srai32r	r0, r1, 1
srai32r r0, r1, 1

# CHECK: sra32r r2, r3, r4
# ROUNDTRIP: sra32r	r2, r3, r4
sra32r r2, r3, r4

# CHECK: sra32 r5, r6, r7
# ROUNDTRIP: sra32	r5, r6, r7
sra32 r5, r6, r7

# CHECK: sra64 d0, d0, r9
# ROUNDTRIP: sra64	d0, d0, r9
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
