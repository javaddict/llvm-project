//===-- HaydnFormat.cpp - PacketFormats + encoding registry ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PacketFormats lookup helpers and the neutral ObjectEncodingProfile /
// BundleFormatRow registry. Production profile is E96 only. Synthetic
// multi-bundle fixtures live in HaydnTestEncodingProfileProvider (test
// support only) and are never registered in this shipping descriptor.
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
// Shipping tables are production E96 only (AIEFormat.h:29-120 PacketFormats
// / VLIWFormat peer). Synthetic multi-bundle fixtures are not listed here.
// Product EncodedBytes/Bits come from generated Format E golden pins
// (GET_FORMAT_E_GOLDEN_PINS). AIE VLIWFormat::Size is the TableGen size
// (AIEFormat.h:44-52), not a parallel hand 12.

namespace {

// Header predicate handles (stable IDs for later generated decode tables).
constexpr HeaderPredicateID HP_E96_TwoEntry = 1;
constexpr HeaderPredicateID HP_E96_ThreeEntry = 2;

// Phase transition handles (E96 reduces to a single alignment phase today).
constexpr PhaseTransitionID PT_E96_Default = 1;

namespace format_e_pins {
#define GET_FORMAT_E_GOLDEN_PINS
#include "HaydnGenFormatERecords.inc"
} // namespace format_e_pins

static_assert(format_e_pins::FormatEEncodedBytes * 8u ==
                  format_e_pins::FormatEBundleBits,
              "generated Format E EncodedBytes must match FormatEBundleBits");
// FE8: sole product parcel is Format E 96-bit / 12-byte. No dual 8/16 path
// and no parallel E96ParcelBytes{12} literal.
static_assert(format_e_pins::FormatEEncodedBytes == 12u,
              "product EncodedBytes must be Format E 12 (non-E96 sizes retired)");
static_assert(format_e_pins::FormatEBundleBits == 96u,
              "product EncodedBits must be Format E 96 (non-E96 sizes retired)");

// Product rows (immutable production set). EncodedBytes/Bits are the generated
// Format E parcel width — not an independent E96ParcelBytes{12} literal.
constexpr BundleFormatRowDesc ProductRows[] = {
    {BundleFormatRowID::E96TwoEntry, BundleFormatID::FormatE96, "E96TwoEntry",
     EncodedBits{format_e_pins::FormatEBundleBits},
     EncodedBytes{format_e_pins::FormatEEncodedBytes}, HP_E96_TwoEntry,
     PT_E96_Default,
     /*EntryCount=*/2, /*TopPadBits=*/4, /*IsProduct=*/true},
    {BundleFormatRowID::E96ThreeEntry, BundleFormatID::FormatE96,
     "E96ThreeEntry", EncodedBits{format_e_pins::FormatEBundleBits},
     EncodedBytes{format_e_pins::FormatEEncodedBytes}, HP_E96_ThreeEntry,
     PT_E96_Default, /*EntryCount=*/3, /*TopPadBits=*/1, /*IsProduct=*/true},
};

static_assert(ProductRows[0].Bytes.Value == format_e_pins::FormatEEncodedBytes &&
                  ProductRows[1].Bytes.Value ==
                      format_e_pins::FormatEEncodedBytes,
              "ProductRows EncodedBytes is generated FormatEEncodedBytes");
static_assert(ProductRows[0].Bits.Value == format_e_pins::FormatEBundleBits &&
                  ProductRows[1].Bits.Value == format_e_pins::FormatEBundleBits,
              "ProductRows EncodedBits is generated FormatEBundleBits");

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

static_assert(sizeof(ProductRows) / sizeof(ProductRows[0]) ==
                  NumShippingBundleFormatRows,
              "shipping rows are E96TwoEntry and E96ThreeEntry only");
static_assert(sizeof(ProductFormats) / sizeof(ProductFormats[0]) ==
                  NumShippingBundleFormats,
              "shipping family is FormatE96 only");
static_assert(sizeof(ProductionPermittedFamilies) /
                      sizeof(ProductionPermittedFamilies[0]) ==
                  NumShippingBundleFormats,
              "shipping profile permits FormatE96 only");

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
  // Length is generated FormatEEncodedBytes, not a hand 12-byte oracle.
  static const uint8_t Bytes[format_e_pins::FormatEEncodedBytes] = {
      static_cast<uint8_t>((FormatEIndicatorBits & 0x7u) |
                           ((FormatEEntryNumTwo & 0x1u) << 3)),
  };
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
  // Shipping lookup is E96 only. Unknown / synthetic fixture IDs stay
  // unresolved so they cannot become a second product format family.
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
