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
//     (any emission order; snapshot reads). Dead-def cohabitation stays legal.
//   * auctionReadySubsetCycle — bounded ready-subset cycle auction.
//   * commitExactMultiMIProductCycle — production multi-MI MIR commit:
//     exactSolve setDesc → MachineBundle SlotMap → applyFormatOrdering →
//     stamp row+completion. Never leaves alts-bearing logicals (no residual
//     logical pack after FE8). Sole product multi-MI commit is post-RA; never
//     a pre-RA SMS freeze path (no force-coissue / multi-member handoff).
//   * commitExactHardRootProductCycle — residual unit-test helper only.
//     Product leaveMBB uses free multi-MI commitExactMultiMIProductCycle and
//     ordinary residual unstamped multi-member commit/sequentialize; it does
//     not keep hard-root freeze identity.
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
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm {

// Defined in HaydnHazardRecognizer.cpp (AIE applyFormatOrdering peer).
// Forward-declared so the shared MIR commit surface does not pull the HR.
void applyFormatOrdering(Haydn::MachineBundle &Bundle, const VLIWFormat &Format,
                         MachineBasicBlock::iterator InsertPoint);

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
/// logical unit cover. An `e3_*` member cannot be an operand of
/// BUNDLE_E96_TWO_ENTRY; an `e2_*` member cannot be an operand of
/// BUNDLE_E96_THREE_ENTRY. AIE: PacketFormats + getSlotKind, no suffix.
/// Dual single-unit logicals must not freeze E2 and rely on MC DFS.
inline BundleFormatRowID
selectProductRowForOpcodes(ArrayRef<unsigned> Opcodes) {
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  bool AnyE3 = false;
  bool AnyE2 = false;
  for (unsigned Opc : Opcodes) {
    const MCSlotKind Kind = Fmts.getSlotKind(Opc);
    if (formatECompositeSlotIsE3(Kind))
      AnyE3 = true;
    else if (formatECompositeSlotIsE2(Kind))
      AnyE2 = true;
  }
  if (AnyE3)
    return BundleFormatRowID::E96ThreeEntry;
  if (AnyE2)
    return BundleFormatRowID::E96TwoEntry;
  if (Opcodes.size() >= 3)
    return BundleFormatRowID::E96ThreeEntry;
  if (Opcodes.size() <= 1)
    return BundleFormatRowID::E96TwoEntry;
  if (opcodesHaveFormatEUnitCoverForMode(Opcodes, /*Mode=*/0))
    return BundleFormatRowID::E96TwoEntry;
  if (opcodesHaveFormatEUnitCoverForMode(Opcodes, /*Mode=*/1))
    return BundleFormatRowID::E96ThreeEntry;
  return selectProductRowForMemberCount(Opcodes.size());
}

inline BundleFormatRowID
selectProductRowForOpcodes(ArrayRef<unsigned> Opcodes,
                           const TargetInstrInfo &TII) {
  (void)TII;
  return selectProductRowForOpcodes(Opcodes);
}

/// Product plan with row chosen from Format E unit cover when possible.
inline BundlePlan makeProductPlanForOpcodes(SlotBits Occupied,
                                            ArrayRef<unsigned> Members,
                                            const TargetInstrInfo &TII) {
  BundlePlan P;
  P.Row = selectProductRowForOpcodes(Members, TII);
  P.Completion = selectCompletionFor(P.Row, Members.size());
  P.OccupiedSlots = Occupied;
  P.MemberOpcodes.assign(Members.begin(), Members.end());
  P.Bytes = productParcelBytes();
  P.Cycles = OneCycle;
  return P;
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
/// Callers that already ran materializeMultiOpcodeInstrs may pass post-setDesc
/// member opcodes; exactTryAdd only accepts alts-bearing logicals, so use
/// exactPackOneOpcodeCycle for the encode-oracle path on fixed members.
inline std::optional<ExactProductCycle>
exactSolveProductOpcodes(ArrayRef<unsigned> Opcodes, const HaydnMCFormats &Fmts) {
  if (Opcodes.empty() || Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return std::nullopt;

  CycleCandidateSet Cands =
      makeProductCandidateSet(Fmts.getPacketFormats());
  for (unsigned Opc : Opcodes) {
    if (!exactTryAddProduct(Cands, Fmts, Opc))
      return std::nullopt;
  }

  const CycleState &S = selectPreferredCandidate(Cands);
  if (S.Members.size() != Opcodes.size())
    return std::nullopt;

  auto Plan = commitProduct(S, Fmts.getPacketFormats());
  if (!Plan || !Plan->isProductLegal())
    return std::nullopt;

  ExactProductCycle Out;
  Out.LogicalOpcodes.assign(Opcodes.begin(), Opcodes.end());
  Out.MemberOpcodes.reserve(S.Members.size());
  for (const CycleMember &M : S.Members)
    Out.MemberOpcodes.push_back(M.MemberOpcode);
  Out.Plan = *Plan;
  Out.State = S;
  return Out;
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

/// True when two different members of one cycle both live-def overlapping
/// registers (WAW). leaveMBB residual seam replay treats same-cycle WAW as a
/// permanent Req/Res conflict (stalls cannot clear it). Multi-stage modulo
/// packs without rename can place stage-N and stage-N+1 redefs of one physreg
/// on the same mod — refuse multi-MI and leave sequential parcels.
inline bool cycleMembersHaveWAW(ArrayRef<MachineInstr *> Instrs,
                                const TargetRegisterInfo *TRI) {
  if (Instrs.size() < 2)
    return false;

  SmallVector<Register, 4> SeenDefs;
  for (MachineInstr *MI : Instrs) {
    if (!MI)
      continue;
    for (const MachineOperand &MO : MI->all_defs()) {
      if (!MO.isReg() || !MO.getReg() || MO.isDead())
        continue;
      Register R = MO.getReg();
      if (!(R.isPhysical() || R.isVirtual()))
        continue;
      for (Register Prev : SeenDefs) {
        if (Prev == R)
          return true;
        if (TRI && Prev.isPhysical() && R.isPhysical() &&
            TRI->regsOverlap(Prev, R))
          return true;
      }
      SeenDefs.push_back(R);
    }
  }
  return false;
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
//   * **WAW** (live def vs live def): \p cycleMembersHaveWAW. Same-cycle
//     multi-stage physreg redefs without rename must not multi-MI commit —
//     residual seam replay cannot stall-clear WAW.
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
    // SET_HWLOOP trip/Off sample cannot share a cycle with a producer of
    // those regs (snapshot no-forwarding — remat ADDI+SET peel).
    if (TII && cycleMembersHaveHwloopTripConflict(Instrs, *TII, TRI))
      return false;
  }

  return opcodesFormOneLegalCycle(Opcodes, Fmts);
}

/// Full **emission** coissue probe for one product cycle (layer 3 + schedule
/// pack). Caller must already have same available/ready cycle (layer 1) and
/// no blocking Data deps (layer 2).
///
/// Runs schedule-order legality, exactSolve/setDesc, MachineBundle encode,
/// and **field-order** no-forwarding RAW. Temporary setDesc is reverted so
/// pre-RA SMS handoff can probe without freezing illegal hard roots.
///
/// Alias kept for existing call sites: \p instrsCanExactCommitProductCycle.
inline bool canCoissueProductCycle(ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2 || Instrs.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;
  for (MachineInstr *MI : Instrs) {
    if (!MI || !MI->getParent() || !MI->getMF() || MI->isInlineAsm())
      return false;
  }

  MachineFunction &MF = *Instrs.front()->getMF();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // SET_HWLOOP must not share a cycle with a producer of its trip/Off regs
  // (snapshot no-forwarding: WAR samples stale trip; RAW needs forwarding).
  if (cycleMembersHaveHwloopTripConflict(Instrs, TII, TRI))
    return false;

  // Format E E2-only logicals cannot form a 3-wide E3 parcel.
  if (Instrs.size() >= 3) {
    for (MachineInstr *MI : Instrs) {
      if (isFormatEE2OnlyOpcodeName(TII.getName(MI->getOpcode())))
        return false;
    }
  }

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  if (!instrsFormOneLegalCycle(Instrs, Fmts))
    return false;

  SmallVector<unsigned, 3> SavedOps;
  SavedOps.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs)
    SavedOps.push_back(MI->getOpcode());

  // Residual FieldSlots can accept dual single-unit logicals; MC unit
  // injectivity cannot. Fail closed before temporary setDesc.
  if (!opcodesHaveFormatEUnitCover(SavedOps, TII))
    return false;

  auto restoreDescs = [&]() {
    for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
      if (Instrs[I]->getOpcode() != SavedOps[I])
        Instrs[I]->setDesc(TII.get(SavedOps[I]));
    }
  };

  // Bake members the same way commitExactMultiMIProductCycle does.
  {
    SmallVector<unsigned, 3> Ops = SavedOps;
    if (auto Exact = exactSolveProductOpcodes(Ops, Fmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
        const unsigned Member = Exact->MemberOpcodes[I];
        if (Member != Instrs[I]->getOpcode())
          Instrs[I]->setDesc(TII.get(Member));
      }
    } else {
      for (unsigned Opc : Ops) {
        if (hasPlacementAlternatives(Fmts, Opc)) {
          restoreDescs();
          return false;
        }
      }
      if (!opcodesFormOneLegalCycle(Ops, Fmts)) {
        restoreDescs();
        return false;
      }
    }
  }

  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI)) {
      restoreDescs();
      return false;
    }
    Bundle.add(MI);
  }
  if (Bundle.size() <= 1 || Bundle.isStandalone()) {
    restoreDescs();
    return false;
  }
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  if (!Fmt) {
    restoreDescs();
    return false;
  }

  SmallVector<MachineInstr *, 3> FieldOrdered =
      getFieldOrderedMembers(Bundle, *Fmt);
  // Field order = emission order. True RAW here means an Anti/WAR that the
  // preferred placement cannot preserve (use-before-redef flipped).
  const bool FieldRAW = cycleMembersHaveTrueRAW(FieldOrdered, TRI);
  // Same-cycle live WAW (incl. multi-stage physreg redefs) cannot multi-MI.
  const bool FieldWAW = cycleMembersHaveWAW(FieldOrdered, TRI) ||
                        cycleMembersHaveWAW(Instrs, TRI);
  restoreDescs();
  return !FieldRAW && !FieldWAW;
}

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
///      lands here). Fail closed if an alts-bearing logical cannot solve —
///      never pack residual logicals into a product BUNDLE.
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
inline bool commitExactMultiMIProductCycle(ArrayRef<MachineInstr *> Instrs) {
  if (Instrs.size() < 2)
    return false;

  // Opaque INLINEASM must stay standalone — never a multi-MI BUNDLE child.
  for (MachineInstr *MI : Instrs) {
    if (MI->isInlineAsm())
      return false;
  }

  MachineBasicBlock *MBB = Instrs.front()->getParent();
  if (!MBB || !MBB->getParent())
    return false;
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // Fail closed: E2-only Format E logicals cannot form a 3-wide E3 parcel.
  if (Instrs.size() >= 3) {
    for (MachineInstr *MI : Instrs) {
      if (isFormatEE2OnlyOpcodeName(TII.getName(MI->getOpcode())))
        return false;
    }
  }

  // Fail closed on same-cycle true RAW before setDesc / finalizeBundle can
  // invent InternalRead markers for an illegal no-forwarding pack.
  if (cycleMembersHaveTrueRAW(Instrs, TRI))
    return false;
  // Fail closed on same-cycle live WAW (leaveMBB seam replay cannot clear it).
  if (cycleMembersHaveWAW(Instrs, TRI))
    return false;
  // SET_HWLOOP trip/Off sample cannot coissue with a producer of those regs
  // (WAR would sample stale trip under snapshot no-forwarding).
  if (cycleMembersHaveHwloopTripConflict(Instrs, TII, TRI))
    return false;

  // Bake format-member descriptors before SlotMap / encode. leaveRegion
  // materializeMultiOpcodeInstrs is the primary AltDescs path; this is the
  // fail-closed second line so multi-MI commit never emits residual logicals.
  {
    SmallVector<unsigned, 3> Ops;
    Ops.reserve(Instrs.size());
    for (MachineInstr *MI : Instrs)
      Ops.push_back(MI->getOpcode());

    // Format E unit injectivity (MC serialize authority). Residual S* packs
    // that over-count single-unit logicals must not freeze a BUNDLE.
    if (!opcodesHaveFormatEUnitCover(Ops, TII))
      return false;

    const HaydnMCFormats &SolveFmts = haydnDefaultMCFormats();
    if (auto Exact = exactSolveProductOpcodes(Ops, SolveFmts)) {
      for (unsigned I = 0, E = Instrs.size(); I != E; ++I) {
        const unsigned Member = Exact->MemberOpcodes[I];
        if (Member != Instrs[I]->getOpcode())
          Instrs[I]->setDesc(TII.get(Member));
      }
    } else {
      // Already-member / no-alt path: encode oracle only. Refuse any residual
      // alts-bearing logical that failed exactSolve (would be residual pack).
      for (MachineInstr *MI : Instrs) {
        if (hasPlacementAlternatives(SolveFmts, MI->getOpcode()))
          return false;
      }
      if (!opcodesFormOneLegalCycle(Ops, SolveFmts))
        return false;
    }
  }

  // finalizeBundle only sets IsInternalRead; it never clears. Members that
  // were previously bundled (hard-root recommit, re-order) may carry stale
  // markers that would survive a field-order change. Drop them so the
  // subsequent finalize rebuild is authoritative.
  for (MachineInstr *MI : Instrs) {
    for (MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.isInternalRead())
        MO.setIsInternalRead(false);
    }
  }

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  Haydn::MachineBundle Bundle(&Fmts);
  for (MachineInstr *MI : Instrs) {
    if (!Bundle.canAdd(MI))
      return false;
    Bundle.add(MI);
  }

  if (Bundle.size() <= 1 || Bundle.isStandalone())
    return false;
  const VLIWFormat *Fmt = Bundle.getFormatOrNull();
  if (!Fmt)
    return false;

  // The schedule-order RAW check above is necessary but NOT sufficient.
  // applyFormatOrdering permutes members into Format.getSlots() field order —
  // the order the encoder / AsmPrinter / hardware observe. A legal same-cycle
  // WAR in schedule order (reader before writer) can be flipped by the field
  // order into writer-before-reader; finalizeBundle would then mark the
  // reader's operand IsInternalRead, modeling a same-cycle read of a
  // same-cycle def — the no-forwarding hazard (the reader observes the new,
  // not the pre-cycle, value). Re-validate the no-forwarding RAW law on the
  // EXACT field-ordered sequence that will be emitted. If it has a true RAW,
  // this cycle cannot be one product parcel: fail closed to sequential parcels
  // (schedule order is reader-before-writer, a legal WAR when split across
  // cycles — the reader observes the pre-cycle value, the writer takes effect
  // next cycle).
  SmallVector<MachineInstr *, 3> FieldOrdered =
      getFieldOrderedMembers(Bundle, *Fmt);
  if (cycleMembersHaveTrueRAW(FieldOrdered, TRI))
    return false;

  // Iterator AFTER the last schedule-order member — re-insert point
  // (AIEHazardRecognizer.cpp:338-339 getBundleEnd of last instr).
  MachineBasicBlock::iterator BundleEnd =
      getBundleEnd(Instrs.back()->getIterator());
  applyFormatOrdering(Bundle, *Fmt, BundleEnd);

  MachineInstr &Root =
      *getBundleStart(Bundle.getInstrs().front()->getIterator());
  if (!Root.isBundle())
    return false;

  // Durable Format E row + completion from real post-setDesc members.
  // Row is selected from generated Format E unit cover so dual single-unit
  // logicals (two ADD32, ADD+XOR) freeze E3 when E2 cannot inject units —
  // never rely on MC residual DFS/row upgrade after commit. Full-slot
  // completion (AllEntriesReal) for non-empty cycles.
  SmallVector<unsigned, 3> MemberOps;
  MemberOps.reserve(Instrs.size());
  for (MachineInstr *MI : Instrs)
    MemberOps.push_back(MI->getOpcode());
  BundlePlan Plan =
      makeProductPlanForOpcodes(Bundle.getOccupiedSlots(), MemberOps, TII);
  stampBundleCommit(Root, Plan);
  return true;
}

/// Residual unit-test helper: dissolve a multi-member BUNDLE shell and
/// recommit via \p commitExactMultiMIProductCycle when membership is one
/// legal product cycle. Product leaveMBB does not keep hard-root freeze
/// identity — free multi-MI and residual unstamped multi-member handling
/// use ordinary multi-MI commit / sequentialize.
///
/// \p BundleRoot must be TargetOpcode::BUNDLE with ≥2 real children.
/// \p MII provides MCInstrDesc for member setDesc (TargetInstrInfo ok).
///
/// \returns true on successful recommit; false if membership is not one
/// legal product cycle.
inline bool commitExactHardRootProductCycle(MachineInstr &BundleRoot,
                                            const MCInstrInfo &MII) {
  if (!BundleRoot.isBundle() || !BundleRoot.getParent())
    return false;

  MachineBasicBlock &MBB = *BundleRoot.getParent();
  MachineFunction *MF = MBB.getParent();
  if (!MF)
    return false;

  SmallVector<MachineInstr *, 3> Kids;
  for (MachineBasicBlock::instr_iterator I =
           std::next(BundleRoot.getIterator());
       I != MBB.instr_end() && I->isBundledWithPred(); ++I)
    Kids.push_back(&*I);

  if (Kids.size() < 2 || Kids.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;

  // Exact solve on the live child opcodes (logical or already-member).
  // INLINEASM is never a legal hard-root member.
  SmallVector<unsigned, 3> Ops;
  Ops.reserve(Kids.size());
  for (MachineInstr *K : Kids) {
    if (K->isInlineAsm())
      return false;
    Ops.push_back(K->getOpcode());
  }

  // Pre-dissolve legality must match commitExactMultiMIProductCycle, including
  // **field-order** no-forwarding RAW. Schedule-order WAR can flip to true RAW
  // under Format field order (residual S2→S0); RA can also turn pre-RA vreg
  // independence into physreg WAR. Probe with canCoissueProductCycle (emission
  // layer: temp setDesc + field-order Anti preservation) so dissolve never
  // runs on a group that commit cannot finish — callers sequentialize safely.
  if (!canCoissueProductCycle(Kids))
    return false;

  // Bake format-member opcodes before dissolve/re-finalize (same as commit).
  // setDesc does not rebuild child operands/ties/implicits — consolidated root
  // ops are rebuilt below by commitExactMultiMIProductCycle.
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  if (auto Exact = exactSolveProductOpcodes(Ops, Fmts)) {
    for (unsigned I = 0, E = Kids.size(); I != E; ++I) {
      const unsigned Member = Exact->MemberOpcodes[I];
      if (Member != Kids[I]->getOpcode())
        Kids[I]->setDesc(MII.get(Member));
    }
  }

  // Dissolve the old root shell (AIE eraseRootFromBlock peer): clear stale
  // InternalRead markers (UnpackMachineBundles peer), detach each child,
  // then erase the BUNDLE header with its stale consolidated operands.
  // Children stay contiguous in MBB order; commitExactMultiMIProductCycle
  // applies field order + finalizeBundle (rebuild root ops/kills/reads) +
  // Format E row+completion stamp.
  for (MachineInstr *K : Kids) {
    for (MachineOperand &MO : K->operands()) {
      if (MO.isReg() && MO.isInternalRead())
        MO.setIsInternalRead(false);
    }
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }
  BundleRoot.eraseFromParent();

  if (!commitExactMultiMIProductCycle(Kids))
    return false;

  // Authoritative post-commit stamp: the new BUNDLE root must carry a product
  // Format E row. Surviving hard groups are never left as unstamped shells.
  MachineInstr &NewRoot = *getBundleStart(Kids.front()->getIterator());
  if (!NewRoot.isBundle() || !getBundleRowID(NewRoot).has_value())
    return false;
  return true;
}

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

  // Prefer exact singleton solve (shared surface).
  if (auto Exact = exactSolveProductOpcodes(ArrayRef<unsigned>{LogicalOpc},
                                            Fmts)) {
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
