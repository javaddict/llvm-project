//===- HaydnPlacementAlternative.h - Format member alts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Thin view over HaydnMCFormats::getAlternateInstsOpcode for generated
// format members (transitional PacketFormats alts; product identity is
// Format E BundleFormatRowID after post-RA commit).
//
// PlacementAlternative is the AIE-shaped placement authority surface.
// Legality is alts-derived only (sparse size-3 AlternateInsts;
// FieldSlots = 1<<index for non-zero members). Bundle/HR placement does not
// reverse-map through FlexMap; encode uses post-RA setDesc member Desc-as-is.
//
// CompatibleFormatMask is the Format E row frontier this residual member may
// occupy. Most residual alts stamp ProductFormatMask (E2|E3). Logicals that
// golden Format E exposes only as E2 (ADDI32, ...) stamp E96TwoEntry only and
// drop residual S2 (no E2 e2). Post-RA commit freezes one BundleFormatRowID
// + CompletionStateID on the BUNDLE root.
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
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <vector>

namespace llvm {

/// One placement choice for a logical opcode under a packet format field.
/// MemberOpcode + CompatibleFormatMask (plan §6.1). FieldSlots = 1<<sparse-alt-index
/// (vector index == field; not a reverse FlexMap lookup).
struct PlacementAlternative {
  /// Post-setDesc / format-member opcode (e.g. ADD32_S1).
  unsigned MemberOpcode = 0;

/// Bitmask of BundleFormatRowIDs this member may occupy.
  /// Bit i = formatRowBit(row). Product alts stamp
  /// haydn::bundle::ProductFormatMask (E96TwoEntry | E96ThreeEntry).
  uint64_t CompatibleFormatMask = 0;

  /// Single-slot Haydn::SLOT* occupancy this member claims (plan §6.1 field).
  /// 0 means unknown / unset (synthetic unit rows may set explicitly).
  SlotBits FieldSlots = 0;

  constexpr PlacementAlternative() = default;
  /// Full ctor: member + row mask + field occupancy bit.
  /// Field last (no default) so (MemberOpc, FormatMask) is unambiguous vs
  /// SlotBits == uint64_t alias with the product-mask default ctor.
  constexpr PlacementAlternative(unsigned MemberOpc, uint64_t FormatMask,
                                 SlotBits Field)
      : MemberOpcode(MemberOpc), CompatibleFormatMask(FormatMask),
        FieldSlots(Field) {}
  /// Member + row mask; FieldSlots left 0 (set by enumerate or tests).
  constexpr PlacementAlternative(unsigned MemberOpc, uint64_t FormatMask)
      : MemberOpcode(MemberOpc), CompatibleFormatMask(FormatMask),
        FieldSlots(0) {}
  /// Default mask = product E2|E3 rows (product stamp on generated members).
  constexpr explicit PlacementAlternative(unsigned MemberOpc)
      : MemberOpcode(MemberOpc),
        CompatibleFormatMask(haydn::bundle::ProductFormatMask),
        FieldSlots(0) {}

  /// True iff this member is legal under \p Row.
  constexpr bool isCompatibleWith(haydn::bundle::BundleFormatRowID Row) const {
    return (CompatibleFormatMask & haydn::bundle::formatRowBit(Row)) != 0;
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

/// FieldSlots for a sparse-alt entry at \p AltIndex is 1<<AltIndex
/// (vector index == field/slot).
inline SlotBits fieldSlotsForAltIndex(unsigned AltIndex) {
  return SlotBits(1) << AltIndex;
}

/// CompatibleFormatMask for a residual sparse-alt at \p AltIndex.
/// E2-only Format E logicals (golden Mode=E2 only) drop residual S2 and keep
/// E96TwoEntry only; other residual alts keep ProductFormatMask (E2|E3).
uint64_t residualAltCompatibleFormatMask(unsigned LogicalOpc,
                                         unsigned AltIndex);

/// True if \p OpcodeName (logical or residual/member form) is a Format E
/// E2-only product logical. Such ops must never co-issue in a 3-wide E3 cycle.
bool isFormatEE2OnlyOpcodeName(llvm::StringRef OpcodeName);

/// Fill \p Out with PlacementAlternative rows for \p LogicalOpc (non-zero
/// sparse members only). Each row stamps CompatibleFormatMask from Format E
/// golden availability (see residualAltCompatibleFormatMask) and FieldSlots =
/// 1<<index. Returns false if there are no non-zero alternatives.
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
    // Format E E2-only logicals: residual S2 is unavailable (no E2 e2) and the
    // alt mask is E96TwoEntry only. Other residual alts keep ProductFormatMask.
    // commitExact / canCoissue also refuse 3-wide packs that include E2-only.
    const uint64_t Mask = residualAltCompatibleFormatMask(LogicalOpc, Index);
    if (Mask == 0)
      continue;
    // Sparse size-3: index == field.
    Out.emplace_back(MemberOpc, Mask, fieldSlotsForAltIndex(Index));
    Any = true;
  }
  return Any;
}

/// Keep only alternatives compatible with \p Row (solver filter).
inline void
filterAlternativesForFormat(SmallVectorImpl<PlacementAlternative> &Alts,
                            haydn::bundle::BundleFormatRowID Row) {
  Alts.erase(std::remove_if(Alts.begin(), Alts.end(),
                            [Row](const PlacementAlternative &A) {
                              return !A.isCompatibleWith(Row);
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
