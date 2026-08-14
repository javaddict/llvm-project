//===- HaydnPlacementAlternative.h - Format member alts -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PlacementAlternative is the AIE-shaped placement authority surface.
// enumeratePlacementAlternatives stamps generated Format E members as
// MemberOpcode (mode → row mask). FieldSlots stay residual AlternateInsts
// occupancy (index == SLOT bit) so Bundle.canAdd does not change.
// Residual FieldSlot opcodes remain only when no Format E span exists
// (CSRW_W, NOP). Product identity after post-RA commit is Format E
// BundleFormatRowID.
//
// CompatibleFormatMask is the Format E row frontier this residual member may
// occupy. Most residual alts stamp ProductFormatMask (E2|E3). Logicals that
// golden Format E exposes only as E2 (ADDI32, ...) stamp E96TwoEntry only and
// drop residual S2 (no E2 e2). Logicals that golden exposes only as E3
// (LOG2, EXP2, ...) stamp E96ThreeEntry only. Post-RA commit freezes one
// BundleFormatRowID + CompletionStateID on the BUNDLE root.
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
/// (vector index == field; generated AlternateInsts order).
struct PlacementAlternative {
  /// Post-setDesc Format E member opcode (e.g. ADD32_E2_E0_ALU0_RR).
  /// Residual FieldSlots (CSRW_W_S0, NOP_S0) appear only when no span exists.
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
/// Identical to HaydnBaseMCFormats::getAlternateInstsOpcode — occupancy
/// plus Format E members (AIE AIEMCFormats.h:376-379 peer). Rows are
/// sparse size-3 (index == residual occupancy class; 0 = hole).
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
/// E96TwoEntry only; E3-only logicals stamp E96ThreeEntry only; other residual
/// alts keep ProductFormatMask (E2|E3).
uint64_t residualAltCompatibleFormatMask(unsigned LogicalOpc,
                                         unsigned AltIndex);

/// True if \p OpcodeName (logical or residual/member form) is a Format E
/// E2-only product logical. Such ops must never co-issue in a 3-wide E3 cycle.
bool isFormatEE2OnlyOpcodeName(llvm::StringRef OpcodeName);

/// True if \p OpcodeName is a Format E E3-only product logical (no E2 row).
bool isFormatEE3OnlyOpcodeName(llvm::StringRef OpcodeName);

/// Fill \p Out with PlacementAlternative rows for \p LogicalOpc.
/// Prefers generated Format E members for MemberOpcode (mode → row mask).
/// FieldSlots stay residual AlternateInsts occupancy. Residual FieldSlot
/// opcodes remain only when no Format E span exists (CSRW_W, NOP).
/// Returns false if there are no alternatives.
bool enumeratePlacementAlternatives(const HaydnMCFormats &Fmts,
                                    unsigned LogicalOpc,
                                    SmallVectorImpl<PlacementAlternative> &Out);

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

/// \returns true if \p LogicalOpc has a Format E member span or a residual
/// FieldSlot AlternateInsts row.
bool hasPlacementAlternatives(const HaydnBaseMCFormats &Fmts,
                              unsigned LogicalOpc);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPLACEMENTALTERNATIVE_H
