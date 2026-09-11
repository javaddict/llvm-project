//===- HaydnBundleVerify.cpp - Fail-closed committed-bundle check ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Out-of-line verifyCommittedBundle / private-member lookup /
// verifyFrozenLayout (GR1.8 EncodedBytes re-walk).
// Peer: AIEBaseInstrInfo.cpp:1595-1614 verifyInstruction fail-closed.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleVerify.h"
#include "Haydn.h"
#include "HaydnBundlePortBudget.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnIntraCycleRAW.h"
#include "HaydnIntraCycleWAW.h"
#include "HaydnLayoutSite.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPackLegality.h"
#include "HaydnPortModel.h"
// Opcode names come from the generated MC tables (HaydnMCTargetDesc.cpp
// GET_INSTRINFO_MC_DESC) — same backing store as haydnOpcodeName
// (HaydnBundleFormatSolver.h:109-111) without including that solver header
// or HaydnBundle.h. This TU never calls Bundle canAdd / hasValidFormat /
// exactTryAddProduct / PacketFormats planner / findFormatEMember /
// opcodesHaveFormatEUnitCover.
#include "HaydnFormatERecords.h"
#include "HaydnMspCloneFamily.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H)
#error "HaydnBundleVerify.cpp must not include HaydnBundle.h (no Bundle.canAdd)"
#endif
#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H)
#error \
    "HaydnBundleVerify.cpp must not include HaydnBundleFormatSolver.h (no forward solver)"
#endif
// PackLegality is included above for cycleHasMayAliasStoreLoad. It must
// not pull HaydnHazardRecognizer.h / HaydnBundle.h (walls below).
#if defined(LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H)
#error \
    "HaydnBundleVerify.cpp must not include HaydnBundleMaterialize.h (no Bundle.canAdd)"
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

