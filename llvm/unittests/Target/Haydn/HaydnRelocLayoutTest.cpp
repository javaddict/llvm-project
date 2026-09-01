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

  // E3 e1 ALU1 LUI: map=1 type=4 opc=1(bit49) at entry 37 → imm abs 54.
  uint8_t E3e1Alu1[12] = {};
  E3e1Alu1[0] = 0x0f;
  E3e1Alu1[4] = 0x20; // map LSB at bit 37
  E3e1Alu1[5] = 0x02; // type=4 at bits [39:42]
  E3e1Alu1[6] = 0x02; // opc=1 at bit 49 (D1.17 pin)
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e1Alu1), 54u);
  patchField(E3e1Alu1, 1, HI.NBytes, HI.FieldSize, 54u);
  EXPECT_EQ(readField(E3e1Alu1, HI.NBytes, HI.FieldSize, 54u), 1u);
  // e1 map/type live in bits [37:42], which overlap the E2 LSB=32 window;
  // do not require that window to read 0.

  // E3 e1 ALU0 LUI: map=2 type=0xa opc=1(bit47) at entry 37 → imm abs 54.
  uint8_t E3e1Alu0[12] = {};
  E3e1Alu0[0] = 0x0f;
  E3e1Alu0[4] = 0x40; // map=2 at bits [38:37]
  E3e1Alu0[5] = 0x05; // type=0xa at [42:39]
  E3e1Alu0[5] = 0x85; // + opc=1 at bit 47 (D1.17 pin)
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e1Alu0), 54u);

  // E3 e2 ALU2 LUI: map=1 type=4 opc=1(bit74) at entry 68 → imm abs 83.
  uint8_t E3e2Alu2[12] = {};
  E3e2Alu2[0] = 0x0f;
  E3e2Alu2[8] = 0x10; // map LSB at bit 68
  E3e2Alu2[9] = 0x05; // type=4 @ bit72 + opc=1 @ bit 74 (D1.17 pin)
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E3e2Alu2), 83u);
  patchField(E3e2Alu2, 1, HI.NBytes, HI.FieldSize, 83u);
  EXPECT_EQ(readField(E3e2Alu2, HI.NBytes, HI.FieldSize, 83u), 1u);
  EXPECT_EQ(readField(E3e2Alu2, HI.NBytes, HI.FieldSize, 32u), 0u);

  // E3 e2 ALU0 LUI: map=2 type=0xa opc=1(bit74) at entry 68 → imm abs 81.
  uint8_t E3e2Alu0[12] = {};
  E3e2Alu0[0] = 0x0f;
  E3e2Alu0[8] = 0xa0; // map=2 @ bit69 + type bit @71
  E3e2Alu0[9] = 0x06; // type bit3 @73 + opc=1 @ bit74 (D1.17 pin)
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

  // E3 e0 ALU0 I8 CSRR: map=2, type=3, opc=4 (D1.17 pin) → imm abs 23.
  uint8_t E3Alu0[12] = {};
  E3Alu0[0] = 0x8f; // indicator 111, entry_num=1, map=2 at bits[6:7]
  E3Alu0[1] = 0x03; // type=3 at bits[8:11]
  E3Alu0[2] = 0x01; // opc=1 at bit 16 → ZERO_GPR: pin refuses
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E3Alu0),
            getRelocFieldInfo(RelocKind::CSR_UImm8).FieldLsb);
  E3Alu0[2] = 0x04; // opc=4 (CSRR) at bits[18:16]
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E3Alu0), 23u);
  patchField(E3Alu0, 10, FI.NBytes, FI.FieldSize, 23u);
  EXPECT_EQ(readRelocAddend(RelocKind::CSR_UImm8, E3Alu0), 10);

  // E3 e0 ALU2 I8 CSRW: map=1, type=1, opc=5 (D1.17 pin) → imm abs 27.
  uint8_t E3Alu2[12] = {};
  E3Alu2[0] = 0x4f; // indicator 111, entry_num=1, map=1 at bits[6:7]
  E3Alu2[1] = 0x01; // type=1 at bits[8:11]
  E3Alu2[2] = 0x05; // opc=5 (CSRW) at bits[18:16]
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
// already returned by resolveFieldLsb. FieldLsbSites / ExtraPublishedLsb
// are generated (HaydnGenRelocFieldLsb.inc); these windows are the
// generator --check ratchet. findFixupFromFixupFields used to require
// Fields[0].Offset == E2 e0 table FieldLsb, so an E3 e0/e1 JALR member
// (LSB 23/54) missed the dedicated ELF 22 row. AIE looks up by the
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
  // ExtraPublishedLsb: HWLRIII shares (mode, entry) with HWLRIIR (F2).
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HWLoopOff1, 32u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HWLoopOff1, 13u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HWLoopOff2, 38u));
  EXPECT_TRUE(isPublishedFieldLsb(RelocKind::HWLoopOff2, 36u));
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::HWLoopOff1, 99u));

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

