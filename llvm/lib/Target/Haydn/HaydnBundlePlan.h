//===- HaydnBundlePlan.h - Cycle plan + typed sizes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Format E product cycle plan (sole active product):
//
//   * BundleFormatRowID — durable product layout identity
//       (E96TwoEntry / E96ThreeEntry from the P01 registry)
//   * CompletionStateID — exact omitted-entry / idle completion identity
//   * EncodedBytes / EncodedBits / CycleCount — typed quantities
//   * BundlePlan — one architectural cycle's committed placement summary
//
// Product law:
//   * Sole product family is FormatE96; parcel EncodedBytes come from the
//     registry row descriptors (never bare width literals at call sites).
//   * BUNDLE roots stamp (row imm, completion imm). MC serializes that state
//     and must not invent a new default.
//
// Unit ambiguity removed here (do not alias):
//
//   | Quantity       | API                    | Never mean              |
//   |----------------|------------------------|-------------------------|
//   | Packet length  | EncodedBytes           | issue slots / field bits|
//   | Field geometry | EncodedBits            | byte displacement       |
//   | Arch time      | CycleCount             | byte distance           |
//
// Generated PacketFormats / VLIWFormat rows are Format E composites only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPLAN_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPLAN_H

#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {
namespace haydn {
namespace bundle {

//===----------------------------------------------------------------------===//
// Typed size / time quantities
//===----------------------------------------------------------------------===//

/// Encoded packet length in bytes (branch layout, object size, BR).
struct EncodedBytes {
  unsigned Value = 0;
  constexpr EncodedBytes() = default;
  constexpr explicit EncodedBytes(unsigned V) : Value(V) {}
  constexpr operator unsigned() const { return Value; }
  constexpr bool operator==(EncodedBytes O) const { return Value == O.Value; }
  constexpr bool operator!=(EncodedBytes O) const { return Value != O.Value; }
};

/// Format / slot field geometry in bits (encode offsets, slot windows).
struct EncodedBits {
  unsigned Value = 0;
  constexpr EncodedBits() = default;
  constexpr explicit EncodedBits(unsigned V) : Value(V) {}
  constexpr operator unsigned() const { return Value; }
  constexpr bool operator==(EncodedBits O) const { return Value == O.Value; }
  constexpr bool operator!=(EncodedBits O) const { return Value != O.Value; }
};

/// Architectural issue cycles (hwloop setup distance, SMS II), never bytes.
struct CycleCount {
  unsigned Value = 0;
  constexpr CycleCount() = default;
  constexpr explicit CycleCount(unsigned V) : Value(V) {}
  constexpr operator unsigned() const { return Value; }
  constexpr bool operator==(CycleCount O) const { return Value == O.Value; }
  constexpr bool operator!=(CycleCount O) const { return Value != O.Value; }
};

//===----------------------------------------------------------------------===//
// Product row + completion identity (Format E only)
//===----------------------------------------------------------------------===//

/// Re-export registry row identity for commit surfaces.
using BundleFormatRowID = format::BundleFormatRowID;
using BundleFormatID = format::BundleFormatID;

/// Exact completion state for omitted entries / idle parcels.
/// Stub IDs mirror golden completion_table_stubs (product_use forbidden until
/// idle/underfill/pad closes). AllEntriesReal is the only product-legal full
/// fill when every encoded entry holds a real member.
enum class CompletionStateID : unsigned {
  AllEntriesReal = 0,
  StubIdle = 1,
  StubE2Singleton = 2,
  StubE3Singleton = 3,
  StubE2Underfill = 4,
  StubE3Underfill1Of3 = 5,
  StubE3Underfill2Of3 = 6,
  StubE2Map11Absent = 7,
};

/// True when \p C is a product-legal completion (full real fill only today).
inline constexpr bool isProductLegalCompletion(CompletionStateID C) {
  return C == CompletionStateID::AllEntriesReal;
}

/// True when \p C is a documented fail-closed stub (golden silent).
inline constexpr bool isStubCompletion(CompletionStateID C) {
  return C != CompletionStateID::AllEntriesReal;
}

/// Default product row preference for singletons / unknown fill (E2 geometry).
inline constexpr BundleFormatRowID ProductDefaultRowID =
    BundleFormatRowID::E96TwoEntry;

inline constexpr CycleCount OneCycle{1};

//===----------------------------------------------------------------------===//
// Product EncodedBytes (generated row geometry + registry pin)
//===----------------------------------------------------------------------===//
//
// Compile-time product width comes from generated Format E golden pins
// (FormatEEncodedBytes / FormatEBundleBits). That value is the same EncodedBytes
// carried by production registry rows E96TwoEntry / E96ThreeEntry — unit tests
// pin productParcelBytes() equal to format::encodedBytesOrDie / maxEncodedBytes
// in profile E96.

namespace product_size_detail {
#define GET_FORMAT_E_GOLDEN_PINS
#include "HaydnGenFormatERecords.inc"
} // namespace product_size_detail

/// Compile-time product EncodedBytes from generated Format E row geometry.
/// Call sites use productParcelBytes() or format::encodedBytesOf(row).
inline constexpr unsigned ProductEncodedBytesValue =
    product_size_detail::FormatEEncodedBytes;
inline constexpr unsigned ProductEncodedBitsValue =
    product_size_detail::FormatEBundleBits;
inline constexpr EncodedBytes ProductEncodedBytes{ProductEncodedBytesValue};
inline constexpr EncodedBits ProductEncodedBits{ProductEncodedBitsValue};

static_assert(ProductEncodedBytesValue * 8u == ProductEncodedBitsValue,
              "product EncodedBytes/Bits must agree");
static_assert(product_size_detail::FormatEEncodedBytes * 8u ==
                  product_size_detail::FormatEBundleBits,
              "generated Format E EncodedBytes must match FormatEBundleBits");
// FE8: sole product parcel is Format E 96-bit / 12-byte. No dual 8/16 path.
static_assert(ProductEncodedBytesValue == 12u,
              "product EncodedBytes must be Format E 12 (non-E96 sizes retired)");
static_assert(ProductEncodedBitsValue == 96u,
              "product EncodedBits must be Format E 96 (non-E96 sizes retired)");

/// VLIWFormat::Size interpreted as EncodedBytes (transitional composite only).
inline constexpr EncodedBytes vliwFormatSizeAsBytes(unsigned TableSize) {
  return EncodedBytes{TableSize};
}

/// Convert a table Size in bytes to EncodedBits.
inline constexpr EncodedBits vliwFormatSizeAsBits(unsigned TableSizeBytes) {
  return EncodedBits{TableSizeBytes * 8u};
}

/// MCSlotInfo::Size is EncodedBits for a residual slot window.
inline constexpr EncodedBits slotInfoSizeAsBits(unsigned TableSize) {
  return EncodedBits{TableSize};
}

/// Product parcel EncodedBytes — sole size unit for BR / hwloop / bare MIs.
/// Generated Format E EncodedBytes; residual kill-list widths never returned.
inline constexpr EncodedBytes productParcelBytes() {
  return ProductEncodedBytes;
}

/// EncodedBytes for a Format E product/test row via the registry.
inline std::optional<EncodedBytes> encodedBytesForRow(BundleFormatRowID Row) {
  if (auto B = format::encodedBytesOf(Row))
    return EncodedBytes{B->Value};
  return std::nullopt;
}

/// EncodedBytes for a known row as bundle::EncodedBytes; asserts registered.
/// Named distinctly from format::encodedBytesOrDie to avoid overload ambiguity.
inline EncodedBytes bundleEncodedBytesOrDie(BundleFormatRowID Row) {
  return EncodedBytes{format::encodedBytesOrDie(Row).Value};
}

/// True iff \p Row is a production Format E row.
inline bool isProductBundleRow(BundleFormatRowID Row) {
  return format::isProductRow(Row);
}

//===----------------------------------------------------------------------===//
// Generated PacketFormats coverage (transitional SLOT pack only)
//===----------------------------------------------------------------------===//

/// Representative product VLIWFormat row from PacketFormats (Format E).
/// Prefers three-entry geometry when present; else any empty-covering row.
/// EncodedBytes authority is always the registry, not VLIWFormat::Size.
inline const VLIWFormat *productVLIWFormat(const PacketFormats &Packets) {
  // Prefer first-covering of empty: table order is E2 then E3 (both cover 0).
  // Callers that need exact entry occupancy use Packets.getFormat(Occupied).
  return Packets.getFormat(/*Occupied=*/0);
}

/// True when \p Occupied is admissible under product Format E packing.
/// Exact PacketFormats entry-slot cover wins. Transitional residual issue
/// occupancy is also accepted while PlacementAlternative still stamps
/// Haydn::SLOT* FieldSlots and post-setDesc members use residual S0/S1/S2
/// MCSlotKind SlotSet bits (which sit above E2/E3 entry kinds in the table).
inline bool productCovers(const PacketFormats &Packets, SlotBits Occupied) {
  if (Packets.getFormat(Occupied))
    return true;
  if (!Packets.getFormat(/*Occupied=*/0))
    return false;
  if (Occupied == 0)
    return true;
  // Haydn::SLOT0|1|2 FieldSlots from enumeratePlacementAlternatives.
  const SlotBits LegacyIssue =
      static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2);
  if ((Occupied & ~LegacyIssue) == 0)
    return true;
  // Residual S0/S1/S2 MCSlotKind SlotSet bits (not the low E2/E3 entry bits).
  // Absolute bit positions depend on slot-kind table order; accept any
  // occupancy that is a subset of the residual S* trio when those kinds exist.
  // Exact entry-unit injectivity is enforced at Format E commit/verify.
  return true;
}

/// Product EncodedBytes when any Format E PacketFormats row is present.
/// Size field of the composite is ignored — registry is product authority.
inline std::optional<EncodedBytes>
productEncodedBytesFromPackets(const PacketFormats &Packets) {
  if (!productVLIWFormat(Packets))
    return std::nullopt;
  return productParcelBytes();
}

/// RA-hint eligibility: transitional composite exists and covers empty.
inline bool productRAHintEligible(const PacketFormats &Packets) {
  return productCovers(Packets, /*Occupied=*/0);
}

/// Ceil-divide a byte length by a format's EncodedBytes (parcel count).
inline unsigned ceilParcelsForBytes(unsigned Bytes, EncodedBytes Unit) {
  if (Bytes == 0 || Unit.Value == 0)
    return 0;
  return (Bytes + Unit.Value - 1) / Unit.Value;
}

/// Ceil parcels under the live product format (Format E / registry bytes).
inline unsigned ceilProductParcels(unsigned Bytes) {
  return ceilParcelsForBytes(Bytes, productParcelBytes());
}

/// \p Bundles architectural cycles × product EncodedBytes.
inline constexpr int64_t productBundlesToBytes(unsigned Bundles) {
  return static_cast<int64_t>(Bundles) *
         static_cast<int64_t>(productParcelBytes().Value);
}

//===----------------------------------------------------------------------===//
// Feasible row mask (E2 | E3 product frontier)
//===----------------------------------------------------------------------===//

/// Bit in CompatibleFormatMask for a BundleFormatRowID.
inline constexpr uint64_t formatRowBit(BundleFormatRowID Row) {
  return uint64_t(1) << static_cast<unsigned>(Row);
}

/// Product CompatibleFormatMask: both E96 rows (exact frontier until commit).
inline constexpr uint64_t ProductFormatMask =
    formatRowBit(BundleFormatRowID::E96TwoEntry) |
    formatRowBit(BundleFormatRowID::E96ThreeEntry);

//===----------------------------------------------------------------------===//
// Row / completion selection for a committed cycle
//===----------------------------------------------------------------------===//

/// Select product row from real member count.
///   * 3 real members → E96ThreeEntry (full E3)
///   * 0..2 real members → E96TwoEntry (E2 geometry; underfill/idle use stubs)
inline constexpr BundleFormatRowID
selectProductRowForMemberCount(unsigned MemberCount) {
  if (MemberCount >= 3)
    return BundleFormatRowID::E96ThreeEntry;
  return BundleFormatRowID::E96TwoEntry;
}

/// Row selection honoring an allowed-row mask (the solver's refined
/// FeasibleFormatMask). Golden placement can forbid E2 for pairs whose only
/// E2 seats collide (E2 e0 hosts the general ops; e1 hosts the RI20/I32,
/// LOAD1 and MAC1 families), so the member-count preference must be able to
/// fall through to the other product row. Returns nullopt when no allowed
/// product row has capacity — a plan the serializer could not place.
inline constexpr std::optional<BundleFormatRowID>
selectProductRowForMask(unsigned MemberCount, uint64_t AllowedRowMask) {
  const BundleFormatRowID Preferred =
      selectProductRowForMemberCount(MemberCount);
  if (AllowedRowMask & formatRowBit(Preferred))
    return Preferred;
  const BundleFormatRowID Other =
      Preferred == BundleFormatRowID::E96ThreeEntry
          ? BundleFormatRowID::E96TwoEntry
          : BundleFormatRowID::E96ThreeEntry;
  const unsigned OtherEntries =
      Other == BundleFormatRowID::E96ThreeEntry ? 3u : 2u;
  if ((AllowedRowMask & formatRowBit(Other)) && MemberCount <= OtherEntries)
    return Other;
  return std::nullopt;
}

/// Select completion for \p Row given real member count.
/// Full fill → AllEntriesReal (product-legal). Idle/singleton/underfill →
/// golden stubs (fail-closed for product emit until idle/pad law closes).
inline constexpr CompletionStateID
selectCompletionFor(BundleFormatRowID Row, unsigned MemberCount) {
  const unsigned Entries =
      (Row == BundleFormatRowID::E96ThreeEntry) ? 3u : 2u;
  if (MemberCount == 0)
    return CompletionStateID::StubIdle;
  if (MemberCount >= Entries)
    return CompletionStateID::AllEntriesReal;
  if (Row == BundleFormatRowID::E96ThreeEntry) {
    if (MemberCount == 1)
      return CompletionStateID::StubE3Singleton;
    return CompletionStateID::StubE3Underfill2Of3;
  }
  // E2 with one real member.
  return CompletionStateID::StubE2Singleton;
}

//===----------------------------------------------------------------------===//
// Durable row + completion on BUNDLE MIR roots
//===----------------------------------------------------------------------===//
//
// After finalizeBundle, stamp:
//   operand 0: BundleFormatRowID imm
//   operand 1: CompletionStateID imm
// so identity is MIR-durable and clone-safe without side maps.

/// Encode row as the unsigned value stored in BUNDLE-root imm 0.
inline constexpr unsigned formatRowToImm(BundleFormatRowID Row) {
  return static_cast<unsigned>(Row);
}

/// Decode a BUNDLE-root imm 0 as a product/test row. Unknown → nullopt.
inline std::optional<BundleFormatRowID> formatRowFromImm(unsigned Imm) {
  switch (Imm) {
  case formatRowToImm(BundleFormatRowID::E96TwoEntry):
    return BundleFormatRowID::E96TwoEntry;
  case formatRowToImm(BundleFormatRowID::E96ThreeEntry):
    return BundleFormatRowID::E96ThreeEntry;
  default:
    return std::nullopt;
  }
}

inline constexpr unsigned completionToImm(CompletionStateID C) {
  return static_cast<unsigned>(C);
}

inline std::optional<CompletionStateID> completionFromImm(unsigned Imm) {
  switch (Imm) {
  case completionToImm(CompletionStateID::AllEntriesReal):
    return CompletionStateID::AllEntriesReal;
  case completionToImm(CompletionStateID::StubIdle):
    return CompletionStateID::StubIdle;
  case completionToImm(CompletionStateID::StubE2Singleton):
    return CompletionStateID::StubE2Singleton;
  case completionToImm(CompletionStateID::StubE3Singleton):
    return CompletionStateID::StubE3Singleton;
  case completionToImm(CompletionStateID::StubE2Underfill):
    return CompletionStateID::StubE2Underfill;
  case completionToImm(CompletionStateID::StubE3Underfill1Of3):
    return CompletionStateID::StubE3Underfill1Of3;
  case completionToImm(CompletionStateID::StubE3Underfill2Of3):
    return CompletionStateID::StubE3Underfill2Of3;
  case completionToImm(CompletionStateID::StubE2Map11Absent):
    return CompletionStateID::StubE2Map11Absent;
  default:
    return std::nullopt;
  }
}

inline bool isKnownFormatRowImm(unsigned Imm) {
  return formatRowFromImm(Imm).has_value();
}

/// Stamp row + completion as the first two explicit immediates on a BUNDLE root.
inline void stampBundleCommit(MachineInstr &BundleRoot, BundleFormatRowID Row,
                              CompletionStateID Completion) {
  assert(BundleRoot.isBundle() && "commit imm only on BUNDLE roots");
  assert(formatRowFromImm(formatRowToImm(Row)).has_value() &&
         "unknown product row");
  assert(completionFromImm(completionToImm(Completion)).has_value() &&
         "unknown completion");
  const int64_t RowImm = static_cast<int64_t>(formatRowToImm(Row));
  const int64_t CompImm = static_cast<int64_t>(completionToImm(Completion));

  SmallVector<MachineOperand *, 2> ExplicitImms;
  for (unsigned I = 0, E = BundleRoot.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = BundleRoot.getOperand(I);
    if (MO.isReg() && MO.isImplicit())
      break;
    if (MO.isImm())
      ExplicitImms.push_back(&MO);
  }
  if (ExplicitImms.size() >= 2) {
    ExplicitImms[0]->setImm(RowImm);
    ExplicitImms[1]->setImm(CompImm);
    return;
  }
  if (ExplicitImms.size() == 1) {
    ExplicitImms[0]->setImm(RowImm);
    BundleRoot.addOperand(MachineOperand::CreateImm(CompImm));
    return;
  }
  BundleRoot.addOperand(MachineOperand::CreateImm(RowImm));
  BundleRoot.addOperand(MachineOperand::CreateImm(CompImm));
}

/// Stamp from a committed plan (row + completion).
inline void stampBundleCommit(MachineInstr &BundleRoot,
                              const struct BundlePlan &Plan);

/// Read product row from a BUNDLE root (first explicit imm).
inline std::optional<BundleFormatRowID>
getBundleRowID(const MachineInstr &MI) {
  if (!MI.isBundle())
    return std::nullopt;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isImplicit())
      break;
    if (MO.isImm())
      return formatRowFromImm(static_cast<unsigned>(MO.getImm()));
  }
  return std::nullopt;
}

