//===- HaydnBundleVerify.cpp - Fail-closed committed-bundle check ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Out-of-line verifyCommittedBundle / private-member lookup.
// Peer: AIEBaseInstrInfo.cpp:1595-1614 verifyInstruction fail-closed.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleVerify.h"
#include "Haydn.h"
#include "HaydnBundlePortBudget.h"
// haydnOpcodeName is the pure generated MC name-table accessor (solver header
// is pulled by HaydnBundle.h). The independent verifier never calls the
// forward planner (Bundle canAdd / hasValidFormat / exactTryAddProduct /
// PacketFormats planner / findFormatEMember / opcodesHaveFormatEUnitCover).
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"
#define GET_FORMAT_E_INVERSE_INDEX
#include "HaydnGenFormatEInverse.inc"

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

static bool encodeableInverseRecord(const format_e::FormatEInverseRec &R);

/// Opcode → generated FormatEInverse row ids (table indices, not MemberId).
/// Sole inverse source is HaydnGenFormatEInverse.inc / FormatEInverse.
/// Member opcodes come from the generated MemberId→opcode column; catalog
/// logicals from lookupGeneratedMemberToLogical; public aliases from a
/// closed opcode→catalog extra key (AIE AIEMCFormats.h:376-379 overlay).
/// Never Bundle.canAdd, occupancy DFS, or peelLogicalOpcodeName.
static void collectInverseIdsForOpcode(unsigned Opc,
                                       SmallVectorImpl<unsigned> &Ids) {
  Ids.clear();
  if (Opc == 0 || Opc == Haydn::NOP)
    return;

  static const DenseMap<unsigned, SmallVector<unsigned, 8>> Generated = [] {
    DenseMap<unsigned, SmallVector<unsigned, 8>> M;
    M.reserve(FormatEMemberOpcodeCount);
    const unsigned InverseN =
        sizeof(format_e::FormatEInverse) / sizeof(format_e::FormatEInverse[0]);
    for (unsigned I = 0; I < InverseN; ++I) {
      // Independently sorted inverse row — never FormatEInverse[MemberId].
      const format_e::FormatEInverseRec &R = format_e::FormatEInverse[I];
      if (!encodeableInverseRecord(R))
        continue;
      const unsigned Mid = R.MemberId;
      if (Mid >= FormatEMemberOpcodeCount)
        continue;
      const unsigned MemberOpc = FormatEMemberOpcodes[Mid];
      if (MemberOpc == 0 || MemberOpc == Haydn::NOP)
        continue;
      M[MemberOpc].push_back(I);
      if (const unsigned Log =
              format_e::lookupGeneratedMemberToLogical(MemberOpc)) {
        if (Log != MemberOpc && Log != 0 && Log != Haydn::NOP)
          M[Log].push_back(I);
      }
    }
    // Extra inverse keys: public mnemonic opcode → catalog logical opcode.
    // Opcode-keyed (not suffix peel). Catalog rows already sit in M.
    static const std::pair<unsigned, unsigned> AliasToCatalog[] = {
        {Haydn::LD32, Haydn::S_LW_WITH_IMM},
        {Haydn::ST32, Haydn::S_SW_WITH_IMM},
        {Haydn::LD64, Haydn::D_LDW_WITH_IMM},
        {Haydn::ST64, Haydn::D_SDW_WITH_IMM},
        {Haydn::LD8, Haydn::S_LBS_WITH_IMM},
        {Haydn::LDU8, Haydn::S_LBU_WITH_IMM},
        {Haydn::ST8, Haydn::S_SB_WITH_IMM},
        {Haydn::LD16, Haydn::S_LHWS_WITH_IMM},
        {Haydn::LDU16, Haydn::S_LHWU_WITH_IMM},
        {Haydn::ST16, Haydn::S_SHW_WITH_IMM},
        {Haydn::LD32_POST, Haydn::S_LW_POST_IMM},
        {Haydn::LD32_POST_INC, Haydn::S_LW_POST_IMM},
        {Haydn::ST32_POST, Haydn::S_SW_POST_IMM},
        {Haydn::ST32_POST_INC, Haydn::S_SW_POST_IMM},
        {Haydn::LD64_POST, Haydn::D_LDW_POST_IMM},
        {Haydn::ST64_POST, Haydn::D_SDW_POST_IMM},
        {Haydn::SEXT_GPR32_TO_DR64, Haydn::SEXT32T64},
        {Haydn::MOV_GPR_TO_DR64, Haydn::SEXT32T64},
    };
    for (const auto &Pair : AliasToCatalog) {
      if (Pair.first == Pair.second || M.count(Pair.first))
        continue;
      auto It = M.find(Pair.second);
      if (It == M.end())
        continue;
      M[Pair.first] = It->second;
    }
    return M;
  }();

  if (auto It = Generated.find(Opc); It != Generated.end())
    Ids.append(It->second.begin(), It->second.end());
}

static unsigned inverseTableCount() {
  return static_cast<unsigned>(sizeof(format_e::FormatEInverse) /
                               sizeof(format_e::FormatEInverse[0]));
}

/// Inverse row is encodeable when its placement key uniquely reconstructs
/// MemberId and the generated member-opcode column is a real non-NOP opcode.
/// FormatEInverse only — never FormatEMembers / UnitMap / name peel.
/// Peer: inverse of AIEMCFormats::getAlternateInstsOpcode
/// (AIEMCFormats.h:376-379; CodeGenFormat.cpp:155-163).
static bool encodeableInverseRecord(const format_e::FormatEInverseRec &R) {
  if (R.MemberId >= format_e::FormatEMemberCount || !R.Logical)
    return false;
  if (StringRef(R.Logical).equals_insensitive("NOP"))
    return false;
  if (!R.TypeName || StringRef(R.TypeName).empty())
    return false;
  if (R.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[R.MemberId];
  if (MemberOpc == 0 || MemberOpc == Haydn::NOP)
    return false;
  const int Hit = format_e::findInverseMemberId(R.Mode, R.EntryIdx, R.Unit,
                                                R.TypeCode, R.Opcode);
  return Hit >= 0 && static_cast<unsigned>(Hit) == R.MemberId;
}

static const format_e::FormatEInverseRec *
haydnInverseRecordFromOpcode(unsigned Opc, uint8_t Mode, uint8_t EntryIdx,
                             uint32_t UsedUnitMask, bool MatchEntry) {
  SmallVector<unsigned, 8> Ids;
  collectInverseIdsForOpcode(Opc, Ids);
  for (unsigned I : Ids) {
    if (I >= inverseTableCount())
      continue;
    const format_e::FormatEInverseRec &R = format_e::FormatEInverse[I];
    if (R.Mode != Mode)
      continue;
    if (MatchEntry && R.EntryIdx != EntryIdx)
      continue;
    if (R.Unit < 32 && (UsedUnitMask & (1u << R.Unit)))
      continue;
    if (!encodeableInverseRecord(R))
      continue;
    return &R;
  }
  return nullptr;
}

static uint32_t inverseUnitMaskForOpcode(unsigned Opc, uint8_t Mode) {
  SmallVector<unsigned, 8> Ids;
  collectInverseIdsForOpcode(Opc, Ids);
  uint32_t Mask = 0;
  for (unsigned I : Ids) {
    if (I >= inverseTableCount())
      continue;
    const format_e::FormatEInverseRec &R = format_e::FormatEInverse[I];
    if (R.Mode != Mode || R.Unit >= 32)
      continue;
    if (!encodeableInverseRecord(R))
      continue;
    Mask |= 1u << R.Unit;
  }
  return Mask;
}

static bool inverseOpcodesHaveUnitCoverForMode(ArrayRef<unsigned> Opcodes,
                                               uint8_t Mode) {
  if (Opcodes.size() < 2)
    return true;
  SmallVector<uint32_t, 3> Masks;
  Masks.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes) {
    uint32_t M = inverseUnitMaskForOpcode(Opc, Mode);
    if (M == 0)
      return false;
    Masks.push_back(M);
  }
  return inverseMasksAssignable(Masks);
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

/// MemberId of a generated private Format E member opcode, or ~0u.
/// FormatEMemberOpcodes column only — never FormatEMembers / name peel.
static unsigned privateMemberIdForOpcode(unsigned Opc) {
  if (Opc == 0 || Opc == Haydn::NOP)
    return ~0u;
  static const DenseMap<unsigned, unsigned> Map = [] {
    DenseMap<unsigned, unsigned> M;
    M.reserve(FormatEMemberOpcodeCount);
    for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
      const unsigned MemberOpc = FormatEMemberOpcodes[I];
      if (MemberOpc == 0 || MemberOpc == Haydn::NOP)
        continue;
      M.try_emplace(MemberOpc, I);
    }
    return M;
  }();
  auto It = Map.find(Opc);
  return It == Map.end() ? ~0u : It->second;
}

/// Inverse + unit injectivity + encodeability for one opcode at a known
/// encode-dag entry. Private members use inverseRecordForMemberId (child
/// order may not match after residual rebind). Residual/logicals use
/// membership EntryIdx against opcode-keyed FormatEInverse rows. Source is
/// inverseRecordForMemberId / haydnInverseRecordFromOpcode only
/// (no peelLogicalOpcodeName, no findFormatEMember / Bundle.canAdd).
static std::optional<std::string>
verifyMemberAtStampedEntry(unsigned Opc, uint8_t ExpectMode,
                           uint8_t EntryIdx, unsigned RowEntries,
                           uint32_t &SeenUnits, uint32_t &SeenEntryBits) {
  const format_e::FormatEInverseRec *Inv = nullptr;
  const unsigned PrivId = privateMemberIdForOpcode(Opc);
  if (PrivId != ~0u) {
    // Private members carry entry on the inverse MemberId row. Cutover may
    // rebind a store onto e0 while leaving it as a later child, so
    // membership index is not the encode-dag entry here.
    Inv = format_e::inverseRecordForMemberId(PrivId);
    if (!Inv || Inv->MemberId != PrivId || !encodeableInverseRecord(*Inv))
      return std::string(
          "structural inverse: FormatEInverse misses exact encodeable "
          "MemberId for private member");
  } else {
    Inv = haydnInverseRecordFromOpcode(Opc, ExpectMode, EntryIdx, SeenUnits,
                                       /*MatchEntry=*/true);
    if (!Inv)
      return std::string(
                 "structural inverse: committed logical has no generated "
                 "member at its stamped entry (unknown or misplaced): ") +
             std::string(haydnOpcodeName(Opc)) + " @mode" +
             std::to_string(ExpectMode) + " entry " +
             std::to_string(EntryIdx);
    if (!encodeableInverseRecord(*Inv))
      return std::string(
                 "structural inverse: inverse record not encodeable for "
                 "committed logical at stamped entry: ") +
             std::string(haydnOpcodeName(Opc)) + " @mode" +
             std::to_string(ExpectMode) + " entry " +
             std::to_string(EntryIdx);
    if (Inv->EntryIdx != EntryIdx)
      return std::string(
          "structural inverse: member entry mismatch vs membership order");
  }

  if (static_cast<unsigned>(Inv->EntryIdx) >= RowEntries)
    return std::string(
        "structural inverse: entry index exceeds stamped row capacity");
  if (Inv->Unit < 32) {
    if (SeenUnits & (1u << Inv->Unit))
      return std::string(
          "structural inverse: chosen Format E members are not "
          "unit-injective (unit injectivity)");
    SeenUnits |= 1u << Inv->Unit;
  }
  if (SeenEntryBits & (1u << Inv->EntryIdx))
    return std::string(
        "structural inverse: duplicate entry index among members");
  if (Inv->Mode != ExpectMode)
    return std::string(
        Inv->Mode == 0
            ? "structural inverse: E2 member under E96ThreeEntry row"
            : "structural inverse: E3 member under E96TwoEntry row");
  SeenEntryBits |= 1u << Inv->EntryIdx;
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
///   * private member opcodes: exact encodeable MemberId inverse, Mode equal
///     to the stamped row mode, entry index inside row capacity, entry- and
///     unit-injective
///   * residual/logical opcodes: haydnInverseRecordFromOpcode at the child's
///     MEMBERSHIP ENTRY under the stamped mode (FormatEInverse row ids, never
///     MemberId-as-index). Committed child order IS the entry order; verify
///     checks it, it never re-plans it, never findFormatEMember / name peel.
///     The inverse row must be encodeable (placement key reconstructs
///     MemberId; member-opcode column is a real non-NOP opcode).
///   * anything else (unknown logical, no inverse at the stamped entry)
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
  // independently generated FormatEInverse unit bits keyed by opcode, not
  // Bundle.canAdd / opcodesHaveFormatEUnitCover / name peel. Residual
  // FieldSlots can look like a legal 3-entry E3 pack while two stores both
  // require LOADSTORE0 e0. Refuse here so MC never sees the illegal BUNDLE.
  if (!inverseOpcodesHaveUnitCoverForMode(MemberOpcodes, /*Mode=*/0) &&
      !inverseOpcodesHaveUnitCoverForMode(MemberOpcodes, /*Mode=*/1))
    return std::string(
        "structural inverse: chosen Format E members are not "
        "unit-injective (unit injectivity; execution units are not "
        "encoded entry identity)");

  // One-to-one serialize: stamped row must have enough entries for every
  // real member. E96TwoEntry with 3 reals used to pass verify and then drop
  // a child at AsmPrinter (NumEntries from row imm only).
  const unsigned RowEntries = [&] {
    const format::BundleFormatRowDesc *Desc = format::getBundleFormatRow(Row);
    return Desc ? Desc->EntryCount : 0u;
  }();
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

std::optional<std::string>
verifyParsedBundle(BundleFormatRowID Row, ArrayRef<const MCInst *> Entries,
                   const HaydnBaseMCFormats &Fmts, const MCInstrInfo &MII,
                   const MCRegisterInfo *MRI) {
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
  SmallVector<unsigned, 3> MemberOpcodes;
  SmallVector<const MCInst *, 3> RealInsts;
  for (const MCInst *Inst : Entries) {
    if (!Inst || isPadNopOpcode(Inst->getOpcode()))
      continue;
    MemberOpcodes.push_back(Inst->getOpcode());
    RealInsts.push_back(Inst);
  }
  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");
  if (!inverseOpcodesHaveUnitCoverForMode(MemberOpcodes, ExpectMode))
    return std::string(
        "structural inverse: chosen Format E members are not "
        "unit-injective (unit injectivity; execution units are not "
        "encoded entry identity)");

  uint32_t SeenUnits = 0;
  uint32_t SeenEntryBits = 0;
  for (unsigned E = 0, EE = Entries.size(); E != EE; ++E) {
    const MCInst *Inst = Entries[E];
    if (!Inst || isPadNopOpcode(Inst->getOpcode()))
      continue;
    if (isRepresentationExpandPseudo(Inst->getOpcode())) {
      if (MemberOpcodes.size() != 1)
        return std::string(
            "structural inverse: representation-expand pseudo must be a "
            "solo committed cycle (printer expands one-to-one)");
      continue;
    }
    if (auto MemErr = verifyMemberAtStampedEntry(
            Inst->getOpcode(), ExpectMode, static_cast<uint8_t>(E), RowEntries,
            SeenUnits, SeenEntryBits))
      return MemErr;
  }

  if (auto RegErr = haydnCheckParsedBundleRegs(RealInsts, MII, MRI))
    return std::string("structural inverse: ") + *RegErr;
  return std::nullopt;
}

/// MIR entry: rebuild plan from BUNDLE root row + completion imms + children.
/// Fail-closed: missing/unknown row imm or missing completion is an error.
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

  // Completion is mandatory on every residual root. Unused entry windows
  // are architectural NOP (AllEntriesReal). Empty membership with no pad is
  // residual idle stub. Golden-row fill, not selectCompletionForMembersAndPads.
  auto Comp = getBundleCompletionID(BundleRoot);
  if (!Comp)
    return std::string(
        "structural inverse: missing CompletionStateID on BUNDLE root "
        "(residual/private members require typed completion)");
  if (!isStubCompletion(*Comp) && !isProductLegalCompletion(*Comp))
    return std::string("BUNDLE root has unknown CompletionStateID");
  const CompletionStateID Expected = expectedGoldenRowCompletion(
      static_cast<unsigned>(Members.size()), bundleHasPadNop(BundleRoot));
  if (*Comp != Expected)
    return std::string(
        "BUNDLE root CompletionStateID does not match golden row fill");
  if (OutPlan)
    OutPlan->Completion = *Comp;
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