// D1.12: pin the E2 HWLRIII (non-F2 SET_HWLOOP) branch of the
// resolveFieldLsb Loc sniff (HaydnRelocLayout.cpp E2 e0 Type==0x10 →
// Off1@13 / Off2@36). Before this pin the branch was traversed only
// incidentally by the lld arm of hwloop-offset-reloc-shift.s; nothing
// named it, and the MC assemble-time (applyFixup) path had zero
// coverage (d486/cb90 are F2/0x0c only). Golden:
// format_e_bit_layout_v2_2.json entry_num_0.entry0.ALU0.HWLRIII —
// type_code_bin 10000, imm1(uimm6_offset1) bit[18:13],
// imm2(uimm12_offset2) bit[47:36].
//
// Buffers are hand-built headers, not transcripts: byte0 bit3 is
// entry_num (0=E2), byte1 bits[4:0] is the 5-bit entry type at parcel
// bits[12:8]. A sniff regression that falls back to the F2 default
// would return 32/38 here — these EXPECT_EQs fail loudly instead.
TEST(HaydnRelocLayoutTest, HWLoopOffFieldLsbE2HWLRIIIBranch) {
  const RelocFieldInfo &O1 = getRelocFieldInfo(RelocKind::HWLoopOff1);
  const RelocFieldInfo &O2 = getRelocFieldInfo(RelocKind::HWLoopOff2);

  // E2 e0 HWLRIII: header 0x07 (indicator 111, entry_num=0), Type=0x10
  // at bits[12:8] → byte1 = 0x10.
  uint8_t HWLRIII[12] = {};
  HWLRIII[0] = 0x07;
  HWLRIII[1] = 0x10;
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff1, HWLRIII), 13u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff2, HWLRIII), 36u);
  // The two windows are distinct: 36-13 >= 12 so Off2 clears Off1.
  EXPECT_GE(36u - 13u, O1.FieldSize);

  // E2 e0 HWLRIIR (F2, Type=0x0c): table default windows 32/38.
  uint8_t HWLRIIR[12] = {};
  HWLRIIR[0] = 0x07;
  HWLRIIR[1] = 0x0c;
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff1, HWLRIIR), 32u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff2, HWLRIIR), 38u);

  // Round-trip on a live 0x10 parcel: patch 3/6 (12/24 bytes after ÷4
  // scale) then read the addend back through ValueShift=2.
  patchField(HWLRIII, 3, O1.NBytes, O1.FieldSize, 13u);
  patchField(HWLRIII, 6, O2.NBytes, O2.FieldSize, 36u);
  EXPECT_EQ(readField(HWLRIII, O1.NBytes, O1.FieldSize, 13u), 3u);
  EXPECT_EQ(readField(HWLRIII, O2.NBytes, O2.FieldSize, 36u), 6u);
  EXPECT_EQ(readRelocAddend(RelocKind::HWLoopOff1, HWLRIII), 12); // ×4
  EXPECT_EQ(readRelocAddend(RelocKind::HWLoopOff2, HWLRIII), 24); // ×4
  // The non-overlapping part of the F2-default Off1 window (bits[35:32];
  // bits 36-37 belong to the HWLRIII Off2 field) must stay clear — a sniff
  // regression that patched 32/38 writes imm3/cnt bits instead.
  EXPECT_EQ(readField(HWLRIII, O1.NBytes, 4u, 32u), 0u);

  // Generated member windows (HaydnGenRelocFieldLsb.inc): E3 F2 e0/e1
  // and E2 e0 F2 defaults. resolveFieldLsbForMember is the typed-API
  // twin of the sniff — pin both so the D1.17 producer-side switch to
  // the typed API cannot drift from the Loc windows.
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff1, 0, 0), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff2, 0, 0), 38u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff1, 1, 0), 18u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff2, 1, 0), 24u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff1, 1, 1), 49u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff2, 1, 1), 55u);
  // No E3 e2 site: fail closed to the E2 e0 table window.
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff1, 1, 2), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HWLoopOff2, 1, 2), 38u);

  // E3 F2 sniff sites match the typed rows: map=2 @ entry+0 (2b),
  // type=0xc @ entry+2 (4b). e0 entry lo = abs bit 6; e1 = abs bit 37.
  uint8_t E3F2e0[12] = {};
  E3F2e0[0] = 0x8f;  // indicator 111, entry_num=1, map[7:6]=2
  E3F2e0[1] = 0x0c;  // type=0xc at bits[11:8]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff1, E3F2e0), 18u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff2, E3F2e0), 24u);

  uint8_t E3F2e1[12] = {};
  E3F2e1[0] = 0x0f;  // indicator 111, entry_num=1
  E3F2e1[4] = 0x40;  // map=2 at bits[38:37]
  E3F2e1[5] = 0x06;  // type=0xc at bits[42:39]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff1, E3F2e1), 49u);
  EXPECT_EQ(resolveFieldLsb(RelocKind::HWLoopOff2, E3F2e1), 55u);
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
               "2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb");
  EXPECT_STREQ(FormatEJSONSHA256,
               "c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba");
  static constexpr const char *kNineFile[][2] = {
      {"format_e_bit_layout_v2_2.xlsx",
       "2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb"},
      {"format_e_bit_layout_v2_2.json",
       "c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba"},
      {"format_e_canonical_vectors_v1.json",
       "741f5b4141990dc27dc217d2b0c0d7ab57240e08ef31c34bc036f11bbda1938e"},
      {"instruction_type_index.json",
       "3f306463d108c8240b6f8afa876e3fa4ca06b91ee7f38efe1fb4eba631c3433b"},
      {"operands_info.md",
       "e4b61bf5b5be2634b1665474bf4906db0df017a939289a49d40e82bc2121fb12"},
      {"instruction_type_operands.json",
       "434544ef336fe703ff69c6c59316c3e89f3350e1d4cae6fd6790929d01e7bd20"},
      {"instruction_to_entry.xlsx#cells",
       "ba6d65066b812083925ec5b68dae6dee26323ceb8aaab0040db0ff083f5d3098"},
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

// D1.17: HI12 Loc-sniff opc pins. Map/type alone is NOT member-unique on
// the ALU0 I12 sites (LUI opc=1 shares with BEQZ..BLTZ opc=4..7; layout
// 123 at E3 e2 carries the same sharing) — the pre-fix sniff matched a
// BRANCH member and returned the branch window while the symbolic LUI sat
// at another entry. Buffers are hand-built headers: byte0 bit3 entry_num,
// map/type at each entry's absolute base (e0:6, e1:37, e2:68).
TEST(HaydnRelocLayoutTest, Hi12MixedParcelSniffOpcPins) {
  const RelocFieldInfo &HI = getRelocFieldInfo(RelocKind::HI12);

  // E3 parcel with a BEQZ-opc member at e1 ALU0 (map=2 type=0xa opc=4)
  // and a LUI at e2 (map=2 type=0xa opc=1): the HI12 sniff must NOT match
  // the e1 branch arm — pre-fix it returned 54; post-pin it matches the
  // e2 ALU0 LUI and returns 81.
  uint8_t MixedBranchE1LuiE2[12] = {};
  MixedBranchE1LuiE2[0] = 0x0f; // indicator 111 (bits[2:0]) + entry_num=1
  MixedBranchE1LuiE2[4] = 0x40; // e1 map=2 @ [38:37]
  MixedBranchE1LuiE2[5] = 0x05; // e1 type=0xa @ [42:39]
  MixedBranchE1LuiE2[6] = 0x02; // e1 opc=4 @ [49:47]
  MixedBranchE1LuiE2[8] = 0xa0; // e2 map=2 @ [69:68] + type bit
  MixedBranchE1LuiE2[9] = 0x06; // e2 type=0xa bit3 @73 + opc=1 @ [76:74]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, MixedBranchE1LuiE2), 81u);

  // Same parcel shape with the LUI at e2 ALU2 (map=1 type=4 opc=1 @74):
  // window 83, never the e1 branch's 54.
  uint8_t MixedBranchE1LuiE2Alu2[12] = {};
  MixedBranchE1LuiE2Alu2[0] = 0x0f;
  MixedBranchE1LuiE2Alu2[4] = 0x40;
  MixedBranchE1LuiE2Alu2[5] = 0x05;
  MixedBranchE1LuiE2Alu2[6] = 0x02; // e1 BEQZ opc=4
  MixedBranchE1LuiE2Alu2[8] = 0x10; // e2 map=1 @68
  MixedBranchE1LuiE2Alu2[9] = 0x05; // e2 type=4 + opc=1 @74
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, MixedBranchE1LuiE2Alu2), 83u);

  // Reverse mix: LUI at e1 ALU0 (opc=1), branch at e2 (opc=4): 54.
  uint8_t MixedLuiE1BranchE2[12] = {};
  MixedLuiE1BranchE2[0] = 0x0f;
  MixedLuiE1BranchE2[4] = 0x40; // map=2
  MixedLuiE1BranchE2[5] = 0x85; // type=0xa + opc=1 @47
  MixedLuiE1BranchE2[8] = 0xa0;
  MixedLuiE1BranchE2[9] = 0x12; // type bit3 + opc=4 @ [76:74]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, MixedLuiE1BranchE2), 54u);
  patchField(MixedLuiE1BranchE2, 1, HI.NBytes, HI.FieldSize, 54u);
  EXPECT_EQ(readField(MixedLuiE1BranchE2, HI.NBytes, HI.FieldSize, 54u), 1u);

  // NOP-opc(0) members never satisfy a pinned arm: an E3 parcel whose
  // only I12-shaped entries are NOPs falls to the table default (E2 e0
  // geometry) — the documented fail-through.
  uint8_t AllNopOpc[12] = {};
  AllNopOpc[0] = 0x0f;
  AllNopOpc[4] = 0x40; // map=2 @37
  AllNopOpc[5] = 0x05; // type=0xa @39 (opc @47..49 = 0 → NOP)
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, AllNopOpc),
            getRelocFieldInfo(RelocKind::HI12).FieldLsb);
}

