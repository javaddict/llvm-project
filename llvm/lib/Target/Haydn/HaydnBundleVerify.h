//===- HaydnBundleVerify.h - Fail-closed committed-bundle check -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pure fail-closed invariant checker for a committed architectural cycle
// (BUNDLE root + children after post-RA materialize / HaydnFinalizeBundle,
// including late PreEmit re-commit and SMS hard-root exact-commit).
//
// Product: Format E BundleFormatRowID + CompletionStateID on the BUNDLE root.
// EncodedBytes from the registry product rows. INDEPENDENT INVERSE ONLY:
// the verifier never consults the forward solver (no Haydn::Bundle
// canAdd/hasValidFormat, no PacketFormats planner, no DFS, no
// findFormatEMember / opcodesHaveFormatEUnitCover stamper reuse). It decodes
// the committed state against separately generated FormatEInverse records
// and checks invariants (row mode, entry capacity/order, unit injectivity,
// inverse encodeability, mandatory completion, RF port budgets via the
// shared port-budget predicate HaydnBundlePortBudget.h — one predicate with
// commit). Unknown or misplaced committed state fails closed.
//
// SMS post-RA contract: multi-member hard roots are exact-committed inside
// the frozen group only; verifyCommittedBundle is the post-commit certificate
// that the group is one product Format E parcel (never free-repacked).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H

#include "HaydnBundle.h"
#include "HaydnBundlePlan.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCChecker.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <optional>
#include <string>

