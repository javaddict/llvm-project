//===- HaydnPlacementAlternative.h - Format member alts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Thin view over HaydnMCFormats::getAlternateInstsOpcode for BUNDLE128_FULL
// format members (plan §6.1).
//
// PlacementAlternative is the AIE-shaped placement authority surface.
// Legality is alts-derived only (sparse size-3 AlternateInsts;
// FieldSlots = 1<<index for non-zero members). Bundle/HR placement does not
// reverse-map through FlexMap; encode uses post-RA setDesc member Desc-as-is.
//
// AIE peer: getAlternateInstsOpcode (AIEMCFormats.h) — slot kind always
// embeddable in any packet. Haydn strengthens with CompatibleFormatMask so
// members may be restricted to a FormatID subset when multi-format lands.
// Slot identity for multi-slot logicals lives on the alt vector index (AIE
// peer shape: AIEBaseMCFormats.cpp:66-75 getSlotKind on member opcode; Haydn
// stamps FieldSlots from sparse index == field).
//
// leaveRegion materializeMultiOpcodeInstrs does MI.setDesc(selected
// MemberOpcode) from HaydnAlternateDescriptors
// (AIEMachineScheduler.cpp:1121-1139 peer).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPLACEMENTALTERNATIVE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPLACEMENTALTERNATIVE_H

#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <vector>

namespace llvm {

/// One placement choice for a logical opcode under a packet format field.
/// MemberOpcode + CompatibleFormatMask (plan §6.1). FieldSlots = 1<<sparse-alt-index
/// (vector index == field; not a reverse FlexMap lookup).
struct PlacementAlternative {
  /// Post-setDesc / format-member opcode (e.g. ADD32_S1).
  unsigned MemberOpcode = 0;

  /// Bitmask of FormatIDs this member may occupy (plan §6.1 / G8).
  /// Bit i = formatIDBit(FormatID with imm i). Product alts stamp
  /// haydn::bundle::ProductFormatMask (BUNDLE128_FULL only).
  uint64_t CompatibleFormatMask = 0;

  /// Single-slot Haydn::SLOT* occupancy this member claims (plan §6.1 field).
  /// 0 means unknown / unset (synthetic unit rows may set explicitly).
  SlotBits FieldSlots = 0;

  constexpr PlacementAlternative() = default;
  /// Full ctor: member + FormatID mask + field occupancy bit.
  /// Field last (no default) so (MemberOpc, FormatMask) is unambiguous vs
  /// SlotBits == uint64_t alias with the product-mask default ctor.
  constexpr PlacementAlternative(unsigned MemberOpc, uint64_t FormatMask,
                                 SlotBits Field)
      : MemberOpcode(MemberOpc), CompatibleFormatMask(FormatMask),
        FieldSlots(Field) {}
  /// Member + FormatID mask; FieldSlots left 0 (set by enumerate or tests).
  constexpr PlacementAlternative(unsigned MemberOpc, uint64_t FormatMask)
      : MemberOpcode(MemberOpc), CompatibleFormatMask(FormatMask),
        FieldSlots(0) {}
  /// Default mask = product Full only (product stamp on generated members).
  constexpr explicit PlacementAlternative(unsigned MemberOpc)
      : MemberOpcode(MemberOpc),
        CompatibleFormatMask(haydn::bundle::ProductFormatMask),
        FieldSlots(0) {}

