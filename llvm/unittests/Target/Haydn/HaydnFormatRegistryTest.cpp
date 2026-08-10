//===- HaydnFormatRegistryTest.cpp - ObjectEncodingProfile registry -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the neutral ObjectEncodingProfile / BundleFormatRow registry:
// production profile is E96 only (shipping HaydnFormat). Synthetic short
// families are compiled only into this test binary (INERT_TEST_ONLY).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include <cassert>
#include <optional>
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::haydn::format;

//===----------------------------------------------------------------------===//
// Test-only multi-length fixtures (not linked into shipping LLVMHaydnDesc)
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test-support-only multi-length encoding fixtures (decision-guard
// INERT_TEST_ONLY). Linked into unit/lit test support exclusively via
// LLVMHaydnTestEncodingSupport. Never linked into shipping LLVMHaydnDesc
// selectors, writers, or tools. No driver/-mattr/assembler product path.
//
//===----------------------------------------------------------------------===//


#include "llvm/ADT/ArrayRef.h"
#include <optional>

namespace llvm {
namespace haydn {
namespace format {
namespace test {

/// Non-product multi-family profile (synthetic short rows). Never product.
inline constexpr ObjectEncodingProfileID TestSyntheticMulti =
    static_cast<ObjectEncodingProfileID>(0x8000u);

/// Test-only short families (unequal byte lengths). Never product.
inline constexpr BundleFormatID SynthShortA =
    static_cast<BundleFormatID>(0x8000u);
inline constexpr BundleFormatID SynthShortB =
    static_cast<BundleFormatID>(0x8001u);

/// Synthetic rows: unequal sizes across families; equal-size pair inside A.
inline constexpr BundleFormatRowID SynthA_RowWide =
    static_cast<BundleFormatRowID>(0x8000u);
inline constexpr BundleFormatRowID SynthA_RowNarrow =
    static_cast<BundleFormatRowID>(0x8001u);
inline constexpr BundleFormatRowID SynthB_RowTiny =
    static_cast<BundleFormatRowID>(0x8002u);

/// Synthetic non-product profile descriptor.
const ObjectEncodingProfileDesc &getTestSyntheticProfile();

/// Synthetic non-product rows for unit tests only.
ArrayRef<BundleFormatRowDesc> getSyntheticTestBundleFormatRows();

/// Synthetic non-product families for unit tests only.
ArrayRef<BundleFormatDesc> getSyntheticTestBundleFormats();

/// Profile lookup covering product E96 plus the test synthetic profile.
const ObjectEncodingProfileDesc *
getObjectEncodingProfile(ObjectEncodingProfileID ID);

/// Family lookup covering product plus synthetic families.
const BundleFormatDesc *getBundleFormat(BundleFormatID ID);

/// Row lookup covering product plus synthetic rows.
const BundleFormatRowDesc *getBundleFormatRow(BundleFormatRowID ID);

std::optional<EncodedBytes> encodedBytesOf(BundleFormatRowID Row);
std::optional<EncodedBits> encodedBitsOf(BundleFormatRowID Row);
EncodedBytes encodedBytesOrDie(BundleFormatRowID Row);
EncodedBits encodedBitsOrDie(BundleFormatRowID Row);
EncodedBytes maxEncodedBytesInProfile(ObjectEncodingProfileID Profile);

bool isProductRow(BundleFormatRowID Row);
bool profilePermitsFamily(ObjectEncodingProfileID Profile,
                          BundleFormatID Format);
bool profilePermitsRow(ObjectEncodingProfileID Profile, BundleFormatRowID Row);
ArrayRef<BundleFormatRowDesc> rowsOfFamily(BundleFormatID Format);

} // namespace test
} // namespace format
} // namespace haydn
} // namespace llvm


//===-- HaydnTestEncodingProfileProvider.cpp - Non-product fixtures -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Synthetic multi-length encoding fixtures for architecture-proof unit tests.
// Built only into LLVMHaydnTestEncodingSupport (BUILDTREE_ONLY). Absent from
// shipping LLVMHaydnDesc symbols, selectors, and writers.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include <cassert>

using namespace llvm;
using namespace llvm::haydn::format;

namespace {

// Synthetic test-only sizes (must differ from E96 and from each other so
// registry iteration cannot hard-code a single parcel width).
constexpr EncodedBytes SynthAWideBytes{8};
constexpr EncodedBits SynthAWideBits{64};
constexpr EncodedBytes SynthANarrowBytes{8};
constexpr EncodedBits SynthANarrowBits{64};
constexpr EncodedBytes SynthBTinyBytes{4};
constexpr EncodedBits SynthBTinyBits{32};

constexpr HeaderPredicateID HP_SynthA_Wide = 0x8001;
constexpr HeaderPredicateID HP_SynthA_Narrow = 0x8002;
constexpr HeaderPredicateID HP_SynthB_Tiny = 0x8003;
constexpr PhaseTransitionID PT_Synth = 0x8001;

const BundleFormatRowDesc SyntheticRows[] = {
    // Two equal-length rows with different entry counts (family SynthShortA).
    {test::SynthA_RowWide, test::SynthShortA, "SynthA_RowWide", SynthAWideBits,
     SynthAWideBytes, HP_SynthA_Wide, PT_Synth,
     /*EntryCount=*/2, /*TopPadBits=*/0, /*IsProduct=*/false},
    {test::SynthA_RowNarrow, test::SynthShortA, "SynthA_RowNarrow",
     SynthANarrowBits, SynthANarrowBytes, HP_SynthA_Narrow, PT_Synth,
     /*EntryCount=*/1, /*TopPadBits=*/0, /*IsProduct=*/false},
    // Unequal length vs SynthShortA (family SynthShortB).
    {test::SynthB_RowTiny, test::SynthShortB, "SynthB_RowTiny", SynthBTinyBits,
     SynthBTinyBytes, HP_SynthB_Tiny, PT_Synth,
     /*EntryCount=*/1, /*TopPadBits=*/0, /*IsProduct=*/false},
};

const BundleFormatRowDesc SynthARows[] = {
    SyntheticRows[0],
    SyntheticRows[1],
};

const BundleFormatRowDesc SynthBRows[] = {
    SyntheticRows[2],
};

const BundleFormatDesc SyntheticFormats[] = {
    {test::SynthShortA, "SynthShortA", ArrayRef<BundleFormatRowDesc>(SynthARows),
     /*IsProduct=*/false, /*StableOrdinal=*/1},
    {test::SynthShortB, "SynthShortB", ArrayRef<BundleFormatRowDesc>(SynthBRows),
     /*IsProduct=*/false, /*StableOrdinal=*/2},
};

const BundleFormatID SyntheticPermittedFamilies[] = {
    test::SynthShortA,
    test::SynthShortB,
};

const ObjectEncodingProfileDesc SyntheticTestProfile = {
    test::TestSyntheticMulti,
    "TestSyntheticMulti",
    ArrayRef<BundleFormatID>(SyntheticPermittedFamilies),
    /*DecodeDispatch=*/0x8001,
    /*PaddingPolicy=*/0x8001,
    /*CostPolicy=*/0x8001,
    /*StreamPhases=*/0x8001,
    /*ELFFlagsValue=*/0,
    /*IsProduct=*/false,
};

const BundleFormatRowDesc *findSyntheticRow(BundleFormatRowID ID) {
  for (const BundleFormatRowDesc &R : SyntheticRows)
    if (R.Row == ID)
      return &R;
  return nullptr;
}

const BundleFormatDesc *findSyntheticFamily(BundleFormatID ID) {
  for (const BundleFormatDesc &F : SyntheticFormats)
    if (F.Format == ID)
      return &F;
  return nullptr;
}

} // end anonymous namespace

namespace llvm {
namespace haydn {
namespace format {
namespace test {

const ObjectEncodingProfileDesc &getTestSyntheticProfile() {
  return SyntheticTestProfile;
}

ArrayRef<BundleFormatRowDesc> getSyntheticTestBundleFormatRows() {
  return ArrayRef<BundleFormatRowDesc>(SyntheticRows);
}

ArrayRef<BundleFormatDesc> getSyntheticTestBundleFormats() {
  return ArrayRef<BundleFormatDesc>(SyntheticFormats);
}

const ObjectEncodingProfileDesc *
getObjectEncodingProfile(ObjectEncodingProfileID ID) {
  if (const ObjectEncodingProfileDesc *P =
          format::getObjectEncodingProfile(ID))
    return P;
  if (ID == TestSyntheticMulti)
    return &SyntheticTestProfile;
  return nullptr;
}

const BundleFormatDesc *getBundleFormat(BundleFormatID ID) {
  if (const BundleFormatDesc *F = format::getBundleFormat(ID))
    return F;
  return findSyntheticFamily(ID);
}

const BundleFormatRowDesc *getBundleFormatRow(BundleFormatRowID ID) {
  if (const BundleFormatRowDesc *R = format::getBundleFormatRow(ID))
    return R;
  return findSyntheticRow(ID);
}

std::optional<EncodedBytes> encodedBytesOf(BundleFormatRowID Row) {
  if (const BundleFormatRowDesc *D = test::getBundleFormatRow(Row))
    return D->Bytes;
  return std::nullopt;
}

std::optional<EncodedBits> encodedBitsOf(BundleFormatRowID Row) {
  if (const BundleFormatRowDesc *D = test::getBundleFormatRow(Row))
    return D->Bits;
  return std::nullopt;
}

EncodedBytes encodedBytesOrDie(BundleFormatRowID Row) {
  auto B = test::encodedBytesOf(Row);
  assert(B && "unknown BundleFormatRowID");
  return *B;
}

EncodedBits encodedBitsOrDie(BundleFormatRowID Row) {
  auto B = test::encodedBitsOf(Row);
  assert(B && "unknown BundleFormatRowID");
  return *B;
}

EncodedBytes maxEncodedBytesInProfile(ObjectEncodingProfileID Profile) {
  const ObjectEncodingProfileDesc *P = test::getObjectEncodingProfile(Profile);
  assert(P && "unknown ObjectEncodingProfileID");
  EncodedBytes Max{0};
  for (BundleFormatID Fam : P->PermittedFamilies) {
    const BundleFormatDesc *F = test::getBundleFormat(Fam);
    if (!F)
      continue;
    for (const BundleFormatRowDesc &R : F->Rows)
      if (R.Bytes.Value > Max.Value)
        Max = R.Bytes;
  }
  return Max;
}

bool isProductRow(BundleFormatRowID Row) {
  const BundleFormatRowDesc *D = test::getBundleFormatRow(Row);
  return D && D->IsProduct;
}

bool profilePermitsFamily(ObjectEncodingProfileID Profile,
                          BundleFormatID Format) {
  const ObjectEncodingProfileDesc *P = test::getObjectEncodingProfile(Profile);
  if (!P)
    return false;
  return llvm::is_contained(P->PermittedFamilies, Format);
}

bool profilePermitsRow(ObjectEncodingProfileID Profile,
                       BundleFormatRowID Row) {
  const BundleFormatRowDesc *R = test::getBundleFormatRow(Row);
  if (!R)
    return false;
  return test::profilePermitsFamily(Profile, R->Format);
}

ArrayRef<BundleFormatRowDesc> rowsOfFamily(BundleFormatID Format) {
  if (const BundleFormatDesc *F = test::getBundleFormat(Format))
    return F->Rows;
  return {};
}

} // namespace test
} // namespace format
} // namespace haydn
} // namespace llvm


//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//
namespace {

TEST(HaydnFormatRegistryTest, ProductionProfileIsE96Only) {
  const ObjectEncodingProfileDesc &Prod = getProductionObjectEncodingProfile();
  EXPECT_EQ(Prod.Profile, ObjectEncodingProfileID::E96);
  EXPECT_TRUE(Prod.IsProduct);
  EXPECT_TRUE(isProductionProfile(Prod.Profile));
  EXPECT_EQ(Prod.ELFFlagsValue, EF_HAYDN_E96);
  EXPECT_NE(Prod.ELFFlagsValue, 0u);
  ASSERT_EQ(Prod.PermittedFamilies.size(), 1u);
  EXPECT_EQ(Prod.PermittedFamilies[0], BundleFormatID::FormatE96);

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
  EncodedBytes TwoBytes = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBytes ThreeBytes =
      encodedBytesOrDie(BundleFormatRowID::E96ThreeEntry);
  EncodedBits TwoBits = encodedBitsOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBits ThreeBits = encodedBitsOrDie(BundleFormatRowID::E96ThreeEntry);

  EXPECT_EQ(TwoBytes, ThreeBytes);
  EXPECT_EQ(TwoBits, ThreeBits);
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
  EXPECT_FALSE(productionProfilePermitsRow(test::SynthA_RowWide));
  EXPECT_FALSE(productionProfilePermitsRow(test::SynthA_RowNarrow));
  EXPECT_FALSE(productionProfilePermitsRow(test::SynthB_RowTiny));
  EXPECT_EQ(getBundleFormatRow(test::SynthA_RowWide), nullptr);
  EXPECT_EQ(getObjectEncodingProfile(test::TestSyntheticMulti), nullptr);

  EXPECT_TRUE(
      profilePermitsFamily(ObjectEncodingProfileID::E96, BundleFormatID::FormatE96));
  EXPECT_FALSE(
      profilePermitsFamily(ObjectEncodingProfileID::E96, test::SynthShortA));
  EXPECT_FALSE(
      profilePermitsFamily(ObjectEncodingProfileID::E96, test::SynthShortB));
}

TEST(HaydnFormatRegistryTest, SyntheticFamiliesAreNonProductAndUnequalSize) {
  auto SynthRows = test::getSyntheticTestBundleFormatRows();
  ASSERT_GE(SynthRows.size(), 3u);
  for (const BundleFormatRowDesc &R : SynthRows) {
    EXPECT_FALSE(R.IsProduct);
    EXPECT_FALSE(test::isProductRow(R.Row));
    EXPECT_FALSE(productionProfilePermitsRow(R.Row));
  }

  auto SynthFamilies = test::getSyntheticTestBundleFormats();
  ASSERT_EQ(SynthFamilies.size(), 2u);
  EXPECT_FALSE(SynthFamilies[0].IsProduct);
  EXPECT_FALSE(SynthFamilies[1].IsProduct);

  EncodedBytes AWide = test::encodedBytesOrDie(test::SynthA_RowWide);
  EncodedBytes BTiny = test::encodedBytesOrDie(test::SynthB_RowTiny);
  EXPECT_NE(AWide, BTiny);

  EncodedBytes ANarrow = test::encodedBytesOrDie(test::SynthA_RowNarrow);
  EXPECT_EQ(AWide, ANarrow);
  const BundleFormatRowDesc *Wide =
      test::getBundleFormatRow(test::SynthA_RowWide);
  const BundleFormatRowDesc *Narrow =
      test::getBundleFormatRow(test::SynthA_RowNarrow);
  ASSERT_NE(Wide, nullptr);
  ASSERT_NE(Narrow, nullptr);
  EXPECT_NE(Wide->EntryCount, Narrow->EntryCount);

  EXPECT_TRUE(
      test::profilePermitsRow(test::TestSyntheticMulti, test::SynthA_RowWide));
  EXPECT_TRUE(
      test::profilePermitsRow(test::TestSyntheticMulti, test::SynthB_RowTiny));
  EXPECT_FALSE(test::profilePermitsRow(test::TestSyntheticMulti,
                                        BundleFormatRowID::E96TwoEntry));
  EXPECT_FALSE(isProductionProfile(test::TestSyntheticMulti));
}

TEST(HaydnFormatRegistryTest, MaxEncodedBytesFromProfileNotBareLiteral) {
  EncodedBytes ProdMax =
      maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
  EncodedBytes RowBytes = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EXPECT_EQ(ProdMax, RowBytes);

  EncodedBytes SynthMax =
      test::maxEncodedBytesInProfile(test::TestSyntheticMulti);
  EncodedBytes AWide = test::encodedBytesOrDie(test::SynthA_RowWide);
  EncodedBytes BTiny = test::encodedBytesOrDie(test::SynthB_RowTiny);
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

  EXPECT_EQ(getObjectEncodingProfile(test::TestSyntheticMulti), nullptr);
  const ObjectEncodingProfileDesc *Synth =
      test::getObjectEncodingProfile(test::TestSyntheticMulti);
  ASSERT_NE(Synth, nullptr);
  EXPECT_NE(A->Profile, Synth->Profile);
  EXPECT_NE(A->IsProduct, Synth->IsProduct);
  EXPECT_EQ(Synth, &test::getTestSyntheticProfile());

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

  EXPECT_TRUE(rowsOfFamily(test::SynthShortA).empty());
  EXPECT_TRUE(rowsOfFamily(test::SynthShortB).empty());
  auto SynthA = test::rowsOfFamily(test::SynthShortA);
  ASSERT_EQ(SynthA.size(), 2u);
  auto SynthB = test::rowsOfFamily(test::SynthShortB);
  ASSERT_EQ(SynthB.size(), 1u);
  EXPECT_TRUE(rowsOfFamily(static_cast<BundleFormatID>(0xDEAD)).empty());
}

TEST(HaydnFormatRegistryTest, FormatEHeaderGeometryConstants) {
  EXPECT_EQ(FormatEIndicatorBits, 0x7u);
  EXPECT_EQ(FormatEEntryNumTwo, 0u);
  EXPECT_EQ(FormatEEntryNumThree, 1u);
}

TEST(HaydnFormatRegistryTest, ProductParcelIsRegistryEncodedBytesFromE96) {
  EncodedBytes Prod = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry);
  EncodedBytes Max = maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
  EXPECT_EQ(Prod, Max);
  EXPECT_EQ(Prod, encodedBytesOrDie(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(Prod.Value * 8u,
            encodedBitsOrDie(BundleFormatRowID::E96TwoEntry).Value);

  EXPECT_FALSE(test::isProductRow(test::SynthA_RowWide));
  EXPECT_NE(Prod, test::encodedBytesOrDie(test::SynthA_RowWide));
  EXPECT_NE(Prod, test::encodedBytesOrDie(test::SynthB_RowTiny));
}

TEST(HaydnFormatRegistryTest, ShippingRegistryRejectsSyntheticIDs) {
  EXPECT_EQ(getObjectEncodingProfile(test::TestSyntheticMulti), nullptr);
  EXPECT_EQ(getBundleFormat(test::SynthShortA), nullptr);
  EXPECT_EQ(getBundleFormat(test::SynthShortB), nullptr);
  EXPECT_EQ(getBundleFormatRow(test::SynthA_RowWide), nullptr);
  EXPECT_EQ(getBundleFormatRow(test::SynthA_RowNarrow), nullptr);
  EXPECT_EQ(getBundleFormatRow(test::SynthB_RowTiny), nullptr);
  EXPECT_FALSE(encodedBytesOf(test::SynthA_RowWide).has_value());
  EXPECT_FALSE(isProductRow(test::SynthA_RowWide));
  EXPECT_FALSE(isProductionProfile(test::TestSyntheticMulti));
}

} // namespace
