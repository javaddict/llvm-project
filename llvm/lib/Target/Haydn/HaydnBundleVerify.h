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
// EncodedBytes from the registry product rows. Structural inverse rebuilds
// product geometry from registry + Format E unit cover. Fully private-member
// cycles and residual fixed-slot cycles certify without the forward
// Haydn::Bundle encode-oracle; bare multi-slot logicals still use canAdd.
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
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInst.h"
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
/// Independent structural inverse (product geometry):
///   * known product BundleFormatRowID
///   * memberCount <= ISSUE_SLOT_COUNT and stamped row entry capacity
///   * registry product EncodedBytes agree with product parcel
///   * typed private members: exact MemberId inverse + unit injectivity
///   * fixed-slot member kinds match the stamped row mode (E2 vs E3) and
///     do not collide on the same single-slot identity
///   * OutPlan rebuilt from makeProductPlan only (no PacketFormats planner)
///
/// Residual encode-oracle (transitional PacketFormats canAdd):
///   * used only when at least one member is still a bare multi-slot logical
///     or unknown (not private MemberId and not residual fixed-slot)
///   * Haydn::Bundle canAdd/add is not product geometry authority; skipped for
///     all-private cycles and residual fixed-slot cycles
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

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