namespace llvm {
namespace haydn {
namespace bundle {

/// late-MC residual cycle-forming / multi-cycle / loop-control pseudos.
/// Shared fail-closed law for VerifyBundles (late firewall) and AsmPrinter
/// (one-to-one serialize-only). ExpandPseudos / BranchRelaxation hooks /
/// FixupHwLoops must exact-commit final real MIs before layout.
/// Defined in HaydnVerifyBundles.cpp (avoids dual GET_INSTRINFO_ENUM includes).
bool isResidualCycleFormingPseudo(unsigned Opc);

/// Typed presentation expands allowed in AsmPrinter (B/RET/BR_JT/
/// PseudoCALLIndirect). Every other residual executable pseudo is fatal at
/// the late firewall — no silent drop inside committed BUNDLEs.
bool isRepresentationExpandPseudo(unsigned Opc);

/// Bare residual semantic pseudo (VerifyBundles + AsmPrinter): cycle-forming
/// or Expand-owned multi-MI only. MultiSlot_Pseudo logicals are not residual
/// while bare — Finalize owns setDesc+wrap.
bool isExpandOwnedSemanticPseudo(unsigned Opc);
bool isResidualExecutablePseudo(const MachineInstr &MI);

/// Reverse map: live private Format E member opcode → generated member record.
/// Residual `_S*` / bare logicals return nullptr. Defined in HaydnBundleVerify.cpp.
const format_e::FormatEMemberRec *lookupPrivateFormatEMember(unsigned Opc);

/// Completion-state pad NOP (logical NOP / NOP_S0 / generated NOP member).
/// Not a membership entry. Shared by Finalize cutover and verify.
bool isPadNopOpcode(unsigned Opc);

/// Collect non-meta, non-pad child opcodes of a BUNDLE root (membership).
SmallVector<unsigned, 3>
collectBundleMemberOpcodes(const MachineInstr &BundleRoot);

/// True when a BUNDLE root has at least one pad-NOP child.
bool bundleHasPadNop(const MachineInstr &BundleRoot);

/// Inverse-table unit mask for a catalog logical under \p Mode.
/// FormatEInverse only — never FormatEMembers / findFormatEMember / UnitMap.
/// Walks independently sorted inverse rows; never treats MemberId as an index.
inline uint32_t inverseUnitMaskForLogical(StringRef Logical, uint8_t Mode) {
  uint32_t Mask = 0;
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return 0;
  SmallVector<unsigned, 8> Ids;
  format_e::inverseIdsForLogical(Logical, Ids);
  for (unsigned I : Ids) {
    if (I >= format_e::FormatEMemberCount)
      continue;
    const format_e::FormatEInverseRec &R = format_e::FormatEInverse[I];
    if (R.Mode != Mode || R.Unit >= 32 || !R.Logical)
      continue;
    if (!format_e::completeInverseRecord(R))
      continue;
    Mask |= 1u << R.Unit;
  }
  return Mask;
}

inline bool inverseMasksAssignable(ArrayRef<uint32_t> Masks) {
  const unsigned N = Masks.size();
  if (N == 0)
    return true;
  if (N == 1)
    return Masks[0] != 0;
  if (N == 2) {
    for (unsigned U0 = 0; U0 < 32; ++U0) {
      if (!(Masks[0] & (1u << U0)))
        continue;
      if (Masks[1] & ~(1u << U0))
        return true;
    }
    return false;
  }
  if (N != 3)
    return false;
  for (unsigned U0 = 0; U0 < 32; ++U0) {
    if (!(Masks[0] & (1u << U0)))
      continue;
    for (unsigned U1 = 0; U1 < 32; ++U1) {
      if (U1 == U0 || !(Masks[1] & (1u << U1)))
        continue;
      if (Masks[2] & ~((1u << U0) | (1u << U1)))
        return true;
    }
  }
  return false;
}

/// True when \p Logs have injective inverse-table units under \p Mode.
/// Unknown logicals (mask 0) fail closed — residual names that are not in
/// FormatEInverse cannot structurally accept. Mapped stores stay exclusive.
inline bool inverseLogicalsHaveUnitCoverForMode(ArrayRef<std::string> Logs,
                                                uint8_t Mode) {
  if (Logs.empty())
    return true;
  SmallVector<uint32_t, 3> Masks;
  Masks.reserve(Logs.size());
  for (const std::string &L : Logs) {
    uint32_t M = inverseUnitMaskForLogical(L, Mode);
    // Unknown logicals have mask 0 and must not pass — even as a singleton.
    if (M == 0)
      return false;
    Masks.push_back(M);
  }
  if (Masks.size() < 2)
    return true;
  return inverseMasksAssignable(Masks);
}

/// True when \p Logs have injective inverse-table units under E2 or E3.
/// Independent of Bundle.canAdd / opcodesHaveFormatEUnitCover.
/// Unknown singleton logicals fail closed (mask 0) — never structural accept.
inline bool inverseLogicalsHaveUnitCover(ArrayRef<std::string> Logs) {
  if (Logs.empty())
    return true;
  return inverseLogicalsHaveUnitCoverForMode(Logs, /*Mode=*/0) ||
         inverseLogicalsHaveUnitCoverForMode(Logs, /*Mode=*/1);
}

/// Residual/logical inverse: first unused FormatEInverse record at
/// (logical, mode, membership-entry). FormatEInverse is independently
/// sorted — never index it by MemberId. Never findFormatEMember (that
/// helper is MC/Finalize placement and picks UnitMap — stamper reuse).
/// Selection does not re-filter FormatEMembers by Mode/Entry/Logical;
/// FormatEMembers[MemberId] is only the AsmPrinter fill vehicle.
inline const format_e::FormatEMemberRec *
findInverseLogicalAtEntry(StringRef Logical, uint8_t Mode, uint8_t EntryIdx,
                          uint32_t UsedUnitMask) {
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return nullptr;
  SmallVector<unsigned, 8> Ids;
  format_e::inverseIdsForLogical(Logical, Ids);
  for (unsigned I : Ids) {
    if (I >= format_e::FormatEMemberCount)
      continue;
    const format_e::FormatEInverseRec &R = format_e::FormatEInverse[I];
    if (R.Mode != Mode || R.EntryIdx != EntryIdx)
      continue;
    if (!format_e::completeInverseRecord(R))
      continue;
    if (R.Unit < 32 && (UsedUnitMask & (1u << R.Unit)))
      continue;
    // Return vehicle for AsmPrinter fill; selection is FormatEInverse only
    // (never FormatEMembers Mode/Entry/Logical re-filter).
    return &format_e::FormatEMembers[R.MemberId];
  }
  return nullptr;
}

/// Pure fail-closed check for one committed cycle by row + members.
///
/// Independent inverse (product geometry) — never the forward solver:
///   * known product BundleFormatRowID
///   * memberCount <= ISSUE_SLOT_COUNT and stamped row entry capacity
///   * registry product EncodedBytes agree with product parcel
///   * typed private members: exact encodeable MemberId inverse, Mode equal
///     to the stamped row mode, entry index inside row capacity, entry- and
///     unit-injective
///   * residual/logical members: independently generated FormatEInverse
///     records at the child's MEMBERSHIP ENTRY under the stamped mode
///     (opcode-keyed inverse row ids, never FormatEInverse[MemberId];
///     committed child order IS the entry order — verify checks, it
///     never re-plans; never findFormatEMember / UnitMap stamper). Inverse
///     rows must be completed (unit injectivity, membership, encodeability)
///     on every residual root — never structural/forward acceptance.
///   * anything else fails closed
///   * OutPlan rebuilt from makeProductPlan only (no PacketFormats planner)
///
/// \returns nullopt on success; human-readable reason on failure.
std::optional<std::string>
verifyCommittedBundle(BundleFormatRowID Row, ArrayRef<unsigned> MemberOpcodes,
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan = nullptr);

/// MIR entry: rebuild plan from BUNDLE root row + completion imms + children.
/// Fail-closed: missing/unknown row imm or missing completion is an error.
std::optional<std::string>
verifyCommittedBundle(const MachineInstr &BundleRoot,
                      const HaydnBaseMCFormats &Fmts,
                      BundlePlan *OutPlan = nullptr);

/// Post-RA hard-root / SMS commit-inside-group certificate.
/// Requires multi-member membership (hard root shape) and a product Format E
/// stamp. Used by leaveMBB after exactCommitHardRoots — fail closed when a
/// frozen group is missing a row, underfilled, or not encode-legal.
std::optional<std::string>
verifyExactHardRootCommit(const MachineInstr &BundleRoot,
                          const HaydnBaseMCFormats &Fmts,
                          BundlePlan *OutPlan = nullptr);

/// Parse-time bundle legality (Hexagon MCChecker; AIE AIEBaseAsmParser.h:192
/// is the structural peer — Haydn overlay is FormatEInverse, never Bundle.canAdd).
/// One law with verifyCommittedBundle: opcode-keyed inverse + unit injectivity
/// at each encode-dag entry, plus same-reg WAW, SET_HWLOOP same-sel, and
/// RF port budget (GPR 4R/2W, DR 7R/3W, AR 2R/2W) from MC operands.
/// Entries is encode-dag order; nullptr or NOP is an unused entry.
/// Out-of-line so residual/logical members resolve through generated inverse
/// records (no peelLogicalOpcodeName / DFS / canAdd).
std::optional<std::string>
verifyParsedBundle(BundleFormatRowID Row, ArrayRef<const MCInst *> Entries,
                   const HaydnBaseMCFormats &Fmts, const MCInstrInfo &MII,
                   const MCRegisterInfo *MRI = nullptr);

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
