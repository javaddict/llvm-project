# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -s --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST (RISK-6): HWLoopOff1/HWLoopOff2 FieldLsb must match
# the Bundle128 s0 slot window, NOT the legacy 48-bit-parcel geometry. This
# was the REAL root.
#
# Bug: HaydnRelocLayout.cpp HWLoopOff1 (FieldLsb=26) + HWLoopOff2 (FieldLsb=14)
# were transcribed from the LEGACY 48-bit parcel layout (Fmt48_WideSET_HWLOOP_F2
# in HaydnInstrFormatsC.td: offset1@bits[31:26], offset2@bits[25:14]) and never
# updated when routed all emission through Bundle128. The actually-emitted
# def is SET_HWLOOP_F2_S0 (HaydnFormatsALU32.td, class
# HaydnFU_ALU32_S0_HWLOOP_F2_W: `s0 = {FU, opcode, reserved, rs, offset2
# offset1, sel}`), which places offset1 at s0 bits[6:1] and offset2 at s0
# bits[18:7]. With stale FieldLsb=26, applyFixup wrote the 6-bit offset1 into
# bits[31:26] of the LoWord -- corrupting the rs/reserved bits and leaving
# bits[6:1] zero. The IsSigned=false fix alone was incomplete: the field RANGE
# was correct but the field POSITION was wrong.
#
# Test design: emit set_hwloop_f2 with symbolic labels at known Bundle128
# offsets, then dump raw.text bytes. The fixup resolves locally (same
# fragment) so applyFixup patches both fields before the object is written.
# Lbody = 16 bytes ahead (one Bundle128 parcel) -> off1 = 16/4 = 4
# Lend = 32 bytes ahead (two parcels) -> off2 = 32/4 = 8
# The load-bearing assertion: byte0 of the LoWord carries (off1 << 1) | sel
# = (4 << 1) | 0 = 0x08. With stale FieldLsb=26, off1 patched bits[31:26]
# (byte3) and byte0 was 0x00. This test does NOT depend on the WIDE-path
# decoder (separately XFAIL'd in bug2-hwloop-wide-fixup-off1-off2.s); it
# inspects raw section bytes directly via `llvm-objdump -s`.

.text
.globl test_d486_hwloop_fieldlsb
.balign 16
test_d486_hwloop_fieldlsb:
    # SET_HWLOOP_F2 at offset 0 -> 16-byte Bundle128 parcel (bytes 0..15).
    # The s0 LoWord is bytes 0..5; bytes 6..15 are s1/s2 NOP padding.
    # Symbolic off1/off2 emit FIXUP_HAYDN_HWLoopOff1/Off2, patched by
    # applyFixup at the FieldLsb positions (1 and 7).
    set_hwloop_f2 0, .Lbody, .Lend, r1
.Lbody:
    # offset 16 from SET_HWLOOP_F2 base -> off1 = 16/4 = 4 (bits[6:1]=000100)
    # 16-byte Bundle128 parcel (bytes 16..31)
    { add32 r1, r2, r3 }
.Lend:
    # offset 32 from SET_HWLOOP_F2 base -> off2 = 32/4 = 8 (bits[18:7]=0x008)
    # 16-byte Bundle128 parcel (bytes 32..47)
    { add32 r4, r5, r6 }

# CHECK-LABEL: Contents of section .text:
# The first 16 bytes are the SET_HWLOOP_F2 Bundle128 parcel. Byte0 = LoWord
# bit[7:0] = (off1=4 << 1) | sel=0 = 0x08 (FIX). With stale FieldLsb=26, byte0
# was 0x00 (off1 missed byte0 entirely, corrupting byte3 instead). We assert
# the hex pattern "08" appears at the start of the section content line -- this
# is the deterministic discriminator between the fixed and stale FieldLsb.
# CHECK:      0000 08
