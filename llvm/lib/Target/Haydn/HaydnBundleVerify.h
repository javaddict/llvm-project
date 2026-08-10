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
// EncodedBytes from the registry product rows. Encode-oracle packing still
// uses transitional SLOT* PacketFormats coverage until CodeGenFormat E96
// rows replace it.
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
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
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

/// Collect non-meta child opcodes of a BUNDLE root (schedule / MIR order).
inline SmallVector<unsigned, 3>
collectBundleMemberOpcodes(const MachineInstr &BundleRoot) {
  SmallVector<unsigned, 3> Ops;
  assert(BundleRoot.isBundle() && "expected BUNDLE root");
  const MachineBasicBlock *MBB = BundleRoot.getParent();
  if (!MBB)
    return Ops;
  for (MachineBasicBlock::const_instr_iterator I =
           std::next(BundleRoot.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
      continue;
    Ops.push_back(I->getOpcode());
  }
  return Ops;
}

/// Pure fail-closed check for one committed cycle by row + members.
///
/// Requires:
///   * known product BundleFormatRowID
///   * memberCount <= ISSUE_SLOT_COUNT
///   * registry product EncodedBytes agree with plan
///   * encode-oracle pack: Haydn::Bundle canAdd/add for members in order
///
/// \returns nullopt on success; human-readable reason on failure.
inline std::optional<std::string>
verifyCommittedBundle(BundleFormatRowID Row, ArrayRef<unsigned> MemberOpcodes,
                      HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan = nullptr) {
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  // One-to-one serialize: stamped row must have enough entries for every
  // real member. E96TwoEntry with 3 reals used to pass verify and then drop
  // a child at AsmPrinter (NumEntries from row imm only).
  const unsigned RowEntries =
      (Row == BundleFormatRowID::E96ThreeEntry) ? 3u : 2u;
  if (MemberOpcodes.size() > RowEntries)
    return std::string(
        "BUNDLE membership exceeds stamped row entry count (E2 holds 2; "
        "three real members require E96ThreeEntry)");

  auto GenBytes = productEncodedBytesFromPackets(Fmts.getPacketFormats());
  if (!GenBytes.has_value() || *GenBytes != productParcelBytes())
    return std::string(
        "product EncodedBytes missing or disagree with registry parcel");

  auto RowBytes = encodedBytesForRow(Row);
  if (!RowBytes.has_value() || *RowBytes != *GenBytes)
    return std::string(
        "row EncodedBytes disagree with product registry parcel");

  // Empty members: architectural idle — plan is legal as product geometry
  // with stub completion (encode remains fail-closed until idle law closes).
  if (MemberOpcodes.empty()) {
    auto Stall = planFromPacketFormats(Fmts.getPacketFormats(), /*Occupied=*/0);
    if (!Stall.has_value())
      return std::string("PacketFormats missing product composite for stall");
    Stall->Row = Row;
    Stall->Completion = selectCompletionFor(Row, 0);
    if (!Stall->isProductLegal())
      return std::string("empty cycle BundlePlan not product-legal");
    if (OutPlan)
      *OutPlan = *Stall;
    return std::nullopt;
  }

  SmallVector<MCInst, 3> Storage;
  Storage.reserve(MemberOpcodes.size());
  for (unsigned Opc : MemberOpcodes) {
    Storage.emplace_back();
    Storage.back().setOpcode(Opc);
  }

  Haydn::Bundle<MCInst> B(&Fmts);
  for (unsigned I = 0, E = MemberOpcodes.size(); I != E; ++I) {
    MCInst *MI = &Storage[I];
    if (!B.canAdd(MI->getOpcode()))
      return std::string("encode-oracle canAdd failed at member ") +
             std::to_string(I) + " opcode=" + std::to_string(MI->getOpcode());
    B.add(MI);
  }

  if (!B.isStandalone()) {
    if (!B.hasValidFormat())
      return std::string(
          "encode-oracle hasValidFormat failed (no covering packet format)");
  }

  SlotBits Occ = B.getOccupiedSlots();
  // Prefer PacketFormats coverage plan; if transitional slot bits do not yet
  // match Format E entry SlotSet, fall back to a registry-sized product plan
  // so EncodedBytes authority is not blocked on residual SLOT mapping.
  BundlePlan Plan;
  if (auto TablePlan =
          planFromPacketFormats(Fmts.getPacketFormats(), Occ, MemberOpcodes)) {
    Plan = *TablePlan;
  } else {
    Plan = makeProductPlan(Occ, MemberOpcodes);
  }
  Plan.Row = Row;
  Plan.Completion = selectCompletionFor(Row, MemberOpcodes.size());
  Plan.Bytes = productParcelBytes();
  if (Plan.Bytes != *GenBytes)
    return std::string("rebuilt plan Bytes != product EncodedBytes");
  if (!Plan.isProductLegal())
    return std::string("rebuilt BundlePlan fails isProductLegal");

  if (OutPlan)
    *OutPlan = Plan;
  return std::nullopt;
}

/// MIR entry: rebuild plan from BUNDLE root row imm + children.
/// Fail-closed: missing/unknown row imm is an error.
inline std::optional<std::string>
verifyCommittedBundle(const MachineInstr &BundleRoot, HaydnBaseMCFormats &Fmts,
                      BundlePlan *OutPlan = nullptr) {
  if (!BundleRoot.isBundle())
    return std::string("not a BUNDLE root");

  auto Row = getBundleRowID(BundleRoot);
  if (!Row.has_value())
    return std::string(
        "BUNDLE root missing or unknown BundleFormatRowID imm");

  SmallVector<unsigned, 3> Members = collectBundleMemberOpcodes(BundleRoot);
  auto Err = verifyCommittedBundle(*Row, Members, Fmts, OutPlan);
  if (Err)
    return Err;

  // Completion imm, when present, must be a known ID and match row+member
  // count exactly — no stub/product reselection at verify or MC. Missing
  // completion remains allowed on residual row-only stamps (BUNDLE 0);
  // multi-member hard-root verify requires it separately.
  if (auto Comp = getBundleCompletionID(BundleRoot)) {
    if (!isStubCompletion(*Comp) && !isProductLegalCompletion(*Comp))
      return std::string("BUNDLE root has unknown CompletionStateID");
    CompletionStateID Expected =
        selectCompletionFor(*Row, Members.size());
    if (*Comp != Expected)
      return std::string(
          "BUNDLE root CompletionStateID does not match row and member count");
    if (OutPlan)
      OutPlan->Completion = *Comp;
  }
  return std::nullopt;
}

/// Post-RA hard-root / SMS commit-inside-group certificate.
/// Requires multi-member membership (hard root shape) and a product Format E
/// stamp. Used by leaveMBB after exactCommitHardRoots — fail closed when a
/// frozen group is missing a row, underfilled, or not encode-legal.
inline std::optional<std::string>
verifyExactHardRootCommit(const MachineInstr &BundleRoot,
                          HaydnBaseMCFormats &Fmts,
                          BundlePlan *OutPlan = nullptr) {
  if (!BundleRoot.isBundle())
    return std::string("hard-root verify: not a BUNDLE root");

  SmallVector<unsigned, 3> Members = collectBundleMemberOpcodes(BundleRoot);
  if (Members.size() < 2)
    return std::string(
        "hard-root verify: expected multi-member hard root (>=2)");
  if (Members.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string(
        "hard-root verify: membership exceeds ISSUE_SLOT_COUNT");

  // Durable stamp: hard roots carry row + completion so MC/AsmPrinter cannot
  // reselect geometry or silently underfill from a lone FormatID imm.
  if (!getBundleCompletionID(BundleRoot).has_value())
    return std::string(
        "hard-root verify: missing CompletionStateID on BUNDLE root");

  auto Err = verifyCommittedBundle(BundleRoot, Fmts, OutPlan);
  if (Err)
    return Err;

  // Row must be the product selection for this membership (E2 for 2, E3 for 3).
  auto Row = getBundleRowID(BundleRoot);
  assert(Row.has_value() && "verifyCommittedBundle requires a product row");
  if (*Row != selectProductRowForMemberCount(Members.size()))
    return std::string(
        "hard-root verify: BundleFormatRowID does not match member count");
  return std::nullopt;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
