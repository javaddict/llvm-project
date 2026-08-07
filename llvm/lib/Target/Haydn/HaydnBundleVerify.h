//===- HaydnBundleVerify.h - Fail-closed committed-bundle check -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// G-BUNDLE-FORMAT B1.4 / B4.3 — pure fail-closed invariant checker for a
// committed architectural cycle (BUNDLE root + children after post-RA
// materialize / HaydnFinalizeBundle, including late PreEmit re-commit).
//
// AIE peers (structure port; Haydn uses FormatID imm + BundlePlan, not only
// re-inferred VLIWFormat*):
//   * AIEBundle.h:150-156 getFormatOrNull — format coverage after slot pack
//     (Haydn: Bundle::hasValidFormat + planFromPacketFormats)
//   * AIEHazardRecognizer.cpp:278-312 applyFormatOrdering assert +
//     finalizeBundle — commit surface under test
//   * AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed
//     MachineVerifier pattern
//
// Product: FormatID::BundleE2 / BundleE3 (N-format-typed API). EncodedBytes=12.
// B3.1/B4.3: members may already be format-member opcodes (post-setDesc);
// Bundle canAdd uses getSlotKind for those (AIE shape). No MCFlags writers.
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

/// Pure fail-closed check for one committed cycle.
///
/// Requires:
///   * known product FormatID (BundleE2 / BundleE3; unknown imm fails)
///   * memberCount <= ISSUE_SLOT_COUNT
///   * EncodedBytes == 16 for that FormatID
///   * encode-oracle pack: Haydn::Bundle canAdd/add for members in order;
///     hasValidFormat (AIE getFormatOrNull peer) when not standalone;
///     planFromPacketFormats covers occupancy
///
/// \returns nullopt on success; human-readable reason on failure.
/// On success, fills \p OutPlan when non-null.
inline std::optional<std::string>
verifyCommittedBundle(FormatID FID, ArrayRef<unsigned> MemberOpcodes,
                      HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan = nullptr) {
  // N-format-ready gate: only known product FormatID (imm-encoded identity).
  if (!formatIDFromImm(formatIDToImm(FID)).has_value())
    return std::string("unknown FormatID (not N-format table row)");
  if (!isProductFormat(FID))
    return std::string("non-product FormatID (BundleE2 / BundleE3 live)");

  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  auto Bytes = encodedBytesFor(FID);
  if (!Bytes.has_value() || *Bytes != ProductEncodedBytes)
    return std::string("EncodedBytes != 16 for product FormatID");

  // Empty members: architectural stall / NOP-fill parcel still legal as
  // Bundle128 (makeStallPlan). Encode oracle is vacuously true.
  if (MemberOpcodes.empty()) {
    BundlePlan Stall = makeStallPlan();
    Stall.FID = FID;
    if (!Stall.isProductLegal())
      return std::string("empty cycle BundlePlan not product-legal");
    auto Table = planFromPacketFormats(Fmts.getPacketFormats(), /*Occupied=*/0);
    if (!Table.has_value())
      return std::string("PacketFormats has no row covering an empty stall");
    if (OutPlan)
      *OutPlan = Stall;
    return std::nullopt;
  }

  // Encode-oracle: same Haydn::Bundle canAdd/add path as materialize/pack
  // (AIEBundle.h canAdd / getFormatOrNull structure).
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

  // AIE getFormatOrNull peer: hasValidFormat when slots were assigned.
  // Standalone (unsupported single op) has no slot occupancy — still a
  // product 16 B parcel via NOP-fill / escape (planFromPacketFormats occ 0).
  if (!B.isStandalone()) {
    if (!B.hasValidFormat())
      return std::string(
          "encode-oracle hasValidFormat failed (no covering packet format)");
  }

  SlotBits Occ = B.getOccupiedSlots();
  auto TablePlan = planFromPacketFormats(Fmts.getPacketFormats(), Occ);
  if (!TablePlan.has_value())
    return std::string("planFromPacketFormats rejected occupancy");

  BundlePlan Plan = makeProductPlan(Occ, MemberOpcodes);
  Plan.FID = FID;
  auto FIDBytes = encodedBytesFor(FID);
  if (FIDBytes)
    Plan.Bytes = *FIDBytes;
  if (!Plan.isProductLegal())
    return std::string("rebuilt BundlePlan fails isProductLegal");

  if (OutPlan)
    *OutPlan = Plan;
  return std::nullopt;
}

/// MIR entry: rebuild plan from BUNDLE root FormatID imm + children.
/// Fail-closed: missing/unknown FormatID imm is an error (no silent product
/// default — that is only for legacy getBundleFormatIDOrProduct readers).
inline std::optional<std::string>
verifyCommittedBundle(const MachineInstr &BundleRoot, HaydnBaseMCFormats &Fmts,
                      BundlePlan *OutPlan = nullptr) {
  if (!BundleRoot.isBundle())
    return std::string("not a BUNDLE root");

  auto FID = getBundleFormatID(BundleRoot);
  if (!FID.has_value())
    return std::string(
        "BUNDLE root missing or unknown FormatID imm (B1.1/B1.2 required)");

  SmallVector<unsigned, 3> Members = collectBundleMemberOpcodes(BundleRoot);
  return verifyCommittedBundle(*FID, Members, Fmts, OutPlan);
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEVERIFY_H
