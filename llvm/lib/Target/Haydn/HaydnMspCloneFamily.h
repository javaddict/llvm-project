//===- HaydnMspCloneFamily.h - encode-inverse family, ONE table --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sole encode-inverse family table (D1.43 / D1.74 / D1.130). ONE definition
// consumed by every seat that must agree on which catalog logical a gMIR
// opcode serializes as:
//
//   * MC-lower entry binding — HaydnMCInstLower.cpp
//     haydnMemberOpcodeForMspClone picks the opcode's generated member by
//     Logical NAME at the stamped (Mode, EntryIdx);
//   * structural inverse walk — HaydnBundleVerify.cpp
//     collectInverseIdsForOpcode / verifyMemberAtStampedEntry resolve the
//     inverse span by Logical OPCODE;
//   * InstrInfo peel — HaydnInstrInfo.cpp haydnLogicalOpcode
//     calls logicalOpcodeForMspClone first (catalog BEQZ/JAL/JALR, never
//     a second `_W` remap). WIDE operands/range are isWideCondClone, not
//     a forked opcode map.
//
// D1.130: `_MSP` flag clones are deleted. gMIR names the role (JALR_CALL
// returning fnptr call, JAL_TCO musttail-direct, JALR_TCO musttail-jalr).
// Encoder peels those honest opcodes to catalog bits through this same
// table. Uncond is B (already a named pseudo); B is NOT a clone here —
// F peels B to the BEQZ member with rs=R0 in MCInstLower. Catalog BEQZ /
// BEQZ_W stay real conds. Catalog JALR_W is RET / computed-goto.
// ADD32_MSP is AIE MultiSlot_Pseudo (slot map), unmapped/fail-closed.
//
// Opcode-keyed, never a suffix peel: the generated member tables key
// Logical as "BEQZ"/"JAL"/"JALR". peelLogicalOpcodeName and
// generate_format_e_records.py::_SINGLETON_EXACT_PEELS are the name-class
// twin (JALR_CALL/JALR_TCO -> JALR, JAL_TCO -> JAL). B->BEQZ stays
// MC-only; occupancy peel does not map B.
//
// Unmapped `_MSP` opcodes (ADD32_MSP) and every non-clone return 0 / empty
// and every seat fails closed on them ("no free Format E entry" cannot be
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

/// Encode-inverse family census (D1.130): honest gMIR opcodes that peel
/// to a catalog logical, then the unmapped fail-closed MultiSlot pseudo.
/// ADD32_MSP is the only remaining `_MSP`-named opcode (HaydnMultiSlotPseudo.td
/// slot map — not a flag overlay). Census arm for the unit test.
inline constexpr unsigned MspCloneOpcodes[] = {
    Haydn::JALR_CALL, Haydn::JAL_TCO, Haydn::JALR_TCO, Haydn::ADD32_MSP};

/// Catalog logical opcode an encode-inverse family member serializes as,
/// or 0 when \p Opc is not a mapped clone (fail closed — never identity,
/// never a suffix peel). Consumed by the structural inverse walk
/// (HaydnBundleVerify.cpp), MC-lower via the name accessor below, and
/// InstrInfo haydnLogicalOpcode (D1.74 — no second handwritten clone switch).
/// B peels to catalog BEQZ (rs=R0 at MC). Not a wide-cond clone.
inline unsigned logicalOpcodeForMspClone(unsigned Opc) {
  switch (Opc) {
  case Haydn::B:
    return Haydn::BEQZ;
  case Haydn::JALR_CALL:
  case Haydn::JALR_TCO:
    return Haydn::JALR;
  case Haydn::JAL_TCO:
    return Haydn::JAL;
  default:
    return 0;
  }
}

/// Generated-member Logical NAME for the same mapping ("JALR"/"JAL" —
/// the exact FormatEMembers.Logical keys), or empty for unmapped
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

/// WIDE-operand/range cond clone of catalog BEQZ. D1.130 deleted
/// BEQZ_W_MSP. B peels to BEQZ but is uncond, not a wide cond clone.
inline bool isWideCondClone(unsigned Opc) {
  (void)Opc;
  return false;
}

} // namespace msp
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMSPCLONEFAMILY_H
