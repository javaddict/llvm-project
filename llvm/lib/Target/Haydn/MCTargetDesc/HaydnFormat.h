//===-- HaydnFormat.h - Format utilities for Haydn VLIW bundles -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two cooperating surfaces:
//
// 1) Neutral object-encoding registry (production identity):
//    ObjectEncodingProfileID → BundleFormatID → BundleFormatRowID with typed
//    EncodedBytes / EncodedBits / header / entry / phase schema. The sole
//    production profile is E96 (family FormatE96, rows E96TwoEntry and
//    E96ThreeEntry). Synthetic short families exist for unit tests only and are
//    never product-selectable. Production callers query row/profile size APIs;
//    they must not spell parcel bit/byte widths as bare literals.
//
// 2) AIE-shaped PacketFormats / VLIWFormat tables (CodeGenFormat product
//    BUNDLE_E96_* composites). FE8 retired the 128-bit composite; product
//    PacketFormats are Format E only and match the production profile.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H

#include "HaydnBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {

// Bitmask type used to represent a set of VLIW slots.
// Haydn uses 3 bits (Haydn::SLOT0/1/2), but a 64-bit width is retained for
// parity with AIE and future-proofing.
using SlotBits = uint64_t;

// Maximum number of slots addressable in a SlotBits word.
const int MaxSlots = 64;

class MCSlotKind;

// Description of a VLIW bundle format. Mirrors AIE's VLIWFormat exactly
// (5-field constexpr ctor) so the generated HaydnGenFormats.inc packet-format
// tables compile against it.
class VLIWFormat {
public:
  // A range to iterate over the slots of a format (AIE pattern).
  class SlotKindRange {
  public:
    constexpr SlotKindRange() = default;
    constexpr SlotKindRange(const MCSlotKind *Begin, const MCSlotKind *End)
        : Begin(Begin), End(End) {}
    constexpr const MCSlotKind *begin() const { return Begin; }
    constexpr const MCSlotKind *end() const { return End; }

  private:
    const MCSlotKind *Begin = nullptr;
    const MCSlotKind *End = nullptr;
  };

  constexpr VLIWFormat(unsigned Opcode, const char *Name, SlotKindRange Range,
                       unsigned Size, SlotBits Bits)
      : Opcode(Opcode), Name(Name), Slots(Range), Size(Size), SlotSet(Bits) {}

  const SlotKindRange &getSlots() const { return Slots; }

  SlotBits getSlotSet() const { return SlotSet; }

  const unsigned &getSize() const { return Size; }

  // \returns whether this format can accommodate all slots in \p Slots.
  // True when `Slots` is a subset of `SlotSet` (no bit outside SlotSet).
  bool covers(SlotBits Slots) const;

  // Composite (packet) opcode for this format row. Product rows are
  // BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY. Size==0 is the table
  // terminator (not Opcode==0).
  unsigned Opcode;

  // Name of the format.
  const char *Name;

private:
  // The slots in the format-specific order.
  SlotKindRange Slots;
  // Encoded packet length from the generated table (bytes for the live
  // composite row). See the neutral registry for production E96 sizes.
  unsigned Size;

  // Precomputed OR of the Haydn::SLOT* masks this format can hold.
  SlotBits SlotSet = 0;
};

// Wrapper over a NULL-sentinel-terminated array of VLIWFormat rows providing
// format lookups by slot-combo and by size. Mirrors AIE's PacketFormats.
class PacketFormats {
public:
  PacketFormats(const VLIWFormat *Formats) : FormatsTable(Formats) {}

  // \returns the first format (smallest-first by table order) that covers
  // \p SlotSet, or nullptr if none.
  const VLIWFormat *getFormat(SlotBits SlotSet) const;

  // \returns the first format whose size equals \p Size and that covers
  // \p SlotSet. The table is assumed sorted by ascending Size; the scan
  // terminates once it passes \p Size.
  const VLIWFormat *getFormatBySize(SlotBits SlotSet, unsigned Size) const;

  /// Count of non-sentinel rows (Size != 0).
  unsigned getNumFormats() const;

private:
  const VLIWFormat *FormatsTable;
};

// Hand-authored HaydnFormats / HaydnPacketFormats / HaydnFormatAvailable
// deleted. getPacketFormats / getIsFormatAvailable return CodeGenFormat-
// generated tables (HaydnGenFormats.inc).

//===----------------------------------------------------------------------===//
// Neutral object-encoding registry (production profile E96 only)
//===----------------------------------------------------------------------===//
//
// Identity hierarchy (do not collapse):
//   ObjectEncodingProfileID
//     -> permitted BundleFormatID set + stream/ELF policy handles
//   BundleFormatID
//     -> one architectural family and its rows
//   BundleFormatRowID
//     -> one exact header predicate, encoded length, entry geometry
//
// Production profile is immutable E96. Synthetic families are test-only
// (NonProduct) and rejected by product profile selection.

namespace haydn {
namespace format {

/// Encoded packet length in bytes. Distinct from bits, cycles, and entry count.
struct EncodedBytes {
  unsigned Value = 0;
  constexpr EncodedBytes() = default;
  constexpr explicit EncodedBytes(unsigned V) : Value(V) {}
  constexpr operator unsigned() const { return Value; }
  constexpr bool operator==(EncodedBytes O) const { return Value == O.Value; }
  constexpr bool operator!=(EncodedBytes O) const { return Value != O.Value; }
  constexpr bool operator<(EncodedBytes O) const { return Value < O.Value; }
};

/// Field / parcel geometry in bits. Distinct from EncodedBytes.
struct EncodedBits {
  unsigned Value = 0;
  constexpr EncodedBits() = default;
  constexpr explicit EncodedBits(unsigned V) : Value(V) {}
  constexpr operator unsigned() const { return Value; }
  constexpr bool operator==(EncodedBits O) const { return Value == O.Value; }
  constexpr bool operator!=(EncodedBits O) const { return Value != O.Value; }
};

/// Opaque policy / automaton handles (filled by later generated tables).
using HeaderPredicateID = unsigned;
using PhaseTransitionID = unsigned;
using DecodeDispatchID = unsigned;
using PaddingPolicyID = unsigned;
using BundleCostPolicyID = unsigned;
using StreamPhaseAutomatonID = unsigned;

/// Object encoding profile identity. Production selection is always E96.
enum class ObjectEncodingProfileID : unsigned {
  E96 = 0,
  /// Test-only multi-family profile (synthetic short rows). Never product.
  TestSyntheticMulti = 0x8000u,
};

/// Architectural format family.
enum class BundleFormatID : unsigned {
  FormatE96 = 0,
  /// Test-only short families (unequal byte lengths). Never product.
  SynthShortA = 0x8000u,
  SynthShortB = 0x8001u,
};

/// Globally unique layout row. Family/size/phase derive from the descriptor.
enum class BundleFormatRowID : unsigned {
  E96TwoEntry = 0,
  E96ThreeEntry = 1,
  /// Synthetic rows: unequal sizes across families; equal-size pair inside A.
  SynthA_RowWide = 0x8000u,
  SynthA_RowNarrow = 0x8001u,
  SynthB_RowTiny = 0x8002u,
};

/// One exact encoded row (header, size, entry count, phase transition).
struct BundleFormatRowDesc {
  BundleFormatRowID Row = BundleFormatRowID::E96TwoEntry;
  BundleFormatID Format = BundleFormatID::FormatE96;
  const char *Name = nullptr;
  EncodedBits Bits{};
  EncodedBytes Bytes{};
  HeaderPredicateID Header = 0;
  PhaseTransitionID PhaseTransition = 0;
  /// Encoded entry positions owned by this row (2 for E2, 3 for E3).
  unsigned EntryCount = 0;
  /// Top-pad width in bits (geometry only; pad value policy is separate).
  unsigned TopPadBits = 0;
  /// False for synthetic test rows; true only for production E96 rows.
  bool IsProduct = false;
};

/// One architectural family and the rows it owns.
struct BundleFormatDesc {
  BundleFormatID Format = BundleFormatID::FormatE96;
  const char *Name = nullptr;
  /// Contiguous row descriptors for this family (stable order).
  ArrayRef<BundleFormatRowDesc> Rows;
  bool IsProduct = false;
  /// Stable ordinal for tie-break only after semantic/cost equality.
  unsigned StableOrdinal = 0;
};

/// Object-level encoding profile: permitted families + stream/ELF policy.
struct ObjectEncodingProfileDesc {
  ObjectEncodingProfileID Profile = ObjectEncodingProfileID::E96;
  const char *Name = nullptr;
  ArrayRef<BundleFormatID> PermittedFamilies;
  DecodeDispatchID DecodeDispatch = 0;
  PaddingPolicyID PaddingPolicy = 0;
  BundleCostPolicyID CostPolicy = 0;
  StreamPhaseAutomatonID StreamPhases = 0;
  /// ELF e_flags payload for this profile. ABI number allocation is owned
  /// elsewhere; until allocated this remains 0 and must not alone authorize
  /// object emission of a non-zero flag.
  uint32_t ELFFlagsValue = 0;
  bool IsProduct = false;
};

//===----------------------------------------------------------------------===//
// Registry queries (typed size APIs; no bare parcel-width literals at call sites)
//===----------------------------------------------------------------------===//

/// Sole production object-encoding profile (E96).
const ObjectEncodingProfileDesc &getProductionObjectEncodingProfile();

/// Profile descriptor, or nullptr if \p ID is unknown.
const ObjectEncodingProfileDesc *
getObjectEncodingProfile(ObjectEncodingProfileID ID);

/// Family descriptor, or nullptr if \p ID is unknown.
const BundleFormatDesc *getBundleFormat(BundleFormatID ID);

/// Row descriptor, or nullptr if \p ID is unknown.
const BundleFormatRowDesc *getBundleFormatRow(BundleFormatRowID ID);

/// All product rows (E96TwoEntry, E96ThreeEntry), stable order.
ArrayRef<BundleFormatRowDesc> getProductBundleFormatRows();

/// All product families (exactly FormatE96).
ArrayRef<BundleFormatDesc> getProductBundleFormats();

/// Synthetic non-product rows for unit tests only.
ArrayRef<BundleFormatRowDesc> getSyntheticTestBundleFormatRows();

/// Synthetic non-product families for unit tests only.
ArrayRef<BundleFormatDesc> getSyntheticTestBundleFormats();

/// EncodedBytes for a known row; nullopt if unknown.
std::optional<EncodedBytes> encodedBytesOf(BundleFormatRowID Row);

/// EncodedBits for a known row; nullopt if unknown.
std::optional<EncodedBits> encodedBitsOf(BundleFormatRowID Row);

/// EncodedBytes of a known row; asserts product/test row is registered.
EncodedBytes encodedBytesOrDie(BundleFormatRowID Row);

/// EncodedBits of a known row; asserts product/test row is registered.
EncodedBits encodedBitsOrDie(BundleFormatRowID Row);

/// Maximum EncodedBytes among rows permitted by \p Profile.
EncodedBytes maxEncodedBytesInProfile(ObjectEncodingProfileID Profile);

/// True iff \p Row is a production (E96) row.
bool isProductRow(BundleFormatRowID Row);

/// True iff \p Format is a production family.
bool isProductFamily(BundleFormatID Format);

/// True iff production profile E96 permits \p Row.
bool productionProfilePermitsRow(BundleFormatRowID Row);

/// True iff \p Profile permits \p Row (family membership).
bool profilePermitsRow(ObjectEncodingProfileID Profile,
                       BundleFormatRowID Row);

/// True iff \p Profile permits \p Format.
bool profilePermitsFamily(ObjectEncodingProfileID Profile,
                          BundleFormatID Format);

/// True iff \p Profile is the immutable production profile.
bool isProductionProfile(ObjectEncodingProfileID Profile);

/// Rows of a family (empty if unknown).
ArrayRef<BundleFormatRowDesc> rowsOfFamily(BundleFormatID Format);

/// Header indicator bits for Format E (geometry constant on the family).
inline constexpr unsigned FormatEIndicatorBits = 0x7u; // low 3 bits = 111

/// entry_num values selecting E2 / E3 internal geometry (not separate formats).
inline constexpr unsigned FormatEEntryNumTwo = 0;
inline constexpr unsigned FormatEEntryNumThree = 1;

} // namespace format
} // namespace haydn

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFORMAT_H
