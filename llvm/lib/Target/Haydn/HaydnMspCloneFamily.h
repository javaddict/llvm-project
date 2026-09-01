//===- HaydnMspCloneFamily.h - `_MSP` clone family, ONE table --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sole `_MSP` encode-clone family table (D1.43). ONE definition consumed by
// BOTH seats that must agree on which catalog logical a clone serializes
// as:
//
//   * MC-lower entry binding — HaydnMCInstLower.cpp
//     haydnMemberOpcodeForMspClone picks the clone's generated member by
//     Logical NAME at the stamped (Mode, EntryIdx);
//   * structural inverse walk — HaydnBundleVerify.cpp
//     collectInverseIdsForOpcode / verifyMemberAtStampedEntry resolve the
//     clone's inverse span by Logical OPCODE.
//
// Before D1.43 these were two hand switches bound only by comment; the
// tables could drift silently. Both accessors below are the same switch —
// a clone added or re-targeted here changes serializer and verifier at
// once, and the census unit arm (HaydnMspCloneFamilyTest.cpp) pins the
// mapping against the generated MC name tables plus the ADD32_MSP
// fail-closed arm.
//
// Opcode-keyed, never peelLogicalOpcodeName / catalogOccupancyName (those
// are name-class utilities, not a family table): the generated member
// tables key Logical as "BEQZ"/"JAL"/"JALR" — the catalog `_W` names
// (BEQZ_W/JAL_W/JALR_W) have no FormatEInverse rows of their own, so a
// suffix peel would target dead entries for three of the four clones.
//
// Unmapped `_MSP` opcodes (ADD32_MSP) and every non-clone return 0 / empty
// and BOTH seats fail closed on them ("no free Format E entry" cannot be
// reached for ADD32_MSP at lower because the name lookup is empty and the
// opcode passes through; the verify unit-cover pre-check and the walk both
// refuse it). materialize/leaveRegion setDesc baking is the only legal
// commit path for such pseudos.
//
// NOT touched here: the positional-entry residual itself (clone entry
// chosen by operand position at MC-lower) — that is the W68.4 lifecycle
// change (bake-earlier at commit), and the D1.55 freeze carve-out stays in
// HaydnVerifyBundles.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMSPCLONEFAMILY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMSPCLONEFAMILY_H

#include "MCTargetDesc/HaydnMCTargetDesc.h" // GET_INSTRINFO_ENUM: Haydn::*
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace haydn {
namespace msp {

/// Every `_MSP`-named opcode the ISA defines today (TD census:
/// HaydnPseudos.td BEQZ_W_MSP/JALR_MSP, HaydnInstrGISel.td
/// JAL_W_MSP/JALR_W_MSP, HaydnMultiSlotPseudo.td ADD32_MSP). Census arm
/// for the unit test — the four serializable clones first, then the
/// unmapped fail-closed member.
inline constexpr unsigned MspCloneOpcodes[] = {
    Haydn::BEQZ_W_MSP, Haydn::JALR_MSP, Haydn::JALR_W_MSP,
    Haydn::JAL_W_MSP, Haydn::ADD32_MSP};

/// Catalog logical opcode a `_MSP` encode clone serializes as, or 0 when
/// \p Opc is not a mapped clone (fail closed — never identity, never a
/// suffix peel). Consumed by the structural inverse walk
/// (HaydnBundleVerify.cpp) and by the name accessor below.
inline unsigned logicalOpcodeForMspClone(unsigned Opc) {
  switch (Opc) {
  case Haydn::BEQZ_W_MSP:
    return Haydn::BEQZ;
  case Haydn::JALR_MSP:
  case Haydn::JALR_W_MSP:
    return Haydn::JALR;
  case Haydn::JAL_W_MSP:
    return Haydn::JAL;
  default:
    return 0;
  }
}

/// Generated-member Logical NAME for the same mapping ("BEQZ"/"JALR"/
/// "JAL" — the exact FormatEMembers.Logical keys), or empty for unmapped
/// opcodes. Consumed by MC-lower (haydnMemberOpcodeForMspClone); derived
/// from the one switch above so name and opcode cannot disagree.
inline StringRef logicalNameForMspClone(unsigned Opc) {
  switch (logicalOpcodeForMspClone(Opc)) {
  case Haydn::BEQZ:
    return "BEQZ";
  case Haydn::JALR:
    return "JALR";
  case Haydn::JAL:
    return "JAL";
  default:
    return StringRef();
  }
}

} // namespace msp
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMSPCLONEFAMILY_H
