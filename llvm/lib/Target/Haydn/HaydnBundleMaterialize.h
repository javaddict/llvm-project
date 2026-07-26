//===- HaydnBundleMaterialize.h - BF1/B4.3 cycle materialize -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pure helpers for post-RA / late cycle materialization (G-BUNDLE-FORMAT):
//
//   * greedySplitLegalOpcodeCycles — when a scheduled multi-MI cycle cannot
//     form one legal Bundle128, split into an ordered sequence of legal
//     sub-cycles (explicit multi-cycle; never a silent "leave all standalone"
//     without a documented split).
//   * commitLateProductCycle — B4.3 late layout firewall: empty-cycle
//     tryAddProduct → format-member opcode for setDesc of a bare late MI
//     (Fixup NOP pad, BR insertBranch, demote LoopDec/LoopJNZ edges).
//   * Each sub-cycle is product FormatID::Bundle128Full (HaydnBundlePlan.h).
//
// Encode-side oracle: Haydn::Bundle + HaydnMCFormats (same as G-PACK-LEGAL).
// Schedule-side MI+AltDesc oracle stays in HaydnPostRASchedStrategy.cpp.
//
// AIE peers:
//   AIEMachineScheduler.cpp:1121-1139 materializeMultiOpcodeInstrs setDesc
//   AIEHazardRecognizer.cpp:174-214 ResourceCycle alt try (empty cycle shape)
//   AIEFinalizeBundle.cpp:40-59 wrap standalone → BUNDLE (Haydn also stamps
//     FormatID; late re-run after PreEmit growth is the Haydn delta — AIE
//     PreEmit is empty: AIE2TargetMachine.cpp:88 / AIEBaseTargetMachine:388)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include <optional>

namespace llvm {
namespace haydn {
namespace bundle {

/// One explicit architectural sub-cycle after a greedy split (logical opcodes).
struct OpcodeCycle {
  SmallVector<unsigned, 3> Opcodes;
  BundlePlan Plan;
};

/// Greedy left-to-right split of a same-cycle opcode list into legal
/// Bundle128 cycles. Order is preserved. Each output cycle has ≤3 members
/// and passes Haydn::Bundle canAdd/add (encode oracle).
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
                             HaydnBaseMCFormats &Fmts) {
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
    C.Plan = makeBundle128Plan(Occ, C.Opcodes);
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

/// True iff the full opcode list packs into a single product Bundle128 cycle.
inline bool opcodesFormOneLegalCycle(ArrayRef<unsigned> Opcodes,
                                     HaydnBaseMCFormats &Fmts) {
  if (Opcodes.empty() || Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
    return false;
  auto Cycles = greedySplitLegalOpcodeCycles(Opcodes, Fmts);
  return Cycles.size() == 1 && Cycles[0].Opcodes.size() == Opcodes.size();
}

//===----------------------------------------------------------------------===//
// B4.3 late layout firewall — empty-cycle tryAdd → setDesc member
//===----------------------------------------------------------------------===//

/// Result of committing one late bare MI as a product singleton cycle.
///
/// AIE has no PreEmit growth, so setDesc+finalize never re-runs
/// (AIE2TargetMachine.cpp:88). Haydn BR / FixupHwLoops may insert bare
/// NOPs, branches, demote LoopDec+LoopJNZ — each becomes one explicit
/// ProductFormatID cycle (no silent reshape, no 2nd format, no MCFlags).
struct LateProductCycle {
  /// Pre-commit public opcode (input logical identity).
  unsigned LogicalOpcode = 0;
  /// Format-member opcode for MI.setDesc (AIE materializeMultiOpcodeInstrs
  /// AIEMachineScheduler.cpp:1126-1132). Equal to LogicalOpcode when the
  /// opcode has no PlacementAlternatives (fixed-slot / already-member /
  /// branch pseudo without alts).
  unsigned MemberOpcode = 0;
  /// Product cycle plan (Bundle128Full, 16 B, 1 cycle).
  BundlePlan Plan;
  /// True when MemberOpcode != LogicalOpcode (setDesc required).
  bool NeedsSetDesc = false;
};

/// Empty-cycle product tryAdd for a single late bare opcode.
///
/// Port of AIE alt try on an empty ResourceCycle/Bundle
/// (AIEHazardRecognizer.cpp:174-214) + setDesc target selection
/// (AIEMachineScheduler.cpp:1121-1139). Product FormatID only.
///
/// \returns nullopt only when tryAdd fails *and* the opcode has no fixed
/// encode identity suitable for a singleton parcel (should not happen for
/// real late inserts; callers fail-closed via verifier after finalize).
/// When alts exist: MemberOpcode = tryAddProduct empty-cycle choice.
/// When no alts: MemberOpcode = LogicalOpcode (wrap-only; already member
/// or single-slot public opcode / pseudo expanded at emit).
inline std::optional<LateProductCycle>
commitLateProductCycle(unsigned LogicalOpc, HaydnMCFormats &Fmts) {
  LateProductCycle Out;
  Out.LogicalOpcode = LogicalOpc;
  Out.MemberOpcode = LogicalOpc;
  Out.NeedsSetDesc = false;

  CycleState S = makeProductCycleState();
  if (tryAddProduct(S, Fmts, LogicalOpc)) {
    assert(S.Members.size() == 1 && "empty-cycle tryAdd is a singleton");
    Out.MemberOpcode = S.Members[0].MemberOpcode;
    Out.NeedsSetDesc = (Out.MemberOpcode != LogicalOpc);
    if (auto P = commitProduct(S)) {
      Out.Plan = *P;
    } else {
      // tryAdd accepted but commit failed — still product singleton plan.
      Out.Plan = makeBundle128Plan(S.OccupiedSlots, {LogicalOpc});
    }
    return Out;
  }

  // No PlacementAlternatives (B, RET, LoopDec, LoopJNZ, already _S* member).
  // Explicit ProductFormatID singleton — still a legal late cycle.
  // Encode-oracle: lone opcode forms one cycle via greedy split / canAdd.
  SmallVector<unsigned, 1> One = {LogicalOpc};
  if (!opcodesFormOneLegalCycle(One, Fmts)) {
    // Standalone escape still counts as one product parcel for size model
    // (getInstSizeInBytes returns ProductFormatDesc.Bytes for real bare MIs).
    Out.Plan = makeBundle128Plan(/*Occupied=*/0, One);
    return Out;
  }
  auto Cycles = greedySplitLegalOpcodeCycles(One, Fmts);
  assert(Cycles.size() == 1);
  Out.Plan = Cycles[0].Plan;
  return Out;
}

/// Pure setDesc target for a late bare MI (nullopt = leave opcode unchanged).
/// Convenience for unit tests / callers that only need the member opcode.
inline std::optional<unsigned>
lateSingletonSetDescOpcode(unsigned LogicalOpc, HaydnMCFormats &Fmts) {
  auto C = commitLateProductCycle(LogicalOpc, Fmts);
  if (!C || !C->NeedsSetDesc)
    return std::nullopt;
  return C->MemberOpcode;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEMATERIALIZE_H