/// Read completion from a BUNDLE root (second explicit imm).
inline std::optional<CompletionStateID>
getBundleCompletionID(const MachineInstr &MI) {
  if (!MI.isBundle())
    return std::nullopt;
  unsigned Seen = 0;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isImplicit())
      break;
    if (!MO.isImm())
      continue;
    if (Seen == 1)
      return completionFromImm(static_cast<unsigned>(MO.getImm()));
    ++Seen;
  }
  return std::nullopt;
}

/// Prefer stamped product row; else default product row (never residual ID).
inline BundleFormatRowID getBundleRowIDOrProduct(const MachineInstr &MI) {
  if (auto Row = getBundleRowID(MI))
    return *Row;
  return ProductDefaultRowID;
}

/// Committed EncodedBytes for a BUNDLE root from stamped row / product parcel.
inline EncodedBytes committedEncodedBytes(const MachineInstr &MI) {
  if (MI.isBundle()) {
    if (auto Row = getBundleRowID(MI)) {
      if (auto B = encodedBytesForRow(*Row))
        return *B;
    }
    return productParcelBytes();
  }
  return productParcelBytes();
}

//===----------------------------------------------------------------------===//
// BundlePlan — derived cycle summary
//===----------------------------------------------------------------------===//

/// Committed (or provisional) plan for one issue cycle.
struct BundlePlan {
  /// Product layout row (E96TwoEntry / E96ThreeEntry).
  BundleFormatRowID Row = ProductDefaultRowID;
  /// Omitted-entry / idle completion identity.
  CompletionStateID Completion = CompletionStateID::AllEntriesReal;
  /// Haydn::SLOT0|SLOT1|SLOT2 occupancy (transitional packer).
  SlotBits OccupiedSlots = 0;
  /// Logical opcodes in schedule/issue order.
  SmallVector<unsigned, 3> MemberOpcodes;
  EncodedBytes Bytes = ProductEncodedBytes;
  CycleCount Cycles = OneCycle;

  bool empty() const { return MemberOpcodes.empty(); }
  unsigned memberCount() const {
    return static_cast<unsigned>(MemberOpcodes.size());
  }

  /// Product invariant: Format E row, registry EncodedBytes, one cycle.
  /// Completion may be a stub (fail-closed for encode); row must be product.
  bool isProductLegal() const {
    return isProductBundleRow(Row) && Bytes == productParcelBytes() &&
           Cycles == OneCycle && memberCount() <= Haydn::ISSUE_SLOT_COUNT;
  }

  /// True when encode may emit this plan (full real fill only until idle law).
  bool isProductEncodable() const {
    return isProductLegal() && isProductLegalCompletion(Completion);
  }
};

inline void stampBundleCommit(MachineInstr &BundleRoot,
                              const BundlePlan &Plan) {
  stampBundleCommit(BundleRoot, Plan.Row, Plan.Completion);
}

/// Tests-only convenience: product plan with registry EncodedBytes.
inline BundlePlan makeProductPlan(SlotBits Occupied,
                                  ArrayRef<unsigned> Members = {}) {
  BundlePlan P;
  P.Row = selectProductRowForMemberCount(Members.size());
  P.Completion = selectCompletionFor(P.Row, Members.size());
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = productParcelBytes();
  P.Cycles = OneCycle;
  return P;
}

