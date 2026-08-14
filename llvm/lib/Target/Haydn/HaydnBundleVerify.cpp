//===- HaydnBundleVerify.cpp - Fail-closed committed-bundle check ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Out-of-line verifyCommittedBundle / private-member lookup.
// Peer: AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleVerify.h"
#include "Haydn.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

namespace llvm {
namespace haydn {
namespace bundle {

const format_e::FormatEMemberRec *lookupPrivateFormatEMember(unsigned Opc) {
  if (Opc == 0 || Opc == Haydn::NOP)
    return nullptr;
  static const DenseMap<unsigned, const format_e::FormatEMemberRec *> Map = [] {
    DenseMap<unsigned, const format_e::FormatEMemberRec *> M;
    M.reserve(FormatEMemberOpcodeCount);
    for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
      const unsigned MemberOpc = FormatEMemberOpcodes[I];
      if (MemberOpc == 0 || MemberOpc == Haydn::NOP)
        continue;
      if (I >= format_e::FormatEMemberCount)
        break;
      const format_e::FormatEMemberRec &Rec = format_e::FormatEMembers[I];
      if (Rec.IsNop)
        continue;
      M.try_emplace(MemberOpc, &Rec);
    }
    return M;
  }();
  auto It = Map.find(Opc);
  if (It == Map.end())
    return nullptr;
  return It->second;
}

bool isPadNopOpcode(unsigned Opc) {
  return format_e::logicalOpcodeOrSelf(Opc) == Haydn::NOP;
}

SmallVector<unsigned, 3>
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
    if (isPadNopOpcode(I->getOpcode()))
      continue;
    Ops.push_back(I->getOpcode());
  }
  return Ops;
}

bool bundleHasPadNop(const MachineInstr &BundleRoot) {
  assert(BundleRoot.isBundle() && "expected BUNDLE root");
  const MachineBasicBlock *MBB = BundleRoot.getParent();
  if (!MBB)
    return false;
  for (MachineBasicBlock::const_instr_iterator I =
           std::next(BundleRoot.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
      continue;
    if (isPadNopOpcode(I->getOpcode()))
      return true;
  }
  return false;
}

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
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan) {
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  // Format E unit injectivity (units ≠ encoded entry identity). Residual
  // FieldSlots can look like a legal 3-entry E3 pack while two stores both
  // require LOADSTORE0 e0. Refuse here so MC never sees the illegal BUNDLE.
  if (!opcodesHaveFormatEUnitCover(MemberOpcodes))
    return std::string(
        "structural inverse: Format E unit injectivity failed "
        "(execution units are not encoded entry identity)");

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

  // Empty members: architectural idle — product geometry only (registry
  // parcel + stub completion). No PacketFormats planner reselection.
  if (MemberOpcodes.empty()) {
    BundlePlan Stall = makeProductPlan(/*Occupied=*/0, /*Members=*/{});
    Stall.Row = Row;
    Stall.Completion = selectCompletionFor(Row, 0);
    Stall.Bytes = productParcelBytes();
    if (!Stall.isProductLegal())
      return std::string("empty cycle BundlePlan not product-legal");
    if (OutPlan)
      *OutPlan = Stall;
    return std::nullopt;
  }

  // Structural inverse from generated Format E surface + fixed single-slot
  // member kinds (independent of residual PacketFormats planner).
  static_assert(format_e::FormatEMemberCount > 0,
                "structural inverse requires generated Format E members");
  static_assert(format_e::FormatESetDescLedgerCount > 0,
                "structural inverse requires setDesc ledger surface");
  (void)format_e::FormatEInverse[0];
  (void)format_e::FormatESetDescLedger[0];

  const bool ExpectE3 = Row == BundleFormatRowID::E96ThreeEntry;
  const uint8_t ExpectMode = ExpectE3 ? 1 : 0;
  // RowEntries already computed above for membership capacity.
  uint32_t SeenSlotBits = 0;
  uint32_t SeenEntryBits = 0;
  uint32_t SeenPrivateUnits = 0;
  unsigned FormatEEntryMembers = 0;
  unsigned PrivateExactMembers = 0;
  unsigned ResidualFixedSlotMembers = 0;
  unsigned BareOrUnknownMembers = 0;

  // Independent inverse member matrix (no forward planner / DFS):
  //   * typed private members: exact MemberId inverse + concrete unit injectivity
  //   * residual fixed-slot kinds are injective
  //   * E2/E3 kinds match stamped row mode
  //   * E2/E3 kinds map to injective entry indices within row capacity
  //   * generated inverse table covers each occupied (Mode, Entry)
  for (unsigned I = 0, E = MemberOpcodes.size(); I != E; ++I) {
    const unsigned Opc = MemberOpcodes[I];

    if (const format_e::FormatEMemberRec *Priv =
            lookupPrivateFormatEMember(Opc)) {
      if (Priv->Mode != ExpectMode)
        return std::string(
            Priv->Mode == 0
                ? "structural inverse: E2 fixed-slot member under E96ThreeEntry row"
                : "structural inverse: E3 fixed-slot member under E96TwoEntry row");
      if (static_cast<unsigned>(Priv->EntryIdx) >= RowEntries)
        return std::string(
            "structural inverse: entry index exceeds stamped row capacity");
      if (SeenEntryBits & (1u << Priv->EntryIdx))
        return std::string(
            "structural inverse: duplicate entry index among members");
      SeenEntryBits |= (1u << Priv->EntryIdx);
      ++FormatEEntryMembers;
      ++PrivateExactMembers;
      if (Priv->Unit < 32) {
        if (SeenPrivateUnits & (1u << Priv->Unit))
          return std::string(
              "structural inverse: private-entry unit collides across members");
        SeenPrivateUnits |= (1u << Priv->Unit);
      }
      if (!format_e::inverseCoversMember(*Priv))
        return std::string(
            "structural inverse: FormatEInverse misses exact MemberId for "
            "private member");
      MCSlotKind Kind = Fmts.getSlotKind(Opc);
      if (Kind != MCSlotKind()) {
        const unsigned KindVal = static_cast<unsigned>(Kind);
        if (KindVal < 32u) {
          if (SeenSlotBits & (1u << KindVal))
            return std::string(
                "structural inverse: duplicate fixed-slot kind among members");
          SeenSlotBits |= (1u << KindVal);
        }
      }
      continue;
    }

    MCSlotKind Kind = Fmts.getSlotKind(Opc);
    if (Kind == MCSlotKind()) {
      ++BareOrUnknownMembers; // multi-slot logical / unknown — residual oracle
      continue;
    }
    const unsigned KindVal = static_cast<unsigned>(Kind);
    if (KindVal < 32u) {
      if (SeenSlotBits & (1u << KindVal))
        return std::string(
            "structural inverse: duplicate fixed-slot kind among members");
      SeenSlotBits |= (1u << KindVal);
    }
    const bool IsE2Slot =
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0) ||
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_1);
    const bool IsE3Slot =
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0) ||
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1) ||
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2);
    if (IsE2Slot && ExpectE3)
      return std::string(
          "structural inverse: E2 fixed-slot member under E96ThreeEntry row");
    if (IsE3Slot && !ExpectE3)
      return std::string(
          "structural inverse: E3 fixed-slot member under E96TwoEntry row");

    int EntryIdx = -1;
    if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0) ||
        Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0))
      EntryIdx = 0;
    else if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_1) ||
             Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1))
      EntryIdx = 1;
    else if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2))
      EntryIdx = 2;
    if (EntryIdx < 0) {
      // Residual S0/S1/S2 FieldSlot — issue-slot injectivity via SeenSlotBits.
      // Format-E entry is membership position among reals (not the suffix).
      // Require generated inverse cover for (Mode, Entry) without forward
      // Haydn::Bundle planning.
      if (static_cast<unsigned>(I) >= RowEntries)
        return std::string(
            "structural inverse: residual membership exceeds stamped row "
            "entry capacity");
      if (SeenEntryBits & (1u << I))
        return std::string(
            "structural inverse: duplicate residual entry index among "
            "members");
      SeenEntryBits |= (1u << I);
      bool ResidualInverseCovers = false;
      for (unsigned J = 0; J < format_e::FormatEMemberCount; ++J) {
        const format_e::FormatEInverseRec &R = format_e::FormatEInverse[J];
        if (R.Mode == ExpectMode && R.EntryIdx == static_cast<uint8_t>(I) &&
            R.Logical && R.Logical[0] != '\0' &&
            !StringRef(R.Logical).equals_insensitive("NOP")) {
          ResidualInverseCovers = true;
          break;
        }
      }
      if (!ResidualInverseCovers)
        return std::string(
            "structural inverse: FormatEInverse has no non-NOP cover for "
            "residual mode/entry");
      ++ResidualFixedSlotMembers;
      ++FormatEEntryMembers;
      continue;
    }

    if (static_cast<unsigned>(EntryIdx) >= RowEntries)
      return std::string(
          "structural inverse: entry index exceeds stamped row capacity");
    if (SeenEntryBits & (1u << EntryIdx))
      return std::string(
          "structural inverse: duplicate entry index among members");
    SeenEntryBits |= (1u << EntryIdx);
    ++FormatEEntryMembers;

    // Generated inverse surface must admit this (Mode, Entry) placement.
    bool InverseCovers = false;
    for (unsigned J = 0; J < format_e::FormatEMemberCount; ++J) {
      const format_e::FormatEInverseRec &R = format_e::FormatEInverse[J];
      if (R.Mode == ExpectMode && R.EntryIdx == static_cast<uint8_t>(EntryIdx) &&
          R.Logical && R.Logical[0] != '\0' &&
          !StringRef(R.Logical).equals_insensitive("NOP")) {
        InverseCovers = true;
        break;
      }
    }
    if (!InverseCovers)
      return std::string(
          "structural inverse: FormatEInverse has no non-NOP cover for "
          "mode/entry");

    // Private fixed-slot members: claim a unit when this entry admits only one
    // unit identity in the generated member table. Multi-unit entries cannot
    // pin unit without the concrete private opcode, so they skip this check.
    uint32_t EntryUnitMask = 0;
    unsigned DistinctUnits = 0;
    for (unsigned J = 0; J < format_e::FormatEMemberCount; ++J) {
      const format_e::FormatEMemberRec &M = format_e::FormatEMembers[J];
      if (M.IsNop || M.Mode != ExpectMode ||
          M.EntryIdx != static_cast<uint8_t>(EntryIdx) || M.Unit >= 32)
        continue;
      if (!(EntryUnitMask & (1u << M.Unit))) {
        EntryUnitMask |= (1u << M.Unit);
        ++DistinctUnits;
      }
    }
    if (DistinctUnits == 1) {
      if (SeenPrivateUnits & EntryUnitMask)
        return std::string(
            "structural inverse: private-entry unit collides across members");
      SeenPrivateUnits |= EntryUnitMask;
    }
  }
  (void)FormatEEntryMembers;
  (void)SeenPrivateUnits;

  // All-private and residual fixed-slot cycles: independent inverse already
  // certified entry/unit or residual membership-position injectivity above.
  // Skip the forward Haydn::Bundle encode-oracle. Bare multi-slot logicals
  // still use canAdd (transitional residual only).
  const bool AllPrivateExact =
      !MemberOpcodes.empty() &&
      PrivateExactMembers == MemberOpcodes.size();
  const bool ResidualStructuralExact =
      !MemberOpcodes.empty() && BareOrUnknownMembers == 0 &&
      (PrivateExactMembers + ResidualFixedSlotMembers) == MemberOpcodes.size();
  const bool SkipEncodeOracle = AllPrivateExact || ResidualStructuralExact;

  SlotBits Occupied = 0;
  if (!SkipEncodeOracle) {
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
               std::to_string(I) + " opcode=" +
               std::to_string(MI->getOpcode());
      B.add(MI);
    }

    if (!B.isStandalone()) {
      if (!B.hasValidFormat())
        return std::string(
            "encode-oracle hasValidFormat failed (no covering packet format)");
    }
    Occupied = B.getOccupiedSlots();
  } else {
    // Private and residual fixed-slot cycles share entry-bit occupancy from
    // the independent inverse matrix (no PacketFormats planner).
    Occupied = static_cast<SlotBits>(SeenEntryBits);
  }

  // Structural inverse product plan: registry row/completion/bytes only.
  BundlePlan Plan = makeProductPlan(Occupied, MemberOpcodes);
  Plan.Row = Row;
  Plan.Completion = selectCompletionFor(Row, MemberOpcodes.size());
  Plan.Bytes = productParcelBytes();
  if (Plan.Bytes != *GenBytes)
    return std::string("rebuilt plan Bytes != product EncodedBytes");
  if (!Plan.isProductLegal())
    return std::string("rebuilt BundlePlan fails isProductLegal");

  static_assert(format_e::FormatEMemberCount > 0,
                "structural inverse requires generated Format E members");
  (void)format_e::FormatEInverse[0];

  if (OutPlan)
    *OutPlan = Plan;
  return std::nullopt;
}

/// MIR entry: rebuild plan from BUNDLE root row imm + children.
/// Fail-closed: missing/unknown row imm is an error.
std::optional<std::string>
verifyCommittedBundle(const MachineInstr &BundleRoot, const HaydnBaseMCFormats &Fmts,
                      BundlePlan *OutPlan) {
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
    CompletionStateID Expected = selectCompletionForMembersAndPads(
        *Row, Members.size(), bundleHasPadNop(BundleRoot));
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
std::optional<std::string>
verifyExactHardRootCommit(const MachineInstr &BundleRoot,
                          const HaydnBaseMCFormats &Fmts,
                          BundlePlan *OutPlan) {
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

  // Row must hold membership: E3 required for 3 reals; dual may be E2 or E3
  // when Format E unit cover needs three-entry geometry (e.g. two ADD32).
  auto Row = getBundleRowID(BundleRoot);
  assert(Row.has_value() && "verifyCommittedBundle requires a product row");
  const unsigned RowEntries =
      (*Row == BundleFormatRowID::E96ThreeEntry) ? 3u : 2u;
  if (Members.size() > RowEntries)
    return std::string(
        "hard-root verify: BundleFormatRowID entry capacity below membership");
  if (Members.size() >= 3 && *Row != BundleFormatRowID::E96ThreeEntry)
    return std::string(
        "hard-root verify: three real members require E96ThreeEntry");
  return std::nullopt;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm
