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

#include "MCTargetDesc/HaydnRelocLayout.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::HaydnReloc;

namespace {

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
//   LO20 / PC_LO20 @31 (bits[31:50]); LUI HI12 @32; NBytes=8
// WIDE_Call is byte PC-relative (ValueShift=0); branches are also byte (GE96-03).
// E3 call windows are resolved dynamically via resolveFieldLsb(Loc).
TEST(HaydnRelocLayoutTest, FormatEE2E0FieldLsbParcelOrigin) {
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_BranchSImm12_RI).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_CallSImm20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::WIDE_CallSImm20).NBytes, 12u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::HI12).FieldLsb, 32u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::HI12).NBytes, 8u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LO20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::PC_LO20).FieldLsb, 31u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).FieldLsb, 28u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).FieldSize, 6u);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::LS_IMM).NBytes, 6u);
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

// WIDE_CallSImm20: signed 20-bit PC-relative BYTE field (ValueShift=0).
// Effective window [-524288, +524287]. Distinct from WIDE_BranchSImm12 (byte simm12).
TEST(HaydnRelocLayoutTest, WideCallDiv2PositiveNegativeBounds) {
  const RelocKind K = RelocKind::WIDE_CallSImm20;
  const RelocFieldInfo &FI = getRelocFieldInfo(K);
  EXPECT_EQ(FI.ValueShift, 0u);
  EXPECT_EQ(FI.Align, 1u);
  EXPECT_EQ(FI.FieldSize, 20u);
  EXPECT_TRUE(FI.IsSigned);

  EXPECT_TRUE(ok(K, +524287));
  EXPECT_EQ(field(K, +524287), 0x7FFFFu);
  EXPECT_FALSE(ok(K, +524288));
  EXPECT_TRUE(ok(K, -524288));
  EXPECT_EQ(field(K, -524288) & 0xFFFFFu, 0x80000u);
  EXPECT_FALSE(ok(K, -524289));
  // Byte scale: odd offsets are legal (no halfword align gate).
  EXPECT_TRUE(ok(K, +1));
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

} // namespace
