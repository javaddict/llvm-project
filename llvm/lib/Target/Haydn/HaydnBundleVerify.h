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
// canAdd/hasValidFormat, no PacketFormats planner, no DFS). It decodes the
// committed state against the separately generated Format E member/inverse
// tables and checks invariants (row mode, entry capacity/order, unit
// injectivity, RF port budgets via the shared port-budget predicate
// HaydnBundlePortBudget.h — one predicate with commit P4). Unknown or
// misplaced committed state fails closed.
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

/// Pure fail-closed check for one committed cycle by row + members.
///
/// Independent inverse (product geometry) — never the forward solver:
///   * known product BundleFormatRowID
///   * memberCount <= ISSUE_SLOT_COUNT and stamped row entry capacity
///   * registry product EncodedBytes agree with product parcel
///   * typed private members: exact MemberId inverse, Mode equal to the
///     stamped row mode, entry index inside row capacity, entry- and
///     unit-injective
///   * bare logical members: peel to a golden catalog logical with an exact
///     generated member at the child's MEMBERSHIP ENTRY under the stamped
///     mode (committed child order IS the entry order — verify checks, it
///     never re-plans)
///   * anything else fails closed
///   * OutPlan rebuilt from makeProductPlan only (no PacketFormats planner)
///
/// \returns nullopt on success; human-readable reason on failure.
std::optional<std::string>
verifyCommittedBundle(BundleFormatRowID Row, ArrayRef<unsigned> MemberOpcodes,
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan = nullptr);

/// MIR entry: rebuild plan from BUNDLE root row imm + children.
/// Fail-closed: missing/unknown row imm is an error.
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
/// canAdd). One law with verifyCommittedBundle: inverse + unit injectivity
/// at each encode-dag entry, plus same-reg WAW, SET_HWLOOP same-sel, and
/// RF port budget (GPR 4R/2W, DR 7R/3W, AR 2R/2W) from MC operands.
/// \p Entries is encode-dag order; nullptr or NOP is an unused entry.
/// Header-inline so AsmParser (HaydnDesc/MC only) shares the predicates
/// without linking CodeGen.
inline std::optional<std::string>
verifyParsedBundle(BundleFormatRowID Row, ArrayRef<const MCInst *> Entries,
                   const HaydnBaseMCFormats &Fmts, const MCInstrInfo &MII,
                   const MCRegisterInfo *MRI = nullptr) {
  (void)Fmts;
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  const unsigned RowEntries = [&] {
    const format::BundleFormatRowDesc *Desc = format::getBundleFormatRow(Row);
    return Desc ? Desc->EntryCount : 0u;
  }();
  if (Entries.size() > RowEntries)
    return std::string(
        "BUNDLE membership exceeds stamped row entry count (E2 holds 2; "
        "three real members require E96ThreeEntry");

  const uint8_t ExpectMode =
      Row == BundleFormatRowID::E96ThreeEntry ? 1 : 0;
  auto isPad = [&](unsigned Opc) {
    return format_e::peelLogicalOpcodeName(MII.getName(Opc)) == "NOP";
  };
  auto isExpand = [&](unsigned Opc) {
    const StringRef N = MII.getName(Opc);
    return N == "B" || N == "RET" || N == "BR_JT" || N == "PseudoCALLIndirect";
  };
  SmallVector<std::string, 3> Logs;
  SmallVector<const MCInst *, 3> RealInsts;
  for (const MCInst *Inst : Entries) {
    if (!Inst || isPad(Inst->getOpcode()))
      continue;
    Logs.push_back(format_e::peelLogicalOpcodeName(MII.getName(Inst->getOpcode())));
    RealInsts.push_back(Inst);
  }
  if (Logs.size() > 3)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");
  if (!format_e::logicalsHaveUnitCoverForMode(Logs, ExpectMode))
    return std::string(
        "structural inverse: Format E unit injectivity failed "
        "(execution units are not encoded entry identity)");

  uint32_t SeenUnits = 0;
  uint32_t SeenEntryBits = 0;
  for (unsigned E = 0, EE = Entries.size(); E != EE; ++E) {
    const MCInst *Inst = Entries[E];
    if (!Inst || isPad(Inst->getOpcode()))
      continue;
    if (isExpand(Inst->getOpcode()) && RealInsts.size() != 1)
      return std::string(
          "structural inverse: representation-expand pseudo must be a "
          "solo committed cycle (printer expands one-to-one)");
    if (isExpand(Inst->getOpcode()))
      continue;
    const std::string Log =
        format_e::peelLogicalOpcodeName(MII.getName(Inst->getOpcode()));
    const format_e::FormatEMemberRec *Exact = format_e::findFormatEMember(
        Log, ExpectMode, static_cast<uint8_t>(E), SeenUnits);
    if (!Exact)
      return std::string(
                 "structural inverse: committed logical has no generated "
                 "member at its stamped entry (unknown or misplaced): ") +
             Log + " @mode" + std::to_string(ExpectMode) + " entry " +
             std::to_string(E);
    if (Exact->EntryIdx != static_cast<uint8_t>(E))
      return std::string(
          "structural inverse: member entry mismatch vs membership order");
    if (Exact->Unit < 32) {
      if (SeenUnits & (1u << Exact->Unit))
        return std::string(
            "structural inverse: chosen Format E members are not "
            "unit-injective");
      SeenUnits |= 1u << Exact->Unit;
    }
    if (SeenEntryBits & (1u << E))
      return std::string(
          "structural inverse: duplicate entry index among members");
    SeenEntryBits |= 1u << E;
  }

  // One operand law with parse-time HaydnMCChecker (HexagonMCChecker.cpp).
  if (auto RegErr = haydnCheckParsedBundleRegs(RealInsts, MII, MRI))
    return std::string("structural inverse: ") + *RegErr;
  return std::nullopt;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
