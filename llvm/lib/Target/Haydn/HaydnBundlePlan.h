//===- HaydnBundlePlan.h - Cycle plan + typed sizes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bundle128 cycle plan foundation (behavior-preserving for Bundle128):
//
//   * FormatID — durable product format identity (today: only Bundle128Full)
//   * EncodedBytes / EncodedBits / CycleCount — typed quantities (plan §3.5)
//   * BundlePlan — one architectural cycle's committed placement summary
//
// Unit ambiguity removed here (do not alias):
//
//   | Quantity       | API                    | Never mean              |
//   |----------------|------------------------|-------------------------|
//   | Packet length  | EncodedBytes           | issue slots / field bits|
//   | Field geometry | EncodedBits            | byte displacement       |
//   | Arch time      | CycleCount             | byte distance           |
//
// Generated table units (pin in unit tests):
//   * VLIWFormat::Size for BUNDLE128_FULL is **EncodedBytes** (value 16).
//     (Older comments said "bits"; the table value 16 is the parcel byte size.)
//   * MCSlotInfo::Size is **EncodedBits** per slot window (48 / 40 / 40).
//     Sum = Bundle128EncodedBits (128).
//
// This header is the single numeric authority for the product parcel size.
// getInstSizeInBytes, FixupHwLoops, and HardwareLoops all read
// encodedBytesFor(FormatID) / ProductFormatDesc.Bytes (AIE
// getAIEMachineBundleSize → Format->getSize(), AIEBaseInstrInfo.cpp:546-555;
// getInstSizeInBytes → table Size, AIE1InstrInfo.cpp:646-651). No second
// hard-coded "always 16" oracle independent of FormatID.
//
// Multi-MI BUNDLE roots carry a durable FormatID immediate (plan §6.3).
// Singleton real MIs also become BUNDLE + FormatID via HaydnFinalizeBundle
// (AIEFinalizeBundle.cpp:22-54 peer).
// Solver-facing FormatDesc {FormatID, Priority, EncodedBytes, SlotSet}
// and first-covering Priority ranking (AIE PacketFormats::getFormat
// AIEFormat.cpp:18-27 table-order first covering → explicit Priority per
// plan §4.3). Product live table remains one row (BUNDLE128_FULL).
// Pure CycleState tryAdd/commit in HaydnBundleFormatSolver.h
// (AIEBundle.h canAdd/add + HazardRecognizer alt try); Bundle/HR/SMS are
// adapters. Placement is alts-only; materialize via setDesc.
//
// productFeasibleFormatMask / Bundle+ResourceCycle getFeasibleFormatMask
// expose the Pre-RA/SMS FormatID frontier (size-1 Full product; logical only;
// plan §7.1). No freeze of FormatID before post-RA.
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
// Typed size / time quantities (plan §3.5)
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
// Product format identity (only Bundle128 until BF0+BF5)
//===----------------------------------------------------------------------===//

/// Stable FormatID. Only Bundle128Full is authorized for product encode.
/// Compact IDs must not be invented here — blocked on BF0 hardware manifest.
enum class FormatID : unsigned {
  Bundle128Full = 0,
};

/// Sole live product format.
inline constexpr FormatID ProductFormatID = FormatID::Bundle128Full;

/// Bundle128 parcel: 16 bytes = 128 bits = one architectural cycle when issued.
inline constexpr unsigned Bundle128EncodedBytesValue = 16;
inline constexpr unsigned Bundle128EncodedBitsValue = 128;
inline constexpr EncodedBytes Bundle128EncodedBytes{Bundle128EncodedBytesValue};
inline constexpr EncodedBits Bundle128EncodedBits{Bundle128EncodedBitsValue};
inline constexpr CycleCount OneCycle{1};

/// Physical slot window widths (bits) — match generated MCSlotInfo.
inline constexpr EncodedBits Slot0EncodedBits{48};
inline constexpr EncodedBits Slot1EncodedBits{40};
inline constexpr EncodedBits Slot2EncodedBits{40};

static_assert(Slot0EncodedBits.Value + Slot1EncodedBits.Value +
                      Slot2EncodedBits.Value ==
                  Bundle128EncodedBitsValue,
              "slot windows must sum to Bundle128 bit width");

//===----------------------------------------------------------------------===//
// Table unit conversions (generated VLIWFormat / MCSlotInfo)
//===----------------------------------------------------------------------===//

