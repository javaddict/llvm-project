//===- HaydnBundleMaterialize.h — exact commit -*- C++ -*-===
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pure helpers for post-RA / late cycle materialization (exact no-split commit):
//
//   * exactSolveProductOpcodes — production exact no-split multi-MI (or
//     singleton) product cycle solve: exactTryAddProduct on the full
//     same-cycle opcode list; returns preferred members + BundlePlan or
//     nullopt when the list is not one legal product cycle. Never splits.
//   * exactPackOneOpcodeCycle — encode-oracle one-cycle check via Bundle
//     canAdd/add (post-setDesc members and logicals). nullopt ⇒ not one cycle.
//   * opcodesFormOneLegalCycle / instrsFormOneLegalCycle — shared one-cycle
//     legality (opcode-list and MI-list views; no PostRA dual walk).
//     MI-list view also enforces Haydn's no-forwarding law: a live def of R
//     and a use of R by different members of one product cycle is never legal
//     (any emission order; snapshot reads). Dead-def cohabitation stays
//     RAW-legal; the WAW law (cycleMembersHaveWAW) is separate and counts
//     dead defs (no dual write).
//   * auctionReadySubsetCycle — bounded ready-subset cycle auction.
//   * commitOneProductCycle — one production multi-MI commit site (AIE
//     applyBundles size()>1 peer at AIEHazardRecognizer.cpp:326-352):
//     canCoissueProductCycle probe, then commitExactMultiMIProductCycle bake.
//     Scheduled leaveMBB / residual unstamped shells / hard-root dissolve
//     all funnel here. Sequentialize after a probe reject is recovery, not
//     a second pack authority. Never a pre-RA freeze path.
//   * commitExactMultiMIProductCycle — bake half of that site (setDesc →
//     SlotMap → applyFormatOrdering → stamp). Callers must not open a
//     second bake path beside commitOneProductCycle.
//   * commitExactHardRootProductCycle — residual unit-test helper only:
//     dissolve then commitOneProductCycle. Product leaveMBB does not keep
//     hard-root freeze identity.
//   * commitLateProductCycle / finalizeExactLateSingleton — late layout
//     firewall: empty-cycle tryAdd → setDesc + stamp Format E commit.
//   * greedySplitLegalOpcodeCycles — DIAGNOSTIC ONLY (ResMII / unit tests).
//
// Each committed cycle stamps Format E BundleFormatRowID + CompletionStateID
// (HaydnBundlePlan). EncodedBytes come from the registry product rows.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "HaydnFormatERecords.h"
#include "HaydnIntraCycleWAW.h" // shared no-dual-write WAW law (hard #7)
#include "HaydnMemberSetDesc.h"
#include "HaydnPortModel.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace llvm {

// Defined in HaydnHazardRecognizer.cpp (AIE applyFormatOrdering peer).
// Forward-declared so the shared MIR commit surface does not pull the HR.
void applyFormatOrdering(Haydn::MachineBundle &Bundle, const VLIWFormat &Format,
                         MachineBasicBlock::iterator InsertPoint);

/// Durable setDesc onto a generated Format E member. Keep-map rewrite drops
/// extra ties / vestigial uses. Never raw-setDesc a Format E member.
/// Residual FieldSlot names still use plain setDesc.
inline void bakeFormatEMemberDesc(MachineInstr &MI, unsigned Member,
                                  const TargetInstrInfo &TII) {
  if (Member == MI.getOpcode())
    return;
  if (isGeneratedFormatEMemberName(TII.getName(Member))) {
    if (memberDescCompatible(MI, Member, TII))
      rewriteFieldSlotToMember(MI, Member, TII);
    return;
  }
  MI.setDesc(TII.get(Member));
}

/// Transactional snapshot of one MachineInstr's commit-mutable identity:
/// opcode descriptor, non-bundle MI flags, and the full operand array
/// (values, per-operand flags, and tie links).
///
/// W23 / CR-B1 (scheduling F2 ≡ encoding F9): the exact multi-MI commit bakes
/// Format E member descriptors (setDesc + keep-map operand rewrite) and clears
/// stale InternalRead markers BEFORE the final coissue checks. A late
/// `return false` used to leave member opcodes baked into MIR with no BUNDLE
/// root — a hard-constraint #8 violation surface (private member identity
/// outside a committed bundle). Capturing this snapshot before the bake and
/// restoring it on every post-bake failure makes a failed commit leave the
/// MIR identical to its pre-attempt state.
///
/// Deliberately NOT captured: MemRefs, DebugLoc, AsmPrinterFlags (no
/// post-snapshot failure path mutates them) and bundle-adjacency flags
/// (BundledPred/BundledSucc encode current MBB topology, which the caller
/// restores separately — dissolve / hard-root shell rebuild).
struct HaydnCommitTxn {
  const MCInstrDesc *Desc = nullptr;
  unsigned Flags = 0;
  SmallVector<MachineOperand, 8> Operands;
};

/// Capture the commit-mutable identity of \p MI (W23 transactional bake).
HaydnCommitTxn snapshotForCommitTxn(const MachineInstr &MI);

/// Restore \p MI to snapshot \p T. The operand array is truncated to zero and
/// re-added in saved order (explicit → regmask → implicit, the well formed MI
/// layout), which keeps tie links intact because ties live inside the copied
/// MachineOperands. MI flags are restored through setFlags, which preserves
/// the CURRENT bundle-adjacency bits: adjacency is topology, not identity,
/// and the caller re-establishes it.
void restoreFromCommitTxn(MachineInstr &MI, const HaydnCommitTxn &T,
                          MachineFunction &MF);

/// Field-ordered (emit-order) member list that applyFormatOrdering produces
/// for \p Bundle under \p Format. Single source of truth for the order the
/// encoder / AsmPrinter / hardware observe, so the no-forwarding intra-bundle
/// RAW law can be validated on the SAME order the bundle is emitted in.
SmallVector<MachineInstr *, 3>
getFieldOrderedMembers(const Haydn::MachineBundle &Bundle,
                       const VLIWFormat &Format);

namespace haydn {
namespace bundle {

/// One explicit architectural sub-cycle after a diagnostic greedy split
/// (logical opcodes). Production uses ExactProductCycle instead.
struct OpcodeCycle {
  SmallVector<unsigned, 3> Opcodes;
  BundlePlan Plan;
};

//===----------------------------------------------------------------------===//
// exact no-split product cycle (production commit surface)
//===----------------------------------------------------------------------===//

/// Result of solving one same-cycle opcode list as a single product cycle.
/// MemberOpcodes are the setDesc targets (AIE materializeMultiOpcodeInstrs).
/// Plan carries Format E row + completion + registry EncodedBytes.
struct ExactProductCycle {
  /// Input logical (or already-member) opcode identity, in schedule order.
  SmallVector<unsigned, 3> LogicalOpcodes;
  /// Format-member opcodes for MI.setDesc (equal to logical when fixed-slot /
  /// already-member / no PlacementAlternatives).
  SmallVector<unsigned, 3> MemberOpcodes;
  /// Product cycle plan (E2/E3 row, completion, registry EncodedBytes).
  BundlePlan Plan;
  /// Preferred CycleState after exact expand (OccupiedSlots + members).
  CycleState State;
};

/// Peel residual / Format E member opcode names to the golden logical catalog
/// string used by Format E unit tables. One map shared with MC encode.
inline std::string peelFormatELogicalOpcodeName(StringRef Name) {
  return format_e::peelLogicalOpcodeName(Name);
}

/// Constraints §Special SIN_COS/ARCTAN window (uimm4+2 occupancy). Shared
/// by commit / verify; the HR scoreboard books the multi-cycle NOP-on-unit
/// and dest-writer lock. Logical, residual FieldSlot, and Format E member
/// names all peel to the same catalog identity.
inline bool isSinCosWindowLogicalName(StringRef Log) {
  return Log == "SIN_COS" || Log == "ARCTAN";
}

inline bool isSinCosWindowOpcode(unsigned Opcode, const MCInstrInfo &MII) {
  if (haydnOpcodeIssuesAloneInCycle(Opcode))
    return true;
  return isSinCosWindowLogicalName(format_e::peelLogicalOpcodeName(
      MII.getName(Opcode), /*StripWide=*/false));
}

/// Golden occupancy is uimm4+2 (2..17). Missing or out-of-range imm
/// fail-closes to uimm4_max+2 so an under-booked window cannot encode.
inline unsigned sinCosWindowOccupancy(const MachineInstr &MI,
                                      const MCInstrInfo &MII) {
  if (!isSinCosWindowOpcode(MI.getOpcode(), MII))
    return 0;
  int64_t Imm = -1;
  for (const MachineOperand &MO : reverse(MI.operands())) {
    if (!MO.isImm())
      continue;
    Imm = MO.getImm();
    break;
  }
  if (Imm < 0 || Imm > 15)
    return 17;
  return static_cast<unsigned>(Imm) + 2u;
}

/// True when the cycle breaks the SIN_COS/ARCTAN alone-in-cycle window
/// (companion members, two window ops, or a misclassified occupancy).
inline bool cycleMembersViolateSinCosWindow(ArrayRef<MachineInstr *> Instrs,
                                           const MCInstrInfo &MII) {
  unsigned WindowOps = 0;
  MachineInstr *WindowMI = nullptr;
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    if (isSinCosWindowOpcode(MI->getOpcode(), MII)) {
      ++WindowOps;
      WindowMI = MI;
    }
  }
  if (WindowOps == 0)
    return false;
  if (WindowOps > 1 || Instrs.size() > 1)
    return true;
  return !WindowMI || sinCosWindowOccupancy(*WindowMI, MII) < 2;
}

/// TII overload kept for existing commitExact call sites; unit cover is the
/// opcode-keyed solver helper (Format E units ≠ encoded entry identity).
inline bool opcodesHaveFormatEUnitCoverForMode(ArrayRef<unsigned> Opcodes,
                                               const TargetInstrInfo &TII,
                                               uint8_t Mode) {
  (void)TII;
  return opcodesHaveFormatEUnitCoverForMode(Opcodes, Mode);
}

/// True when \p Opcodes can be assigned injective Format E units under E2 or
/// E3. Residual FieldSlots alone over-count single-unit stores (dual
/// D_SW_L_WITH_IMM / ST8 both need LOADSTORE0) — refuse before BUNDLE freeze.
inline bool opcodesHaveFormatEUnitCover(ArrayRef<unsigned> Opcodes,
                                        const TargetInstrInfo &TII) {
  (void)TII;
  return opcodesHaveFormatEUnitCover(Opcodes);
}

/// True when \p Kind is an operand of BUNDLE_E96_THREE_ENTRY
/// (`e3_0_slot` / `e3_1_slot` / `e3_2_slot`). AIE getSlotKind is
/// format-desc only — no `_S*` name peel.
inline bool formatECompositeSlotIsE3(MCSlotKind Kind) {
  return Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0) ||
         Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1) ||
         Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2);
}

/// True when \p Kind is an operand of BUNDLE_E96_TWO_ENTRY
/// (`e2_0_slot` / `e2_1_slot`).
inline bool formatECompositeSlotIsE2(MCSlotKind Kind) {
  return Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0) ||
         Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_E2_1);
}

/// Product row from committed member InstSlots (bundle format TD), then
/// generated Mode-only membership, then Format E unit cover. An `e3_*`
/// member cannot be an operand of BUNDLE_E96_TWO_ENTRY; an `e2_*` member
/// cannot be an operand of BUNDLE_E96_THREE_ENTRY. AIE: PacketFormats +
/// getSlotKind, no suffix (AIEFormat.cpp:20-26 first-covering;
/// AIEBundle.h:150-156). Dual single-unit logicals must not freeze E2
/// and rely on MC DFS. Child/member cardinality is never row identity:
/// extra NOP pads are idle fill, E3-only logicals stay E3 at size 1,
/// and a unit-cover miss does not invent the other Format E row.
inline BundleFormatRowID
selectProductRowForOpcodes(ArrayRef<unsigned> Opcodes) {
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  bool AnyE3 = false;
  bool AnyE2 = false;
  bool AnyE3Only = false;
  bool AnyE2Only = false;
  SmallVector<unsigned, 3> Real;
  Real.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes) {
    if (Opc == 0 || format_e::logicalOpcodeOrSelf(Opc) == Haydn::NOP)
      continue;
    Real.push_back(Opc);
    const MCSlotKind Kind = Fmts.getSlotKind(Opc);
    if (formatECompositeSlotIsE3(Kind))
      AnyE3 = true;
    else if (formatECompositeSlotIsE2(Kind))
      AnyE2 = true;
    if (haydnFormatELogicalIsE3Only(Opc))
      AnyE3Only = true;
    if (haydnFormatELogicalIsE2Only(Opc))
      AnyE2Only = true;
  }
  // Mixed E2+E3 members are not one parcel. Prefer E2 so a stray E3
  // slot-kind on a logical cannot stamp E96ThreeEntry over an E2
  // private member (sfr_cmp verifier). Callers must also refuse the pack.
  if (AnyE2 && AnyE3)
    return BundleFormatRowID::E96TwoEntry;
  if (AnyE3)
    return BundleFormatRowID::E96ThreeEntry;
  if (AnyE2)
    return BundleFormatRowID::E96TwoEntry;
  // Generated Mode-only membership, never child count. Mixed Mode-only
  // logicals are not one parcel — keep E2 so callers refuse rather than
  // invent E3 from occupancy of the other Mode.
  if (AnyE2Only && AnyE3Only)
    return BundleFormatRowID::E96TwoEntry;
  if (AnyE3Only)
    return BundleFormatRowID::E96ThreeEntry;
  if (AnyE2Only)
    return BundleFormatRowID::E96TwoEntry;
  // PacketFormats first-covering (AIEFormat.cpp:20-26): smaller product
  // row when both Modes place. opcodesHaveFormatEUnitCoverForMode is
  // vacuously true for |N|<2 — that is not occupancy. Dual-mode idle
  // and singleton take the first-covering E2 row.
  if (Real.size() >= 2) {
    if (opcodesHaveFormatEUnitCoverForMode(Real, /*Mode=*/0))
      return BundleFormatRowID::E96TwoEntry;
    if (opcodesHaveFormatEUnitCoverForMode(Real, /*Mode=*/1))
      return BundleFormatRowID::E96ThreeEntry;
    // No Mode covers: fail closed to ProductDefaultRowID. Do not invent
    // E96ThreeEntry from member count.
    return ProductDefaultRowID;
  }
  return ProductDefaultRowID;
}

inline BundleFormatRowID
selectProductRowForOpcodes(ArrayRef<unsigned> Opcodes,
                           const TargetInstrInfo &TII) {
  (void)TII;
  return selectProductRowForOpcodes(Opcodes);
}

/// Product plan with row chosen from Format E unit cover when possible.
inline BundlePlan makeProductPlanForOpcodes(SlotBits Occupied,
                                            ArrayRef<unsigned> Members) {
  BundlePlan P;
  P.Row = selectProductRowForOpcodes(Members);
  P.Completion = selectCompletionFor(P.Row, Members.size());
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = productParcelBytes();
  P.Cycles = OneCycle;
  return P;
}

inline BundlePlan makeProductPlanForOpcodes(SlotBits Occupied,
                                            ArrayRef<unsigned> Members,
                                            const TargetInstrInfo &TII) {
  (void)TII;
  return makeProductPlanForOpcodes(Occupied, Members);
}

/// Exact no-split product solve for a same-cycle opcode sequence.
///
/// Uses exactTryAddProduct on a private nondominated candidate set,
/// then selectPreferredCandidate + commitProduct. Order is preserved.
///
/// Contract:
///   * Empty input → nullopt.
///   * size > ISSUE_SLOT_COUNT → nullopt.
///   * Every input opcode is accepted into ONE cycle, or nullopt (no split).
///   * On success, MemberOpcodes.size() == LogicalOpcodes.size() and Plan is
///     product-legal.
///
/// Callers that already ran materializeMultiOpcodeInstrs may pass
/// post-setDesc member opcodes. Peel those (and residual aliases) to
/// alts-bearing logicals before exactTryAdd — AIE applyBundles
/// (AIEHazardRecognizer.cpp:325-352) packs after setDesc via getSlotKind;
/// Haydn members are row-specific so the solve must rematch from logicals.
inline std::optional<ExactProductCycle>
exactSolveProductOpcodes(ArrayRef<unsigned> Opcodes, const HaydnMCFormats &Fmts) {
  if (Opcodes.empty() || Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::nullopt;

  SmallVector<unsigned, 3> Logs;
  Logs.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes)
    Logs.push_back(productSolveLogicalOpcode(Opc, Fmts));

  CycleCandidateSet Cands =
      makeProductCandidateSet(Fmts.getPacketFormats());
  for (unsigned Log : Logs) {
    if (!exactTryAddProduct(Cands, Fmts, Log))
      return std::nullopt;
  }

  const CycleState &S = selectPreferredCandidate(Cands);
  if (S.Members.size() != Logs.size())
    return std::nullopt;

  auto Plan = commitProduct(S, Fmts.getPacketFormats());
  if (!Plan || !Plan->isProductLegal())
    return std::nullopt;

  ExactProductCycle Out;
  Out.LogicalOpcodes.assign(Logs.begin(), Logs.end());
  Out.MemberOpcodes.reserve(S.Members.size());
  for (const CycleMember &M : S.Members)
    Out.MemberOpcodes.push_back(M.MemberOpcode);
  Out.Plan = *Plan;
  Out.State = S;

  // Output coherence (CB-153b): CycleState records each member's alt AS
  // ACCEPTED, but later adds can narrow the row frontier — the settled
  // Plan.Row then contradicts earlier members' recorded identities (an
  // e3_* member inside an E2 plan), and every consumer that bakes these
  // opcodes (setDesc, Bundle canAdd, MC serialize) chokes on the mixed
  // set. The recorded Plan.Row can itself sit on the stale side (two
  // plain ALU ops record an E2 plan although the E2 e1 menu cannot host
  // a second general op — only E3 can). Re-bind the members onto a
  // single row via the golden-records entry assignment: the plan's row
  // first, then the other row, correcting the plan when the other row is
  // the one that binds. Refuse to return an incoherent solution.
  {
    const uint8_t Mode =
        Out.Plan.Row == BundleFormatRowID::E96ThreeEntry ? 1 : 0;
    bool Coherent = true;
    for (unsigned M : Out.MemberOpcodes) {
      const MCSlotKind Kind = Fmts.getSlotKind(M);
      const bool IsE2 = formatECompositeSlotIsE2(Kind);
      const bool IsE3 = formatECompositeSlotIsE3(Kind);
      if ((Mode == 0 && IsE3) || (Mode == 1 && IsE2)) {
        Coherent = false;
        break;
      }
    }
    if (!Coherent) {
      bool Fixed = false;
      for (uint8_t TryMode : {Mode, static_cast<uint8_t>(1 - Mode)}) {
        const unsigned TryCap = bundleRowEntryCount(
            TryMode ? BundleFormatRowID::E96ThreeEntry
                    : BundleFormatRowID::E96TwoEntry);
        if (Logs.size() > TryCap)
          continue;
        SmallVector<unsigned, 3> Rebound =
            assignMemberOpcodesForSettledRow(Logs, TryMode);
        if (Rebound.size() != Logs.size())
          continue;
        Out.MemberOpcodes.assign(Rebound.begin(), Rebound.end());
        if (TryMode != Mode) {
          Out.Plan.Row = TryMode ? BundleFormatRowID::E96ThreeEntry
                                 : BundleFormatRowID::E96TwoEntry;
          Out.Plan.Completion =
              selectCompletionFor(Out.Plan.Row, Opcodes.size());
        }
        Fixed = true;
        break;
      }
      if (!Fixed)
        return std::nullopt;
    }
  }
  return Out;
}

/// Exact solve for one CLOSED singleton with the documented row default.
///
/// ProductDefaultRowID is E96TwoEntry: "Default product row preference for
/// singletons / unknown fill (E2 geometry)". The solver's S2→S1→S0
/// materialize preference is an OPEN-cycle fill heuristic (leave low slots
/// free for later adds); reusing it for a cycle that is already closed made
/// every lone ADD32 commit as an e3_* member and stamp BUNDLE row 1,
/// contradicting the default the code documents (CB-152b,
/// singleton-bundle-formatid). Same candidate expansion as
/// exactSolveProductOpcodes; the selection just prefers a state that
/// commits as the E2 row, falling back to the unrestricted preferred state
/// for E3-only menus (ALU2-only opcodes and friends).
inline std::optional<ExactProductCycle>
exactSolveLateSingleton(unsigned LogicalOpc, const HaydnMCFormats &Fmts) {
  CycleCandidateSet Cands = makeProductCandidateSet(Fmts.getPacketFormats());
  if (!exactTryAddProduct(Cands, Fmts, LogicalOpc))
    return std::nullopt;

  CycleCandidateSet E2Cands;
  for (const CycleState &S : Cands)
    if (S.Members.size() == 1 &&
        formatECompositeSlotIsE2(
            Fmts.getSlotKind(S.Members[0].MemberOpcode)))
      E2Cands.push_back(S);

  auto tryCommit = [&](const CycleCandidateSet &Set)
      -> std::optional<ExactProductCycle> {
    if (Set.empty())
      return std::nullopt;
    const CycleState &S = selectPreferredCandidate(Set);
    if (S.Members.size() != 1)
      return std::nullopt;
    auto Plan = commitProduct(S, Fmts.getPacketFormats());
    if (!Plan || !Plan->isProductLegal())
      return std::nullopt;
    ExactProductCycle Out;
    Out.LogicalOpcodes.assign(1, LogicalOpc);
    Out.MemberOpcodes.push_back(S.Members[0].MemberOpcode);
    Out.Plan = *Plan;
    Out.State = S;
    return Out;
  };
  if (auto E2 = tryCommit(E2Cands))
    return E2;
  return tryCommit(Cands);
}

/// Encode-oracle: true one-cycle pack of \p Opcodes via Bundle canAdd/add.
/// Accepts post-setDesc fixed-slot members and alts-bearing logicals.
/// Never splits — full list packs or nullopt.
inline std::optional<OpcodeCycle>
exactPackOneOpcodeCycle(ArrayRef<unsigned> Opcodes,
                        const HaydnBaseMCFormats &Fmts) {
  if (Opcodes.empty() || Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::nullopt;

  SmallVector<MCInst, 3> Storage;
  Storage.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes) {
    Storage.emplace_back();
    Storage.back().setOpcode(Opc);
  }

  Haydn::Bundle<MCInst> B(&Fmts);
  for (unsigned I = 0, E = Opcodes.size(); I != E; ++I) {
    MCInst *MI = &Storage[I];
    if (!B.canAdd(MI->getOpcode()))
      return std::nullopt;
    B.add(MI);
  }

  // Multi-opcode: require a covering packet format (not a silent standalone).
  if (Opcodes.size() > 1 &&
      (B.isStandalone() || B.getFormatOrNull() == nullptr))
    return std::nullopt;

  OpcodeCycle C;
  C.Opcodes.assign(Opcodes.begin(), Opcodes.end());
  // BundlePlan EncodedBytes from generated VLIWFormat::Size only.
  auto Plan = planFromPacketFormats(Fmts.getPacketFormats(),
                                    B.getOccupiedSlots(), C.Opcodes);
  if (!Plan)
    return std::nullopt;
  C.Plan = *Plan;
  return C;
}

/// True iff the full opcode list packs into a single product Format E cycle.
/// Prefers exactTryAddProduct for logicals; falls back to Bundle encode oracle
/// for post-setDesc members (exactPackOneOpcodeCycle).
///
/// Opcode-only: cannot see register RAW. Prefer \p instrsFormOneLegalCycle
/// for production multi-MI commit (format + no-forwarding RAW).
inline bool opcodesFormOneLegalCycle(ArrayRef<unsigned> Opcodes,
                                     const HaydnBaseMCFormats &Fmts) {
  if (Opcodes.empty() || Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;

  // Product formats/alts are generated global — use exact solve first.
  const HaydnMCFormats &ProductFmts =
      static_cast<const HaydnMCFormats &>(Fmts);
  if (exactSolveProductOpcodes(Opcodes, ProductFmts).has_value())
    return true;
  // Already-member / fixed-slot path (post leaveRegion setDesc).
  return exactPackOneOpcodeCycle(Opcodes, Fmts).has_value();
}

/// **Available-cycle detect** (and no-forwarding RAW): true iff \p Instrs in
/// schedule/issue order have a **live** def of R by an EARLIER member and a
/// use of R by a LATER member. That shape must not share one ReadyCycle —
/// Data edges keep latency ≥1 so producer/consumer never share available
/// cycle; seeing this on a same-cycle list means the avail-cycle contract
/// is broken (or Anti use-before-redef was inverted after physreg paint).
///
/// Order-sensitive, matching HaydnHazardRecognizer::hasSameBundleRAW (which
/// tracks CurrentCycleLiveDefs incrementally as members append in issue
/// order).
///
/// A LATER member's live def read by an EARLIER member is WAR/snapshot: the
/// earlier reader correctly observes the pre-cycle (OLD) value — legal. The
/// prior order-independent form conflated WAR with true-RAW and rejected
/// legal snapshot packs (e.g. {add32 r1,r1,r2; add32 r2,r3,r4}). It was a
/// belt over a since-removed ALU→ALU latency-0 collapse that could mis-order
/// a true-RAW into WAR text order; with latency≥1 restored, schedule order is
/// authoritative and WAR is legal.
///
///   * **Read vs dead def** of the same physreg: legal — a dead write has no
///     consumer of the new value (dead defs are not live defs).
///   * Same-MI use+def (tied / normal overwrite): legal (not cross-member).
///
/// Uses are detected via isUse() / partial-def subreg reads, not only
/// MachineOperand::readsReg() (InternalRead would hide a true dep).
inline bool cycleMembersHaveTrueRAW(ArrayRef<MachineInstr *> Instrs,
                                    const TargetRegisterInfo *TRI) {
  if (Instrs.size() < 2)
    return false;

  auto overlaps = [&](Register A, Register B) -> bool {
    if (A == B)
      return true;
    if (!TRI || !A.isPhysical() || !B.isPhysical())
      return false;
    return TRI->regsOverlap(A, B);
  };

  // Walk members in schedule order, accumulating earlier members' live defs.
  // A later member's read overlapping any earlier live def is a true-RAW
  // hazard (no intra-bundle forwarding). Mirrors HR's CurrentCycleLiveDefs.
  SmallVector<Register, 4> PriorLiveDefs;
  for (unsigned J = 0, E = Instrs.size(); J != E; ++J) {
    MachineInstr *MI = Instrs[J];
    if (!MI)
      continue;
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || MO.isUndef() || !MO.getReg())
        continue;
      // Source use, or partial redef that reads the old full register.
      bool IsRead = MO.isUse() || (MO.isDef() && MO.getSubReg());
      if (!IsRead)
        continue;
      Register Reg = MO.getReg();
      if (!(Reg.isPhysical() || Reg.isVirtual()))
        continue;
      for (Register D : PriorLiveDefs)
        if (overlaps(Reg, D))
          return true;
    }
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.getReg() || !MO.isDef() || MO.isDead())
        continue;
      Register Reg = MO.getReg();
      if (Reg.isPhysical() || Reg.isVirtual())
        PriorLiveDefs.push_back(Reg);
    }
  }
  return false;
}

/// True when pooled RF demand of \p Instrs exceeds one issue cycle
/// (GPR 4R/2W, DR 8R/3W, AR 2R/2W, SFR 2R/1W). Defined out of line so
/// this header does not pull `HaydnPortModel.h` / instruction enums.
bool cycleMembersExceedPortBudget(ArrayRef<MachineInstr *> Instrs);

/// True when two different members of one cycle both def overlapping
/// registers (WAW) — golden no-dual-write law, DEAD defs included (the write
/// port / register file cannot serialize two writes regardless of liveness;
/// SFR one-writer and R0 soft-zero included). leaveMBB residual seam replay
/// treats same-cycle WAW as a permanent Req/Res conflict (stalls cannot clear
/// it). Multi-stage modulo packs without rename can place stage-N and
/// stage-N+1 redefs of one physreg on the same mod — refuse multi-MI and
/// leave sequential parcels.
///
/// W39: delegates to the ONE shared intra-cycle WAW mechanism
/// (HaydnIntraCycleWAW.h — the same law HR hasSameBundleWAW and SMS
/// HaydnResourceCycle enforce), so the commit path can no longer accept a
/// dead-def dual write the placement authorities reject. Never fork a second
/// WAW check beside that one.
inline bool cycleMembersHaveWAW(ArrayRef<MachineInstr *> Instrs,
                                const TargetRegisterInfo *TRI) {
  if (Instrs.size() < 2)
    return false;
  return haydnCycleMembersHaveWAW(Instrs, TRI);
}

/// Opcode check for product SET_HWLOOP / LoopStart forms.
/// True when MI is any product SET_HWLOOP / LoopStart form (logical, wide,
/// _S0, Format E member). Name peel so Format E private members match without
/// requiring every member enum in this TU's include set.
inline bool isProductHwloopSetupOpcodeName(StringRef Name) {
  std::string Logical = peelFormatELogicalOpcodeName(Name);
  return StringRef(Logical).starts_with_insensitive("SET_HWLOOP") ||
         StringRef(Logical).equals_insensitive("LoopStart");
}

/// SET_HWLOOP samples trip/Off GPRs under snapshot no-forwarding: refuse
/// coissue with any same-cycle producer of those regs (RAW needs forwarding;
/// WAR samples stale trip — post-pipeliner peel ADDI+SET).
inline bool cycleMembersHaveHwloopTripConflict(
    ArrayRef<MachineInstr *> Instrs, const TargetInstrInfo &TII,
    const TargetRegisterInfo *TRI) {
  if (Instrs.size() < 2)
    return false;
  auto overlaps = [&](Register A, Register B) -> bool {
    if (A == B)
      return true;
    if (!TRI || !A.isPhysical() || !B.isPhysical())
      return false;
    return TRI->regsOverlap(A, B);
  };
  for (MachineInstr *SetMI : Instrs) {
    if (!SetMI)
      continue;
    if (!isProductHwloopSetupOpcodeName(TII.getName(SetMI->getOpcode())))
      continue;
    SmallVector<Register, 4> SetUses;
    for (const MachineOperand &MO : SetMI->operands()) {
      if (!MO.isReg() || !MO.getReg() || MO.isDef() || MO.isUndef())
        continue;
      Register R = MO.getReg();
      if (R.isPhysical() || R.isVirtual())
        SetUses.push_back(R);
    }
    if (SetUses.empty())
      continue;
    for (MachineInstr *Other : Instrs) {
      if (!Other || Other == SetMI)
        continue;
      for (const MachineOperand &MO : Other->operands()) {
        if (!MO.isReg() || !MO.getReg() || !MO.isDef())
          continue;
        Register D = MO.getReg();
        if (!(D.isPhysical() || D.isVirtual()))
          continue;
        for (Register U : SetUses)
          if (overlaps(D, U))
            return true;
      }
    }
  }
  return false;
}


//===----------------------------------------------------------------------===//
// Product coissue law (one architectural issue cycle)
//===----------------------------------------------------------------------===//
//
// **Available cycle is the primary detector.** Ops may share a cycle only if
// the scheduler already gave them the same available/ready cycle
// (TopReadyCycle / SMS cycle). That is not optional bookkeeping:
//
//   * **Data** edges: adjustSchedDependency keeps latency ≥1 ⇒ producer and
//     consumer never share ReadyCycle. \p cycleMembersHaveTrueRAW on the
//     member list is the MI-level restate of that contract (def-before-use
//     of a live reg in one cycle = avail-cycle broken / no-forwarding RAW).
//
//   * **Anti** edges: latency 0 may share ReadyCycle only with **use-before-
//     redef** order. Schedule-order true RAW is exactly "Anti order inverted"
//     after RA paints one physreg onto a former vreg-independent pair
//     (e.g. SEQ(limit) + ADD that redefs the limit reg for an address).
//
//   * **WAW** (any def vs any def — dead defs included): \p
//     cycleMembersHaveWAW (shared HaydnIntraCycleWAW law). Same-cycle
//     multi-stage physreg redefs without rename must not multi-MI commit —
//     residual seam replay cannot stall-clear WAW. A dead def is still a
//     dual write (golden: no two writes to one register per cycle, SFR
//     one-writer included).
//
//   * **Field order** (layer 3): emission permute must still not invent true
//     RAW. \p canCoissueProductCycle checks preferred exactSolve placement.
//
// Free pack: refuse multi-MI if avail-cycle detect fails (true RAW / WAW or Data
// Lat≥1). Residual illegal multi-member shells sequentialize under Anti
// order via \p orderMembersUseBeforeDefForAnti. Not a separate WAR-rewrite
// product path.
//
//===----------------------------------------------------------------------===//

/// Reorder \p Members so that for each physreg that is both live-def'd and
/// read inside the list, every **read** appears before that reg's **def**
/// when a safe swap exists (no new RAW the other way).
///
/// This is the Anti **available-cycle order**: use-before-redef. Call when
/// \p cycleMembersHaveTrueRAW is true on the current order (avail-cycle
/// detect failed) and the list must be lowered sequential rather than packed.
/// Returns true if the order changed.
inline bool orderMembersUseBeforeDefForAnti(
    SmallVectorImpl<MachineInstr *> &Members, const TargetRegisterInfo *TRI) {
  if (Members.size() < 2)
    return false;
  bool Any = false;
  bool Changed = true;
  for (unsigned Guard = 0; Changed && Guard < 8; ++Guard) {
    Changed = false;
    for (unsigned I = 0; I + 1 < Members.size(); ++I) {
      MachineInstr *A = Members[I];
      MachineInstr *B = Members[I + 1];
      if (!A || !B)
        continue;
      auto reads = [&](const MachineInstr &MI, Register R) {
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg() || !MO.getReg().isPhysical())
            continue;
          if (!(MO.isUse() || (MO.isDef() && MO.getSubReg())))
            continue;
          Register Reg = MO.getReg();
          if (Reg == R || (TRI && TRI->regsOverlap(Reg, R)))
            return true;
        }
        return false;
      };
      bool NeedSwap = false;
      for (const MachineOperand &MO : A->all_defs()) {
        if (!MO.isReg() || MO.isDead() || !MO.getReg().isPhysical())
          continue;
        Register R = MO.getReg();
        if (!reads(*B, R))
          continue;
        bool CreatesRAW = false;
        for (const MachineOperand &BD : B->all_defs()) {
          if (!BD.isReg() || BD.isDead() || !BD.getReg().isPhysical())
            continue;
          if (reads(*A, BD.getReg())) {
            CreatesRAW = true;
            break;
          }
        }
        if (!CreatesRAW) {
          NeedSwap = true;
          break;
        }
      }
      if (NeedSwap) {
        std::swap(Members[I], Members[I + 1]);
        Changed = true;
        Any = true;
      }
    }
  }
  return Any;
}

/// MI-list view of product one-cycle legality at **schedule / available-cycle
/// order**. Production PostRA commit authority — strategy must not dual-walk
/// Bundle.
///
/// Rejects:
///   * INLINEASM (opaque layout boundary; multi-MI must not cross it)
///   * \p cycleMembersHaveTrueRAW — **available-cycle detect**: def-before-use
///     of a live reg cannot share one ReadyCycle under no-forwarding
///   * opcode lists that do not pack into one Format E product cycle
///
/// Does **not** validate field-order emission — use \p canCoissueProductCycle.
inline bool instrsFormOneLegalCycle(ArrayRef<MachineInstr *> Instrs,
                                    const HaydnBaseMCFormats &Fmts) {
  if (Instrs.empty() || Instrs.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;
  SmallVector<unsigned, 3> Opcodes;
  Opcodes.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs) {
    if (MI->isInlineAsm())
      return false;
    Opcodes.push_back(MI->getOpcode());
  }

  if (Instrs.size() >= 2) {
    const MachineFunction *MF = Instrs.front()->getMF();
    const TargetRegisterInfo *TRI =
        MF ? MF->getSubtarget().getRegisterInfo() : nullptr;
    const TargetInstrInfo *TII =
        MF ? MF->getSubtarget().getInstrInfo() : nullptr;
    // Available-cycle detect (same as ReadyCycle / Data Lat≥1 contract).
    if (cycleMembersHaveTrueRAW(Instrs, TRI))
      return false;
    if (cycleMembersExceedPortBudget(Instrs))
      return false;
    if (TII && cycleMembersViolateSinCosWindow(Instrs, *TII))
      return false;
    // SET_HWLOOP trip/Off sample cannot share a cycle with a producer of
    // those regs (snapshot no-forwarding — remat ADDI+SET peel).
    if (TII && cycleMembersHaveHwloopTripConflict(Instrs, *TII, TRI))
      return false;
  }

  return opcodesFormOneLegalCycle(Opcodes, Fmts);
}

/// Register-file port budgets for one candidate cycle (golden 4R2W GPR,
/// 7R3W DR, 2R2W AR, 2R1W SFR — the same counters the hazard recognizer
/// charges per issue cycle). The HR enforces these while SCHEDULING; the
/// hard-root recommit and leaveMBB commit surfaces bypass the HR, and the
/// gap was masked by the solver's formerly incoherent member output (mixed
/// rows made the pack fail before ports could matter). With coherent
/// members (CB-153b) the gate must be explicit or a three-GPR-write cycle
/// commits a hardware-illegal bundle. Out-of-line: the counters live in
/// HaydnPortModel.h, which must not be pulled into this header's includers.
bool cycleMembersRespectPortBudgets(ArrayRef<MachineInstr *> Instrs);

/// Single-shot re-solve when per-MI baking left row-mixed members.
/// Implementation lives in HaydnBundleMaterialize.cpp (header split).
/// Peel uses productSolveLogicalOpcode so residual aliases and generated
/// members share one logical map with exactTryAdd.
std::optional<SmallVector<unsigned, 3>>
resolveMixedMemberCycleOnce(ArrayRef<MachineInstr *> Instrs,
                            const TargetInstrInfo &TII,
                            const TargetRegisterInfo *TRI,
                            const HaydnMCFormats &Fmts);


/// Full **emission** coissue probe for one product cycle (layer 3 + schedule
/// pack). Caller must already have same available/ready cycle (layer 1) and
/// no blocking Data deps (layer 2).
///
/// Runs schedule-order legality, exactSolve/setDesc, MachineBundle encode,
/// and **field-order** no-forwarding RAW. Temporary setDesc is reverted so
/// pre-RA SMS handoff can probe without freezing illegal hard roots.
///
/// Alias kept for existing call sites: \p instrsCanExactCommitProductCycle.
bool canCoissueProductCycle(ArrayRef<MachineInstr *> Instrs);

/// Historical name — prefer \p canCoissueProductCycle.
inline bool instrsCanExactCommitProductCycle(ArrayRef<MachineInstr *> Instrs) {
  return canCoissueProductCycle(Instrs);
}

//===----------------------------------------------------------------------===//
// bounded ready-subset cycle auction (post-RA list ranking)
//===----------------------------------------------------------------------===//

/// Cap on ready ops examined per auction. With issue width 3, enumerating
/// subsets/perms of the first 8 ready entries is bounded (≤ 2^8 · 3!).
constexpr unsigned MaxReadySubsetAuctionReady = 8;

/// One auction result: densest legal fill of the open issue cycle.
struct ReadySubsetAuction {
  /// Indices into the caller's ReadyOpcodes (acceptance order).
  SmallVector<unsigned, 3> ReadyIndices;
  /// BaseOpcodes + selected ready opcodes in the packing order tried.
  SmallVector<unsigned, 3> CycleOpcodes;
  /// Preferred exact product solve when available (alts-bearing path).
  std::optional<ExactProductCycle> Exact;
  /// |CycleOpcodes| — primary ranking key (maximize issued ops / density).
  unsigned IssuedCount = 0;
};

/// Bounded ready-subset auction for one architectural issue cycle.
///
/// Given already-placed \p BaseOpcodes (current cycle members, schedule order)
/// and a list of ready opcodes, enumerate subsets of ready ops of size at most
/// `ISSUE_SLOT_COUNT - Base.size()`, examining only the first
/// MaxReadySubsetAuctionReady ready entries. Among subsets that form one legal
/// product cycle with Base (opcodesFormOneLegalCycle / exact rematch),
/// rank lexicographically:
///   1. maximize IssuedCount (Base + selected ready);
///   2. among ties, prefer a subset that exactSolveProductOpcodes accepts;
///   3. among ties, minimize sum of ready indices (deterministic);
///   4. among ties, lexicographically smaller ReadyIndices sequence.
///
/// When \p MustIncludeReadyIdx is set, only subsets containing that ready index
/// are considered (post-RA tryCandidate ranking for one focus SU).
///
/// Contract:
///   * Empty Base and empty Ready → nullopt (idle cycle is not an auction).
///   * Overwidth Base → nullopt.
///   * Never splits a multi-cycle repair; nullopt when nothing legal fits.
///   * Pure: no MI / AltDesc / FormatID mutation.
inline std::optional<ReadySubsetAuction>
auctionReadySubsetCycle(ArrayRef<unsigned> BaseOpcodes,
                        ArrayRef<unsigned> ReadyOpcodes, const HaydnMCFormats &Fmts,
                        std::optional<unsigned> MustIncludeReadyIdx =
                            std::nullopt) {
  const unsigned BaseN = BaseOpcodes.size();
  if (BaseN > Haydn::ISSUE_SLOT_COUNT)
    return std::nullopt;
  if (BaseN == 0 && ReadyOpcodes.empty())
    return std::nullopt;

  const unsigned Cap = Haydn::ISSUE_SLOT_COUNT - BaseN;
  const unsigned ReadyN = std::min<unsigned>(ReadyOpcodes.size(),
                                             MaxReadySubsetAuctionReady);
  if (MustIncludeReadyIdx && *MustIncludeReadyIdx >= ReadyN)
    return std::nullopt;

  std::optional<ReadySubsetAuction> Best;

  auto considerOrder = [&](ArrayRef<unsigned> Idxs) {
    if (MustIncludeReadyIdx) {
      bool Has = false;
      for (unsigned I : Idxs)
        if (I == *MustIncludeReadyIdx) {
          Has = true;
          break;
        }
      if (!Has)
        return;
    }

    SmallVector<unsigned, 3> CycleOps;
    CycleOps.reserve(BaseN + Idxs.size());
    CycleOps.append(BaseOpcodes.begin(), BaseOpcodes.end());
    for (unsigned I : Idxs)
      CycleOps.push_back(ReadyOpcodes[I]);

    if (CycleOps.empty() || CycleOps.size() > Haydn::ISSUE_SLOT_COUNT)
      return;
    if (!opcodesFormOneLegalCycle(CycleOps, Fmts))
      return;

    ReadySubsetAuction Cand;
    Cand.ReadyIndices.assign(Idxs.begin(), Idxs.end());
    Cand.CycleOpcodes = CycleOps;
    Cand.IssuedCount = CycleOps.size();
    Cand.Exact = exactSolveProductOpcodes(CycleOps, Fmts);

    auto better = [&](const ReadySubsetAuction &A,
                      const ReadySubsetAuction &B) {
      if (A.IssuedCount != B.IssuedCount)
        return A.IssuedCount > B.IssuedCount;
      if (A.Exact.has_value() != B.Exact.has_value())
        return A.Exact.has_value();
      unsigned SumA = 0, SumB = 0;
      for (unsigned I : A.ReadyIndices)
        SumA += I;
      for (unsigned I : B.ReadyIndices)
        SumB += I;
      if (SumA != SumB)
        return SumA < SumB;
      return std::lexicographical_compare(A.ReadyIndices.begin(),
                                          A.ReadyIndices.end(),
                                          B.ReadyIndices.begin(),
                                          B.ReadyIndices.end());
    };

    if (!Best || better(Cand, *Best))
      Best = std::move(Cand);
  };

  // Cap == 0: only the base cycle (must already be legal).
  if (Cap == 0) {
    considerOrder({});
    return Best;
  }

  // Enumerate subsets of Ready[0..ReadyN) with size ≤ Cap (bitmask).
  const unsigned Full = 1u << ReadyN;
  for (unsigned Mask = 0; Mask < Full; ++Mask) {
    const unsigned Bits = llvm::popcount(Mask);
    if (Bits > Cap)
      continue;
    if (MustIncludeReadyIdx && !(Mask & (1u << *MustIncludeReadyIdx)))
      continue;

    SmallVector<unsigned, 3> Idxs;
    Idxs.reserve(Bits);
    for (unsigned I = 0; I < ReadyN; ++I)
      if (Mask & (1u << I))
        Idxs.push_back(I);

    if (Idxs.empty()) {
      considerOrder(Idxs);
      continue;
    }

    // Try all acceptance orders (Bits ≤ 3). Exact rematch can depend on order
    // even though the set oracle is order-insensitive for legality.
    SmallVector<unsigned, 3> P = Idxs;
    considerOrder(P);
    SmallVector<unsigned, 3> C(Bits, 0);
    unsigned I = 0;
    while (I < Bits) {
      if (C[I] < I) {
        if ((I & 1) == 0)
          std::swap(P[0], P[I]);
        else
          std::swap(P[C[I]], P[I]);
        considerOrder(P);
        ++C[I];
        I = 0;
      } else {
        C[I] = 0;
        ++I;
      }
    }
  }

  return Best;
}

/// Auction score for ranking one focus ready opcode against a base cycle and
/// the remaining ready list. Higher is denser co-issue (IssuedCount of the
/// best subset that includes the focus at Ready index 0).
/// ReadyOpcodes must place the focus opcode at index 0; other ready ops follow.
inline unsigned auctionFocusFillScore(ArrayRef<unsigned> BaseOpcodes,
                                      ArrayRef<unsigned> ReadyOpcodes,
                                      const HaydnMCFormats &Fmts) {
  if (ReadyOpcodes.empty())
    return BaseOpcodes.size();
  auto A = auctionReadySubsetCycle(BaseOpcodes, ReadyOpcodes, Fmts,
                                   /*MustIncludeReadyIdx=*/0u);
  if (!A)
    return BaseOpcodes.size();
  return A->IssuedCount;
}

/// Score-only twin of auctionFocusFillScore, same value by construction
/// (CB-153a). The score consumes ONLY IssuedCount — the size of the densest
/// legal subset containing the focus. IssuedCount ranks first among the
/// auction tie-breaks, so the score equals BaseN + (largest focus-containing
/// subset size legal in ANY acceptance order), falling back to BaseN when
/// none is. This twin therefore walks sizes largest-first, tries the same
/// acceptance orders per subset as the auction (legality's exact set solve is
/// order-insensitive, but its exactPackOneOpcodeCycle FALLBACK is a
/// sequential Bundle add, so any-order acceptance must be preserved), and
/// returns at the first legal hit. What it never runs is the auction's
/// per-order Cand.Exact = exactSolveProductOpcodes rematch — that feeds
/// tie-breaks below IssuedCount, which the score cannot observe. The full
/// auction cost ~2.3 ms per score on wide ready lists (every order of every
/// subset, each solving exact legality once for the oracle and once for the
/// rematch); the twin is bounded by the same subset/order walk minus the
/// rematch, and the largest-first early exit usually ends it in a few tests.
/// Optional cross-call memo for auctionFocusFillScoreOnly: "is Base plus this
/// ready-opcode multiset legal in ANY acceptance order" keyed by the Base
/// sequence, a ~0u separator, and the SORTED chosen ready opcodes. Any-order
/// legality is order-invariant in the chosen ready ops by definition (every
/// order is tried before answering no), and Base stays in sequence order in
/// the key because the auction never permutes it.
struct AuctionAnyOrderLegalMemo {
  struct Hash {
    size_t operator()(const std::vector<unsigned> &V) const {
      return static_cast<size_t>(llvm::hash_combine_range(V.begin(), V.end()));
    }
  };
  std::unordered_map<std::vector<unsigned>, bool, Hash> Map;
};

inline unsigned
auctionFocusFillScoreOnly(ArrayRef<unsigned> BaseOpcodes,
                          ArrayRef<unsigned> ReadyOpcodes,
                          const HaydnMCFormats &Fmts,
                          AuctionAnyOrderLegalMemo *LegalMemo = nullptr) {
  const unsigned BaseN = BaseOpcodes.size();
  if (ReadyOpcodes.empty() || BaseN > Haydn::ISSUE_SLOT_COUNT)
    return BaseN;
  const unsigned Cap = Haydn::ISSUE_SLOT_COUNT - BaseN;
  const unsigned ReadyN = std::min<unsigned>(ReadyOpcodes.size(),
                                             MaxReadySubsetAuctionReady);
  if (Cap == 0 || ReadyN == 0)
    return BaseN; // Focus-containing subsets cannot fit: auction nullopt.

  SmallVector<unsigned, 3> CycleOps;
  auto orderIsLegal = [&](ArrayRef<unsigned> Idxs) {
    CycleOps.clear();
    CycleOps.append(BaseOpcodes.begin(), BaseOpcodes.end());
    for (unsigned I : Idxs)
      CycleOps.push_back(ReadyOpcodes[I]);
    return opcodesFormOneLegalCycle(CycleOps, Fmts);
  };
  auto anyOrderLegal = [&](SmallVectorImpl<unsigned> &P) {
    if (orderIsLegal(P))
      return true;
    const unsigned Bits = P.size();
    SmallVector<unsigned, 3> C(Bits, 0);
    unsigned I = 0;
    while (I < Bits) { // Heap's algorithm, as in auctionReadySubsetCycle.
      if (C[I] < I) {
        if ((I & 1) == 0)
          std::swap(P[0], P[I]);
        else
          std::swap(P[C[I]], P[I]);
        if (orderIsLegal(P))
          return true;
        ++C[I];
        I = 0;
      } else {
        C[I] = 0;
        ++I;
      }
    }
    return false;
  };

  for (unsigned Size = std::min(Cap, ReadyN); Size >= 1; --Size) {
    const unsigned Full = 1u << ReadyN;
    for (unsigned Mask = 1; Mask < Full; Mask += 2) { // bit 0 (focus) set
      if (static_cast<unsigned>(llvm::popcount(Mask)) != Size)
        continue;
      SmallVector<unsigned, 3> Idxs;
      for (unsigned I = 0; I < ReadyN; ++I)
        if (Mask & (1u << I))
          Idxs.push_back(I);

      if (LegalMemo) {
        std::vector<unsigned> Key;
        Key.reserve(BaseN + 1 + Idxs.size());
        Key.assign(BaseOpcodes.begin(), BaseOpcodes.end());
        Key.push_back(~0u);
        const size_t At = Key.size();
        for (unsigned I : Idxs)
          Key.push_back(ReadyOpcodes[I]);
        std::sort(Key.begin() + At, Key.end());
        auto It = LegalMemo->Map.find(Key);
        const bool Legal =
            It != LegalMemo->Map.end() ? It->second : anyOrderLegal(Idxs);
        if (It == LegalMemo->Map.end())
          LegalMemo->Map.emplace(std::move(Key), Legal);
        if (Legal)
          return BaseN + Size;
        continue;
      }

      if (anyOrderLegal(Idxs))
        return BaseN + Size;
    }
  }
  return BaseN;
}

//===----------------------------------------------------------------------===//
// exact multi-MI MIR commit (shared production surface)
//===----------------------------------------------------------------------===//

/// Exact no-split multi-MI MIR commit (AIE applyBundles size()>1 peer):
///   1. exactSolveProductOpcodes → MI.setDesc(member) for alts-bearing
///      logicals (sole surface that may still see residual logicals after
///      leaveRegion materializeMultiOpcodeInstrs; hard-root recommit also
///      lands here). Already-baked members that exactPackOneOpcodeCycle
///      accepts are kept as-is (AIE applyBundles getSlotKind after setDesc).
///      Residual alts-bearing logicals that cannot solve stay sequential.
///   2. Clear stale IsInternalRead on members (finalizeBundle only sets).
///   3. Build MachineBundle SlotMap from post-setDesc getSlotKind (fixed-slot
///      members; alts tryAdd only if a no-alt residual remains legal).
///   4. applyFormatOrdering → field order + finalizeBundle (rebuilds
///      consolidated BUNDLE-root operands, kill/dead flags, InternalRead).
///   5. stampBundleCommit(row, completion) on the BUNDLE root.
///
/// Precondition: \p Instrs are contiguous schedule-order members, size >= 2,
/// and form one legal product cycle (instrsFormOneLegalCycle). Never splits.
/// Children must live in a MachineFunction (setDesc needs TargetInstrInfo).
///
/// \returns true on successful BUNDLE + Format E commit with real member
/// descriptors; false if the live Bundle cannot pack, has no covering packet
/// format, or would leave residual logicals (caller fail-closed).
///
/// This is the bake half of the one production commit site. Ordinary
/// scheduled multi-MI callers must go through \p commitOneProductCycle so
/// emission legality (including the shared RF-port predicate) cannot drift
/// from the bake.
bool commitExactMultiMIProductCycle(ArrayRef<MachineInstr *> Instrs);

/// One production commit site for scheduled multi-MI cycles (AIE
/// applyBundles size()>1 peer). Emission probe (ports, WAW, RAW, format,
/// field order) then exact bake. Callers must not open a second bake
/// path beside this — residual hard-root and SMS/hwloop sites dissolve
/// into the same pair.
bool commitOneProductCycle(ArrayRef<MachineInstr *> Instrs);

/// Residual unit-test helper: dissolve a multi-member BUNDLE shell and
/// recommit via \p commitOneProductCycle when membership is one legal
/// product cycle. Product leaveMBB does not keep hard-root freeze
/// identity — free multi-MI and residual unstamped multi-member handling
/// use ordinary multi-MI commit / sequentialize.
///
/// \p BundleRoot must be TargetOpcode::BUNDLE with ≥2 real children.
/// \p MII provides MCInstrDesc for member setDesc (TargetInstrInfo ok).
///
/// \returns true on successful recommit; false if membership is not one
/// legal product cycle.
bool commitExactHardRootProductCycle(MachineInstr &BundleRoot,
                                     const MCInstrInfo &MII);

//===----------------------------------------------------------------------===//
// Diagnostic greedy split (NOT production post-RA commit)
//===----------------------------------------------------------------------===//

/// Greedy left-to-right split of a same-cycle opcode list into legal
/// product cycles. Order is preserved. Each output cycle has ≤3 members
/// and passes Haydn::Bundle canAdd/add (encode oracle).
///
/// **Diagnostic / ResMII / unit-test only.** Production post-RA must fail
/// closed when a scheduled multi-MI cycle is not one legal product cycle
/// (`NumScheduledCyclesSplit == 0` qualification). Do not call this to repair
/// a scheduled cycle in HaydnPostRASchedStrategy.
///
/// Contract:
///   * Empty input → empty output.
///   * Every input opcode appears in exactly one output cycle, in order.
///   * If opcode N cannot join the open cycle, the open cycle is closed and
///     a new cycle starts at N (explicit cycle boundary).
///   * A lone opcode that cannot reserve any slot still forms a 1-member
///     stall-escape cycle (standalone parcel; Bundle empty-escape).
inline SmallVector<OpcodeCycle, 4>
greedySplitLegalOpcodeCycles(ArrayRef<unsigned> Opcodes,
                             const HaydnBaseMCFormats &Fmts) {
  SmallVector<OpcodeCycle, 4> Out;
  if (Opcodes.empty())
    return Out;

  // Working MCInst storage: Bundle holds pointers; lifetime = this call.
  SmallVector<MCInst, 8> Storage;
  Storage.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes) {
    Storage.emplace_back();
    Storage.back().setOpcode(Opc);
  }

  auto flush = [&](unsigned Begin, unsigned End, SlotBits Occ) {
    OpcodeCycle C;
    for (unsigned I = Begin; I < End; ++I)
      C.Opcodes.push_back(Opcodes[I]);
    // Diagnostic path: EncodedBytes from registry via planFromPacketFormats.
    // Standalone escape (Occ==0 with no covering slots) still builds a
    // product-sized plan from empty occupancy.
    auto P = planFromPacketFormats(Fmts.getPacketFormats(), Occ, C.Opcodes);
    if (!P && Occ == 0)
      P = planFromPacketFormats(Fmts.getPacketFormats(), /*Occupied=*/0,
                                C.Opcodes);
    if (!P)
      P = makeProductPlan(Occ, C.Opcodes);
    C.Plan = *P;
    Out.push_back(std::move(C));
  };

  Haydn::Bundle<MCInst> B(&Fmts);
  unsigned GroupBegin = 0;
  for (unsigned I = 0, E = Opcodes.size(); I != E; ++I) {
    MCInst *MI = &Storage[I];
    if (B.empty() && B.getOccupiedSlots() == 0) {
      // Start a cycle. Empty bundle always accepts (standalone escape).
      B.add(MI);
      GroupBegin = I;
      continue;
    }
    if (B.canAdd(MI->getOpcode())) {
      B.add(MI);
      continue;
    }
    // Close current cycle, open a new one at I.
    flush(GroupBegin, I, B.getOccupiedSlots());
    B.clear();
    B.add(MI);
    GroupBegin = I;
  }
  flush(GroupBegin, Opcodes.size(), B.getOccupiedSlots());
  return Out;
}