/// Expand-owned / cycle-forming / leftover generic COPY-subreg / leftover
/// CFG representation shells that must not complete an inverse record.
/// There is no printer-expand accept path: B/RET/BR_JT/PseudoCALLIndirect
/// are residual until a typed pre-closure expansion. Peer:
/// AIEPseudoBranchExpansion.cpp:43-57; AIEBaseInstrInfo.cpp:1616-1635.
static bool isUnexpandedResidualPseudo(unsigned Opc) {
  return isRepresentationExpandPseudo(Opc) ||
         isLeftoverGenericResidualPseudo(Opc) ||
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

  if (auto It = Generated.find(Opc); It != Generated.end()) {
    Ids.append(It->second.begin(), It->second.end());
    return;
  }
  // `_MSP` encode clones share the catalog logical's inverse span through
  // the same opcode-keyed mapping the stamped-entry walk uses
  // (msp::logicalOpcodeForMspClone, HaydnMspCloneFamily.h — ONE table with
  // the serializer). This feeds haydnInverseRecordFromOpcode,
  // inverseUnitMaskForOpcode, and the unit-cover pre-check — without it a
  // legal solo clone dies as mask-0 "not unit-injective". Unmapped `_MSP`
  // opcodes fall through to logicalOpcodeOrSelf / exact-key lookup below
  // and end with empty Ids, so the walk fails closed on them.
  if (const unsigned CloneLog = msp::logicalOpcodeForMspClone(Opc)) {
    if (auto It = Generated.find(CloneLog); It != Generated.end())
      Ids.append(It->second.begin(), It->second.end());
    return;
  }
  // Compiler `_MSP` / `_W` clones share the catalog logical's inverse span
  // (peelLogicalOpcodeName already maps them; occupancy is not DFS).
  const unsigned Log = format_e::logicalOpcodeOrSelf(Opc);
  if (Log != Opc)
    if (auto It = Generated.find(Log); It != Generated.end())
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
    if (Opc == 0 || isPadNopOpcode(Opc))
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
/// Private members and pad NOP are not residual inverse roots.
/// Representation-expand shells are residual (no printer carve-out).
/// Peer: AIEBaseInstrInfo.cpp:1616-1635 verifyInstruction fail-closed.
static bool haydnIsMspEncodeClone(unsigned Opc) {
  return inverseOpcodeName(Opc).ends_with("_MSP");
}

static bool haydnResidualLogicalNeedsCompletedInverse(unsigned Opc) {
  if (Opc == 0 || isPadNopOpcode(Opc))
    return false;
  // CFG uncond shell through BranchRelaxation. Freeze still rejects via
  // haydnRejectRepresentationExpand.
  if (isRepresentationExpandPseudo(Opc))
    return false;
  // Encoder peels `_MSP`; occupancy uses the catalog logical.
  if (haydnIsMspEncodeClone(Opc))
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
    // `_MSP` encode clones complete an inverse record for the CATALOG
    // LOGICAL the serializer also selects (msp::logicalNameForMspClone in
    // HaydnMCInstLower, ONE table with this walk) at the stamped (mode,
    // membership entry) — MatchEntry stays true so a clone at an entry
    // with no complete inverse under the stamped mode fails closed.
    // Unmapped `_MSP` opcodes (ADD32_MSP) have no catalog logical and fail
    // as "no generated member"; materialize/setDesc baking is the only
    // legal commit path for them.
    unsigned LookupOpc = msp::logicalOpcodeForMspClone(Opc);
    if (!LookupOpc)
      LookupOpc = Opc;
    Inv = haydnInverseRecordFromOpcode(LookupOpc, ExpectMode, EntryIdx,
                                       SeenUnits, /*MatchEntry=*/true);
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

static std::optional<std::string>
haydnRejectRepresentationExpand(ArrayRef<unsigned> MemberOpcodes) {
  for (unsigned Opc : MemberOpcodes) {
    if (Opc == 0 || isPadNopOpcode(Opc))
      continue;
    if (!isRepresentationExpandPseudo(Opc))
      continue;
    return std::string(
               "structural inverse: residual representation-expand pseudo "
               "(printer expansion is not a verifier carve-out): ") +
           std::string(inverseOpcodeName(Opc));
  }
  return std::nullopt;
}

static std::optional<std::string>
haydnRejectMixedLogicalPrivate(ArrayRef<unsigned> MemberOpcodes) {
  bool AnyPriv = false;
  bool AnyLog = false;
  for (unsigned Opc : MemberOpcodes) {
    if (Opc == 0 || isPadNopOpcode(Opc) || isRepresentationExpandPseudo(Opc))
      continue;
    if (lookupPrivateFormatEMember(Opc))
      AnyPriv = true;
    else
      AnyLog = true;
  }
  if (AnyPriv && AnyLog)
    return std::string(
        "structural inverse: mixed logical and private inverse children");
  return std::nullopt;
}

static std::optional<std::string>
haydnRejectFreezeResidualLogical(ArrayRef<unsigned> MemberOpcodes) {
  for (unsigned Opc : MemberOpcodes) {
    if (Opc == 0 || isPadNopOpcode(Opc))
      continue;
    if (isRepresentationExpandPseudo(Opc))
      continue;
    if (lookupPrivateFormatEMember(Opc))
      continue;
    if (haydn::msp::logicalOpcodeForMspClone(Opc) != 0)
      continue;
    if (inverseOpcodeName(Opc).ends_with("_MSP"))
      continue;
    return std::string(
               "structural inverse: freeze residual logical child; concrete "
               "generated member required: ") +
           std::string(inverseOpcodeName(Opc));
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
///     residual root — never structural/forward acceptance. Compiler `_MSP`
///     encode clones verify through the catalog logical the serializer also
///     selects (msp::logicalOpcodeForMspClone, HaydnMspCloneFamily.h — ONE
///     table with MC-lower) at the stamped entry; a
///     clone at an entry with no complete inverse under the stamped mode, or
///     an `_MSP` opcode with no catalog mapping, fails closed — no member
///     class is structurally unverifiable and no clone-only bundle takes the
///     idle-plan early return.
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
                      const HaydnBaseMCFormats &Fmts, BundlePlan *OutPlan,
                      bool Freeze) {
  if (!isProductBundleRow(Row))
    return std::string("non-product BundleFormatRowID");

  // Pad NOP is CompletionState, not a membership inverse root. Opcode-only
  // callers may still pass architectural NOP as an unused encode-dag entry.
  // Do not compact pads: compacting would re-plan later residual/logicals
  // onto earlier entries (structural acceptance). Peer: AIE unused format
  // entry is idle, not an alternate opcode (AIEMCFormats.h:376-379).
  // `_MSP` encode clones are Reals: every real member — clone-only bundles
  // included — traverses the inverse matrix below, so the Reals.empty()
  // idle-plan early return is reachable only for genuine pad-only idle (or
  // the non-freeze representation-expand shells, which fail their own wall).
  SmallVector<unsigned, 3> Reals;
  bool HasPadNop = false;
  Reals.reserve(MemberOpcodes.size());
  for (unsigned Opc : MemberOpcodes) {
    if (isPadNopOpcode(Opc)) {
      HasPadNop = true;
      continue;
    }
    if (!Freeze && isRepresentationExpandPseudo(Opc))
      continue;
    Reals.push_back(Opc);
  }

  if (Reals.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  if (auto ExpandErr = haydnRejectRepresentationExpand(MemberOpcodes))
    return ExpandErr;

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
    if (!Freeze && isRepresentationExpandPseudo(Opc))
      continue;

    // Shared inverse: committed child order is the encode-dag entry
    // (leading order, including pad holes; suffix digits never pin
    // entries). Parse-time uses the same helper at the textual entry.
    // Residual/logical members — `_MSP` encode clones included, through
    // the catalog logical at the stamped entry — complete an independently
    // generated inverse record here; never compact pads onto earlier
    // entries. Unmapped `_MSP` opcodes fail closed in the walk.
    if (auto MemErr = verifyMemberAtStampedEntry(
            Opc, ExpectMode, static_cast<uint8_t>(E), RowEntries, SeenUnits,
            SeenEntryBits, ResidualCompletedBits, E))
      return MemErr;
  }

  if (auto ResidualErr = haydnRequireCompletedInverseOnResidualRoots(
          MemberOpcodes, ResidualCompletedBits))
    return ResidualErr;
  if (auto MixedErr = haydnRejectMixedLogicalPrivate(MemberOpcodes))
    return MixedErr;
  if (Freeze) {
    if (auto LogicalErr = haydnRejectFreezeResidualLogical(MemberOpcodes))
      return LogicalErr;
  }

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
  if (auto ExpandErr = haydnRejectRepresentationExpand(MemberOpcodes))
    return ExpandErr;
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
    unsigned ChildOpc = Inst->getOpcode();
    if (auto ExpandErr =
            haydnRejectRepresentationExpand(ArrayRef(&ChildOpc, 1)))
      return ExpandErr;
    ResidualAtEntry[E] = Inst->getOpcode();
    if (auto MemErr = verifyMemberAtStampedEntry(
            Inst->getOpcode(), ExpectMode, static_cast<uint8_t>(E), RowEntries,
            SeenUnits, SeenEntryBits, ResidualCompletedBits, E))
      return MemErr;
  }
  if (auto ResidualErr = haydnRequireCompletedInverseOnResidualRoots(
          ResidualAtEntry, ResidualCompletedBits))
    return ResidualErr;
  if (auto MixedErr = haydnRejectMixedLogicalPrivate(MemberOpcodes))
    return MixedErr;

  if (auto RegErr = haydnCheckParsedBundleRegs(RealInsts, MII, MRI))
    return std::string("structural inverse: ") + *RegErr;
  return std::nullopt;
}

/// Leftover catalog ALU/LS may carry implicit-def $sfr the descriptor does
/// not name (haydnDescNamesSfrPort; PackLegality rule 3). Named SFR writers
/// (CSRW/SET_HWLOOP/flag-setters) still collide. HR/SMS/commit keep the
/// strict HaydnIntraCycleWAW.h law.
static bool haydnIsLeftoverUnnamedSfrDef(const MachineInstr &MI,
                                         const MachineOperand &MO) {
  if (!MO.isReg() || !MO.isDef())
    return false;
  if (!isHaydnSFRPortReg(MO.getReg()))
    return false;
  return !haydnDescNamesSfrPort(MI);
}

/// haydnCycleMembersHaveWAW with leftover unnamed $sfr defs ignored.
static bool
haydnCycleMembersHaveWAWSkipLeftoverSfr(ArrayRef<MachineInstr *> Kids,
                                        const TargetRegisterInfo *TRI) {
  SmallSet<Register, 8> Defs;
  for (MachineInstr *MI : Kids) {
    if (!MI)
      continue;
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      if (haydnIsLeftoverUnnamedSfrDef(*MI, MO))
        continue;
      if (haydnRegOverlapsDefSet(MO.getReg(), Defs, TRI))
        return true;
    }
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      if (haydnIsLeftoverUnnamedSfrDef(*MI, MO))
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      if (!Reg.isPhysical() && !Reg.isVirtual())
        continue;
      Defs.insert(Reg);
    }
  }
  return false;
}

/// D1.54 — generated member descriptor/operand/tie/implicit/side-effect law.
///
/// One unconditional, descriptor-sourced validation of every real member of
/// a committed cycle, BEFORE the port-budget re-check and the shared commit
/// hazard predicates below. This is the freeze-contract member-shape wall:
/// none of it may depend on the optional MachineVerifier. Authority split
/// (no second table is invented):
///   * arity and per-operand kinds come from the COMPILED member
///     MCInstrDesc — the exact identity constraint #4 (direct alternate
///     compatibility) freezes at setDesc time. The golden ledger census
///     (FormatESetDescLedger.OperandCount / .OperandSignature) counts
///     GOLDEN field roles and deliberately does NOT include compiler-only
///     extras (tied acc, duplicated writeback fields — 1988 members carry
///     them), so the compiled desc is the only positional authority for
///     the MI's explicit operand block. The ledger row's presence is still
///     verified: every generated member opcode must resolve to one.
///   * ties come from the same compiled member MCInstrDesc: for a
///     committed member the descriptor IS the generated tie authority, so
///     a reg-use tie that disagrees with the desc's TIED_TO constraints
///     is a corrupt setDesc/late-mutation artifact (one generic
///     predicate — hasComplexRegisterTies — no second tie walk);
///   * implicit operand closure: every implicit reg on a committed member
///     must be named by the compiled member descriptor, be an implicit
///     operand of the member's authored LOGICAL descriptor (the
///     documented setDesc carry class, generate_format_e_records.py
///     EXPECTED_IMPLICIT_DIVERGENT: conditional branches / CSRW carry
///     Defs=[SFR], JAL/JALR carry the call-clobber list, MOVESFR2GPR /
///     MOVT64 / X2MOVT32-class readers carry Uses=[SFR];
///     rewriteFieldSlotToMember preserves that tail by design), or be the
///     documented anonymous leftover implicit-def $sfr (countSFRPorts
///     class (b) — charged to the SFR port ceiling, visible to the WAW
///     law). Any other implicit reg is unattributed traffic: fail closed;
///   * side-effect closure over the generated member defs: no member is
///     both mayLoad and mayStore (the generated tables admit none), a
///     store member must carry at least one MachineMemOperand (stores are
///     the aliasing authority for the same-cycle overlap law — a store
///     with no MMO makes every load pairing unverifiable, and the
///     pipeline always stamps store MMOs), and an MMO on a member whose
///     desc models no memory effect is stale unattributed aliasing
///     evidence. Pure loads keep the historical hand-MIR shape (MMO
///     optional on a lone load; the overlap law only consults a load when
///     a store joins the cycle, where the missing MMO refuses the pair).
///     The remaining side-effect surface (CSR/HWLoop/call/UA-CB classes'
///     hasSideEffects bit) is enforced through the implicit-closure arm
///     above plus the existing named same-cycle laws.
///
/// `_MSP` encode clones are NOT private members (no ledger row); they keep
/// their logical descriptors and are validated by the inverse walk only —
/// same class split as lookupPrivateFormatEMember. Pad NOP children are
/// completion fill, not members. Residual/logical children (pre-cutover
/// seats) return nullopt here: their descriptors are the logical schema's,
/// and freeze rejects them independently (haydnRejectFreezeResidualLogical).
///
/// \returns nullopt on success; human-readable reason on failure.
static std::optional<std::string>
haydnVerifyMemberDescriptorLaws(const MachineInstr &Kid) {
  const unsigned Opc = Kid.getOpcode();
  if (Opc == 0 || isPadNopOpcode(Opc))
    return std::nullopt;
  const unsigned PrivId = privateMemberIdForOpcode(Opc);
  if (PrivId == ~0u)
    // Residual/logical child or `_MSP` clone: pre-cutover representation;
    // the inverse walk and the freeze residual-logical wall own it.
    return std::nullopt;
  // Member opcode → MemberId → ledger row presence. The ledger is keyed by
  // MemberId (FormatESetDescLedgerRec.MemberId), NOT by the opcode-census
  // index: FormatEMemberOpcodes counts 4310 rows including NOP rows, the
  // ledger 4182 non-NOP rows, so a member whose census index exceeds the
  // ledger row count still HAS a row (S_LW_WITH_IMM_E3_E2_LOAD1_RI6,
  // census index 4263, ledger MemberId row present). The census is a
  // DenseMap<opcode, census-index>; PrivId from that map always satisfies
  // FormatEMemberOpcodes[PrivId] == Opc, so that half of the old test was
  // vacuous and the count half was wrong. Presence is decided by the
  // member's own MemberId being named by some ledger row; a miss here is
  // itself corruption of the member census.
  if (PrivId >= format_e::FormatEMemberCount)
    return std::string("member descriptor: generated member opcode has no "
                       "member record: ") +
           std::string(inverseOpcodeName(Opc));
  {
    static const DenseSet<uint16_t> LedgerMemberIds = [] {
      DenseSet<uint16_t> S;
      S.reserve(format_e::FormatESetDescLedgerCount);
      for (unsigned I = 0; I < format_e::FormatESetDescLedgerCount; ++I)
        S.insert(format_e::FormatESetDescLedger[I].MemberId);
      return S;
    }();
    if (!LedgerMemberIds.contains(
            format_e::FormatEMembers[PrivId].MemberId))
      return std::string("member descriptor: generated member opcode has no "
                         "setDesc ledger row: ") +
             std::string(inverseOpcodeName(Opc));
  }

  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const MCInstrDesc &Desc = MII.get(Opc);

  // Member's authored logical opcode — the implicit-carry class is keyed by
  // the LOGICAL's implicit lists (setDesc keeps the tail; see
  // rewriteFieldSlotToMember).
  const unsigned Logical = format_e::logicalOpcodeOrSelf(Opc);
  const MCInstrDesc &LogDesc = Logical == Opc ? Desc : MII.get(Logical);

  // ---- Arity + operand kind census (compiled member descriptor) ----
  // No generated member is variadic; the explicit block is exactly the
  // compiled desc census. Count the MI's OWN leading non-implicit
  // operands (MachineInstr::getNumExplicitOperands returns the DESC count
  // for non-variadic defs, so it cannot see truncation; and regmask
  // tails sit after the explicit block and are not reg operands).
  // MIParser never arity-checks a non-variadic def, so a truncated or
  // padded member reaches here intact.
  const unsigned Expected = Desc.getNumOperands();
  unsigned Explicit = 0;
  for (const MachineOperand &MO : Kid.operands()) {
    if (MO.isReg() && MO.isImplicit())
      break;
    if (MO.isRegMask())
      break;
    ++Explicit;
  }
  if (Explicit != Expected)
    return std::string("member descriptor: explicit operand count "
                       "disagrees with the generated member descriptor "
                       "(expected ") +
           std::to_string(Expected) + ", have " + std::to_string(Explicit) +
           "): " + std::string(inverseOpcodeName(Opc));
  // Operand-kind census, positional and fail-closed against the desc's own
  // operand info: a reg slot (REGISTER type or a reg class) takes a reg
  // operand; any other slot takes the imm/symbolic family the keep-map's
  // kindOk admits (branch targets, globals, symbols, CPI/JTI). The arity
  // arm above already failed any shape where the explicit block is not
  // exactly Expected long, so indexing is in range.
  for (unsigned I = 0; I != Expected; ++I) {
    const MCOperandInfo &Info = Desc.operands()[I];
    const MachineOperand &MO = Kid.getOperand(I);
    const bool WantReg =
        Info.OperandType == MCOI::OPERAND_REGISTER || Info.RegClass >= 0;
    const bool KindOk =
        WantReg ? MO.isReg()
                : (MO.isImm() || MO.isMBB() || MO.isGlobal() ||
                   MO.isSymbol() || MO.isCPI() || MO.isJTI() ||
                   MO.isBlockAddress() || MO.isMCSymbol() ||
                   MO.isTargetIndex());
    if (!KindOk)
      return std::string("member descriptor: operand kind disagrees with "
                         "the generated member descriptor at index ") +
             std::to_string(I) + ": " + std::string(inverseOpcodeName(Opc));
  }

  // ---- Tie closure (compiled member descriptor authority) ----
  // hasComplexRegisterTies() is true exactly when some reg-use tie on the
  // MI disagrees with the desc's TIED_TO constraint — the one generic
  // predicate for "this MI's ties are not the descriptor's ties".
  if (Kid.hasComplexRegisterTies())
    return std::string("member descriptor: register tie disagrees with the "
                       "generated member descriptor: ") +
           std::string(inverseOpcodeName(Opc));

  // ---- Implicit operand closure ----
  for (const MachineOperand &MO : Kid.implicit_operands()) {
    if (!MO.isReg())
      continue;
    const Register Reg = MO.getReg();
    if (!Reg)
      continue;
    const MCPhysReg Phys = Reg.asMCReg();
    if (Desc.hasImplicitDefOfPhysReg(Phys) ||
        Desc.hasImplicitUseOfPhysReg(Phys))
      continue;
    // setDesc carry class: an implicit operand the authored LOGICAL names.
    if (LogDesc.hasImplicitDefOfPhysReg(Phys) ||
        LogDesc.hasImplicitUseOfPhysReg(Phys))
      continue;
    // Documented leftover (countSFRPorts class (b),
    // haydnIsLeftoverUnnamedSfrDef): an anonymous implicit-def $sfr the
    // logical shell carried onto a geometry-only member desc. It stays
    // attributed traffic — charged to the SFR 1W port ceiling and visible
    // to the WAW law — so it is not unattributed corruption. An implicit
    // SFR USE is never in that class (readers name $sfr on the logical).
    if (MO.isDef() && isHaydnSFRPortReg(Phys))
      continue;
    // Circular-buffer selection/programming carry (selector):
    //   * addCircularBufferUse — CB members READ the cbr_sel-selected CBR
    //     bank as an implicit use. Golden models cbr_sel as an immediate
    //     FIELD (CBRI/CBRR member encodings), so neither the member nor
    //     the authored logical descriptor names the bank register in
    //     TableGen; the selector stamps it at ISel and
    //     rewriteFieldSlotToMember keeps the tail.
    //   * haydn_setcbr_begin/end — the CSRW_W setup WRITES the selected
    //     bank as an implicit def (ordering barrier so CB loads/stores
    //     cannot reorder past the boundary write; selector comment at
    //     HaydnInstructionSelector.cpp:5695).
    // Address-register stream carry (selector): PLDWWUA/PLQHWUA-class
    // unaligned/post-inc streams and FLAR WRITE AR[ar_sel] as an implicit
    // def (ar_sel is a golden immediate FIELD, AR WRITE_CONFLICT ordering
    // barrier; HaydnInstructionSelector.cpp:6927-6967).
    // CBR banks are reserved status/control registers
    // (haydnIsSimplifiableReservedReg class); AR banks are the 2R/2W
    // stream file — neither is named by golden member encodings, so the
    // selector-stamped implicit operand is the only representation.
    if (Phys == Haydn::CBR0 || Phys == Haydn::CBR1 || Phys == Haydn::AR0 ||
        Phys == Haydn::AR1)
      continue;
    // Tied-writeback liveness carry (VirtRegRewriter /
    // MachineInstr::addRegisterDefined class): a tied two-address def
    // whose use was rewritten onto the SAME physreg keeps an implicit def
    // of that register beside the explicit (dead) tied def — bundle
    // liveness stamps it so the packet's def is visible to the
    // bundle-level implicit list (D_LDW_CB_IMM `$d0, dead $r1 = ...
    // killed $r1, ..., implicit-def $r1`). The register is already an
    // EXPLICIT def of this member (or a super-register of one), so this
    // is not new traffic — it names the member's own def. Admitted for
    // defs only: an implicit USE this law cannot attribute stays
    // unattributed traffic.
    if (MO.isDef()) {
      const TargetRegisterInfo *KidTRI =
          Kid.getMF()->getSubtarget().getRegisterInfo();
      bool NamesOwnDef = false;
      for (const MachineOperand &D : Kid.all_defs()) {
        if (&D == &MO || D.isImplicit())
          continue;
        if (D.getReg() == Phys ||
            (D.getReg().isPhysical() && KidTRI &&
             KidTRI->regsOverlap(D.getReg().asMCReg(), Phys)))
          NamesOwnDef = true;
      }
      if (NamesOwnDef)
        continue;
    }
    return std::string("member descriptor: implicit operand not named by "
                       "the generated member or its authored logical "
                       "descriptor: ") +
           std::string(inverseOpcodeName(Opc));
  }

  // ---- Side-effect closure ----
  const bool Loads = Desc.mayLoad(), Stores = Desc.mayStore();
  const bool HasMMO = !Kid.memoperands().empty();
  if (Loads && Stores)
    return std::string("member descriptor: no generated member is both "
                       "mayLoad and mayStore: ") +
           std::string(inverseOpcodeName(Opc));
  // Store members are the aliasing authority for the same-cycle overlap law
  // (cycleHasMayAliasStoreLoad): a store with no MMO makes every pairing
  // with a load unverifiable, so its own descriptor law refuses it here.
  if (Stores && !HasMMO)
    return std::string("member descriptor: store member carries no machine "
                       "memory operand: ") +
           std::string(inverseOpcodeName(Opc));
  if (!Loads && !Stores && HasMMO)
    return std::string("member descriptor: memory operand on a member whose "
                       "generated descriptor models no memory effect: ") +
           std::string(inverseOpcodeName(Opc));

  return std::nullopt;
}