/// VLIWFormat::Size for the product BUNDLE128_FULL row is EncodedBytes (16).
inline constexpr EncodedBytes vliwFormatSizeAsBytes(unsigned TableSize) {
  return EncodedBytes{TableSize};
}

/// Convert product packet table Size (bytes) to bits.
inline constexpr EncodedBits vliwFormatSizeAsBits(unsigned TableSizeBytes) {
  return EncodedBits{TableSizeBytes * 8u};
}

/// MCSlotInfo::Size is EncodedBits for the slot window.
inline constexpr EncodedBits slotInfoSizeAsBits(unsigned TableSize) {
  return EncodedBits{TableSize};
}

/// EncodedBytes for a FormatID, or nullopt if it is not a live product row.
///
/// Reads the format table rather than switching on the ID. The switch was
/// equivalent while there was one row, but format E has two (BUNDLE_E2 and
/// BUNDLE_E3) and a switch is the shape that has to be edited every time the
/// row set changes — which is precisely what this migration does. Defined
/// below the table; declared here because encodedBytesOrProduct wants it.
inline std::optional<EncodedBytes> encodedBytesFor(FormatID ID);

/// EncodedBytes for a known FormatID, else the default parcel size.
inline EncodedBytes encodedBytesOrProduct(FormatID ID);

/// Whether \p ID names a live product format row.
inline bool isProductFormat(FormatID ID);

//===----------------------------------------------------------------------===//
// FormatDesc — solver-facing format row (plan §6.1)
//===----------------------------------------------------------------------===//
//
// AIE VLIWFormat (AIEFormat.h:44-70) carries Opcode/Name/Slots/Size/SlotSet and
// PacketFormats::getFormat does first-covering scan in table order
// (AIEFormat.cpp:18-27). Haydn strengthens that into an explicit Priority
// ranking (plan §4.3): among formats that cover OccupiedSlots, the lowest
// Priority wins; equal Priority keeps earlier table index (stable AIE-like
// order). Product table is still a single BUNDLE128_FULL row; unit tests may
// build multi-row synthetic tables (AIE BundleTest.cpp:33-41 FormatData[]
// pattern) without product emit.

/// Priority among covering formats. Lower value is preferred.
using FormatPriority = unsigned;

/// Solver-facing description of one packet format (plan §6.1 FormatDesc).
/// Distinct from MCFormatDesc (encoding geometry); this is the plan/solver
/// authority for FormatID + size + slot coverage + Priority.
struct FormatDesc {
  FormatID FID = ProductFormatID;
  /// Lower wins among covering formats (AIE table-order strengthen).
  FormatPriority Priority = 0;
  EncodedBytes Bytes = Bundle128EncodedBytes;
  /// Haydn::SLOT* bitset this format can accommodate (VLIWFormat::SlotSet).
  SlotBits SlotSet = 0;

  constexpr FormatDesc() = default;
  constexpr FormatDesc(FormatID ID, FormatPriority Prio, EncodedBytes B,
                       SlotBits Slots)
      : FID(ID), Priority(Prio), Bytes(B), SlotSet(Slots) {}

  /// AIE VLIWFormat::covers (AIEFormat.cpp:18): true iff Slots ⊆ SlotSet.
  constexpr bool covers(SlotBits Slots) const {
    return !(Slots & ~SlotSet);
  }

  constexpr bool isProduct() const { return FID == ProductFormatID; }
};

//===----------------------------------------------------------------------===//
// The product format table
//===----------------------------------------------------------------------===//
//
// ONE ROW TODAY, and the code below must not assume that. Format E replaces
// this table with two rows — BUNDLE_E2 (entries P20,P21) and BUNDLE_E3
// (P30,P31,P32), both 12 bytes — and everything here is written so that the
// switch is a change of DATA, not of shape. What changes at the switch:
//
//   * the rows: one becomes two, 16 bytes becomes 12
//   * FormatID's enumerators
//   * the slot-window widths and their static_assert
//
// What does NOT change: every lookup below, because they all scan the table.
//
// Note the two format E rows have the same Bytes, so productParcelBytes()
// stays single-valued across the switch. It is asserted, not assumed — a
// future mixed-size table would fire the assert rather than silently hand
// BranchRelaxation the wrong parcel size (§ 6.x: a wrong stride does not fail
// cleanly).
inline constexpr FormatDesc ProductFormatRows[] = {
    {FormatID::Bundle128Full, /*Priority=*/0, Bundle128EncodedBytes,
     /*SlotSet=*/static_cast<SlotBits>(Haydn::SLOT0 | Haydn::SLOT1 |
                                       Haydn::SLOT2)},
};

inline constexpr unsigned ProductFormatRowCount =
    sizeof(ProductFormatRows) / sizeof(ProductFormatRows[0]);

/// Back-compat alias for the first row. Prefer the table; this exists for the
/// callers that genuinely want "the default format" rather than "some format".
inline constexpr const FormatDesc &ProductFormatDesc = ProductFormatRows[0];

inline std::optional<EncodedBytes> encodedBytesFor(FormatID ID) {
  for (const FormatDesc &F : ProductFormatRows)
    if (F.FID == ID)
      return F.Bytes;
  return std::nullopt;
}

inline EncodedBytes encodedBytesOrProduct(FormatID ID) {
  if (auto B = encodedBytesFor(ID))
    return *B;
  return ProductFormatRows[0].Bytes;
}

inline bool isProductFormat(FormatID ID) {
  return encodedBytesFor(ID).has_value();
}

/// Product parcel EncodedBytes — the size unit for BR / hwloop / bare MIs.
/// Prefer this over a free-floating "16" or a parallel Bundle128Bytes magic.
///
/// Meaningful only while every row is the same size, which holds for Bundle128
/// (one row) and for format E (two rows, both 12 bytes). The assert is the
/// gate: if a row set ever mixes sizes, callers that want "the" parcel size
/// have to be revisited rather than silently given the first row's.
inline constexpr EncodedBytes productParcelBytes() {
  return ProductFormatRows[0].Bytes;
}

namespace detail {
inline constexpr bool allRowsSameSize() {
  for (const FormatDesc &F : ProductFormatRows)
    if (F.Bytes.Value != ProductFormatRows[0].Bytes.Value)
      return false;
  return true;
}
} // namespace detail
static_assert(detail::allRowsSameSize(),
              "productParcelBytes() assumes every product format row encodes "
              "to the same number of bytes; add a per-format query instead");

/// Ceil-divide a byte length by a format's EncodedBytes (parcel count).
/// AIE ZOL setup distances sum Format->getSize() then compare in bytes
/// (AIEMachineAlignment.cpp:287+); Haydn also needs parcel counts for
/// MinSetupBundles — always divide by FormatDesc.Bytes, never a second oracle.
inline unsigned ceilParcelsForBytes(unsigned Bytes, EncodedBytes Unit) {
  if (Bytes == 0 || Unit.Value == 0)
    return 0;
  return (Bytes + Unit.Value - 1) / Unit.Value;
}

/// Ceil parcels under the live product format (BUNDLE128_FULL / 16 B).
inline unsigned ceilProductParcels(unsigned Bytes) {
  return ceilParcelsForBytes(Bytes, productParcelBytes());
}

/// \p Bundles architectural cycles × product EncodedBytes.
inline constexpr int64_t productBundlesToBytes(unsigned Bundles) {
  return static_cast<int64_t>(Bundles) *
         static_cast<int64_t>(productParcelBytes().Value);
}

/// Bit in CompatibleFormatMask for \p ID (1u << formatID imm).
inline constexpr uint64_t formatIDBit(FormatID ID) {
  return uint64_t(1) << static_cast<unsigned>(ID);
}

/// Product CompatibleFormatMask: every live row, ORed from the table.
/// Was a single formatIDBit(Bundle128Full); format E makes it two bits, and a
/// member compatible with only one composite becomes expressible.
inline constexpr uint64_t computeProductFormatMask() {
  uint64_t M = 0;
  for (const FormatDesc &F : ProductFormatRows)
    M |= formatIDBit(F.FID);
  return M;
}
inline constexpr uint64_t ProductFormatMask = computeProductFormatMask();

/// Product FormatDesc table. Scan it; do not assume its length.
inline ArrayRef<FormatDesc> productFormatTable() {
  return ArrayRef<FormatDesc>(ProductFormatRows, ProductFormatRowCount);
}

/// First covering format with best (lowest) Priority.
/// Port of AIE PacketFormats::getFormat first-covering scan
/// (AIEFormat.cpp:18-27) with explicit Priority ranking (plan §4.3).
/// Equal Priority: earlier table index wins (stable AIE table-order tie-break).
/// \returns nullptr if no row covers \p Occupied.
inline const FormatDesc *
selectFormatByPriority(ArrayRef<FormatDesc> Table, SlotBits Occupied) {
  const FormatDesc *Best = nullptr;
  for (const FormatDesc &F : Table) {
    if (!F.covers(Occupied))
      continue;
    if (!Best || F.Priority < Best->Priority)
      Best = &F;
  }
  return Best;
}

//===----------------------------------------------------------------------===//
// Durable FormatID on BUNDLE MIR roots (plan §6.3)
//===----------------------------------------------------------------------===//
//
// AIE re-infers format from occupied slots after finalizeBundle
// (AIEHazardRecognizer.cpp:278-312 applyFormatOrdering→finalizeBundle;
// AIEBundle.h:150-156 getFormatOrNull). Haydn strengthens that for multi-format
// readiness: after finalizeBundle, stamp FormatID as the first explicit imm on
// the TargetOpcode::BUNDLE root so identity is MIR-durable and clone-safe
// without pointer plans or MF side maps.
//
// Product today: only Bundle128Full (imm 0). API is FormatID-typed for N rows.
// Multi-MI stamped in PostRA materialize; singletons in HaydnFinalizeBundle.

/// Encode FormatID as the unsigned value stored in the BUNDLE-root imm.
inline constexpr unsigned formatIDToImm(FormatID ID) {
  return static_cast<unsigned>(ID);
}

/// Decode a BUNDLE-root imm. Unknown values → nullopt (N-format-ready gate).
inline std::optional<FormatID> formatIDFromImm(unsigned Imm) {
  switch (Imm) {
  case formatIDToImm(FormatID::Bundle128Full):
    return FormatID::Bundle128Full;
  }
  return std::nullopt;
}

/// True iff \p Imm is a known FormatID encoding.
inline bool isKnownFormatIDImm(unsigned Imm) {
  return formatIDFromImm(Imm).has_value();
}

/// Stamp FormatID as the first explicit immediate on a BUNDLE root.
/// Call after finalizeBundle (AIEHazardRecognizer.cpp:312 peer). Replaces an
/// existing FormatID imm if present; otherwise inserts before implicit regs.
inline void stampBundleFormatID(MachineInstr &BundleRoot, FormatID ID) {
  assert(BundleRoot.isBundle() && "FormatID imm only on BUNDLE roots");
  assert(formatIDFromImm(formatIDToImm(ID)).has_value() &&
         "unknown FormatID");
  const int64_t Imm = static_cast<int64_t>(formatIDToImm(ID));
  // Prefer the first explicit Imm in the explicit-operand prefix.
  for (unsigned I = 0, E = BundleRoot.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = BundleRoot.getOperand(I);
    if (MO.isReg() && MO.isImplicit())
      break;
    if (MO.isImm()) {
      MO.setImm(Imm);
      return;
    }
  }
  // MachineInstr::addOperand inserts explicit ops before implicit regs.
  BundleRoot.addOperand(MachineOperand::CreateImm(Imm));
}

/// Read FormatID from a BUNDLE root. nullopt if not a BUNDLE, no imm, or
/// unknown encoding (legacy roots without FormatID / other targets' BUNDLEs).
inline std::optional<FormatID> getBundleFormatID(const MachineInstr &MI) {
  if (!MI.isBundle())
    return std::nullopt;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isImplicit())
      break;
    if (MO.isImm())
      return formatIDFromImm(static_cast<unsigned>(MO.getImm()));
  }
  return std::nullopt;
}

/// FormatID for a cycle: stamped imm if present, else product Full (legacy /
/// unstamped roots still encode as Bundle128).
inline FormatID getBundleFormatIDOrProduct(const MachineInstr &MI) {
  if (auto ID = getBundleFormatID(MI))
    return *ID;
  return ProductFormatID;
}

/// Committed EncodedBytes for a BUNDLE root (AIE Format->getSize() peer,
/// AIEBaseInstrInfo.cpp:546-555). Bare / unstamped non-BUNDLE → product parcel.
/// BR, Fixup, and HardwareLoops must not invent a parallel size constant.
inline EncodedBytes committedEncodedBytes(const MachineInstr &MI) {
  if (MI.isBundle())
    return encodedBytesOrProduct(getBundleFormatIDOrProduct(MI));
  return productParcelBytes();
}

//===----------------------------------------------------------------------===//
// BundlePlan — one architectural cycle summary object
//===----------------------------------------------------------------------===//

/// Committed (or provisional) plan for one issue cycle.
/// FormatID is always Bundle128Full; EncodedBytes always 16.
/// MemberOpcodes hold logical public opcodes (never _S* private peers).
/// OccupiedSlots is the Haydn::SLOT* bitmask (not member count).
struct BundlePlan {
  FormatID FID = ProductFormatID;
  /// Haydn::SLOT0|SLOT1|SLOT2 occupancy (may be 0 for an explicit stall).
  SlotBits OccupiedSlots = 0;
  /// Logical opcodes in schedule/issue order (not necessarily encode field order).
  SmallVector<unsigned, 3> MemberOpcodes;
  EncodedBytes Bytes = Bundle128EncodedBytes;
  CycleCount Cycles = OneCycle;

  bool empty() const { return MemberOpcodes.empty(); }
  unsigned memberCount() const {
    return static_cast<unsigned>(MemberOpcodes.size());
  }

  /// Product invariant: one cycle, Bundle128, 16 bytes.
  bool isProductLegal() const {
    return isProductFormat(FID) && Bytes == Bundle128EncodedBytes &&
           Cycles == OneCycle && memberCount() <= Haydn::ISSUE_SLOT_COUNT;
  }
};

/// Build a product Bundle128 plan for the given occupancy and members.
inline BundlePlan makeBundle128Plan(
    SlotBits Occupied, ArrayRef<unsigned> Members = {}) {
  BundlePlan P;
  P.FID = FormatID::Bundle128Full;
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = Bundle128EncodedBytes;
  P.Cycles = OneCycle;
  return P;
}

/// Explicit architectural stall (idle cycle): FormatID still Bundle128, 16 B NOP parcel.
inline BundlePlan makeStallPlan() {
  return makeBundle128Plan(/*Occupied=*/0, /*Members=*/{});
}

/// Build a BundlePlan from a selected FormatDesc (solver path; N-format-ready).
/// Product callers still use makeBundle128Plan when FID is known Full.
inline BundlePlan makePlanFromFormatDesc(const FormatDesc &F, SlotBits Occupied,
                                         ArrayRef<unsigned> Members = {}) {
  BundlePlan P;
  P.FID = F.FID;
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = F.Bytes;
  P.Cycles = OneCycle;
  return P;
}

/// Select by Priority then build a plan. nullopt if no covering format.
/// Empty occupancy (stall): first best-Priority format that covers 0 (all do).
inline std::optional<BundlePlan>
planFromFormatTable(ArrayRef<FormatDesc> Table, SlotBits Occupied,
                    ArrayRef<unsigned> Members = {}) {
  const FormatDesc *F = selectFormatByPriority(Table, Occupied);
  if (!F)
    return std::nullopt;
  return makePlanFromFormatDesc(*F, Occupied, Members);
}

/// Query the generated PacketFormats table for the row covering \p Occupied.
/// Returns nullopt if no row covers it, or if the row's size disagrees with
/// the plan model. Empty occupancy takes the first row (NOP fill).
///
/// The query used to be `getFormat(SLOT0|SLOT1|SLOT2)` — "give me the full
/// format" — and then checked coverage separately. That is the same answer
/// while one row covers every slot, and it is WRONG the moment there are two
/// disjoint ones: no format E row covers all five entry slots, so the lookup
/// would return nullptr for every bundle. Ask for what is actually occupied
/// and let the table pick; that is also how the composite gets chosen at
/// encode time (HaydnMCCodeEmitter), so the two agree by construction.
inline std::optional<BundlePlan>
planFromPacketFormats(const PacketFormats &Packets, SlotBits Occupied) {
  const VLIWFormat *F = Packets.getFormat(Occupied);
  if (!F)
    return std::nullopt;
  // Product table Size is EncodedBytes.
  EncodedBytes B = vliwFormatSizeAsBytes(F->getSize());
  if (B != productParcelBytes())
    return std::nullopt;
  return makeBundle128Plan(Occupied);
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEPLAN_H