// D1.24/D1.17 E2 arm pin: the HI12 E2 e0 sniff requires the LUI member
// (map=0, 5-bit type @8 = 0x0a, opc @17..19 == 1). A parcel whose e0 is
// NOT the LUI (opc fabricated != 1) must fall through to the table
// default — never patch the e0 tail bits[43:32] for an LUI elsewhere.
TEST(HaydnRelocLayoutTest, Hi12E2ArmLuiOpcPin) {
  uint8_t E2Lui[12] = {};
  E2Lui[0] = 0x07;              // indicator 111, entry_num=0
  E2Lui[1] = 0x0a;              // 5-bit type @8 = 0x0a (map @6..7 = 0)
  E2Lui[2] = 0x02;              // opc=1 @ bits[19:17]
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E2Lui), 32u);
  patchField(E2Lui, 1, 12, 12, 32u);
  EXPECT_EQ(readField(E2Lui, 12, 12, 32u), 1u);

  // opc=4 (BEQZ shape at E2 e0): pin refuses — table default fall-through
  // (a symbolic HI12 whose parcel's e0 is a branch must never patch the
  // e0 tail bits[43:32]; the producer emits a qualified twin instead).
  uint8_t E2Branch[12] = {};
  E2Branch[0] = 0x07;
  E2Branch[1] = 0x0a;
  E2Branch[2] = 0x08; // opc=4
  EXPECT_EQ(resolveFieldLsb(RelocKind::HI12, E2Branch),
            getRelocFieldInfo(RelocKind::HI12).FieldLsb);
}

// D1.17: CSR_UImm8 Loc-sniff opc pins. I8 hosts NOP(0) and
// ZERO_GPR(1)/ZERO_DR(2)/ZERO_SFR(3) alongside CSRR(4)/CSRW(5) at every
// site — the pre-fix sniff matched ZERO_* and returned the wrong window
// (`{ nop; csrr; zero_gpr }` returned 23 from the e0 ZERO_GPR while the
// CSR sat at e1 window 54).
TEST(HaydnRelocLayoutTest, CsrMixedParcelSniffOpcPins) {
  const RelocFieldInfo &CI = getRelocFieldInfo(RelocKind::CSR_UImm8);

  // E3 parcel with ZERO_GPR-opc(1) at e0 ALU0 (map=2 type=3 opc=1) and
  // CSRR(4) at e1 (map=2 type=3 opc=4): pre-fix returned 23 (the e0 arm
  // matched ZERO_GPR); post-pin the e0 arm refuses and e1 matches CSRR.
  uint8_t MixedZeroE0CsrrE1[12] = {};
  MixedZeroE0CsrrE1[0] = 0x8f; // indicator 111, entry_num=1, map=2 @6..7
  MixedZeroE0CsrrE1[1] = 0x03; // type=3 @8..11
  MixedZeroE0CsrrE1[2] = 0x01; // opc=1 (ZERO_GPR) @16..18
  // e1 ALU0 CSRR: map=2 @37, type=3 @39, opc=4 @47..49.
  MixedZeroE0CsrrE1[4] = 0xc0; // map=2 @ [38:37]
  MixedZeroE0CsrrE1[5] = 0x01; // type=3 @ [42:39]
  MixedZeroE0CsrrE1[6] = 0x02; // opc=4 @ [49:47]
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, MixedZeroE0CsrrE1), 54u);
  patchField(MixedZeroE0CsrrE1, 10, CI.NBytes, CI.FieldSize, 54u);
  EXPECT_EQ(readRelocAddend(RelocKind::CSR_UImm8, MixedZeroE0CsrrE1), 10);

  // ZERO_GPR alone at e0: refused (opc 1 not in {4,5}) — fail-through to
  // the table default, never the e0 window 23.
  uint8_t ZeroAlone[12] = {};
  ZeroAlone[0] = 0x8f;
  ZeroAlone[1] = 0x03;
  ZeroAlone[2] = 0x01;
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, ZeroAlone),
            getRelocFieldInfo(RelocKind::CSR_UImm8).FieldLsb);

  // e2 CSRW (opc=5) at both units: ALU2 (map=1 type=1 opc @74) and ALU0
  // (map=2 type=3 opc @78) both resolve 85.
  uint8_t E2Alu2Csrw[12] = {};
  E2Alu2Csrw[0] = 0x0f; // indicator 111 + entry_num=1
  E2Alu2Csrw[8] = 0x50; // map=1 @68 + opc bit74
  E2Alu2Csrw[9] = 0x14; // type=1 @70 + opc bit76
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E2Alu2Csrw), 85u);

  uint8_t E2Alu0Csrw[12] = {};
  E2Alu0Csrw[0] = 0x0f;
  E2Alu0Csrw[8] = 0xe0; // map=2 @ [69:68] + type bit @71
  E2Alu0Csrw[9] = 0x40; // type=3 bit3 @73
  E2Alu0Csrw[10] = 0x01; // opc=5 @ [80:78]
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E2Alu0Csrw), 85u);

  // E2 e0 arm pin: CSRR (map=0, 5-bit type @8=3, opc @17=4) → 32; a
  // ZERO_* opc(1) at E2 e0 falls through to the table default.
  uint8_t E2Csrr[12] = {};
  E2Csrr[0] = 0x07;
  E2Csrr[1] = 0x03;   // type=3 @8..12 (map @6..7 = 0)
  E2Csrr[2] = 0x08;   // opc=4 @19..17
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E2Csrr), 32u);
  uint8_t E2Zero[12] = {};
  E2Zero[0] = 0x07;
  E2Zero[1] = 0x03;
  E2Zero[2] = 0x02; // opc=1 (ZERO_GPR)
  EXPECT_EQ(resolveFieldLsb(RelocKind::CSR_UImm8, E2Zero),
            getRelocFieldInfo(RelocKind::CSR_UImm8).FieldLsb);
}

// D1.17: the nine HI12/CSR entry-qualified rows (ELF 34..42) mirror the
// base geometry except FieldLsb; mapFixupKind / mapRelocKindToFixup
// round-trip; resolveFieldLsb early-returns the typed row (no sniff) for
// every qualified kind; baseKindFor folds each twin back to its base.
TEST(HaydnRelocLayoutTest, QualifiedHi12CsrKindRows) {
  struct Row {
    RelocKind K;
    RelocKind Base;
    unsigned FieldLsb;
    unsigned ElfVal;
    unsigned Fixup;
  };
  const Row Rows[] = {
      {RelocKind::HI12_E3E0_ALU2, RelocKind::HI12, 21,
       ELF::R_HAYDN_HI12_E3E0_ALU2, Haydn::FIXUP_HAYDN_HI12_E3E0_ALU2},
      {RelocKind::HI12_E3E0_ALU0, RelocKind::HI12, 23,
       ELF::R_HAYDN_HI12_E3E0_ALU0, Haydn::FIXUP_HAYDN_HI12_E3E0_ALU0},
      {RelocKind::HI12_E3E1, RelocKind::HI12, 54, ELF::R_HAYDN_HI12_E3E1,
       Haydn::FIXUP_HAYDN_HI12_E3E1},
      {RelocKind::HI12_E3E2_ALU2, RelocKind::HI12, 83,
       ELF::R_HAYDN_HI12_E3E2_ALU2, Haydn::FIXUP_HAYDN_HI12_E3E2_ALU2},
      {RelocKind::HI12_E3E2_ALU0, RelocKind::HI12, 81,
       ELF::R_HAYDN_HI12_E3E2_ALU0, Haydn::FIXUP_HAYDN_HI12_E3E2_ALU0},
      {RelocKind::CSR_UImm8_E3E0_ALU2, RelocKind::CSR_UImm8, 27,
       ELF::R_HAYDN_CSR_UImm8_E3E0_ALU2,
       Haydn::FIXUP_HAYDN_CSR_UImm8_E3E0_ALU2},
      {RelocKind::CSR_UImm8_E3E0_ALU0, RelocKind::CSR_UImm8, 23,
       ELF::R_HAYDN_CSR_UImm8_E3E0_ALU0,
       Haydn::FIXUP_HAYDN_CSR_UImm8_E3E0_ALU0},
      {RelocKind::CSR_UImm8_E3E1, RelocKind::CSR_UImm8, 54,
       ELF::R_HAYDN_CSR_UImm8_E3E1, Haydn::FIXUP_HAYDN_CSR_UImm8_E3E1},
      {RelocKind::CSR_UImm8_E3E2, RelocKind::CSR_UImm8, 85,
       ELF::R_HAYDN_CSR_UImm8_E3E2, Haydn::FIXUP_HAYDN_CSR_UImm8_E3E2},
  };
  for (const Row &R : Rows) {
    const RelocFieldInfo &Q = getRelocFieldInfo(R.K);
    const RelocFieldInfo &B = getRelocFieldInfo(R.Base);
    EXPECT_EQ(Q.NBytes, B.NBytes);
    EXPECT_EQ(Q.FieldSize, B.FieldSize);
    EXPECT_EQ(Q.ValueShift, B.ValueShift);
    EXPECT_EQ(Q.Align, B.Align);
    EXPECT_EQ(Q.IsSigned, B.IsSigned);
    EXPECT_EQ(Q.IsPCRel, B.IsPCRel);
    EXPECT_EQ(Q.Trans, B.Trans);
    EXPECT_EQ(Q.FieldLsb, R.FieldLsb);
    EXPECT_NE(Q.FieldLsb, B.FieldLsb);
    EXPECT_EQ(static_cast<unsigned>(R.K), R.ElfVal);
    EXPECT_TRUE(isEntryQualifiedKind(R.K));
    EXPECT_EQ(baseKindFor(R.K), R.Base);
    EXPECT_EQ(mapFixupKind(R.Fixup), R.K);
    EXPECT_EQ(mapRelocKindToFixup(R.K), R.Fixup);
    EXPECT_TRUE(isRelocTransformReady(R.K));
    // The early-return: even on a Loc buffer that would sniff a DIFFERENT
    // window (E2 header), the qualified kind patches its typed row.
    uint8_t E2Buf[12] = {};
    E2Buf[0] = 0x07;
    EXPECT_EQ(resolveFieldLsb(R.K, E2Buf), R.FieldLsb);
  }
  // Base kinds are not entry-qualified; MC-only kinds still sit past the
  // shared range.
  EXPECT_FALSE(isEntryQualifiedKind(RelocKind::HI12));
  EXPECT_FALSE(isEntryQualifiedKind(RelocKind::CSR_UImm8));
  EXPECT_GT(static_cast<unsigned>(RelocKind::C_BranchSImm4), 42u);
  EXPECT_EQ(baseKindFor(RelocKind::HI12), RelocKind::HI12);
  // Value equality with the base transform: HI12 twins keep Hi12; CSR
  // twins keep the unsigned 8-bit None transform.
  EXPECT_EQ(getRelocFieldInfo(RelocKind::HI12_E3E1).Trans, RelocTrans::Hi12);
  EXPECT_EQ(getRelocFieldInfo(RelocKind::CSR_UImm8_E3E2).Trans,
            RelocTrans::None);
}

// D1.17 producer-API pin: resolveFieldLsbForMember unit splits (HI12
// 21/23 at E3 e0, 81/83 at E3 e2; CSR 27/23 at E3 e0) — the tuple the
// emitter already holds at encode time. E2 e1 has no I12/I8 member: both
// kinds fail closed to the E2 e0 table default (D1.24 structural law).
TEST(HaydnRelocLayoutTest, ResolveFieldLsbForMemberHi12CsrUnitSplits) {
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 0, 2), 21u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 0, 0), 23u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 0, 1), 32u); // no ALU1 site
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 1, 0), 54u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 1, 1), 54u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 2, 0), 81u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 1, 2, 2), 83u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::HI12, 0, 1, 1), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 0, 2), 27u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 0, 0), 23u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 0, 1), 32u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 1, 0), 54u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 1, 1), 54u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 2, 0), 85u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 2, 2), 85u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 1, 2), 85u);
  EXPECT_EQ(resolveFieldLsbForMember(RelocKind::CSR_UImm8, 0, 1, 1), 32u);
  // No E2-e1 window is published for either kind.
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::HI12, 65u));
  EXPECT_FALSE(isPublishedFieldLsb(RelocKind::CSR_UImm8, 65u));
}

} // namespace