  /// True iff this member is legal under \p ID's format row.
  constexpr bool isCompatibleWith(haydn::bundle::FormatID ID) const {
    return (CompatibleFormatMask & haydn::bundle::formatIDBit(ID)) != 0;
  }
};

/// \returns the generated alternate member-opcode vector for \p LogicalOpc,
/// or nullptr if the opcode has no multi-slot / format-member alternatives.
/// Identical to HaydnBaseMCFormats::getAlternateInstsOpcode — the
/// placement authority entry point (AIE AIEMCFormats.h:376-379 peer).
/// Flex-derived rows are sparse size-3 (index == field; 0 = hole).
inline const std::vector<unsigned> *
getPlacementMemberOpcodes(const HaydnBaseMCFormats &Fmts,
                          unsigned LogicalOpc) {
  return Fmts.getAlternateInstsOpcode(LogicalOpc);
}

/// FieldSlots for a member is the bit of the slot its own format-desc names.
///
/// This used to be `1 << AltIndex`, valid only while the alternates vector was
/// indexed by slot. Format E indexes it by PLACEMENT — the (entry position,
/// unit) pair — so index 6 and index 10 are both "ALU0", in entries P30 and
/// P31 respectively, and index 6 and index 9 are both entry P30, on ALU0 and
/// MAC0. Neither the index nor its low bits are the slot. Ask the member.
inline SlotBits fieldSlotsForMember(const HaydnMCFormats &Fmts,
                                    unsigned MemberOpc) {
  MCSlotKind Kind = Fmts.getSlotKind(MemberOpc);
  if (Kind == MCSlotKind())
    return 0;
  return SlotBits(1) << static_cast<unsigned>(Kind);
}

/// Fill \p Out with PlacementAlternative rows for \p LogicalOpc (non-zero
/// sparse members only). Each row stamps CompatibleFormatMask =
/// ProductFormatMask and FieldSlots = the member's own slot bit.
/// Returns false if there are no non-zero alternatives.
///
/// NOTE two alternatives can now carry the SAME FieldSlots and differ only in
/// unit — a bundle entry is one slot, but several units can fill it. The
/// solver picks the first that fits, which is a unit choice made implicitly.
/// Nothing currently enforces format E's rule that no two entries of a bundle
/// may name the same unit; see FORMAT-E-SWITCH-PLAN.md § 5.2.
inline bool
enumeratePlacementAlternatives(const HaydnMCFormats &Fmts,
                               unsigned LogicalOpc,
                               SmallVectorImpl<PlacementAlternative> &Out) {
  Out.clear();
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Alts || Alts->empty())
    return false;
  Out.reserve(Alts->size());
  bool Any = false;
  for (unsigned Index = 0, E = static_cast<unsigned>(Alts->size()); Index < E;
       ++Index) {
    const unsigned MemberOpc = (*Alts)[Index];
    if (MemberOpc == 0)
      continue; // sparse hole — not a placement choice
    SlotBits Field = fieldSlotsForMember(Fmts, MemberOpc);
    if (!Field)
      continue; // member with no single slot — not placeable
    Out.emplace_back(MemberOpc, haydn::bundle::ProductFormatMask, Field);
    Any = true;
  }
  return Any;
}

/// \returns the member of \p LogicalOpc that sits in slot \p Kind, or 0.
/// When several members share the entry and differ only in unit, the first
/// wins — see the note on enumeratePlacementAlternatives.
inline unsigned findMemberForSlot(const HaydnMCFormats &Fmts,
                                  unsigned LogicalOpc, MCSlotKind Kind) {
  const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Alts)
    return 0;
  for (unsigned MemberOpc : *Alts)
    if (MemberOpc && Fmts.getSlotKind(MemberOpc) == Kind)
      return MemberOpc;
  return 0;
}

/// Keep only alternatives compatible with \p ID (solver filter).
inline void
filterAlternativesForFormat(SmallVectorImpl<PlacementAlternative> &Alts,
                            haydn::bundle::FormatID ID) {
  Alts.erase(std::remove_if(Alts.begin(), Alts.end(),
                            [ID](const PlacementAlternative &A) {
                              return !A.isCompatibleWith(ID);
                            }),
             Alts.end());
}

/// \returns true if \p LogicalOpc has at least one non-zero format-member alt.
inline bool hasPlacementAlternatives(const HaydnBaseMCFormats &Fmts,
                                     unsigned LogicalOpc) {
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Alts)
    return false;
  return llvm::any_of(*Alts, [](unsigned M) { return M != 0; });
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPLACEMENTALTERNATIVE_H
