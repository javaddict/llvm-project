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
#include "HaydnPortModel.h"
// Opcode names come from the generated MC tables (HaydnMCTargetDesc.cpp
// GET_INSTRINFO_MC_DESC) — same backing store as haydnOpcodeName
// (HaydnBundleFormatSolver.h:109-111) without including that solver header
// or HaydnBundle.h. This TU never calls Bundle canAdd / hasValidFormat /
// exactTryAddProduct / PacketFormats planner / findFormatEMember /
// opcodesHaveFormatEUnitCover.
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

#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H)
#error "HaydnBundleVerify.cpp must not include HaydnBundle.h (no Bundle.canAdd)"
#endif
#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H)
#error \
    "HaydnBundleVerify.cpp must not include HaydnBundleFormatSolver.h (no forward solver)"
#endif

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

namespace llvm {

// Generated MC name tables (HaydnMCTargetDesc.cpp GET_INSTRINFO_MC_DESC).
// Local accessor so this TU never includes HaydnBundleFormatSolver.h.
extern const unsigned HaydnInstrNameIndices[];
extern const char HaydnInstrNameData[];

namespace haydn {
namespace bundle {

static StringRef inverseOpcodeName(unsigned Opcode) {
  return StringRef(&HaydnInstrNameData[HaydnInstrNameIndices[Opcode]]);
}

/// Expand-owned / cycle-forming / leftover generic COPY-subreg that must not
/// complete an inverse record. Representation-expand solo cycles (B/RET/
/// BR_JT/PseudoCALLIndirect) are the typed printer exception and are skipped
/// by the caller.
/// Peer: AIEPseudoBranchExpansion.cpp:43-57 expands named branch desc only.
static bool isUnexpandedResidualPseudo(unsigned Opc) {
  if (isRepresentationExpandPseudo(Opc))
    return false;
  return isLeftoverGenericResidualPseudo(Opc) ||
         isResidualCycleFormingPseudo(Opc) || isExpandOwnedSemanticPseudo(Opc);
}

bool isLeftoverGenericResidualPseudo(unsigned Opc) {
  switch (Opc) {
  case TargetOpcode::COPY:
  case TargetOpcode::SUBREG_TO_REG:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::EXTRACT_SUBREG:
  case TargetOpcode::REG_SEQUENCE:
    return true;
  default:
    return false;
  }
}

const format_e::FormatEMemberRec *lookupPrivateFormatEMember(unsigned Opc) {
  if (Opc == 0 || Opc == Haydn::NOP)
    return nullptr;
  // Opcode → MemberId, then independently generated FormatEInverse
  // (inverseRecordForMemberId). Peer: AIE getFormatDescIndex opcode switch
  // (AIEMCFormats.h:373-374; CodeGenFormat.cpp:132) — never a linear walk of
  // independently sorted FormatEInverse as if the row index were MemberId,
  // never peelLogicalOpcodeName.
  static const DenseMap<unsigned, unsigned> Map = [] {
    DenseMap<unsigned, unsigned> M;
    M.reserve(FormatEMemberOpcodeCount);
    for (unsigned Mid = 0; Mid < FormatEMemberOpcodeCount; ++Mid) {
      const unsigned MemberOpc = FormatEMemberOpcodes[Mid];
      if (MemberOpc == 0 || MemberOpc == Haydn::NOP)
        continue;
      const format_e::FormatEInverseRec *Inv =
          format_e::inverseRecordForMemberId(Mid);
      if (!Inv || Inv->MemberId != Mid || !format_e::completeInverseRecord(*Inv))
        continue;
      M.try_emplace(MemberOpc, Mid);
    }
    return M;
  }();
  auto It = Map.find(Opc);
  if (It == Map.end())
    return nullptr;
  return &format_e::FormatEMembers[It->second];
}

} // namespace bundle
} // namespace haydn

// CB-161 (2026-08-21): countSFRPorts's member classification. Lives beside
// the inverse-table map it reuses (one classification site, no second
// opcode set); defined outside the haydn::bundle block because PortModel.h
// declares it directly in namespace llvm. True only for complete-inverse
// generated members — the same population D493 lets appear in MIR after
// the exact post-RA commit.
bool haydnIsPrivateFormatEMemberOpcode(unsigned Opcode) {
  return haydn::bundle::lookupPrivateFormatEMember(Opcode) != nullptr;
}

namespace haydn {
namespace bundle {

static bool encodeableInverseRecord(const format_e::FormatEInverseRec &R);

/// Opcode → generated FormatEInverse row ids (table indices, not MemberId).
/// Sole inverse source is HaydnGenFormatEInverse.inc / FormatEInverse.
/// Keys: MemberId opcode column, generated member→logical, canonical MC
/// name matching FormatEInverse.Logical (MCInstrInfo::getName — same
/// backing store as TII getName). Extra public mnemonic opcodes that are
/// not MC-pseudo may share a catalog logical's inverse span (AIE
/// getAlternateInstsOpcode overlay, AIEMCFormats.h:376-379). Residual
/// cycle-forming / expand-owned / other MC-pseudos that the inverse table
/// does not list cannot complete through that extra key.
/// Never Bundle.canAdd, occupancy DFS, or peelLogicalOpcodeName.
static void collectInverseIdsForOpcode(unsigned Opc,
                                       SmallVectorImpl<unsigned> &Ids) {
  Ids.clear();
  if (Opc == 0 || Opc == Haydn::NOP || isUnexpandedResidualPseudo(Opc))
    return;

  static const DenseMap<unsigned, SmallVector<unsigned, 8>> Generated = [] {
    DenseMap<unsigned, SmallVector<unsigned, 8>> M;
    M.reserve(FormatEMemberOpcodeCount);
    const unsigned InverseN =
        sizeof(format_e::FormatEInverse) / sizeof(format_e::FormatEInverse[0]);
    const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
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
    // Canonical opcode name (MC / TII getName) matching generated Logical.
    // Residual FieldSlot suffixes are not Logical names — no peel.
    SmallVector<unsigned, 8> NameIds;
    for (unsigned NameOpc = 1; NameOpc < MII.getNumOpcodes(); ++NameOpc) {
      if (NameOpc == Haydn::NOP || M.count(NameOpc) ||
          isUnexpandedResidualPseudo(NameOpc) || MII.get(NameOpc).isPseudo())
        continue;
      NameIds.clear();
      format_e::inverseIdsForLogical(MII.getName(NameOpc), NameIds);
      if (NameIds.empty())
        continue;
      SmallVector<unsigned, 8> Encodeable;
      for (unsigned I : NameIds) {
        if (I >= InverseN)
          continue;
        if (encodeableInverseRecord(format_e::FormatEInverse[I]))
          Encodeable.push_back(I);
      }
      if (!Encodeable.empty())
        M[NameOpc] = std::move(Encodeable);
    }
    // Extra inverse keys: public mnemonic opcode → catalog logical opcode.
    // Opcode-keyed (not suffix peel). Catalog rows already sit in M.
    // MC-pseudo sources cannot complete through this extra key: residual
    // expand-owned / cycle-forming leftovers are not inverse records.
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
        {Haydn::ST32_POST, Haydn::S_SW_POST_IMM},
        {Haydn::LD64_POST, Haydn::D_LDW_POST_IMM},
        {Haydn::ST64_POST, Haydn::D_SDW_POST_IMM},
        {Haydn::SEXT_GPR32_TO_DR64, Haydn::SEXT32T64},
    };
    for (const auto &Pair : AliasToCatalog) {
      if (Pair.first == Pair.second || M.count(Pair.first))
        continue;
      if (Pair.first >= MII.getNumOpcodes() || MII.get(Pair.first).isPseudo())
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
  if (!format_e::completeInverseRecord(R))
    return false;
  if (R.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[R.MemberId];
  return MemberOpc != 0 && MemberOpc != Haydn::NOP;
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
  SmallVector<uint32_t, 3> Masks;
  Masks.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes) {
    if (Opc == 0 || isPadNopOpcode(Opc) || isRepresentationExpandPseudo(Opc))
      continue;
    uint32_t M = inverseUnitMaskForOpcode(Opc, Mode);
    // Unknown opcodes have mask 0 and must not pass — even as a singleton.
    if (M == 0)
      return false;
    Masks.push_back(M);
  }
  if (Masks.empty())
    return true;
  if (Masks.size() < 2)
    return true;
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

/// BundlePlan from inverse-verified facts only.
/// Peer: AIE verifyInstruction (AIEBaseInstrInfo.cpp:1595-1614) checks in
/// place and does not rebuild a planner Bundle. Haydn overlay fills the
/// existing BundlePlan for OutPlan callers from the stamped row, inverse
/// occupancy, and golden-row completion. Never makeProductPlan /
/// selectProductRow / selectCompletionFor /
/// selectCompletionForMembersAndPads (those are the stamper / forward
/// cardinality planner).
static BundlePlan makeInverseVerifiedPlan(BundleFormatRowID Row,
                                          SlotBits Occupied,
                                          ArrayRef<unsigned> Members,
                                          bool HasPadNop) {
  BundlePlan P;
  P.Row = Row;
  P.Completion = expectedGoldenRowCompletion(
      static_cast<unsigned>(Members.size()), HasPadNop);
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = productParcelBytes();
  P.Cycles = OneCycle;
  return P;
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

/// Residual/logical member that must have a completed FormatEInverse record.
/// Private members, pad NOP, and representation-expand solo cycles are not
/// residual inverse roots. Peer: AIEBaseInstrInfo.cpp:1595-1614
/// verifyInstruction fail-closed (Haydn overlay is FormatEInverse).
static bool haydnResidualLogicalNeedsCompletedInverse(unsigned Opc) {
  if (Opc == 0 || isPadNopOpcode(Opc) || isRepresentationExpandPseudo(Opc))
    return false;
  if (isUnexpandedResidualPseudo(Opc))
    return true;
  return privateMemberIdForOpcode(Opc) == ~0u;
}

/// True when Inv is a row of the independently generated FormatEInverse
/// table (pointer identity). A mutated stack copy is not a generated record.
/// Peer: AIE getAlternateInstsOpcode returns generated table entries
/// (CodeGenFormat.cpp:155-163; AIEMCFormats.h:376-379).
static bool isGeneratedInverseTableRow(const format_e::FormatEInverseRec *Inv) {
  if (!Inv)
    return false;
  const format_e::FormatEInverseRec *ById =
      format_e::inverseRecordForMemberId(Inv->MemberId);
  return ById == Inv && encodeableInverseRecord(*Inv);
}

/// Complete an independently generated inverse record: membership,
/// encodeability, unit injectivity, and (for residual/logical) stamped
/// entry. Never FormatEMembers Mode/Entry/Logical re-filter, never
/// Bundle.canAdd / occupancy DFS / name peel. Rejects mutated copies —
/// only independently generated FormatEInverse table rows complete.
static std::optional<std::string>
completeInverseRecord(const format_e::FormatEInverseRec &R, unsigned Opc,
                      uint8_t ExpectMode, uint8_t EntryIdx,
                      unsigned RowEntries, uint32_t UsedUnitMask,
                      bool MatchEntry) {
  if (!isGeneratedInverseTableRow(&R) || !encodeableInverseRecord(R))
    return std::string(
               "structural inverse: inverse record not encodeable for "
               "committed member at stamped entry: ") +
           std::string(inverseOpcodeName(Opc)) + " @mode" +
           std::to_string(ExpectMode) + " entry " + std::to_string(EntryIdx);
  if (R.Mode != ExpectMode)
    return std::string(
        R.Mode == 0 ? "structural inverse: E2 member under E96ThreeEntry row"
                    : "structural inverse: E3 member under E96TwoEntry row");
  if (MatchEntry && R.EntryIdx != EntryIdx)
    return std::string(
        "structural inverse: member entry mismatch vs membership order");
  if (static_cast<unsigned>(R.EntryIdx) >= RowEntries)
    return std::string(
        "structural inverse: entry index exceeds stamped row capacity");
  if (R.Unit < 32 && (UsedUnitMask & (1u << R.Unit)))
    return std::string(
        "structural inverse: chosen Format E members are not "
        "unit-injective (unit injectivity)");
  return std::nullopt;
}

/// Inverse + unit injectivity + encodeability for one opcode at a known
/// encode-dag entry. Private members use inverseRecordForMemberId (child
/// order may not match after residual rebind). Residual/logicals use
/// membership EntryIdx against opcode-keyed FormatEInverse rows, then
/// completeInverseRecord (mandatory; ResidualCompletedBits is not optional).
/// Source is inverseRecordForMemberId / haydnInverseRecordFromOpcode only
/// (no peelLogicalOpcodeName, no findFormatEMember / Bundle.canAdd).
static std::optional<std::string>
verifyMemberAtStampedEntry(unsigned Opc, uint8_t ExpectMode,
                           uint8_t EntryIdx, unsigned RowEntries,
                           uint32_t &SeenUnits, uint32_t &SeenEntryBits,
                           uint32_t &ResidualCompletedBits,
                           unsigned ResidualBit) {
  // Pad NOP is completion fill, not a membership inverse root.
  // Peer: AIE idle slot is an unused format entry, not an alternate opcode.
  if (isPadNopOpcode(Opc))
    return std::nullopt;

  // Unexpanded residual pseudos are not inverse keys. A catalog extra key
  // must not complete LD32_POST_INC / MOV_GPR_TO_DR64 as if they were the
  // real member (MC-pseudo sources are excluded from that extra key).
  // Peer: AIEPseudoBranchExpansion.cpp:43-57 leftover expand-owned is fatal
  // after the expand pass.
  if (isUnexpandedResidualPseudo(Opc))
    return std::string(
               "structural inverse: residual/logical inverse record not "
               "completed at membership entry: ") +
           std::string(inverseOpcodeName(Opc)) + " entry " +
           std::to_string(EntryIdx);

  const format_e::FormatEInverseRec *Inv = nullptr;
  const unsigned PrivId = privateMemberIdForOpcode(Opc);
  const bool ResidualLogical = PrivId == ~0u;
  if (!ResidualLogical) {
    // Private members carry entry on the inverse MemberId row. Cutover may
    // rebind a store onto e0 while leaving it as a later child, so
    // membership index is not the encode-dag entry here.
    Inv = format_e::inverseRecordForMemberId(PrivId);
    if (!Inv || Inv->MemberId != PrivId)
      return std::string(
          "structural inverse: FormatEInverse misses exact encodeable "
          "MemberId for private member");
  } else {
    Inv = haydnInverseRecordFromOpcode(Opc, ExpectMode, EntryIdx, SeenUnits,
                                       /*MatchEntry=*/true);
    if (!Inv)
      return std::string(
                 "structural inverse: residual/logical inverse record not "
                 "completed at membership entry (no generated member): ") +
             std::string(inverseOpcodeName(Opc)) + " @mode" +
             std::to_string(ExpectMode) + " entry " +
             std::to_string(EntryIdx);
  }

  // Consume only independently generated FormatEInverse table rows.
  // A mutated copy (stack / rewritten fields) is not a generated record.
  if (!isGeneratedInverseTableRow(Inv)) {
    if (ResidualLogical)
      return std::string(
                 "structural inverse: residual/logical inverse record not "
                 "completed at membership entry: ") +
             std::string(inverseOpcodeName(Opc)) + " entry " +
             std::to_string(EntryIdx);
    return std::string(
        "structural inverse: FormatEInverse misses exact encodeable "
        "MemberId for private member");
  }

  if (auto CompErr = completeInverseRecord(*Inv, Opc, ExpectMode, EntryIdx,
                                           RowEntries, SeenUnits,
                                           /*MatchEntry=*/ResidualLogical))
    return CompErr;

  if (Inv->Unit < 32)
    SeenUnits |= 1u << Inv->Unit;
  if (SeenEntryBits & (1u << Inv->EntryIdx))
    return std::string(
        "structural inverse: duplicate entry index among members");
  SeenEntryBits |= 1u << Inv->EntryIdx;
  if (ResidualLogical && ResidualBit < 32)
    ResidualCompletedBits |= 1u << ResidualBit;
  return std::nullopt;
}

/// Residual/logical roots with no FormatEInverse ids fail before unit-cover
/// so the diagnostic is inverse-record completion, not a structural mask.
static std::optional<std::string>
haydnRequireInverseIdsOnResidualRoots(ArrayRef<unsigned> MemberOpcodes) {
  for (unsigned I = 0, E = MemberOpcodes.size(); I != E; ++I) {
    const unsigned Opc = MemberOpcodes[I];
    if (!haydnResidualLogicalNeedsCompletedInverse(Opc))
      continue;
    SmallVector<unsigned, 8> Ids;
    collectInverseIdsForOpcode(Opc, Ids);
    if (Ids.empty())
      return std::string(
                 "structural inverse: residual/logical inverse record not "
                 "completed at membership entry: ") +
             std::string(inverseOpcodeName(Opc)) + " entry " +
             std::to_string(I);
  }
  return std::nullopt;
}

/// After the per-member inverse walk: every residual/logical member must
/// have completed an independently generated inverse record. A skip in
/// the first walk (structural/forward acceptance) fails closed here.
static std::optional<std::string>
haydnRequireCompletedInverseOnResidualRoots(ArrayRef<unsigned> MemberOpcodes,
                                            uint32_t ResidualCompletedBits) {
  for (unsigned I = 0, E = MemberOpcodes.size(); I != E; ++I) {
    if (!haydnResidualLogicalNeedsCompletedInverse(MemberOpcodes[I]))
      continue;
    if (I >= 32 || !(ResidualCompletedBits & (1u << I)))
      return std::string(
                 "structural inverse: residual/logical inverse record not "
                 "completed at membership entry: ") +
             std::string(inverseOpcodeName(MemberOpcodes[I])) + " entry " +
             std::to_string(I);
  }
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
///     MemberId-as-index), then completeInverseRecord (unit injectivity,
///     membership, encodeability). Committed child order IS the entry order,
///     including pad NOP as unused windows; verify checks it, it never
///     compact-replans pads onto earlier entries, never findFormatEMember /
///     name peel. Completion of the inverse record is mandatory on every
///     residual root — never structural/forward acceptance.
///   * anything else (unknown logical, no inverse at the stamped entry)
///     fails closed — the verifier must never ask the forward solver which
///     format fits
///   * OutPlan filled from stamped row + inverse occupancy + golden-row
///     completion (never makeProductPlan / selectProductRow /
///     selectCompletionFor / selectCompletionForMembersAndPads)
///
/// \returns nullopt on success; human-readable reason on failure.
std::optional<std::string>
verifyCommittedBundle(BundleFormatRowID Row, ArrayRef<unsigned> MemberOpcodes,
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan) {
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  // Pad NOP is CompletionState, not a membership inverse root. Opcode-only
  // callers may still pass architectural NOP as an unused encode-dag entry.
  // Do not compact pads: compacting would re-plan later residual/logicals
  // onto earlier entries (structural acceptance). Peer: AIE unused format
  // entry is idle, not an alternate opcode (AIEMCFormats.h:376-379).
  SmallVector<unsigned, 3> Reals;
  bool HasPadNop = false;
  Reals.reserve(MemberOpcodes.size());
  for (unsigned Opc : MemberOpcodes) {
    if (isPadNopOpcode(Opc)) {
      HasPadNop = true;
      continue;
    }
    Reals.push_back(Opc);
  }

  if (Reals.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  if (auto ResidualIdsErr =
          haydnRequireInverseIdsOnResidualRoots(MemberOpcodes))
    return ResidualIdsErr;

  // Format E unit injectivity pre-check (units ≠ encoded entry identity):
  // independently generated FormatEInverse unit bits keyed by opcode, not
  // Bundle.canAdd / opcodesHaveFormatEUnitCover / name peel. Residual
  // FieldSlots can look like a legal 3-entry E3 pack while two stores both
  // require LOADSTORE0 e0. Refuse here so MC never sees the illegal BUNDLE.
  if (!inverseOpcodesHaveUnitCoverForMode(Reals, /*Mode=*/0) &&
      !inverseOpcodesHaveUnitCoverForMode(Reals, /*Mode=*/1))
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
  // Encode-dag length includes pad holes. Extra pads must not compact away
  // so a 3-slot sequence cannot hide in an E2 row.
  if (MemberOpcodes.size() > RowEntries || Reals.size() > RowEntries)
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

  // Empty membership: pad-only idle is full-slot architectural NOP
  // (AllEntriesReal). Empty with no pad stays residual idle stub.
  // Inverse-verified plan only — never makeProductPlan / selectProductRow.
  if (Reals.empty()) {
    BundlePlan Stall = makeInverseVerifiedPlan(Row, /*Occupied=*/0, Reals,
                                               HasPadNop);
    if (!Stall.isProductLegal())
      return std::string("empty cycle BundlePlan not product-legal");
    if (OutPlan)
      *OutPlan = Stall;
    return std::nullopt;
  }

  // Independent inverse member matrix (no forward planner / DFS).
  static_assert(format_e::FormatEMemberCount > 0,
                "structural inverse requires generated Format E members");
  static_assert(FormatEMemberOpcodeCount == format_e::FormatEMemberCount,
                "member opcode column must match independently generated "
                "FormatEInverse");
  static_assert(format_e::FormatESetDescLedgerCount > 0,
                "structural inverse requires setDesc ledger surface");
  (void)format_e::FormatEInverse[0];
  (void)format_e::FormatESetDescLedger[0];
  // Consume the separately generated inverse index (HaydnGenFormatEInverse.inc),
  // never FormatEMembers[MemberId] as an inverse row.
  (void)format_e::inverseRecordForMemberId(0);

  const uint8_t ExpectMode =
      Row == BundleFormatRowID::E96ThreeEntry ? 1 : 0;
  uint32_t SeenEntryBits = 0;
  uint32_t SeenUnits = 0;
  uint32_t ResidualCompletedBits = 0;

  for (unsigned E = 0, EE = MemberOpcodes.size(); E != EE; ++E) {
    const unsigned Opc = MemberOpcodes[E];
    if (isPadNopOpcode(Opc))
      continue;

    // Representation-expand pseudos (B / RET / BR_JT / PseudoCALLIndirect)
    // expand to a real Format E member at AsmPrinter emission. They are
    // legal committed SOLO cycles only: the expansion target occupies an
    // entry the committed members must not already hold. Co-issue with a
    // representation expand is a corruption — fail closed. Pad holes are
    // unused windows, not co-issue partners.
    if (isRepresentationExpandPseudo(Opc)) {
      if (Reals.size() != 1)
        return std::string(
            "structural inverse: representation-expand pseudo must be a "
            "solo committed cycle (printer expands one-to-one)");
      continue;
    }

    // Shared inverse: committed child order is the encode-dag entry
    // (leading order, including pad holes; suffix digits never pin
    // entries). Parse-time uses the same helper at the textual entry.
    // Residual/logical members complete an independently generated inverse
    // record here — never compact pads onto earlier entries.
    if (auto MemErr = verifyMemberAtStampedEntry(
            Opc, ExpectMode, static_cast<uint8_t>(E), RowEntries, SeenUnits,
            SeenEntryBits, ResidualCompletedBits, E))
      return MemErr;
  }

  if (auto ResidualErr = haydnRequireCompletedInverseOnResidualRoots(
          MemberOpcodes, ResidualCompletedBits))
    return ResidualErr;

  // Inverse-verified product plan: stamped row + inverse occupancy +
  // golden-row completion. Never makeProductPlan / PacketFormats planner /
  // selectCompletionForMembersAndPads.
  SlotBits Occupied = static_cast<SlotBits>(SeenEntryBits);
  BundlePlan Plan = makeInverseVerifiedPlan(Row, Occupied, Reals, HasPadNop);
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
  SmallVector<unsigned, 3> EntryOpcodes;
  SmallVector<const MCInst *, 3> RealInsts;
  EntryOpcodes.reserve(Entries.size());
  for (const MCInst *Inst : Entries) {
    const unsigned Opc = Inst ? Inst->getOpcode() : 0;
    EntryOpcodes.push_back(Opc);
    if (!Inst || isPadNopOpcode(Opc))
      continue;
    MemberOpcodes.push_back(Opc);
    RealInsts.push_back(Inst);
  }
  if (MemberOpcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");
  if (auto ResidualIdsErr =
          haydnRequireInverseIdsOnResidualRoots(EntryOpcodes))
    return ResidualIdsErr;
  if (!inverseOpcodesHaveUnitCoverForMode(MemberOpcodes, ExpectMode))
    return std::string(
        "structural inverse: chosen Format E members are not "
        "unit-injective (unit injectivity; execution units are not "
        "encoded entry identity)");

  uint32_t SeenUnits = 0;
  uint32_t SeenEntryBits = 0;
  uint32_t ResidualCompletedBits = 0;
  SmallVector<unsigned, 3> ResidualAtEntry(Entries.size(), 0);
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
    ResidualAtEntry[E] = Inst->getOpcode();
    if (auto MemErr = verifyMemberAtStampedEntry(
            Inst->getOpcode(), ExpectMode, static_cast<uint8_t>(E), RowEntries,
            SeenUnits, SeenEntryBits, ResidualCompletedBits, E))
      return MemErr;
  }
  if (auto ResidualErr = haydnRequireCompletedInverseOnResidualRoots(
          ResidualAtEntry, ResidualCompletedBits))
    return ResidualErr;

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

  // Encode-dag order includes pad holes so residual/logical inverse
  // completes at the stamped entry (never compact-replan). Census of
  // real members stays collectBundleMemberOpcodes.
  SmallVector<unsigned, 3> EntryOpcodes;
  if (const MachineBasicBlock *MBB = BundleRoot.getParent()) {
    for (MachineBasicBlock::const_instr_iterator I =
             std::next(BundleRoot.getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
        continue;
      EntryOpcodes.push_back(I->getOpcode());
    }
  }
  auto Err = verifyCommittedBundle(*Row, EntryOpcodes, Fmts, OutPlan);
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
  SmallVector<unsigned, 3> Members = collectBundleMemberOpcodes(BundleRoot);
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
  const unsigned RowEntries = [&] {
    const format::BundleFormatRowDesc *Desc = format::getBundleFormatRow(*Row);
    return Desc ? Desc->EntryCount : 0u;
  }();
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