/// MIR entry: rebuild plan from BUNDLE root row + completion imms + children.
/// Fail-closed: missing/unknown row imm or missing completion is an error.
std::optional<std::string>
verifyCommittedBundle(const MachineInstr &BundleRoot, const HaydnBaseMCFormats &Fmts,
                      BundlePlan *OutPlan, bool Freeze, AAResults *AA) {
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
  auto Err = verifyCommittedBundle(*Row, EntryOpcodes, Fmts, OutPlan, Freeze);
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
    // D1.54 member-shape wall first: unconditional generated
    // descriptor/operand/tie/implicit/side-effect validation of every real
    // member BEFORE any resource/inverse-hazard predicate can accept the
    // cycle (never the optional MachineVerifier's job).
    for (MachineBasicBlock::const_instr_iterator I =
             std::next(BundleRoot.getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
        continue;
      if (auto DescErr = haydnVerifyMemberDescriptorLaws(*I))
        return DescErr;
    }
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
    // Shared commit hazard predicates (one MI-overload hook covers all
    // four Verify seats). Legal WAR/use-before-def and dead-def read-old
    // pass; live RAW, dual WAW, named same-cycle, SET trip/Off vs a
    // same-cycle producer, and unproven store/load overlap fail closed.
    // No vreg/FI or range walls here.
    if (Kids.size() >= 2) {
      const MachineFunction *MF = BundleRoot.getMF();
      const TargetRegisterInfo *TRI =
          MF ? MF->getSubtarget().getRegisterInfo() : nullptr;
      SmallVector<const MachineInstr *, 3> ConstKids(Kids.begin(), Kids.end());
      if (haydnCycleMembersHaveTrueRAW(
              ArrayRef<const MachineInstr *>(ConstKids), TRI))
        return std::string(
            "structural inverse: intra-cycle RAW (no-forwarding live "
            "def-then-use)");
      if (haydnCycleMembersHaveWAWSkipLeftoverSfr(Kids, TRI))
        return std::string(
            "structural inverse: intra-cycle WAW (no dual write)");
      if (haydnCycleViolatesNamedSameCycleLaws(Kids))
        return std::string(
            "structural inverse: named same-cycle law (CSRW-HWLR, "
            "LUI/ADDI32_W e0-alone, or SIN_COS/ARCTAN alone)");
      if (haydnCycleMembersHaveHwloopTripConflict(Kids, TRI))
        return std::string(
            "structural inverse: hwloop trip/Off vs same-cycle producer");
      // Hexagon DFAPacketizer.cpp:252-283 alias uses AA; empty MMO=alias.
      // ConstKids: cycleHasMayAliasStoreLoad takes ArrayRef<const MI *>.
      if (pack::cycleHasMayAliasStoreLoad(ConstKids, AA))
        return std::string(
            "structural inverse: store/load pair is not proven disjoint");
    }
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

static bool isJalrLogicalOpcode(unsigned Log) {
  return Log == Haydn::JALR || Log == Haydn::JALR_W ||
         Log == Haydn::JALR_CALL || Log == Haydn::JALR_TCO;
}

static bool isFreezeControlChild(const MachineInstr &MI,
                                 const HaydnInstrInfo &TII) {
  if (MI.isBundle() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isPosition() || MI.isKill() || MI.isImplicitDef() ||
      MI.isCFIInstruction() || MI.isInlineAsm())
    return false;
  if (isPadNopOpcode(MI.getOpcode()))
    return false;
  if (TII.isHardwareLoopSetupInstr(MI))
    return true;
  return MI.isBranch(MachineInstr::IgnoreBundle) ||
         MI.isCall(MachineInstr::IgnoreBundle);
}

std::optional<std::string> verifyFrozenLayout(MachineFunction &MF) {
  // Independent terminal layout wall. Do not call isBranchOffsetInRange
  // (safety-buffer false-fatals legal near-limit sites) and do not reuse
  // computeLayoutBlockStarts (namedLateLayoutGrowthBytes overcounts SET).
  const HaydnInstrInfo &TII = *static_cast<const HaydnInstrInfo *>(
      MF.getSubtarget().getInstrInfo());
  const HaydnMachineFunctionInfo *MFI =
      MF.getInfo<HaydnMachineFunctionInfo>();
  const unsigned Parcel = productParcelBytes().Value;

  if (MFI) {
    DenseMap<int, const MachineBasicBlock *> CounterHome;
    for (const MachineBasicBlock &MBB : MF) {
      const int FI = MFI->getHwLoopStackCounterFIForLatch(&MBB);
      if (FI < 0)
        continue;
      auto [It, Inserted] = CounterHome.try_emplace(FI, &MBB);
      if (!Inserted && It->second != &MBB)
        return std::string("freeze layout: overlapping hwloop counter homes");
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      if (MI.isDebugInstr() || MI.isMetaInstruction() || MI.isPosition() ||
          MI.isCFIInstruction() || MI.isKill() || MI.isImplicitDef())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg().isVirtual())
          return std::string("freeze layout: virtual register operand");
        if (MO.isFI())
          return std::string(
              "freeze layout: unresolved frame-index operand");
      }
    }
  }

  DenseMap<const MachineInstr *, uint64_t> PacketPC;
  DenseMap<const MachineBasicBlock *, uint64_t> BlockStart;
  uint64_t Offset = 0;
  for (const MachineBasicBlock &MBB : MF) {
    BlockStart[&MBB] = Offset;
    if (MBB.getAlignment() != Align(1) || MBB.getMaxBytesForAlignment() != 0)
      return std::string("freeze layout: residual MBB alignment metadata");
    if (Parcel != 0 && (Offset % Parcel) != 0)
      return std::string("freeze layout: MBB offset not EncodedBytes-aligned");
    for (const MachineInstr &MI : MBB.instrs()) {
      if (MI.isInsideBundle() || !MI.isBundle())
        continue;
      PacketPC[&MI] = Offset;
      Offset += committedEncodedBytes(MI).Value;
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      if (MI.isInsideBundle() || !MI.isBundle())
        continue;
      unsigned Controls = 0;
      for (const MachineInstr *Kid : members(MI)) {
        if (isFreezeControlChild(*Kid, TII))
          ++Controls;
      }
      if (Controls > 1)
        return std::string(
            "freeze layout: duplicate-control in packet "
            "(neutralized sibling must be a logical NOP)");
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      if (MI.isBundledWithPred())
        continue;
      HaydnJalrAddrMaterializeChain Chain =
          TII.getJalrAddrMaterializeChain(MI);
      if (!Chain.Control)
        continue;
      if (Chain.Incoherent || (Chain.Lui && Chain.Addi && !Chain.Dest))
        return std::string("freeze layout: complete-tail/template identity");
    }
  }

  LayoutSiteTable Table;
  std::string CollectErr;
  if (!Table.collect(MF, TII, CollectErr)) {
    // Symbolic JALR is this wall (ISA-69). Unknown control / malformed
    // SET stay on later freeze representation-escape and inverse seats
    // so D1.55 FileCheck strings are not stolen.
    if (StringRef(CollectErr).contains("symbolic JALR") ||
        CollectErr == HaydnReloc::kUnsupportedSymbolicJalrDiag)
      return std::string("freeze layout: ") + CollectErr;
    return std::nullopt;
  }

  auto packetPCFor = [&](const LayoutSite &S) -> uint64_t {
    if (S.Root && S.Root->isBundle()) {
      auto It = PacketPC.find(S.Root);
      if (It != PacketPC.end())
        return It->second;
    }
    const MachineBasicBlock *Parent =
        S.Root ? S.Root->getParent()
               : (S.Member ? S.Member->getParent() : nullptr);
    if (Parent) {
      auto It = BlockStart.find(Parent);
      if (It != BlockStart.end())
        return It->second;
    }
    return 0;
  };

  auto destStart = [&](const MachineBasicBlock *Dest) -> std::optional<uint64_t> {
    if (!Dest)
      return std::nullopt;
    auto It = BlockStart.find(Dest);
    if (It == BlockStart.end())
      return std::nullopt;
    return It->second;
  };

  auto checkDisp = [&](HaydnReloc::RelocKind K,
                       int64_t Disp) -> std::optional<std::string> {
    if (K == HaydnReloc::RelocKind::None || K == HaydnReloc::RelocKind::Invalid)
      return std::nullopt;
    if (HaydnReloc::isSymbolicJalrReloc(K))
      return std::string("freeze layout: ") +
             HaydnReloc::kUnsupportedSymbolicJalrDiag;
    HaydnReloc::RelocCompute C =
        HaydnReloc::computeRelocValue(K, static_cast<uint64_t>(Disp));
    if (C.OK)
      return std::nullopt;
    return std::string("freeze layout: ") +
           (C.Err ? C.Err : "relocation compute failed");
  };

  for (const LayoutSite &S : Table.sites()) {
    if (!S.Member)
      continue;
    if (isJalrLogicalOpcode(haydnLogicalOpcode(S.Member->getOpcode())))
      continue;

    const uint64_t PC = packetPCFor(S);
    if (S.Kind == LayoutSiteKind::HWLoop) {
      const unsigned SetBytes = S.Root ? committedEncodedBytes(*S.Root).Value
                                       : productParcelBytes().Value;
      if (S.Dest) {
        auto DS = destStart(S.Dest);
        if (!DS)
          return std::string("freeze layout: missing HWLoop dest layout");
        const int64_t AfterSet =
            static_cast<int64_t>(*DS) - static_cast<int64_t>(PC + SetBytes);
        const int64_t Anchored = ::llvm::haydn::hwloop::anchoredFromAfterSet(
            AfterSet, static_cast<int64_t>(SetBytes));
        if (auto Err = checkDisp(S.FieldKind, Anchored))
          return Err;
      }
      if (S.Dest2) {
        auto DS2 = destStart(S.Dest2);
        if (!DS2)
          return std::string("freeze layout: missing HWLoop dest2 layout");
        const int64_t AfterSet2 =
            static_cast<int64_t>(*DS2) - static_cast<int64_t>(PC + SetBytes);
        const int64_t Anchored2 = ::llvm::haydn::hwloop::anchoredFromAfterSet(
            AfterSet2, static_cast<int64_t>(SetBytes));
        if (auto Err = checkDisp(S.FieldKind2, Anchored2))
          return Err;
      }
      continue;
    }

    if (!S.Dest)
      continue;
    auto DS = destStart(S.Dest);
    if (!DS)
      return std::string("freeze layout: missing branch/call dest layout");
    const int64_t Disp = static_cast<int64_t>(*DS) - static_cast<int64_t>(PC);
    if (auto Err = checkDisp(S.FieldKind, Disp))
      return Err;
  }

  return std::nullopt;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm
