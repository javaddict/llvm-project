//===- HaydnRelocLayoutTest.cpp - reloc range/scale authority -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pin HaydnRelocLayout::computeRelocValue as the sole alignment/scale/range
// authority for PC-rel branch (ValueShift=0, byte PC+imm) and hwloop
// (ValueShift=2, word) kinds. MC applyFixup and lld inBranchRange/relocate
// share this table — these unit pins freeze the positive and negative
// boundaries so a consumer cannot silently reintroduce hand-coded isInt
// field widths.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
#include "llvm/BinaryFormat/ELF.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::HaydnReloc;

namespace {

#define GET_FORMAT_E_GOLDEN_PINS
#include "HaydnGenFormatERecords.inc"

static bool ok(RelocKind K, int64_t ByteOffset) {
  return computeRelocValue(K, static_cast<uint64_t>(ByteOffset)).OK;
}

static const char *err(RelocKind K, int64_t ByteOffset) {
  return computeRelocValue(K, static_cast<uint64_t>(ByteOffset)).Err;
}

static uint64_t field(RelocKind K, int64_t ByteOffset) {
  RelocCompute C = computeRelocValue(K, static_cast<uint64_t>(ByteOffset));
  EXPECT_TRUE(C.OK) << "expected in-range offset " << ByteOffset;
  return C.FieldVal;
}

// Format E E2 e0 product FieldLsb (parcel-origin r_offset), pin production
// HaydnRelocLayout table (golden E2 e0 / FE8 12-byte parcels):
//   I12/RI12 branch imm12 @32 (bits[32:43]); NBytes=12 (E3 e1/e2 past bit 48)
//   WIDE_Call table FieldLsb=31 (E2 e0); NBytes=12 (E3 e1 imm may reach bit 67)
//   LO20 / PC_LO20 @31 (bits[31:50]); LUI HI12 @32; NBytes=12
// WIDE_Call is byte PC-relative (ValueShift=0); branches are also byte.
// E3 call windows are resolved dynamically via resolveFieldLsb(Loc).
TEST(HaydnRelocLayoutTest, FormatEE2E0FieldLsbParcelOrigin) {
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_CallSImm20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_CallSImm20).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::HI12).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::HI12).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LO20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::PC_LO20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).FieldLsb, 28u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).FieldSize, 6u);
  // NBytes=12 (W25): covers the non-e0 LOAD1 windows (E2 e1 @72, E3 e2
  // @85) via resolveFieldLsb; the table FieldLsb stays E2 e0 @28.
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).ValueShift, 0u);
  EXPECT_TRUE(getRelocFieldInfo(RelocKind::LS_IMM).IsSigned);
  EXPECT_TRUE(isRelocTransformReady(RelocKind::LS_IMM));
  EXPECT_TRUE(isRelocTransformReady(RelocKind::WIDE_BranchSImm12));
  EXPECT_TRUE(isRelocTransformReady(RelocKind::WIDE_BranchSImm12_RI));
  EXPECT_TRUE(isRelocTransformReady(RelocKind::WIDE_CallSImm20));
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).Trans,
            RelocTrans::None);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_CallSImm20).ValueShift, 0u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).ValueShift, 0u);
}

// WIDE_BranchSImm12: signed 12-bit byte PC+imm → window [-2048, +2046] even.
TEST(HaydnRelocLayoutTest, WideBranchBytePositiveNegativeBounds) {
  const RelocKind K = RelocKind::WIDE_BranchSImm12;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 2u);
  EXPECT_EQ(FI.FieldSize, 12u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +2046));
  EXPECT_EQ(field(K, +2046), 2046u);
  EXPECT_FALSE(ok(K, +2048));
  EXPECT_STREQ(err(K, +2048), "relocation offset out of range");

  EXPECT_TRUE(ok(K, -2048));
  EXPECT_EQ(field(K, -2048) & 0xFFFu, 0x800u);
  EXPECT_FALSE(ok(K, -2050));
  EXPECT_STREQ(err(K, -2050), "relocation offset out of range");

  // Odd byte offset fails closed as misaligned (not as range).
  EXPECT_FALSE(ok(K, +1));
  EXPECT_STREQ(err(K, +1), "mis-aligned relocation target");

  // Direct B/JAL records are EncodedBytes apart. Field is the byte
  // displacement (no extra scale); 12 and 24 must encode as themselves.
  EXPECT_TRUE(ok(K, +12));
  EXPECT_EQ(field(K, +12), 12u);
  EXPECT_TRUE(ok(K, +24));
  EXPECT_EQ(field(K, +24), 24u);
  EXPECT_TRUE(ok(K, -12));
  EXPECT_EQ(field(K, -12) & 0xFFFu, 0xFF4u);
}

// BranchSImm16: signed 16-bit byte PC+imm → window [-32768, +32766] even.
TEST(HaydnRelocLayoutTest, BranchSImm16BytePositiveNegativeBounds) {
  const RelocKind K = RelocKind::BranchSImm16;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 2u);
  EXPECT_EQ(FI.FieldSize, 16u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +32766));
  EXPECT_EQ(field(K, +32766), 32766u);
  EXPECT_FALSE(ok(K, +32768));
  EXPECT_TRUE(ok(K, -32768));
  EXPECT_EQ(field(K, -32768) & 0xFFFFu, 0x8000u);
  EXPECT_FALSE(ok(K, -32770));
}

// CallSImm20: signed 20-bit PC-relative BYTE field (ValueShift=0, Align=1).
// Effective window [-524288, +524287]. Distinct from WIDE_BranchSImm12 (byte simm12).
TEST(HaydnRelocLayoutTest, CallSImm20BytePositiveNegativeBounds) {
  const RelocKind K = RelocKind::CallSImm20;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 1u);
  EXPECT_EQ(FI.FieldSize, 20u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +524287));
  EXPECT_EQ(field(K, +524287), 0x7FFFFu);
  EXPECT_FALSE(ok(K, +524288));
  EXPECT_STREQ(err(K, +524288), "relocation offset out of range");

  EXPECT_TRUE(ok(K, -524288));
  EXPECT_EQ(field(K, -524288) & 0xFFFFFu, 0x80000u);
  EXPECT_FALSE(ok(K, -524289));
  EXPECT_STREQ(err(K, -524289), "relocation offset out of range");
}

// WIDE_CallSImm20: signed 20-bit PC-relative BYTE field (ValueShift=0,
// Align=2 — parcel-aligned window per MinBundleAddressAlignBytes; the row
// comment in HaydnRelocLayout.cpp pins this to the branch path). Effective
// window [-524288, +524286] in even bytes. Distinct from
// WIDE_BranchSImm12 (byte simm12) and non-WIDE CallSImm20 (Align=1).
TEST(HaydnRelocLayoutTest, WideCallDiv2PositiveNegativeBounds) {
  const RelocKind K = RelocKind::WIDE_CallSImm20;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 2u);
  EXPECT_EQ(FI.FieldSize, 20u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +524286));
  EXPECT_EQ(field(K, +524286), 0x7FFFEu);
  EXPECT_FALSE(ok(K, +524288));
  EXPECT_TRUE(ok(K, -524288));
  EXPECT_EQ(field(K, -524288) & 0xFFFFFu, 0x80000u);
  EXPECT_FALSE(ok(K, -524289));
  // Parcel-aligned byte scale: odd offsets are rejected (even-byte gate).
  EXPECT_FALSE(ok(K, +1));
  // Parcel-sized JAL displacement is a plain (even) byte field.
  EXPECT_TRUE(ok(K, +12));
  EXPECT_EQ(field(K, +12), 12u);
  EXPECT_TRUE(ok(K, +24));
  EXPECT_EQ(field(K, +24), 24u);
}

// HWLoopOff1: unsigned 6-bit after ÷4 → byte window [0, 252].
TEST(HaydnRelocLayoutTest, HWLoopOff1Div4PositiveNegativeBounds) {
  const RelocKind K = RelocKind::HWLoopOff1;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 2u);
  EXPECT_EQ(FI.Align, 4u);
  EXPECT_EQ(FI.FieldSize, 6u);
  EXPECT_FALSE(FI.IsSigned);

  EXPECT_TRUE(ok(K, 0));
  EXPECT_EQ(field(K, 0), 0u);
  EXPECT_TRUE(ok(K, 252));
  EXPECT_EQ(field(K, 252), 63u); // 252 >> 2
  EXPECT_FALSE(ok(K, 256));
  EXPECT_STREQ(err(K, 256), "relocation offset out of range");

  // Negative is out of unsigned range (body cannot start before SET).
  EXPECT_FALSE(ok(K, -4));
  EXPECT_STREQ(err(K, -4), "relocation offset out of range");

  // Halfword-only gap is misaligned for word-scaled hwloop.
  EXPECT_FALSE(ok(K, 2));
  EXPECT_STREQ(err(K, 2), "mis-aligned relocation target");
}

// LS_IMM: signed 6-bit byte field (ValueShift=0) → window [-32, +31].
TEST(HaydnRelocLayoutTest, LSImmSigned6Bounds) {
  const RelocKind K = RelocKind::LS_IMM;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 1u);
  EXPECT_EQ(FI.FieldSize, 6u);
  EXPECT_EQ(FI.FieldLsb, 28u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +31));
  EXPECT_EQ(field(K, +31), 31u);
  EXPECT_FALSE(ok(K, +32));
  EXPECT_STREQ(err(K, +32), "relocation offset out of range");

  EXPECT_TRUE(ok(K, -32));
  EXPECT_EQ(field(K, -32) & 0x3Fu, 0x20u);
  EXPECT_FALSE(ok(K, -33));
}

// HWLoopOff2: unsigned 12-bit after ÷4 → byte window [0, 16380].
TEST(HaydnRelocLayoutTest, HWLoopOff2Div4PositiveNegativeBounds) {
  const RelocKind K = RelocKind::HWLoopOff2;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 2u);
  EXPECT_EQ(FI.Align, 4u);
  EXPECT_EQ(FI.FieldSize, 12u);
  EXPECT_FALSE(FI.IsSigned);

  EXPECT_TRUE(ok(K, 0));
  EXPECT_TRUE(ok(K, 16380));
  EXPECT_EQ(field(K, 16380), 4095u); // 16380 >> 2
  EXPECT_FALSE(ok(K, 16384));
  EXPECT_FALSE(ok(K, -4));
}

// RI12 WIDE branch shares byte PC+imm / signed-12 geometry with the I12 kind.
// Format E E2 e0 places both imm12 fields at parcel bits[32:43] (FieldLsb=32).
// E3 I12/RI12 windows are resolved from the parcel (resolveFieldLsb).
TEST(HaydnRelocLayoutTest, WideBranchRISharesByteScale) {
  const RelocFieldInfo &I12 = getRelocFieldInfo(RelocKind::WIDE_BranchSImm12);
  const RelocFieldInfo &RI12 =
      getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_EQ(I12.ValueShift, RI12.ValueShift);
  EXPECT_EQ(I12.Align, RI12.Align);
  EXPECT_EQ(I12.FieldSize, RI12.FieldSize);
  EXPECT_EQ(I12.IsSigned, RI12.IsSigned);
  EXPECT_EQ(I12.FieldLsb, RI12.FieldLsb);
  EXPECT_EQ(I12.FieldLsb, 32u);
  EXPECT_TRUE(ok(RelocKind::WIDE_BranchSImm12_RI, +2046));
  EXPECT_FALSE(ok(RelocKind::WIDE_BranchSImm12_RI, +2048));
  EXPECT_TRUE(ok(RelocKind::WIDE_BranchSImm12_RI, -2048));
  EXPECT_FALSE(ok(RelocKind::WIDE_BranchSImm12_RI, -2050));
}

// LongBranchSImm20 (MC-only): same byte PC+imm as WIDE_CallSImm20, Align=2.
TEST(HaydnRelocLayoutTest, LongBranchBytePositiveNegativeBounds) {
  const RelocKind K = RelocKind::LongBranchSImm20;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 2u);
  EXPECT_EQ(FI.FieldSize, 20u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +524286));
  EXPECT_EQ(field(K, +524286), 0x7FFFEu);
  EXPECT_FALSE(ok(K, +524288));
  EXPECT_TRUE(ok(K, -524288));
  EXPECT_EQ(field(K, -524288) & 0xFFFFFu, 0x80000u);
  EXPECT_FALSE(ok(K, -524290));
  EXPECT_FALSE(ok(K, +1));
  EXPECT_STREQ(err(K, +1), "mis-aligned relocation target");
}

// Legacy HWLoopOffset placeholder: signed 16-bit after ÷4 (Align=4).
// Product SET_HWLOOP uses unsigned Off1/Off2; this pins the shared ÷4 scale.
TEST(HaydnRelocLayoutTest, LegacyHWLoopOffsetDiv4PositiveNegativeBounds) {
  const RelocKind K = RelocKind::HWLoopOffset;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 2u);
  EXPECT_EQ(FI.Align, 4u);
  EXPECT_EQ(FI.FieldSize, 16u);
  EXPECT_TRUE(FI.IsSigned);

  // Signed 16 after ÷4 → byte window [-131072, +131068].
  EXPECT_TRUE(ok(K, +131068));
  EXPECT_EQ(field(K, +131068), 32767u);
  EXPECT_FALSE(ok(K, +131072));
  EXPECT_TRUE(ok(K, -131072));
  EXPECT_EQ(field(K, -131072) & 0xFFFFu, 0x8000u);
  EXPECT_FALSE(ok(K, -131076));
  EXPECT_FALSE(ok(K, +2));
  EXPECT_STREQ(err(K, +2), "mis-aligned relocation target");
}

// Reader undoes ValueShift: branch byte, call byte, hwloop word addends.
TEST(HaydnRelocLayoutTest, ReadAddendUndoesBranchAndHwloopScale) {
  uint8_t Buf[8] = {};
  const RelocFieldInfo &Br = getRelocFieldInfo(RelocKind::WIDE_BranchSImm12);
  patchField(Buf, 100, Br.NBytes, Br.FieldSize, Br.FieldLsb); // field = 100
  EXPECT_EQ(readRelocAddend(RelocKind::WIDE_BranchSImm12, Buf), 100);

  // WIDE_Call is byte PC-relative (ValueShift=0): field value == addend.
  // Full 12-byte parcel buffer (NBytes=12); non-Format-E header keeps
  // resolveFieldLsb on the E2 table default FieldLsb=31.
  uint8_t CBuf[12] = {};
  const RelocFieldInfo &Call = getRelocFieldInfo(RelocKind::WIDE_CallSImm20);
  patchField(CBuf, 50, Call.NBytes, Call.FieldSize, Call.FieldLsb);
  EXPECT_EQ(readRelocAddend(RelocKind::WIDE_CallSImm20, CBuf), 50);

  // E3 e0 JAL (indicator=7, entry_num=1, map=2, type=14, opc=1, rt=15):
  // imm FieldLsb resolves to 17, not the E2 table default of 31.
  uint8_t E3[12] = {};
  E3[0] = 0x8f; // indicator 111, entry_num=1
  // map=2 @ bits[6:7] already in 0x8f (bits 6-7 = 10); type=14 @ [8:11],
  // opc=1 @ [12], dest=15 @ [13:16] → match encode of freestanding E3 JAL.
  E3[1] = 0xfe; // bits[8:15]
  E3[2] = 0x01; // bits[16:23] (imm low still zero)
  EXPECT_EQ(resolveFieldLsb(RelocKind::WIDE_CallSImm20, E3), 17u);
  patchField(E3, 36, Call.NBytes, Call.FieldSize, 17u);
  EXPECT_EQ(readRelocAddend(RelocKind::WIDE_CallSImm20, E3), 36);

  // E3 e0 RI12 BNE (indicator=7, entry_num=1, map=2, type=0xd):
  // imm FieldLsb resolves to 23, not the E2 table default of 32.
  uint8_t E3Br[12] = {};
  E3Br[0] = 0x8f; // indicator 111, entry_num=1, map=2 at bits[6:7]
  E3Br[1] = 0x0d; // type=0xd at bits[8:11]
  EXPECT_EQ(resolveFieldLsb(RelocKind::WIDE_BranchSImm12_RI, E3Br), 23u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::WIDE_BranchSImm12, E3Br), 23u);
  const RelocFieldInfo &Br12 = getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI);
  patchField(E3Br, 24, Br12.NBytes, Br12.FieldSize, 23u);
  EXPECT_EQ(readRelocAddend(RelocKind::WIDE_BranchSImm12_RI, E3Br), 24);

  uint8_t E3I12[12] = {};
  E3I12[0] = 0x8f;
  E3I12[1] = 0x0a; // type=0xa I12
  EXPECT_EQ(resolveFieldLsb(RelocKind::WIDE_BranchSImm12, E3I12), 23u);

  uint8_t HBuf[8] = {};
  const RelocFieldInfo &H1 = getRelocFieldInfo(RelocKind::HWLoopOff1);
  patchField(HBuf, 60, H1.NBytes, H1.FieldSize, H1.FieldLsb); // field = 60
  EXPECT_EQ(readRelocAddend(RelocKind::HWLoopOff1, HBuf), 240); // ×4
}

// HI12 FieldLsb is the committed LUI I12 window, not a single E2-e0 constant.
// E3 e0 ALU2 LUI (map=1, type=4, opc=1) is golden abs[32:21]. Patching the
// table LSB=32 writes only bit 32 of that field: hi12=1 becomes executed
// imm 0x800 (plat_extras freopen %hi12 of a VA ≥ 0x80000).
TEST(HaydnRelocLayoutTest, Hi12FieldLsbFollowsCommittedLuiWindow) {
  const RelocFieldInfo &HI = getRelocFieldInfo(RelocKind::HI12);
  EXPECT_EQ(HI.FieldLsb, 32u);
  EXPECT_EQ(HI.FieldSize, 12u);
  EXPECT_EQ(HI.NBytes, 12u);

  // E2 (entry_num=0): table window.
  uint8_t E2[12] = {};
  E2[0] = 0x07; // indicator 111, entry_num=0
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E2), 32u);

  // E3 e0 ALU2 LUI: header 0x4f (indicator 111, entry_num=1, map=1 @ bits[6:7]),
  // type=4 @ [8:11], opc=1 @ [12].
  uint8_t E3Alu2[12] = {};
  E3Alu2[0] = 0x4f;
  E3Alu2[1] = 0x14;
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3Alu2), 21u);
  patchField(E3Alu2, 1, HI.NBytes, HI.FieldSize, 21u);
  EXPECT_EQ(readField(E3Alu2, HI.NBytes, HI.FieldSize, 21u), 1u);
  // The E2 LSB must stay clear — that was the plat_extras mispatch.
  EXPECT_EQ(readField(E3Alu2, HI.NBytes, HI.FieldSize, 32u), 0u);

  // E3 e0 ALU0 LUI: map=2, type=0xa, opc=1 @ bits[16:18] → imm abs 23.
  uint8_t E3Alu0[12] = {};
  E3Alu0[0] = 0x8f;
  E3Alu0[1] = 0x0a;
  E3Alu0[2] = 0x01;
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3Alu0), 23u);
  patchField(E3Alu0, 1, HI.NBytes, HI.FieldSize, 23u);
  EXPECT_EQ(readField(E3Alu0, HI.NBytes, HI.FieldSize, 23u), 1u);

  // E3 e1 ALU1 LUI: map=1 type=4 at entry 37 → imm abs 54.
  uint8_t E3e1Alu1[12] = {};
  E3e1Alu1[0] = 0x0f;
  E3e1Alu1[4] = 0x20; // map LSB at bit 37
  E3e1Alu1[5] = 0x02; // type=4 at bits [39:42]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e1Alu1), 54u);
  patchField(E3e1Alu1, 1, HI.NBytes, HI.FieldSize, 54u);
  EXPECT_EQ(readField(E3e1Alu1, HI.NBytes, HI.FieldSize, 54u), 1u);
  // e1 map/type live in bits [37:42], which overlap the E2 LSB=32 window;
  // do not require that window to read 0.

  // E3 e1 ALU0 LUI: map=2 type=0xa at entry 37 → imm abs 54.
  uint8_t E3e1Alu0[12] = {};
  E3e1Alu0[0] = 0x0f;
  E3e1Alu0[4] = 0x40; // map=2 at bits [37:38]
  E3e1Alu0[5] = 0x05; // type=0xa at bits [39:42]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e1Alu0), 54u);

  // E3 e2 ALU2 LUI: map=1 type=4 at entry 68 → imm abs 83.
  uint8_t E3e2Alu2[12] = {};
  E3e2Alu2[0] = 0x0f;
  E3e2Alu2[8] = 0x10; // map LSB at bit 68
  E3e2Alu2[9] = 0x01; // type=4 at bits [70:73]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e2Alu2), 83u);
  patchField(E3e2Alu2, 1, HI.NBytes, HI.FieldSize, 83u);
  EXPECT_EQ(readField(E3e2Alu2, HI.NBytes, HI.FieldSize, 83u), 1u);
  EXPECT_EQ(readField(E3e2Alu2, HI.NBytes, HI.FieldSize, 32u), 0u);

  // E3 e2 ALU0 LUI: map=2 type=0xa at entry 68 → imm abs 81.
  uint8_t E3e2Alu0[12] = {};
  E3e2Alu0[0] = 0x0f;
  E3e2Alu0[8] = 0xa0; // map=2 + type bit1 at [68:71]
  E3e2Alu0[9] = 0x02; // type bit3 at bit 73
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e2Alu0), 81u);
  patchField(E3e2Alu0, 1, HI.NBytes, HI.FieldSize, 81u);
  EXPECT_EQ(readField(E3e2Alu0, HI.NBytes, HI.FieldSize, 81u), 1u);
}

// JALRSImm12 (RI12 type-opcode 1): dedicated row for the JALR symbolic
// imm12. Same golden E2 e0 field numbers as the RI12 branch row (imm12 @
// bits[43:32], signed byte displacement from parcel origin, Align=2) but a
// DISTINCT kind so a JALR fixup can never borrow the branch row.
// findFixupFromFixupFields must route RI12 opc 1 here and opc 2..7 to the
// branch row. Pinned byte-displacement semantics: branch-all.s 0x114 ->
// target1@0 = -276 = 0xEEC in the imm12 field.
TEST(HaydnRelocLayoutTest, JalrSImm12DedicatedRowNotBranchAlias) {
  const RelocKind K = RelocKind::JALRSImm12;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  const RelocFieldInfo &Br = getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_NE(K, RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_EQ(FI.FieldLsb, 32u);
  EXPECT_EQ(FI.FieldLsb, Br.FieldLsb);
  EXPECT_EQ(FI.FieldSize, 12u);
  EXPECT_EQ(FI.NBytes, 12u);
  EXPECT_EQ(FI.ValueShift, 0u); // byte displacement, no extra scale
  EXPECT_EQ(FI.Align, 2u);
  EXPECT_TRUE(FI.IsSigned);
  EXPECT_TRUE(FI.IsPCRel);
  EXPECT_TRUE(isRelocTransformReady(K));

  // Signed 12-bit byte window [-2048, +2046] even — same bounds math as
  // the branch row, but reachable only through the dedicated kind.
  EXPECT_TRUE(ok(K, +2046));
  EXPECT_EQ(field(K, -276) & 0xFFFu, 0xEECu); // branch-all.s 0x114 → 0x0
  EXPECT_FALSE(ok(K, +2048));
  EXPECT_TRUE(ok(K, -2048));
  EXPECT_FALSE(ok(K, -2050));
  EXPECT_FALSE(ok(K, +1));
  EXPECT_STREQ(err(K, +1), "mis-aligned relocation target");

  // Generated-schema routing: RI12 opc 1 → JALRSImm12; opc 2..7 → branch.
  const FixupField Imm12{kUnspecifiedFieldLsb, 12};
  EXPECT_EQ(findFixupFromFixupFields("RI12", 1, Imm12, 12, false),
            RelocKind::JALRSImm12);
  EXPECT_EQ(findFixupFromFixupFields("RI12", 2, Imm12, 12, false),
            RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_EQ(findFixupFromFixupFields("RI12", 7, Imm12, 12, false),
            RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_EQ(static_cast<unsigned>(RelocKind::JALRSImm12),
            static_cast<unsigned>(ELF::R_HAYDN_JALRSImm12));
  EXPECT_EQ(mapRelocKindToFixup(RelocKind::JALRSImm12),
            Haydn::FIXUP_HAYDN_JALRSImm12);
  EXPECT_EQ(mapFixupKind(Haydn::FIXUP_HAYDN_JALRSImm12), RelocKind::JALRSImm12);

  // E3 e0 RI12 JALR (map=2, type=0xd): same 23/54 windows as the branch
  // kinds — identical golden RI12 entry geometry.
  uint8_t E3[12] = {};
  E3[0] = 0x8f; // indicator 111, entry_num=1, map=2 at bits[6:7]
  E3[1] = 0x0d; // type=0xd at bits[8:11]
  EXPECT_EQ(resolveFieldLsb(RelocKind::JALRSImm12, E3), 23u);
  patchField(E3, 24, FI.NBytes, FI.FieldSize, 23u);
  EXPECT_EQ(readRelocAddend(RelocKind::JALRSImm12, E3), 24);
}

// CSR_UImm8 (I8 type-opcodes 4/5 = CSRR/CSRW): dedicated row for the
// reloc CSRW_W / CSRR uimm8. Golden E2 e0 imm @ bits[39:32]; unsigned
// 8-bit, ValueShift=0, Align=1, not PC-relative. Distinct from Data8
// (1-byte data image). Other I8 opcodes (NOP/ZERO_*) have no reloc row.
TEST(HaydnRelocLayoutTest, CsrUImm8TypedRowNotData8) {
  const RelocKind K = RelocKind::CSR_UImm8;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_NE(K, RelocKind::Data8);
  EXPECT_EQ(FI.FieldLsb, 32u);
  EXPECT_EQ(FI.FieldSize, 8u);
  EXPECT_EQ(FI.NBytes, 12u);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 1u);
  EXPECT_FALSE(FI.IsSigned);
  EXPECT_FALSE(FI.IsPCRel);
  EXPECT_TRUE(isRelocTransformReady(K));

  EXPECT_TRUE(ok(K, 0));
  EXPECT_TRUE(ok(K, 10));
  EXPECT_TRUE(ok(K, 255));
  EXPECT_EQ(field(K, 10), 10u);
  EXPECT_FALSE(ok(K, 256));
  EXPECT_STREQ(err(K, 256), "relocation offset out of range");

  const FixupField Imm8{kUnspecifiedFieldLsb, 8};
  EXPECT_EQ(findFixupFromFixupFields("I8", 4, Imm8, 12, false),
            RelocKind::CSR_UImm8);
  EXPECT_EQ(findFixupFromFixupFields("I8", 5, Imm8, 12, false),
            RelocKind::CSR_UImm8);
  EXPECT_EQ(findFixupFromFixupFields("I8", 0, Imm8, 12, false),
            RelocKind::Invalid);
  EXPECT_EQ(findFixupFromFixupFields("I8", 1, Imm8, 12, false),
            RelocKind::Invalid);
  EXPECT_EQ(static_cast<unsigned>(RelocKind::CSR_UImm8),
            static_cast<unsigned>(ELF::R_HAYDN_CSR_UImm8));
  EXPECT_EQ(mapRelocKindToFixup(RelocKind::CSR_UImm8),
            Haydn::FIXUP_HAYDN_CSR_UImm8);
  EXPECT_EQ(mapFixupKind(Haydn::FIXUP_HAYDN_CSR_UImm8), RelocKind::CSR_UImm8);

  // E3 e0 ALU0 I8: map=2, type=3 → imm abs 23.
  uint8_t E3Alu0[12] = {};
  E3Alu0[0] = 0x8f; // indicator 111, entry_num=1, map=2 at bits[6:7]
  E3Alu0[1] = 0x03; // type=3 at bits[8:11]
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E3Alu0), 23u);
  patchField(E3Alu0, 10, FI.NBytes, FI.FieldSize, 23u);
  EXPECT_EQ(readRelocAddend(RelocKind::CSR_UImm8, E3Alu0), 10);

  // E3 e0 ALU2 I8: map=1, type=1 → imm abs 27.
  uint8_t E3Alu2[12] = {};
  E3Alu2[0] = 0x4f; // indicator 111, entry_num=1, map=1 at bits[6:7]
  E3Alu2[1] = 0x01; // type=1 at bits[8:11]
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E3Alu2), 27u);
  patchField(E3Alu2, 10, FI.NBytes, FI.FieldSize, 27u);
  EXPECT_EQ(readRelocAddend(RelocKind::CSR_UImm8, E3Alu2), 10);
}

// W25 / encoding F15 residual: LO20/PC_LO20 (ALU RI20, E2-only type) and
// LS_IMM (LS RI6) FieldLsb must follow the committed Format E entry window.
// The table FieldLsb (LO20 31, LS_IMM 28) is E2 e0 authority only; a symbolic
// ADDI32 at E2 e1 ALU1 or S_LW_WITH_IMM at E3 e0/e1/e2 / E2 e1 LOAD1 used to
// patch the E2-e0 window — writing a neighbor entry's bits and leaving the
// executed imm zero in BOTH MC applyFixup and lld relocate (shared
// resolveFieldLsb). Golden absolute parcel bits:
//   RI20: E2 e0 ALU0 imm[50:31] @31; E2 e1 ALU1 imm[84:65] @65
//   RI6:  E2 e0 LS0 imm[33:28] @28; E2 e1 LOAD1 imm[77:72] @72;
//         E3 e0 LS0 imm[30:25] @25; E3 e1 LOAD1 imm[59:54] @54;
//         E3 e2 LOAD1 imm[90:85] @85
// Buffer bytes are the live encodes of the bundles named at each site.
TEST(HaydnRelocLayoutTest, Lo20AndLsImmFieldLsbFollowEntryWindow) {
  const RelocFieldInfo &LO = getRelocFieldInfo(RelocKind::LO20);
  EXPECT_EQ(LO.FieldLsb, 31u); // E2 e0 table default
  EXPECT_EQ(LO.NBytes, 12u);   // must cover e1 imm past bit 63

  // E2 e1 ALU1 RI20: `{ xor32 r0,r0,r0; addi32 r1,r2,imm }`
  // (e0 = XOR32 ALU0 RR, e1 = ADDI32 ALU1 RI20; map[52:51]=0, type@53=0).
  uint8_t E2e1RI20[12] = {0x07, 0x4b, 0x81, 0x88, 0x00, 0x00,
                          0x00, 0x43, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LO20, E2e1RI20), 65u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::PC_LO20, E2e1RI20), 65u);
  patchField(E2e1RI20, 12, LO.NBytes, LO.FieldSize, 65u);
  EXPECT_EQ(readRelocAddend(RelocKind::LO20, E2e1RI20), 12);

  // E2 e0 RI20 (`{ addi32 rt, rs, imm }` solo — live encode; e1 is a NOP
  // with zero map/type, so the e0 RI20 discriminator must win): table 31.
  uint8_t E2e0RI20[12] = {0x07, 0x0f, 0x12, 0x82, 0x02, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LO20, E2e0RI20), 31u);

  const RelocFieldInfo &LS = getRelocFieldInfo(RelocKind::LS_IMM);
  EXPECT_EQ(LS.FieldLsb, 28u); // E2 e0 table default
  EXPECT_EQ(LS.NBytes, 12u);   // must cover E3 e2 imm @85

  // E2 e1 LOAD1 RI6: `{ s_lw_with_imm r5,r6,imm; xor32 r8,r8,r8 }`
  // (map[52:51]=2, 2-bit type @53=1).
  uint8_t E2e1RI6[12] = {0x07, 0x4b, 0x81, 0x88, 0x00, 0x00,
                         0x30, 0xd0, 0x65, 0x00, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LS_IMM, E2e1RI6), 72u);
  patchField(E2e1RI6, 12, LS.NBytes, LS.FieldSize, 72u);
  EXPECT_EQ(readRelocAddend(RelocKind::LS_IMM, E2e1RI6), 12);

  // E3 e0 LOADSTORE0 RI6: `{ xor32 r8; xor32 r9; s_lw_with_imm r1,r2,imm }`
  // (map[7:6]=3, 3-bit type @8=3).
  uint8_t E3e0RI6[12] = {0xcf, 0x6b, 0x42, 0x00, 0xa0, 0x74,
                         0x64, 0x26, 0x50, 0x3a, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LS_IMM, E3e0RI6), 25u);
  patchField(E3e0RI6, 12, LS.NBytes, LS.FieldSize, 25u);
  EXPECT_EQ(readRelocAddend(RelocKind::LS_IMM, E3e0RI6), 12);

  // E3 e1 LOAD1 RI6: `{ xor32 r8; s_lw_with_imm r3,r4,12; xor32 r9 }`
  // (map[38:37]=3, 2-bit type @39=1).
  uint8_t E3e1RI6[12] = {0x4f, 0xe9, 0xc8, 0x4c, 0xe0, 0xf4,
                         0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LS_IMM, E3e1RI6), 54u);
  patchField(E3e1RI6, 12, LS.NBytes, LS.FieldSize, 54u);
  EXPECT_EQ(readRelocAddend(RelocKind::LS_IMM, E3e1RI6), 12);

  // E3 e2 LOAD1 RI6: `{ s_lw_with_imm r1,r2,imm; xor32 r8; xor32 r9 }`
  // (map[69:68]=3, 2-bit type @70=1).
  uint8_t E3e2RI6[12] = {0x4f, 0xe9, 0xc8, 0x4c, 0xa0, 0x74,
                         0x20, 0x22, 0x70, 0x3a, 0x00, 0x00};
  EXPECT_EQ(resolveFieldLsb(RelocKind::LS_IMM, E3e2RI6), 85u);
  patchField(E3e2RI6, 12, LS.NBytes, LS.FieldSize, 85u);
  EXPECT_EQ(readRelocAddend(RelocKind::LS_IMM, E3e2RI6), 12);

  // E2 e0 LOADSTORE0 RI6 (`ld32 rt, rs, imm`, live encode prefix
  // 87 43 03 01 …): map[7:6]=2 (golden "10"), 3-bit type @8=3 → default 28.
  uint8_t E2e0RI6[12] = {};
  E2e0RI6[0] = 0x87;
  E2e0RI6[1] = 0x43;
  EXPECT_EQ(resolveFieldLsb(RelocKind::LS_IMM, E2e0RI6), 28u);

  // Fail-closed: RI20 is an E2-only type — an E3 parcel keeps the table
  // default rather than inventing an E3 window.
  uint8_t E3RI20[12] = {};
  E3RI20[0] = 0x8f;
  EXPECT_EQ(resolveFieldLsb(RelocKind::LO20, E3RI20), 31u);
}

// Typed (mode, entry, unit) FieldLsb must match the Loc-sniffing windows
// already returned by resolveFieldLsb. findFixupFromFixupFields used to
// require Fields[0].Offset == E2 e0 table FieldLsb, so an E3 e0/e1 JALR
// member (LSB 23/54) missed the dedicated ELF 22 row. AIE looks up by the
// actual FixupField Offset (AIEMCFixupKinds.cpp:36-65); Haydn keeps one
// ELF kind per equation and accepts every published parcel-absolute LSB.
TEST(HaydnRelocLayoutTest, PublishedMemberFieldLsbAndFixupFields) {
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::JALRSImm12, 32u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::JALRSImm12, 23u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::JALRSImm12, 54u));
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::JALRSImm12, 81u));
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::JALRSImm12, 99u));

  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 32u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 21u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 23u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 54u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 81u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HI12, 83u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::PC_LO20, 31u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::PC_LO20, 65u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::LO20, 65u));
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::PC_LO20, 23u));

  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 0, 0), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 0, 2), 21u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 0, 0), 23u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 1), 54u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 2, 2), 83u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 2, 0), 81u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::LO20, 0, 0, 0), 31u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::LO20, 0, 1, 1), 65u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::PC_LO20, 0, 1, 1), 65u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::PC_LO20, 1, 0, 0), 31u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 0, 0, 0), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 1, 0, 0), 23u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 1, 1, 0), 54u);
  // JALR has no E3 e2 member — fail closed to the E2 e0 table window.
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 1, 2, 0), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::WIDE_BranchSImm12, 1, 2), 81u);

  const FixupField JalrE3e0{23, 12};
  const FixupField JalrE3e1{54, 12};
  const FixupField JalrE3e2{81, 12};
  const FixupField Hi12E3Alu2{21, 12};
  EXPECT_EQ(findFixupFromFixupFields("RI12", 1, JalrE3e0, 12, false),
            RelocKind::JALRSImm12);
  EXPECT_EQ(findFixupFromFixupFields("RI12", 1, JalrE3e1, 12, false),
            RelocKind::JALRSImm12);
  EXPECT_EQ(findFixupFromFixupFields("RI12", 1, JalrE3e2, 12, false),
            RelocKind::Invalid);
  EXPECT_EQ(findFixupFromFixupFields("RI12", 2, JalrE3e0, 12, false),
            RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_EQ(findFixupFromFixupFields("I12", 1, Hi12E3Alu2, 12, false),
            RelocKind::HI12);
  EXPECT_EQ(findFixupFromFixupFields("I12", 4, Hi12E3Alu2, 12, false),
            RelocKind::Invalid);

  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data32PCRel),
            static_cast<unsigned>(ELF::R_HAYDN_32_PCREL));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::GOT_HI20),
            static_cast<unsigned>(ELF::R_HAYDN_GOT_HI20));
  EXPECT_NE(RelocKind::Data32PCRel, RelocKind::GOT_HI20);
  EXPECT_NE(RelocKind::Data32PCRel, RelocKind::Data32);
}

// Shared ELF 0..23 coverage: RelocKind values are the ELF R_HAYDN_* numbers.
// Symbolic JALR stays ELF 22; do not remint or alias the RI12 branch row or
// PIC/JT label-diff. CSR_UImm8 (ELF 23) is owned elsewhere.
TEST(HaydnRelocLayoutTest, SharedRelocKindMatchesElfCoverage) {
  EXPECT_EQ(static_cast<unsigned>(RelocKind::None), unsigned(ELF::R_HAYDN_NONE));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data32), unsigned(ELF::R_HAYDN_32));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::SImm16),
            unsigned(ELF::R_HAYDN_SImm16));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::BranchSImm16),
            unsigned(ELF::R_HAYDN_BranchSImm16));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::CallSImm20),
            unsigned(ELF::R_HAYDN_CallSImm20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::HI20), unsigned(ELF::R_HAYDN_HI20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::LO16), unsigned(ELF::R_HAYDN_LO16));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data32PCRel),
            unsigned(ELF::R_HAYDN_32_PCREL));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::GOT_HI20),
            unsigned(ELF::R_HAYDN_GOT_HI20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::TPREL_HI20),
            unsigned(ELF::R_HAYDN_TPREL_HI20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::TPREL_LO16),
            unsigned(ELF::R_HAYDN_TPREL_LO16));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data8), unsigned(ELF::R_HAYDN_8));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data16), unsigned(ELF::R_HAYDN_16));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::HI12), unsigned(ELF::R_HAYDN_HI12));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::LO20), unsigned(ELF::R_HAYDN_LO20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::PC_LO20),
            unsigned(ELF::R_HAYDN_PC_LO20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::HWLoopOff1),
            unsigned(ELF::R_HAYDN_HWLoopOff1));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::HWLoopOff2),
            unsigned(ELF::R_HAYDN_HWLoopOff2));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::WIDE_BranchSImm12),
            unsigned(ELF::R_HAYDN_WIDE_BranchSImm12));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::WIDE_CallSImm20),
            unsigned(ELF::R_HAYDN_WIDE_CallSImm20));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::WIDE_BranchSImm12_RI),
            unsigned(ELF::R_HAYDN_WIDE_BranchSImm12_RI));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::LS_IMM),
            unsigned(ELF::R_HAYDN_LS_IMM));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::JALRSImm12),
            unsigned(ELF::R_HAYDN_JALRSImm12));
  EXPECT_EQ(static_cast<unsigned>(RelocKind::CSR_UImm8),
            unsigned(ELF::R_HAYDN_CSR_UImm8));

  EXPECT_EQ(static_cast<unsigned>(RelocKind::JALRSImm12), 22u);
  EXPECT_EQ(static_cast<unsigned>(ELF::R_HAYDN_JALRSImm12), 22u);
  EXPECT_NE(RelocKind::JALRSImm12, RelocKind::WIDE_BranchSImm12_RI);
  EXPECT_NE(RelocKind::JALRSImm12, RelocKind::WIDE_BranchSImm12);
  EXPECT_NE(RelocKind::JALRSImm12, RelocKind::Data32PCRel);
  EXPECT_EQ(static_cast<unsigned>(RelocKind::Data32PCRel), 7u);
  EXPECT_EQ(static_cast<unsigned>(RelocKind::CSR_UImm8), 23u);
  // MC-only kinds sit after the shared ELF range.
  EXPECT_GT(static_cast<unsigned>(RelocKind::C_BranchSImm4), 23u);
}

// Nine-file layout authority for JALR FieldLsb windows + provisional object
// identity. Hashes match FormatE/GOLDEN_INPUTS.sha256 / STATUS. EM_HAYDN=259
// collides with Kalray KVX; distinguisher stays EF_HAYDN_E96=0x1.
TEST(HaydnRelocLayoutTest, NineFileHashAndProvisionalObjectIdentity) {
  EXPECT_STREQ(FormatEXLSXSHA256,
               "dd8491b7c182d006ad7d05c8cd46f64c02f439ae41bad0416f7139703d07b76f");
  EXPECT_STREQ(FormatEJSONSHA256,
               "2609877075156dd9749e1e8dd0b45ff1ef326dae2e1c2a9c10fbd9cd1c1c8f6a");
  static constexpr const char *kNineFile[][2] = {
      {"format_e_bit_layout_v2_1.xlsx",
       "dd8491b7c182d006ad7d05c8cd46f64c02f439ae41bad0416f7139703d07b76f"},
      {"format_e_bit_layout_v2_1.json",
       "2609877075156dd9749e1e8dd0b45ff1ef326dae2e1c2a9c10fbd9cd1c1c8f6a"},
      {"format_e_canonical_vectors_v1.json",
       "741f5b4141990dc27dc217d2b0c0d7ab57240e08ef31c34bc036f11bbda1938e"},
      {"instruction_type_index.json",
       "7a13453ad934d6be9a303b51fcaeb6e908d97015b3e75db7d76fa13cb7a6dede"},
      {"operands_info.md",
       "e4b61bf5b5be2634b1665474bf4906db0df017a939289a49d40e82bc2121fb12"},
      {"instruction_type_operands.json",
       "0f97fdf5ecf56172190fa21aeb22049a0a0cace28640314e3209623a167413e7"},
      {"instruction_to_entry.xlsx#cells",
       "6b084277e2b92a5166feb06cad7050651f2b06cf99138c885ce9e9da9e7cdb6c"},
      {"VLIW_Engine_Compiler_Constraints.md",
       "e0d7f7f0e7ce06622f4ae90dc9366caf16da48993c7f803c02d460473fd9b56a"},
      {"VLIW_Engine_Reference_Manual.docx",
       "550dac0c82c160397c510bd403116056e046a43d8df41cd34d81ab678cd9b49b"},
  };
  EXPECT_EQ(sizeof(kNineFile) / sizeof(kNineFile[0]), 9u);
  EXPECT_STREQ(kNineFile[0][1], FormatEXLSXSHA256);
  EXPECT_STREQ(kNineFile[1][1], FormatEJSONSHA256);
  EXPECT_EQ(ELF::EM_HAYDN, 259u);
  EXPECT_EQ(ELF::EF_HAYDN_E96, 0x1u);
  EXPECT_NE(ELF::EM_HAYDN, 0u);
  EXPECT_NE(ELF::EF_HAYDN_E96, 0u);

  const RelocFieldInfo &Jalr = getRelocFieldInfo(RelocKind::JALRSImm12);
  EXPECT_EQ(Jalr.ValueShift, 0u);
  EXPECT_EQ(Jalr.FieldSize, 12u);
  EXPECT_EQ(Jalr.FieldLsb, 32u);
  EXPECT_EQ(Jalr.Align, 2u);
  EXPECT_TRUE(Jalr.IsSigned);
  EXPECT_TRUE(Jalr.IsPCRel);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 0, 0, 0), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 1, 0, 0), 23u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::JALRSImm12, 1, 1, 0), 54u);
}

} // namespace
