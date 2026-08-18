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
#include "HaydnBundlePortBudget.h"
// HaydnBundleFormatSolver.h is included ONLY for haydnOpcodeName (the pure
// generated MC name-table accessor). The independent verifier never calls
// the forward planner (Bundle canAdd / hasValidFormat / exactTryAddProduct /
// PacketFormats planner) — independence per the topics/encoding P7 row.
#include "HaydnBundleFormatSolver.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
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

/// Inverse + unit injectivity for one opcode at a known encode-dag entry.
/// Private members use MemberId entry (child order may not match after
/// residual rebind). Logicals use EntryIdx (membership / parse position).
static std::optional<std::string>
verifyMemberAtStampedEntry(unsigned Opc, uint8_t ExpectMode,
                           uint8_t EntryIdx, unsigned RowEntries,
                           uint32_t &SeenUnits, uint32_t &SeenEntryBits) {
  if (const format_e::FormatEMemberRec *Priv = lookupPrivateFormatEMember(Opc)) {
    // Private members carry entry in the MemberId. Cutover may rebind a
    // store onto e0 while leaving it as a later child, so membership
    // index is not the encode-dag entry here. Parse-time uses the
    // positional EntryIdx path below for logicals / encode-dag holes.
    if (Priv->Mode != ExpectMode)
      return std::string(
          Priv->Mode == 0
              ? "structural inverse: E2 member under E96ThreeEntry row"
              : "structural inverse: E3 member under E96TwoEntry row");
    if (static_cast<unsigned>(Priv->EntryIdx) >= RowEntries)
      return std::string(
          "structural inverse: entry index exceeds stamped row capacity");
    if (SeenEntryBits & (1u << Priv->EntryIdx))
      return std::string(
          "structural inverse: duplicate entry index among members");
    if (!format_e::inverseCoversMember(*Priv))
      return std::string(
          "structural inverse: FormatEInverse misses exact MemberId for "
          "private member");
    if (Priv->Unit < 32) {
      if (SeenUnits & (1u << Priv->Unit))
        return std::string(
            "structural inverse: chosen Format E members are not "
            "unit-injective");
      SeenUnits |= 1u << Priv->Unit;
    }
    SeenEntryBits |= 1u << Priv->EntryIdx;
    return std::nullopt;
  }

  const std::string Log =
      format_e::peelLogicalOpcodeName(haydnOpcodeName(Opc));
  const format_e::FormatEMemberRec *Exact = format_e::findFormatEMember(
      Log, ExpectMode, EntryIdx, SeenUnits);
  if (!Exact)
    return std::string(
               "structural inverse: committed logical has no generated "
               "member at its stamped entry (unknown or misplaced): ") +
           Log + " @mode" + std::to_string(ExpectMode) + " entry " +
           std::to_string(EntryIdx);
  if (Exact->EntryIdx != EntryIdx)
    return std::string(
        "structural inverse: member entry mismatch vs membership order");
  if (Exact->Unit < 32) {
    if (SeenUnits & (1u << Exact->Unit))
      return std::string(
          "structural inverse: chosen Format E members are not "
          "unit-injective");
    SeenUnits |= 1u << Exact->Unit;
  }
  SeenEntryBits |= 1u << EntryIdx;
  return std::nullopt;
}

/// Pure fail-closed check for one committed cycle by row + members.
///
/// INDEPENDENT INVERSE ONLY (topics/encoding P7 row; hard constraints #7/#8):
/// this function NEVER consults the forward solver — no Haydn::Bundle
/// canAdd/add, no hasValidFormat, no PacketFormats planner, no DFS, no
/// permutation search. The committed state is decoded and checked against
/// the separately generated Format E member/inverse tables:
///
///   * known product BundleFormatRowID
///   * memberCount <= ISSUE_SLOT_COUNT and stamped row entry capacity
///   * registry product EncodedBytes agree with the product parcel
///   * private member opcodes: exact MemberId inverse, Mode equal to the
///     stamped row mode, entry index inside row capacity, entry- and
///     unit-injective
///   * bare logical opcodes: peel to a golden catalog logical that has an
///     exact generated member at the child's MEMBERSHIP ENTRY under the
///     stamped mode with a unit unused by earlier members (Finalize
///     leading-order law — committed child order IS the entry order; verify
///     checks it, it never re-plans it)
///   * anything else (unknown logical, no member at the stamped entry)
///     fails closed — the verifier must never ask the forward solver which
///     format fits
///   * OutPlan rebuilt from makeProductPlan only (registry identity)
///
/// \returns nullopt on success; human-readable reason on failure.
std::optional<std::string>
verifyCommittedBundle(BundleFormatRowID Row, ArrayRef<unsigned> MemberOpcodes,
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan) {
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  // Format E unit injectivity pre-check (units ≠ encoded entry identity):
  // pure generated-records derivation (logicalsHaveUnitCoverForMode), NOT a
  // forward-solver call. Residual FieldSlots can look like a legal 3-entry
  // E3 pack while two stores both require LOADSTORE0 e0. Refuse here so MC
  // never sees the illegal BUNDLE.
  if (!opcodesHaveFormatEUnitCover(MemberOpcodes))
    return std::string(
        "structural inverse: Format E unit injectivity failed "
        "(execution units are not encoded entry identity)");

  // One-to-one serialize: stamped row must have enough entries for every
  // real member. E96TwoEntry with 3 reals used to pass verify and then drop
  // a child at AsmPrinter (NumEntries from row imm only).
  const unsigned RowEntries = bundleRowEntryCount(Row);
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
    Stall.Completion = expectedGoldenRowCompletion(/*RealMembers=*/0,
                                                   /*HasPadNop=*/false);
    Stall.Bytes = productParcelBytes();
    if (!Stall.isProductLegal())
      return std::string("empty cycle BundlePlan not product-legal");
    if (OutPlan)
      *OutPlan = Stall;
    return std::nullopt;
  }

  // Independent inverse member matrix (no forward planner / DFS).
  static_assert(format_e::FormatEMemberCount > 0,
                "structural inverse requires generated Format E members");
  static_assert(format_e::FormatESetDescLedgerCount > 0,
                "structural inverse requires setDesc ledger surface");
  (void)format_e::FormatEInverse[0];
  (void)format_e::FormatESetDescLedger[0];

  const uint8_t ExpectMode =
      Row == BundleFormatRowID::E96ThreeEntry ? 1 : 0;
  uint32_t SeenEntryBits = 0;
  uint32_t SeenUnits = 0;

  for (unsigned I = 0, E = MemberOpcodes.size(); I != E; ++I) {
    const unsigned Opc = MemberOpcodes[I];

    // Representation-expand pseudos (B / RET / BR_JT / PseudoCALLIndirect)
    // expand to a real Format E member at AsmPrinter emission. They are
    // legal committed SOLO cycles only: the expansion target occupies an
    // entry the committed members must not already hold. Co-issue with a
    // representation expand is a corruption — fail closed.
    if (isRepresentationExpandPseudo(Opc)) {
      if (MemberOpcodes.size() != 1)
        return std::string(
            "structural inverse: representation-expand pseudo must be a "
            "solo committed cycle (printer expands one-to-one)");
      continue;
    }

    // Shared inverse: membership index is the encode-dag entry (leading
    // order; suffix digits never pin entries). Parse-time uses the same
    // helper at the textual entry, including NOP holes.
    if (auto MemErr = verifyMemberAtStampedEntry(
            Opc, ExpectMode, static_cast<uint8_t>(I), RowEntries, SeenUnits,
            SeenEntryBits))
      return MemErr;
  }

  // Structural inverse product plan: registry row/completion/bytes only
  // (entry occupancy from the inverse matrix, never the PacketFormats
  // planner).
  SlotBits Occupied = static_cast<SlotBits>(SeenEntryBits);
  BundlePlan Plan = makeProductPlan(Occupied, MemberOpcodes);
  Plan.Row = Row;
  Plan.Completion =
      expectedGoldenRowCompletion(MemberOpcodes.size(), /*HasPadNop=*/false);
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

  // Shared port-budget re-check (one mechanism with commit P4:
  // canCoissueProductCycle / instrsFormOneLegalCycle). The named hook is
  // haydnVerifyCommittedBundlePortBudget — same arithmetic as commit.
  // Port demand is an operand fact, so
  // this check lives on the MI overload — the opcode-only view cannot see
  // it (same split as ResourceCycle MID-vs-MI port counting). Three GPR
  // writes (3xADD32) need >= 2 cycles under 2W even when E3 unit geometry
  // admits the entries.
  if (const MachineBasicBlock *MBB = BundleRoot.getParent()) {
    SmallVector<MachineInstr *, 3> Kids;
    for (MachineBasicBlock::const_instr_iterator I =
             std::next(BundleRoot.getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
        continue;
      if (isPadNopOpcode(I->getOpcode()))
        continue;
      Kids.push_back(const_cast<MachineInstr *>(&*I));
    }
    if (haydnVerifyCommittedBundlePortBudget(Kids))
      return std::string(
          "structural inverse: cycle RF port demand exceeds one issue "
          "cycle (shared haydnVerifyCommittedBundlePortBudget hook)");
  }

  // Completion, when present, is checked against golden product-row fill
  // independently of the stamper helper: unused entry windows are
  // architectural NOP (AllEntriesReal). Empty membership with no pad is
  // residual idle stub. Missing completion stays allowed on row-only
  // stamps; multi-member hard-root verify requires it separately.
  if (auto Comp = getBundleCompletionID(BundleRoot)) {
    if (!isStubCompletion(*Comp) && !isProductLegalCompletion(*Comp))
      return std::string("BUNDLE root has unknown CompletionStateID");
    const CompletionStateID Expected = expectedGoldenRowCompletion(
        static_cast<unsigned>(Members.size()), bundleHasPadNop(BundleRoot));
    if (*Comp != Expected)
      return std::string(
          "BUNDLE root CompletionStateID does not match golden row fill");
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
  const unsigned RowEntries = bundleRowEntryCount(*Row);
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
