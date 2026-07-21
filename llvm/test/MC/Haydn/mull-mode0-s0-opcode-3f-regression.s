# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: MULL family (MULL/MULSSH/MULSUH/MULUUH) — Mode-0 §6.5 s0
# encoding, opcode 0x3F (extended-ALU32 marker) + ext[4:2] sub-variant.
#
# Bug (/ H5): the MULL family was defined as FmtALU64<0x67, funct> — the
# legacy 4-byte parcel (bits[31:30]=01, opcode 0x67) — with no getEncOpcodeMap
# entry, so it was unencodable in Mode-0 bundles (deferred this). The
# legacy parcel is NOT the spec §6.5 layout. This test pins the migrated
# Mode-0 s0 byte layout and MUST FAIL on the legacy FmtALU64 encoding.
#
# Spec §6.5 s0 ALU32 (20 bits, little-endian 64-bit Mode-0 bundle):
# pattern[3:0]=0011 | ext[8:4] | rt[12:9] | rs[16:13] | opcode[22:17] | FU[23]
# MULL family: opcode=0x3F, ext[4:2]=variant (0/1/2/3), ext[1:0]=0.
# byte[2] = 0x7e = FU(0) | opcode(111111) | rs_hi(0) — the 0x3F marker.
# ext distinguishes the 4 variants: 0x00/0x04/0x08/0x0c at byte[0].
#
# tied-destructive form: the wire form is destructive 2-operand
# (rt = rs*rt); the architectural/asm view is 3-operand with a tied
# constraint "$rd = $rs2". The asm writes rs2 == rd explicitly (the tied
# source mirrors the dest). The encoder packs rt=$rd, rs1=$rs1, ext=variant;
# operand 2 (rs2) is tied to $rd and dropped. The decoder reconstructs
# rs2 = rt. So `mull r0, r1, r0` round-trips losslessly (rd==rs2).
#
# Layout check: rd=r0(rt=0), rs1=r1(rs=1), variant 0/1/2/3 -> ext=0/4/8/12.
# word = 0x3 | (ext<<4) | (rt<<9) | (rs<<13) | (0x3F<<17) | (0<<23)
# MULL ext=0 -> [0x03,0x20,0x7e,0x00,0x00,0x00,0x00,0x00]
# MULSSH ext=4 -> [0x43,0x20,0x7e,0x00,0x00,0x00,0x00,0x00]
# MULSUH ext=8 -> [0x83,0x20,0x7e,0x00,0x00,0x00,0x00,0x00]
# MULUUH ext=12 -> [0xc3,0x20,0x7e,0x00,0x00,0x00,0x00,0x00]

.text
.globl test_mull_mode0
test_mull_mode0:
  # opcode 0x3F @ bits[22:17]; variant 0 (MULL) @ ext[4:2]; ext[1:0]=0.
  # 16-byte Bundle128 composite. MAC ops occupy the s1 slot window
  # (bytes 6-9); s0 and s2 are NOP. The variant discriminator is at byte[8].
  # ROUNDTRIP: { nop; mull	r0, r1, r0; nop }
  mull r0, r1, r0

  # variant 1 (MULSSH) @ ext[4:2]=001.
  # ROUNDTRIP: { nop; mulssh	r0, r1, r0; nop }
  mulssh r0, r1, r0

  # variant 2 (MULSUH) @ ext[4:2]=010.
  # ROUNDTRIP: { nop; mulsuh	r0, r1, r0; nop }
  mulsuh r0, r1, r0

  # variant 3 (MULUUH) @ ext[4:2]=011.
  # ROUNDTRIP: { nop; muluuh	r0, r1, r0; nop }
  muluuh r0, r1, r0
