//===-- HaydnFormat.cpp - PacketFormats + encoding registry ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PacketFormats lookup helpers and the neutral ObjectEncodingProfile /
// BundleFormatRow registry. Production profile is E96 only. Non-product
// multi-length fixtures live in HaydnTestEncodingProfileProvider (test
// support only) and are never product-selectable.
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
// Neutral registry tables (production E96 only)
//===----------------------------------------------------------------------===//
//
// Parcel sizes live only in these row descriptors. Call sites use
// encodedBytesOf / encodedBitsOf / maxEncodedBytesInProfile.

namespace {

// Header predicate handles (stable IDs for later generated decode tables).
constexpr HeaderPredicateID HP_E96_TwoEntry = 1;
constexpr HeaderPredicateID HP_E96_ThreeEntry = 2;

// Phase transition handles (E96 reduces to a single alignment phase today).
constexpr PhaseTransitionID PT_E96_Default = 1;

// Production E96 row sizes: one fixed Format E family, 96-bit / 12-byte parcel,
// with two internal entry geometries selected by header entry_num.
constexpr EncodedBytes E96ParcelBytes{12};
constexpr EncodedBits E96ParcelBits{96};

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
    /*ELFFlagsValue=*/EF_HAYDN_E96,
    /*IsProduct=*/true,
};

const BundleFormatRowDesc *findRow(BundleFormatRowID ID) {
  for (const BundleFormatRowDesc &R : ProductRows)
    if (R.Row == ID)
      return &R;
  return nullptr;
}

const BundleFormatDesc *findFamily(BundleFormatID ID) {
  for (const BundleFormatDesc &F : ProductFormats)
    if (F.Format == ID)
      return &F;
  return nullptr;
}

} // end anonymous namespace

namespace llvm {
namespace haydn {
namespace format {

ArrayRef<uint8_t> canonicalFullSlotIdleParcel() {
  static const uint8_t Bytes[] = {
      static_cast<uint8_t>((FormatEIndicatorBits & 0x7u) |
                           ((FormatEEntryNumTwo & 0x1u) << 3)),
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const unsigned Parcel = encodedBytesOrDie(BundleFormatRowID::E96TwoEntry).Value;
  assert(Parcel == sizeof(Bytes) &&
         "full-slot idle length must match production EncodedBytes");
  return ArrayRef<uint8_t>(Bytes, Parcel);
}

const ObjectEncodingProfileDesc &getProductionObjectEncodingProfile() {
  return ProductionProfile;
}

const ObjectEncodingProfileDesc *
getObjectEncodingProfile(ObjectEncodingProfileID ID) {
  if (ID == ObjectEncodingProfileID::E96)
    return &ProductionProfile;
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
