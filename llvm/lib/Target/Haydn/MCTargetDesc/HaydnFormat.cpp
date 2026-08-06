//===-- HaydnFormat.cpp - PacketFormats + encoding registry ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PacketFormats lookup helpers and the neutral ObjectEncodingProfile /
// BundleFormatRow registry. Production profile is E96 only; synthetic short
// families are test-only and never product-selectable.
//
//===----------------------------------------------------------------------===//

#include "HaydnFormat.h"
#include "llvm/ADT/STLExtras.h"
#include <cassert>

using namespace llvm;
using namespace llvm::haydn::format;

//===----------------------------------------------------------------------===//
// PacketFormats (legacy CodeGenFormat composite table helpers)
//===----------------------------------------------------------------------===//

bool VLIWFormat::covers(SlotBits Slots) const { return !(Slots & ~SlotSet); }

const VLIWFormat *PacketFormats::getFormat(SlotBits Slots) const {
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); Fmt++) {
    if (Fmt->covers(Slots)) {
      return Fmt;
    }
  }
  return nullptr;
}

const VLIWFormat *PacketFormats::getFormatBySize(SlotBits Slots,
                                                 unsigned Size) const {
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); Fmt++) {
    unsigned ThisSize = Fmt->getSize();
    if (ThisSize > Size) {
      // Formats are sorted by size. Once we are beyond the requested Size
      // we won't find it.
      break;
    }
    if (ThisSize < Size) {
      continue;
    }
    if (Fmt->covers(Slots)) {
      return Fmt;
    }
  }
  return nullptr;
}

unsigned PacketFormats::getNumFormats() const {
  unsigned N = 0;
  for (const VLIWFormat *Fmt = FormatsTable; Fmt->getSize(); ++Fmt)
    ++N;
  return N;
}

//===----------------------------------------------------------------------===//
// Neutral registry tables
//===----------------------------------------------------------------------===//
//
// Parcel sizes live only in these row descriptors. Call sites use
// encodedBytesOf / encodedBitsOf / maxEncodedBytesInProfile.

namespace {

// Header predicate handles (stable IDs for later generated decode tables).
constexpr HeaderPredicateID HP_E96_TwoEntry = 1;
constexpr HeaderPredicateID HP_E96_ThreeEntry = 2;
constexpr HeaderPredicateID HP_SynthA_Wide = 0x8001;
constexpr HeaderPredicateID HP_SynthA_Narrow = 0x8002;
constexpr HeaderPredicateID HP_SynthB_Tiny = 0x8003;

// Phase transition handles (E96 reduces to a single alignment phase today).
constexpr PhaseTransitionID PT_E96_Default = 1;
constexpr PhaseTransitionID PT_Synth = 0x8001;

// Production E96 row sizes: one fixed Format E family, 96-bit / 12-byte parcel,
// with two internal entry geometries selected by header entry_num.
constexpr EncodedBytes E96ParcelBytes{12};
constexpr EncodedBits E96ParcelBits{96};

// Synthetic test-only sizes (must differ from E96 and from each other so
// registry iteration cannot hard-code a single parcel width).
constexpr EncodedBytes SynthAWideBytes{8};
constexpr EncodedBits SynthAWideBits{64};
constexpr EncodedBytes SynthANarrowBytes{8};
constexpr EncodedBits SynthANarrowBits{64};
constexpr EncodedBytes SynthBTinyBytes{4};
constexpr EncodedBits SynthBTinyBits{32};

// Product rows (immutable production set).
const BundleFormatRowDesc ProductRows[] = {
    {BundleFormatRowID::E96TwoEntry, BundleFormatID::FormatE96, "E96TwoEntry",
     E96ParcelBits, E96ParcelBytes, HP_E96_TwoEntry, PT_E96_Default,
     /*EntryCount=*/2, /*TopPadBits=*/4, /*IsProduct=*/true},
    {BundleFormatRowID::E96ThreeEntry, BundleFormatID::FormatE96,
     "E96ThreeEntry", E96ParcelBits, E96ParcelBytes, HP_E96_ThreeEntry,
     PT_E96_Default, /*EntryCount=*/3, /*TopPadBits=*/1, /*IsProduct=*/true},
};

const BundleFormatDesc ProductFormats[] = {
    {BundleFormatID::FormatE96, "FormatE96",
     ArrayRef<BundleFormatRowDesc>(ProductRows),
     /*IsProduct=*/true, /*StableOrdinal=*/0},
};

const BundleFormatID ProductionPermittedFamilies[] = {
    BundleFormatID::FormatE96,
};

const ObjectEncodingProfileDesc ProductionProfile = {
    ObjectEncodingProfileID::E96,
    "E96",
    ArrayRef<BundleFormatID>(ProductionPermittedFamilies),
    /*DecodeDispatch=*/1,
    /*PaddingPolicy=*/1,
    /*CostPolicy=*/1,
    /*StreamPhases=*/1,
    /*ELFFlagsValue=*/0,
    /*IsProduct=*/true,
};

// Synthetic non-product rows / families (unit tests only).
const BundleFormatRowDesc SyntheticRows[] = {
    // Two equal-length rows with different entry counts (family SynthShortA).
    {BundleFormatRowID::SynthA_RowWide, BundleFormatID::SynthShortA,
     "SynthA_RowWide", SynthAWideBits, SynthAWideBytes, HP_SynthA_Wide, PT_Synth,
     /*EntryCount=*/2, /*TopPadBits=*/0, /*IsProduct=*/false},
    {BundleFormatRowID::SynthA_RowNarrow, BundleFormatID::SynthShortA,
     "SynthA_RowNarrow", SynthANarrowBits, SynthANarrowBytes, HP_SynthA_Narrow,
     PT_Synth, /*EntryCount=*/1, /*TopPadBits=*/0, /*IsProduct=*/false},
    // Unequal length vs SynthShortA (family SynthShortB).
    {BundleFormatRowID::SynthB_RowTiny, BundleFormatID::SynthShortB,
     "SynthB_RowTiny", SynthBTinyBits, SynthBTinyBytes, HP_SynthB_Tiny, PT_Synth,
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
    {BundleFormatID::SynthShortA, "SynthShortA",
     ArrayRef<BundleFormatRowDesc>(SynthARows),
     /*IsProduct=*/false, /*StableOrdinal=*/1},
    {BundleFormatID::SynthShortB, "SynthShortB",
     ArrayRef<BundleFormatRowDesc>(SynthBRows),
     /*IsProduct=*/false, /*StableOrdinal=*/2},
};

const BundleFormatID SyntheticPermittedFamilies[] = {
    BundleFormatID::SynthShortA,
    BundleFormatID::SynthShortB,
};

const ObjectEncodingProfileDesc SyntheticTestProfile = {
    ObjectEncodingProfileID::TestSyntheticMulti,
    "TestSyntheticMulti",
    ArrayRef<BundleFormatID>(SyntheticPermittedFamilies),
    /*DecodeDispatch=*/0x8001,
    /*PaddingPolicy=*/0x8001,
    /*CostPolicy=*/0x8001,
    /*StreamPhases=*/0x8001,
    /*ELFFlagsValue=*/0,
    /*IsProduct=*/false,
};

const BundleFormatRowDesc *findRow(BundleFormatRowID ID) {
  for (const BundleFormatRowDesc &R : ProductRows)
    if (R.Row == ID)
      return &R;
  for (const BundleFormatRowDesc &R : SyntheticRows)
    if (R.Row == ID)
      return &R;
  return nullptr;
}

const BundleFormatDesc *findFamily(BundleFormatID ID) {
  for (const BundleFormatDesc &F : ProductFormats)
    if (F.Format == ID)
      return &F;
  for (const BundleFormatDesc &F : SyntheticFormats)
    if (F.Format == ID)
      return &F;
  return nullptr;
}

} // end anonymous namespace

namespace llvm {
namespace haydn {
namespace format {

const ObjectEncodingProfileDesc &getProductionObjectEncodingProfile() {
  return ProductionProfile;
}

const ObjectEncodingProfileDesc *
getObjectEncodingProfile(ObjectEncodingProfileID ID) {
  if (ID == ObjectEncodingProfileID::E96)
    return &ProductionProfile;
  if (ID == ObjectEncodingProfileID::TestSyntheticMulti)
    return &SyntheticTestProfile;
  return nullptr;
}

const BundleFormatDesc *getBundleFormat(BundleFormatID ID) {
  return findFamily(ID);
}

const BundleFormatRowDesc *getBundleFormatRow(BundleFormatRowID ID) {
  return findRow(ID);
}

ArrayRef<BundleFormatRowDesc> getProductBundleFormatRows() {
  return ArrayRef<BundleFormatRowDesc>(ProductRows);
}

ArrayRef<BundleFormatDesc> getProductBundleFormats() {
  return ArrayRef<BundleFormatDesc>(ProductFormats);
}

ArrayRef<BundleFormatRowDesc> getSyntheticTestBundleFormatRows() {
  return ArrayRef<BundleFormatRowDesc>(SyntheticRows);
}

ArrayRef<BundleFormatDesc> getSyntheticTestBundleFormats() {
  return ArrayRef<BundleFormatDesc>(SyntheticFormats);
}

std::optional<EncodedBytes> encodedBytesOf(BundleFormatRowID Row) {
  if (const BundleFormatRowDesc *D = findRow(Row))
    return D->Bytes;
  return std::nullopt;
}

std::optional<EncodedBits> encodedBitsOf(BundleFormatRowID Row) {
  if (const BundleFormatRowDesc *D = findRow(Row))
    return D->Bits;
  return std::nullopt;
}

EncodedBytes encodedBytesOrDie(BundleFormatRowID Row) {
  auto B = encodedBytesOf(Row);
  assert(B && "unknown BundleFormatRowID");
  return *B;
}

EncodedBits encodedBitsOrDie(BundleFormatRowID Row) {
  auto B = encodedBitsOf(Row);
  assert(B && "unknown BundleFormatRowID");
  return *B;
}

EncodedBytes maxEncodedBytesInProfile(ObjectEncodingProfileID Profile) {
  const ObjectEncodingProfileDesc *P = getObjectEncodingProfile(Profile);
  assert(P && "unknown ObjectEncodingProfileID");
  EncodedBytes Max{0};
  for (BundleFormatID Fam : P->PermittedFamilies) {
    const BundleFormatDesc *F = findFamily(Fam);
    if (!F)
      continue;
    for (const BundleFormatRowDesc &R : F->Rows)
      if (R.Bytes.Value > Max.Value)
        Max = R.Bytes;
  }
  return Max;
}

bool isProductRow(BundleFormatRowID Row) {
  const BundleFormatRowDesc *D = findRow(Row);
  return D && D->IsProduct;
}

bool isProductFamily(BundleFormatID Format) {
  const BundleFormatDesc *D = findFamily(Format);
  return D && D->IsProduct;
}

bool productionProfilePermitsRow(BundleFormatRowID Row) {
  return profilePermitsRow(ObjectEncodingProfileID::E96, Row);
}

bool profilePermitsFamily(ObjectEncodingProfileID Profile,
                          BundleFormatID Format) {
  const ObjectEncodingProfileDesc *P = getObjectEncodingProfile(Profile);
  if (!P)
    return false;
  return llvm::is_contained(P->PermittedFamilies, Format);
}

bool profilePermitsRow(ObjectEncodingProfileID Profile,
                       BundleFormatRowID Row) {
  const BundleFormatRowDesc *R = findRow(Row);
  if (!R)
    return false;
  return profilePermitsFamily(Profile, R->Format);
}

bool isProductionProfile(ObjectEncodingProfileID Profile) {
  return Profile == ObjectEncodingProfileID::E96;
}

ArrayRef<BundleFormatRowDesc> rowsOfFamily(BundleFormatID Format) {
  if (const BundleFormatDesc *F = findFamily(Format))
    return F->Rows;
  return {};
}

} // namespace format
} // namespace haydn
} // namespace llvm