//===----------------------------------------------------------------------===//
// late layout firewall — empty-cycle tryAdd → setDesc member
//===----------------------------------------------------------------------===//

/// Result of committing one late bare MI as a product singleton cycle.
///
/// AIE has no PreEmit growth, so setDesc+finalize never re-runs
/// (AIE2TargetMachine.cpp:88). Haydn BR / FixupHwLoops may insert bare
/// NOPs, branches, demote LoopDec+LoopJNZ — each becomes one explicit
/// Format E cycle (row+completion; no silent reshape, no MCFlags).
struct LateProductCycle {
  /// Pre-commit public opcode (input logical identity).
  unsigned LogicalOpcode = 0;
  /// Format-member opcode for MI.setDesc (AIE materializeMultiOpcodeInstrs
  /// AIEMachineScheduler.cpp:1126-1132). Equal to LogicalOpcode when the
  /// opcode has no PlacementAlternatives (fixed-slot / already-member /
  /// branch pseudo without alts).
  unsigned MemberOpcode = 0;
  /// Product cycle plan (E2/E3 row, completion, registry EncodedBytes).
  BundlePlan Plan;
  /// True when MemberOpcode != LogicalOpcode (setDesc required).
  bool NeedsSetDesc = false;
};

/// Empty-cycle product tryAdd for a single late bare opcode.
///
/// Port of AIE alt try on an empty ResourceCycle/Bundle
/// (AIEHazardRecognizer.cpp:174-214) + setDesc target selection
/// (AIEMachineScheduler.cpp:1121-1139). Format E product only.
///
/// \returns nullopt when no product plan can be derived for the opcode
/// (fail-closed). Callers fail-closed via verifier after finalize when late
/// insert cannot commit. When alts exist: MemberOpcode = tryAddProduct
/// empty-cycle choice. When no alts: MemberOpcode = LogicalOpcode.
inline std::optional<LateProductCycle>
commitLateProductCycle(unsigned LogicalOpc, const HaydnMCFormats &Fmts) {
  LateProductCycle Out;
  Out.LogicalOpcode = LogicalOpc;
  Out.MemberOpcode = LogicalOpc;
  Out.NeedsSetDesc = false;
  auto finish = [&]() -> LateProductCycle {
    Out.Plan.Row = selectProductRowForOpcodes({Out.MemberOpcode});
    Out.Plan.Completion = selectCompletionFor(Out.Plan.Row, /*MemberCount=*/1);
    return Out;
  };

  // Prefer exact singleton solve, with the documented CLOSED-singleton row
  // default (E2) — see exactSolveLateSingleton.
  if (auto Exact = exactSolveLateSingleton(LogicalOpc, Fmts)) {
    Out.MemberOpcode = Exact->MemberOpcodes.front();
    Out.NeedsSetDesc = (Out.MemberOpcode != LogicalOpc);
    Out.Plan = Exact->Plan;
    return finish();
  }

  CycleState S = makeProductCycleState(Fmts.getPacketFormats());
  if (tryAddProduct(S, Fmts, LogicalOpc)) {
    assert(S.Members.size() == 1 && "empty-cycle tryAdd is a singleton");
    Out.MemberOpcode = S.Members[0].MemberOpcode;
    Out.NeedsSetDesc = (Out.MemberOpcode != LogicalOpc);
    if (auto P = commitProduct(S, Fmts.getPacketFormats())) {
      Out.Plan = *P;
      return finish();
    }
    // tryAdd accepted but FeasibleFormatMask blocked commit — still only
    // accept a plan derived from the generated Full row.
    if (auto Derived = planFromPacketFormats(
            Fmts.getPacketFormats(), S.OccupiedSlots, {LogicalOpc})) {
      Out.Plan = *Derived;
      return finish();
    }
    return std::nullopt;
  }

  // No PlacementAlternatives (B, RET, LoopDec, LoopJNZ, already-member).
  // Explicit Format E singleton — still a legal late cycle plan.
  // Encode-oracle: lone opcode forms one cycle via exact pack / canAdd.
  SmallVector<unsigned, 1> One = {LogicalOpc};
  if (auto Packed = exactPackOneOpcodeCycle(One, Fmts)) {
    Out.Plan = Packed->Plan;
    return finish();
  }
  // Standalone escape still counts as one product parcel for size model
  // (getInstSizeInBytes returns productParcelBytes for real bare MIs).
  // Fail-closed: no plan when transitional composite coverage is absent.
  if (auto Escape =
          planFromPacketFormats(Fmts.getPacketFormats(), /*Occupied=*/0, One)) {
    Out.Plan = *Escape;
    return finish();
  }
  return std::nullopt;
}

/// Pure setDesc target for a late bare MI (nullopt = leave opcode unchanged).
/// Convenience for unit tests / callers that only need the member opcode.
inline std::optional<unsigned>
lateSingletonSetDescOpcode(unsigned LogicalOpc, const HaydnMCFormats &Fmts) {
  auto C = commitLateProductCycle(LogicalOpc, Fmts);
  if (!C || !C->NeedsSetDesc)
    return std::nullopt;
  return C->MemberOpcode;
}

/// Member opcode for one late singleton (empty-cycle tryAdd / wrap-only).
/// Shared by Fixup pads/demotion and BranchRelaxation insertBranch hooks so
/// every late creator builds the same product member form.
inline unsigned lateProductMemberOpcode(unsigned LogicalOpc) {
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  if (auto C = commitLateProductCycle(LogicalOpc, Fmts))
    return C->MemberOpcode;
  return LogicalOpc;
}

/// Wrap a bare real MI as a Format E singleton BUNDLE root (row+completion).
/// Idempotent when already bundled. After return, \p MI is a bundle child;
/// layout size lives on the BUNDLE root (committed EncodedBytes).
inline void finalizeExactLateSingleton(MachineInstr &MI) {
  if (!MI.getParent() || MI.isBundled())
    return;
  // INLINEASM is an opaque byte-emitting boundary, not a product MIR cycle.
  // Wrapping it as a BUNDLE child would drop the APP/NO_APP emission path.
  if (MI.isInlineAsm())
    return;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::instr_iterator MII = MI.getIterator();
  finalizeBundle(MBB, MII, std::next(MII));
  MachineInstr &Root = *getBundleStart(MI.getIterator());
  assert(Root.isBundle() && "exact late singleton must produce a BUNDLE root");
  // Row from the child's InstSlot (BUNDLE_E96_* operand class), not
  // `_S*` / `_E3_` name peel. Completion is full-slot architectural NOP.
  const TargetInstrInfo &TII =
      *MBB.getParent()->getSubtarget().getInstrInfo();
  BundlePlan Plan =
      makeProductPlanForOpcodes(/*Occupied=*/0, {MI.getOpcode()}, TII);
  stampBundleCommit(Root, Plan);
}

/// Layout bytes for an inserted or removed late MI.
/// Bundle children charge the committed root; bare reals charge one product
/// parcel. Call after finalizeExactLateSingleton so insert hooks report the
/// same EncodedBytes the second BranchRelaxation / computeBlockSize sees.
/// INLINEASM / INLINEASM_BR are the sole opaque non-product-cycle exception:
/// charge conservative Full-row length via getInstSizeInBytes (MaxInstLength
/// quanta); never invent a BUNDLE root or product FormatID for them.
inline unsigned lateLayoutBytes(const MachineInstr &MI) {
  if (MI.isInsideBundle() || MI.isBundledWithPred() || MI.isBundledWithSucc())
    return committedEncodedBytes(*getBundleStart(MI.getIterator())).Value;
  if (MI.isBundle())
    return committedEncodedBytes(MI).Value;
  if (MI.isInlineAsm()) {
    const MachineFunction *MF = MI.getMF();
    if (!MF)
      return 0;
    return MF->getSubtarget().getInstrInfo()->getInstSizeInBytes(MI);
  }
  return productParcelBytes().Value;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H
