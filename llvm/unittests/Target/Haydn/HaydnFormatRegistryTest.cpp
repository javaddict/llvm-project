//===- HaydnFormatRegistryTest.cpp - ObjectEncodingProfile registry -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the neutral ObjectEncodingProfile / BundleFormatRow registry:
// production profile is E96 only; synthetic short families are test-only;
// typed EncodedBytes/EncodedBits APIs; profile equality and row membership.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::haydn::format;

namespace {

TEST(HaydnFormatRegistryTest, ProductionProfileIsE96Only) {
  const ObjectEncodingProfileDesc &Prod = getProductionObjectEncodingProfile();
  EXPECT_EQ(Prod.Profile, ObjectEncodingProfileID::E96);
  EXPECT_TRUE(Prod.IsProduct);
  EXPECT_TRUE(isProductionProfile(Prod.Profile));
  ASSERT_EQ(Prod.PermittedFamilies.size(), 1u);
  EXPECT_EQ(Prod.PermittedFamilies[0], BundleFormatID::FormatE96);

  // Exactly one product family and two product rows.
  auto Families = getProductBundleFormats();
  ASSERT_EQ(Families.size(), 1u);
  EXPECT_EQ(Families[0].Format, BundleFormatID::FormatE96);
  EXPECT_TRUE(Families[0].IsProduct);

  auto Rows = getProductBundleFormatRows();
  ASSERT_EQ(Rows.size(), 2u);
  EXPECT_EQ(Rows[0].Row, BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(Rows[1].Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_TRUE(Rows[0].IsProduct);
  EXPECT_TRUE(Rows[1].IsProduct);
  EXPECT_EQ(Rows[0].Format, BundleFormatID::FormatE96);
  EXPECT_EQ(Rows[1].Format, BundleFormatID::FormatE96);
}

TEST(HaydnFormatRegistryTest, E96RowsShareParcelSizeViaTypedAPIs) {
  // Call sites use typed APIs; both internal geometries share one parcel size.
  EncodedBytes TwoBytes = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBytes ThreeBytes =
      encodedBytesOrDie(BundleFormatRowID::E96ThreeEntry);
  EncodedBits TwoBits = encodedBitsOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBits ThreeBits = encodedBitsOrDie(BundleFormatRowID::E96ThreeEntry);

  EXPECT_EQ(TwoBytes, ThreeBytes);
  EXPECT_EQ(TwoBits, ThreeBits);
  // Bits must be exactly 8 * bytes (byte-sized rows only).
  EXPECT_EQ(TwoBits.Value, TwoBytes.Value * 8u);

  const BundleFormatRowDesc *E2 =
      getBundleFormatRow(BundleFormatRowID::E96TwoEntry);
  const BundleFormatRowDesc *E3 =
      getBundleFormatRow(BundleFormatRowID::E96ThreeEntry);
  ASSERT_NE(E2, nullptr);
  ASSERT_NE(E3, nullptr);
  EXPECT_EQ(E2->EntryCount, 2u);
  EXPECT_EQ(E3->EntryCount, 3u);
  EXPECT_NE(E2->Header, E3->Header);
  EXPECT_EQ(E2->PhaseTransition, E3->PhaseTransition);
}

TEST(HaydnFormatRegistryTest, ProductionProfilePermitsOnlyE96Rows) {
  EXPECT_TRUE(productionProfilePermitsRow(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(productionProfilePermitsRow(BundleFormatRowID::E96ThreeEntry));
  EXPECT_FALSE(productionProfilePermitsRow(BundleFormatRowID::SynthA_RowWide));
  EXPECT_FALSE(productionProfilePermitsRow(BundleFormatRowID::SynthA_RowNarrow));
  EXPECT_FALSE(productionProfilePermitsRow(BundleFormatRowID::SynthB_RowTiny));

  EXPECT_TRUE(
      profilePermitsFamily(ObjectEncodingProfileID::E96, BundleFormatID::FormatE96));
  EXPECT_FALSE(profilePermitsFamily(ObjectEncodingProfileID::E96,
                                    BundleFormatID::SynthShortA));
  EXPECT_FALSE(profilePermitsFamily(ObjectEncodingProfileID::E96,
                                    BundleFormatID::SynthShortB));
}

TEST(HaydnFormatRegistryTest, SyntheticFamiliesAreNonProductAndUnequalSize) {
  auto SynthRows = getSyntheticTestBundleFormatRows();
  ASSERT_GE(SynthRows.size(), 3u);
  for (const BundleFormatRowDesc &R : SynthRows) {
    EXPECT_FALSE(R.IsProduct);
    EXPECT_FALSE(isProductRow(R.Row));
    EXPECT_FALSE(productionProfilePermitsRow(R.Row));
  }

  auto SynthFamilies = getSyntheticTestBundleFormats();
  ASSERT_EQ(SynthFamilies.size(), 2u);
  EXPECT_FALSE(SynthFamilies[0].IsProduct);
  EXPECT_FALSE(SynthFamilies[1].IsProduct);

  // Unequal byte lengths across synthetic families (registry must not assume
  // a single product parcel width).
  EncodedBytes AWide = encodedBytesOrDie(BundleFormatRowID::SynthA_RowWide);
  EncodedBytes BTiny = encodedBytesOrDie(BundleFormatRowID::SynthB_RowTiny);
  EXPECT_NE(AWide, BTiny);

  // Equal length, different entry counts within SynthShortA.
  EncodedBytes ANarrow = encodedBytesOrDie(BundleFormatRowID::SynthA_RowNarrow);
  EXPECT_EQ(AWide, ANarrow);
  const BundleFormatRowDesc *Wide =
      getBundleFormatRow(BundleFormatRowID::SynthA_RowWide);
  const BundleFormatRowDesc *Narrow =
      getBundleFormatRow(BundleFormatRowID::SynthA_RowNarrow);
  ASSERT_NE(Wide, nullptr);
  ASSERT_NE(Narrow, nullptr);
  EXPECT_NE(Wide->EntryCount, Narrow->EntryCount);

  // Synthetic profile permits synthetic families only.
  EXPECT_TRUE(profilePermitsRow(ObjectEncodingProfileID::TestSyntheticMulti,
                                 BundleFormatRowID::SynthA_RowWide));
  EXPECT_TRUE(profilePermitsRow(ObjectEncodingProfileID::TestSyntheticMulti,
                                 BundleFormatRowID::SynthB_RowTiny));
  EXPECT_FALSE(profilePermitsRow(ObjectEncodingProfileID::TestSyntheticMulti,
                                  BundleFormatRowID::E96TwoEntry));
  EXPECT_FALSE(
      isProductionProfile(ObjectEncodingProfileID::TestSyntheticMulti));
}

TEST(HaydnFormatRegistryTest, MaxEncodedBytesFromProfileNotBareLiteral) {
  EncodedBytes ProdMax =
      maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
  EncodedBytes RowBytes = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(ProdMax, RowBytes);

  EncodedBytes SynthMax =
      maxEncodedBytesInProfile(ObjectEncodingProfileID::TestSyntheticMulti);
  EncodedBytes AWide = encodedBytesOrDie(BundleFormatRowID::SynthA_RowWide);
  EncodedBytes BTiny = encodedBytesOrDie(BundleFormatRowID::SynthB_RowTiny);
  // Max over permitted synthetic rows is the wider family.
  EXPECT_EQ(SynthMax, AWide);
  EXPECT_GT(SynthMax.Value, BTiny.Value);
}

TEST(HaydnFormatRegistryTest, ProfileEqualityAndLookup) {
  const ObjectEncodingProfileDesc *A =
      getObjectEncodingProfile(ObjectEncodingProfileID::E96);
  const ObjectEncodingProfileDesc *B =
      getObjectEncodingProfile(ObjectEncodingProfileID::E96);
  ASSERT_NE(A, nullptr);
  EXPECT_EQ(A, B);
  EXPECT_EQ(A, &getProductionObjectEncodingProfile());

  const ObjectEncodingProfileDesc *Synth =
      getObjectEncodingProfile(ObjectEncodingProfileID::TestSyntheticMulti);
  ASSERT_NE(Synth, nullptr);
  EXPECT_NE(A->Profile, Synth->Profile);
  EXPECT_NE(A->IsProduct, Synth->IsProduct);

  EXPECT_EQ(getObjectEncodingProfile(
                static_cast<ObjectEncodingProfileID>(0xDEAD)),
            nullptr);
  EXPECT_EQ(getBundleFormatRow(static_cast<BundleFormatRowID>(0xDEAD)),
            nullptr);
  EXPECT_EQ(getBundleFormat(static_cast<BundleFormatID>(0xDEAD)), nullptr);
}

TEST(HaydnFormatRegistryTest, MCFormatsExposesProductionRegistry) {
  HaydnMCFormats Fmts;
  const ObjectEncodingProfileDesc &P = Fmts.getObjectEncodingProfile();
  EXPECT_EQ(P.Profile, ObjectEncodingProfileID::E96);
  EXPECT_TRUE(P.IsProduct);

  const BundleFormatRowDesc *E2 =
      Fmts.getBundleFormatRow(BundleFormatRowID::E96TwoEntry);
  const BundleFormatRowDesc *E3 =
      Fmts.getBundleFormatRow(BundleFormatRowID::E96ThreeEntry);
  ASSERT_NE(E2, nullptr);
  ASSERT_NE(E3, nullptr);

  EXPECT_EQ(Fmts.getEncodedBytes(BundleFormatRowID::E96TwoEntry),
            Fmts.getEncodedBytes(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(Fmts.getEncodedBits(BundleFormatRowID::E96TwoEntry),
            Fmts.getEncodedBits(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(Fmts.getProductionMaxEncodedBytes(),
            Fmts.getEncodedBytes(BundleFormatRowID::E96TwoEntry));
}

TEST(HaydnFormatRegistryTest, RowsOfFamilyAndBitsBytesConsistency) {
  auto E96Rows = rowsOfFamily(BundleFormatID::FormatE96);
  ASSERT_EQ(E96Rows.size(), 2u);
  for (const BundleFormatRowDesc &R : E96Rows) {
    EXPECT_EQ(R.Bits.Value, R.Bytes.Value * 8u);
    EXPECT_TRUE(R.IsProduct);
  }

  auto SynthA = rowsOfFamily(BundleFormatID::SynthShortA);
  ASSERT_EQ(SynthA.size(), 2u);
  auto SynthB = rowsOfFamily(BundleFormatID::SynthShortB);
  ASSERT_EQ(SynthB.size(), 1u);
  EXPECT_TRUE(rowsOfFamily(static_cast<BundleFormatID>(0xDEAD)).empty());
}

TEST(HaydnFormatRegistryTest, FormatEHeaderGeometryConstants) {
  // Indicator and entry_num are family-level geometry, not separate formats.
  EXPECT_EQ(FormatEIndicatorBits, 0x7u);
  EXPECT_EQ(FormatEEntryNumTwo, 0u);
  EXPECT_EQ(FormatEEntryNumThree, 1u);
}

TEST(HaydnFormatRegistryTest, ProductParcelIsRegistryEncodedBytesFromE96) {
  // Sole product EncodedBytes come from E96 rows; synthetic non-product rows
  // are not product-selected and must not equal the product parcel.
  EncodedBytes Prod = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBytes Max = maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
  EXPECT_EQ(Prod, Max);
  EXPECT_EQ(Prod, encodedBytesOrDie(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(Prod.Value * 8u,
            encodedBitsOrDie(BundleFormatRowID::E96TwoEntry).Value);

  // Synthetic non-product rows differ from the production profile.
  EXPECT_FALSE(isProductRow(BundleFormatRowID::SynthA_RowWide));
  EXPECT_NE(Prod, encodedBytesOrDie(BundleFormatRowID::SynthA_RowWide));
  EXPECT_NE(Prod, encodedBytesOrDie(BundleFormatRowID::SynthB_RowTiny));
}

} // namespace