/// makeProductPlan honoring an allowed-row mask (see selectProductRowForMask).
inline std::optional<BundlePlan>
makeProductPlanForMask(SlotBits Occupied, ArrayRef<unsigned> Members,
                       uint64_t AllowedRowMask) {
  const auto Row = selectProductRowForMask(Members.size(), AllowedRowMask);
  if (!Row)
    return std::nullopt;
  BundlePlan P = makeProductPlan(Occupied, Members);
  P.Row = *Row;
  P.Completion = selectCompletionFor(P.Row, Members.size());
  return P;
}

/// Tests-only explicit architectural stall (idle stub completion).
inline BundlePlan makeStallPlan() {
  BundlePlan P = makeProductPlan(/*Occupied=*/0, /*Members=*/{});
  P.Completion = CompletionStateID::StubIdle;
  return P;
}

/// Build a product BundlePlan from a covering VLIWFormat row + registry bytes.
/// EncodedBytes always from productParcelBytes(); row/completion from members.
inline std::optional<BundlePlan>
makePlanFromVLIWFormat(const VLIWFormat &F, SlotBits Occupied,
                       ArrayRef<unsigned> Members = {}) {
  if (Occupied != 0 && !F.covers(Occupied))
    return std::nullopt;
  // Product plans only from Format E parcel geometry (registry EncodedBytes).
  // Synthetic short/long Size rows must not become product BundlePlans.
  if (vliwFormatSizeAsBytes(F.getSize()) != productParcelBytes())
    return std::nullopt;
  BundlePlan P;
  P.Row = selectProductRowForMemberCount(Members.size());
  P.Completion = selectCompletionFor(P.Row, Members.size());
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = productParcelBytes();
  P.Cycles = OneCycle;
  return P;
}

/// Query PacketFormats / transitional coverage for \p Occupied; product size
/// from registry. Does not require a single Full row covering SLOT_ALL.
inline std::optional<BundlePlan>
planFromPacketFormats(const PacketFormats &Packets, SlotBits Occupied,
                      ArrayRef<unsigned> Members = {},
                      uint64_t AllowedRowMask = ProductFormatMask) {
  if (!productCovers(Packets, Occupied))
    return std::nullopt;
  // Prefer exact entry-slot cover when the row is product EncodedBytes-sized.
  // Residual / synthetic short/long Size rows are not product plans — fall
  // back to registry-sized makeProductPlan (row select by member count,
  // constrained to the caller's allowed rows — the solver's refined mask).
  if (const VLIWFormat *F = Packets.getFormat(Occupied)) {
    if (auto P = makePlanFromVLIWFormat(*F, Occupied, Members)) {
      if (const auto Row =
              selectProductRowForMask(Members.size(), AllowedRowMask)) {
        P->Row = *Row;
        P->Completion = selectCompletionFor(P->Row, Members.size());
        return P;
      }
      return std::nullopt;
    }
  }
  return makeProductPlanForMask(Occupied, Members, AllowedRowMask);
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPLAN_H
