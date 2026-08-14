//===- HaydnBundleFormatSolver.h - Pure CycleState tryAdd/commit -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pure CycleState tryAdd/commit solver + FormatID frontier:
//
//   CycleState  members + OccupiedSlots + FeasibleFormatMask
//   tryAdd      transactional; mutates state only on accept
//   commit      selectFormatByPriority → BundlePlan (no setDesc)
//   productFeasibleFormatMask — Pre-RA / SMS occupancy → FormatID bitset
//
// AIE peers (port structure; do not invent a parallel packing theory):
//
//   * AIEBundle.h:62-105  canAdd — format-available / slot conflict
//   * AIEBundle.h:110-145 add    — commits OccupiedSlots / SlotMap
//   * AIEBundle.h:150-156 getFormatOrNull — format-from-occupancy
//   * AIEHazardRecognizer.cpp:173-214 ResourceCycle —
//       getAlternateInstsOpcode + any_of / first canAdd AltOpcode
//   * AIEFormat.cpp:18-27 PacketFormats::getFormat first-covering
//       → Haydn selectFormatByPriority (explicit Priority, plan §4.3)
//
// Product live row is still only BUNDLE128_FULL. Table + CompatibleFormatMask
// are N-format-ready (unit tests may inject synthetic FormatDesc rows).
//
// Bundle / HR / SMS wire through tryAdd (adapters). Placement authority is
// PlacementAlternative + tryAdd. getLegalSlots is alts-derived (OR of non-zero
// sparse indices) for encode helpers only; Bundle/HR no-alt path does not
// consult it.
//
// Pre-RA / SMS expose a feasible FormatID *frontier* (mask) without
// freezing FormatID or setDesc (plan §7.1). Product size-1 → mask is always
// ProductFormatMask when occupancy is Full-coverable.
// productFeasibleFormatMask(OccupiedSlots) remains the occupancy-only rebuild
// for Pre-RA / Bundle adapters.
//
// SMS HaydnResourceCycle holds *live* CycleState (peer of post-RA HR
// CurrentCycleState / commitPlacementForEmit tryAddProduct). FeasibleFormatMask
// and member field choices accumulate via tryAddProduct — not OccupiedSlots-only
// rebuild. computeProductResMII greedy bin-packs opcodes with the same pure
// tryAddProduct depth (plan §7.1 Diff ResMII vs emit packing density).
//
// Still out of scope here:
//   second product format
//
// No Flags placement. HR may stamp AltDesc field slots (transitional).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H

#include "HaydnBundlePlan.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include <cstdint>
#include <optional>

namespace llvm {
namespace haydn {
namespace bundle {

//===----------------------------------------------------------------------===//
// CycleMember / CycleState (plan §6.2)
//===----------------------------------------------------------------------===//

/// One accepted member placement inside a cycle (pre-setDesc logical + field).
struct CycleMember {
  /// Pre-commit public opcode (never _S* private peer as logical identity).
  unsigned LogicalOpcode = 0;
  /// Format-member opcode chosen for this field (post-setDesc identity later).
  unsigned MemberOpcode = 0;
  /// Single Haydn::SLOT* bit claimed by this member.
  SlotBits FieldSlots = 0;
  /// Single Haydn::Unit bit claimed by this member; 0 when the encoding does
  /// not model units (Bundle128).
  Haydn::UnitBits Units = 0;
};

/// Transactional packing state for one architectural issue cycle.
/// Pure data — no MachineInstr / AltDesc / MCFlags side effects.
struct CycleState {
  SmallVector<CycleMember, 3> Members;
  SlotBits OccupiedSlots = 0;
  /// Units already claimed this cycle. A bundle entry uses exactly one unit
  /// and no two entries may share one, which is a SEPARATE constraint from
  /// slot occupancy: two entries at different positions can want the same
  /// unit. Stays 0 under Bundle128, where the member spelling carries no unit
  /// because its slot model pinned one unit per slot.
  Haydn::UnitBits OccupiedUnits = 0;
  /// Intersection of FormatIDs still covering OccupiedSlots and compatible
  /// with every accepted member (bit = formatIDBit(FormatID)).
  uint64_t FeasibleFormatMask = ProductFormatMask;
  /// The mask this cycle STARTED from, before any member narrowed it. The
  /// re-solve in tryAdd re-decides every placement, so it has to seed from the
  /// unnarrowed frontier; FeasibleFormatMask has already been intersected down
  /// by the members it is about to discard. Kept rather than recomputed
  /// because a synthetic table may have been seeded with a subset.
  uint64_t SeedFormatMask = ProductFormatMask;

  bool empty() const { return Members.empty(); }
  unsigned memberCount() const {
    return static_cast<unsigned>(Members.size());
  }
};

/// Initial state whose FeasibleFormatMask is the union of all FormatIDs in
/// \p Table (product: Full only; synthetic multi-row unit tables OK).
inline CycleState makeInitialCycleState(ArrayRef<FormatDesc> Table) {
  CycleState S;
  S.FeasibleFormatMask = 0;
  for (const FormatDesc &F : Table)
    S.FeasibleFormatMask |= formatIDBit(F.FID);
  if (S.FeasibleFormatMask == 0)
    S.FeasibleFormatMask = ProductFormatMask;
  S.SeedFormatMask = S.FeasibleFormatMask;
  return S;
}

/// Product CycleState (BUNDLE128_FULL-only FeasibleFormatMask).
inline CycleState makeProductCycleState() {
  return makeInitialCycleState(productFormatTable());
}

//===----------------------------------------------------------------------===//
// tryAdd — transactional pure accept (AIE Bundle::canAdd + first alt)
//===----------------------------------------------------------------------===//

/// Formats still feasible for \p NewOcc under \p AllowedMask ∩ table rows.
/// AIE peer: PacketFormats::getFormat first-covering scan (AIEFormat.cpp:18-27)
/// strengthened to a FormatID *mask* (plan §6.2 frontier).
inline uint64_t
coveringFormatMask(ArrayRef<FormatDesc> Table, SlotBits NewOcc,
                   uint64_t AllowedMask) {
  uint64_t Out = 0;
  for (const FormatDesc &F : Table) {
    const uint64_t Bit = formatIDBit(F.FID);
    if (!(AllowedMask & Bit))
      continue;
    if (F.covers(NewOcc))
      Out |= Bit;
  }
  return Out;
}

/// Feasible FormatID bitset for \p Occupied under \p Table ∩ \p SeedMask.
/// Empty occupancy keeps every seeded FormatID that covers 0 (all do).
/// Non-empty occupancy drops rows that no longer cover OccupiedSlots —
/// N-format-ready shrink (unit synthetic tables; product size-1 Full).
inline uint64_t feasibleFormatMask(ArrayRef<FormatDesc> Table,
                                   SlotBits Occupied, uint64_t SeedMask) {
  return coveringFormatMask(Table, Occupied, SeedMask);
}

/// Product (BUNDLE128_FULL-only) FormatID frontier for Pre-RA / SMS.
///
/// AIE returns a single VLIWFormat* from occupancy (AIEBundle.h:150-156
/// getFormatOrNull → PacketFormats::getFormat). Haydn keeps a *mask* of still-
/// feasible FormatIDs until post-RA freeze (plan §7.1): never setDesc / never
/// commit FormatID on this path. Product table size 1 → empty and any Full-
/// covering occupancy both yield ProductFormatMask.
inline uint64_t productFeasibleFormatMask(SlotBits Occupied = 0) {
  return feasibleFormatMask(productFormatTable(), Occupied, ProductFormatMask);
}

/// Rebuild a product CycleState from occupied slots only (no member history).
/// Used by Bundle / HR adapters that retain OccupiedSlots as the durable
/// occupancy bitset (SMS ResMII, scoreboard) while tryAdd is legality.
/// FeasibleFormatMask tracks the product FormatID frontier from occupancy.
inline CycleState
makeProductCycleStateFromOccupied(SlotBits Occupied) {
  CycleState S = makeProductCycleState();
  S.OccupiedSlots = Occupied;
  S.FeasibleFormatMask = productFeasibleFormatMask(Occupied);
  return S;
}

/// Map a single-bit FieldSlots to the position index it names.
///
/// The alternates index space is not the number of entries a bundle holds.
/// They are the same three for Bundle128 and diverge under format E, where a
/// bundle still holds at most ISSUE_SLOT_COUNT entries but an instruction may
/// be placeable at any of the (entry position, unit) pairs. Bound by MaxSlots,
/// the width of SlotBits, rather than by the entry count.
/// \returns nullopt when \p Field does not name exactly one position.
inline std::optional<unsigned> fieldSlotsToIndex(SlotBits Field) {
  if (Field == 0 || (Field & (Field - 1)) != 0)
    return std::nullopt;
  const unsigned Index = llvm::countr_zero(Field);
  if (Index >= static_cast<unsigned>(llvm::MaxSlots))
    return std::nullopt;
  return Index;
}

/// Placement alternatives for \p LogicalOpc, restricted to the ones that name
/// exactly one position and ordered the way the greedy walk orders them —
/// highest slot bit first. Sharing this between the greedy path and the
/// re-solve is what makes the two agree: explored in the same order, the first
/// solution the search finds IS the greedy one whenever greedy succeeds.
inline bool
placementAlternativesByPosition(const HaydnMCFormats &Fmts, unsigned LogicalOpc,
                                SmallVectorImpl<PlacementAlternative> &Out,
                                const MCInstrInfo *MII) {
  if (!enumeratePlacementAlternatives(Fmts, LogicalOpc, Out, MII))
    return false;
  llvm::erase_if(Out, [](const PlacementAlternative &A) {
    return A.FieldSlots == 0 || !fieldSlotsToIndex(A.FieldSlots);
  });
  if (Out.empty())
    return false;
  llvm::stable_sort(Out, [](const PlacementAlternative &A,
                            const PlacementAlternative &B) {
    return A.FieldSlots > B.FieldSlots;
  });
  return true;
}

/// Depth-first assignment of one alternative per member to a distinct (slot,
/// unit) pair that keeps some format covering the whole occupancy. Records the
/// accepted alternative per member in \p Chosen.
inline bool
assignMembers(ArrayRef<SmallVector<PlacementAlternative, 8>> Alts,
              ArrayRef<FormatDesc> Table, unsigned I, SlotBits Occ,
              Haydn::UnitBits Units, uint64_t Mask,
              MutableArrayRef<const PlacementAlternative *> Chosen) {
  if (I == Alts.size())
    return true;
  for (const PlacementAlternative &A : Alts[I]) {
    if (Occ & A.FieldSlots)
      continue;
    if (Units & A.Units)
      continue;
    const uint64_t Allowed = Mask & A.CompatibleFormatMask;
    if (Allowed == 0)
      continue;
    const SlotBits NewOcc = Occ | A.FieldSlots;
    const uint64_t NewMask = coveringFormatMask(Table, NewOcc, Allowed);
    if (NewMask == 0)
      continue;
    Chosen[I] = &A;
    if (assignMembers(Alts, Table, I + 1, NewOcc, Units | A.Units, NewMask,
                      Chosen))
      return true;
  }
  Chosen[I] = nullptr;
  return false;
}

/// Re-solve \p S from scratch over its existing members plus \p LogicalOpc.
/// Existing members keep their LOGICAL identity — only the (member opcode,
/// slot, unit) each was assigned may move — so a caller holding per-member
/// state keyed on the logical is unaffected, while one that has already
/// published a member opcode must re-publish it. The post-RA hazard recognizer
/// is the second kind: it stamps an alternate descriptor per instruction as
/// each is accepted, and re-stamps the movers (HaydnHazardRecognizer.cpp).
///
/// \returns true and mutates \p S on accept; false leaves \p S untouched.
inline bool tryReassign(CycleState &S, const HaydnMCFormats &Fmts,
                        ArrayRef<FormatDesc> Table, unsigned LogicalOpc,
                        const MCInstrInfo *MII) {
  const unsigned N = static_cast<unsigned>(S.Members.size());
  SmallVector<SmallVector<PlacementAlternative, 8>, 4> Alts(N + 1);
  for (unsigned I = 0; I != N; ++I)
    if (!placementAlternativesByPosition(Fmts, S.Members[I].LogicalOpcode,
                                         Alts[I], MII))
      return false;
  if (!placementAlternativesByPosition(Fmts, LogicalOpc, Alts[N], MII))
    return false;

  SmallVector<const PlacementAlternative *, 4> Chosen(N + 1, nullptr);
  // Seed from the frontier the cycle STARTED with: this re-decides every
  // placement, so the narrowing the discarded assignment produced must not
  // constrain it.
  if (!assignMembers(Alts, Table, /*I=*/0, /*Occ=*/0, /*Units=*/0,
                     S.SeedFormatMask, Chosen))
    return false;

  CycleState New;
  New.SeedFormatMask = S.SeedFormatMask;
  New.FeasibleFormatMask = S.SeedFormatMask;
  New.Members.reserve(N + 1);
  for (unsigned I = 0; I != N + 1; ++I) {
    const PlacementAlternative &A = *Chosen[I];
    CycleMember M;
    M.LogicalOpcode = I < N ? S.Members[I].LogicalOpcode : LogicalOpc;
    M.MemberOpcode = A.MemberOpcode;
    M.FieldSlots = A.FieldSlots;
    M.Units = A.Units;
    New.Members.push_back(M);
    New.OccupiedSlots |= A.FieldSlots;
    New.OccupiedUnits |= A.Units;
    New.FeasibleFormatMask =
        coveringFormatMask(Table, New.OccupiedSlots,
                           New.FeasibleFormatMask & A.CompatibleFormatMask);
  }
  assert(New.FeasibleFormatMask != 0 &&
         "re-solve accepted an occupancy no format covers");
  S = std::move(New);
  return true;
}

/// First free field assignment for \p LogicalOpc that keeps a covering format.
/// Prefer higher slots first (S2 → S1 → S0) — AIE-shaped alt try order for
/// Haydn multi-slot logicals (leaves S0 free for loads).
///
/// Port of AIE alt try (AIEHazardRecognizer.cpp:174-214): enumerate alts,
/// accept first that canAdd; here "canAdd" = free FieldSlots +
/// CompatibleFormatMask ∩ FeasibleFormatMask covers NewOcc via FormatDesc.
///
/// \returns true and mutates \p S on accept; false leaves \p S unchanged.
inline bool tryAdd(CycleState &S, const HaydnMCFormats &Fmts,
                   ArrayRef<FormatDesc> Table, unsigned LogicalOpc,
                   const MCInstrInfo *MII = nullptr) {
  SmallVector<PlacementAlternative, 4> Alts;
  if (!enumeratePlacementAlternatives(Fmts, LogicalOpc, Alts, MII))
    return false;

  // Snapshot for pure transactional reject path.
  const CycleState Snapshot = S;

  // Highest position first (AIEHazardRecognizer.cpp:183-194 any_of / first
  // canAdd; Haydn multi-slot prefers high slots so loads keep S0). Stated over
  // the alternatives rather than over a 0..ISSUE_SLOT_COUNT range: for
  // Bundle128 the two are the same walk, S2 → S1 → S0, but the alternates index
  // space is not the entry count and a fixed range would leave every position
  // past the third unreachable.
  SmallVector<const PlacementAlternative *, 8> ByPosition;
  for (const PlacementAlternative &Alt : Alts)
    if (Alt.FieldSlots != 0 && fieldSlotsToIndex(Alt.FieldSlots))
      ByPosition.push_back(&Alt);
  llvm::stable_sort(ByPosition, [](const PlacementAlternative *A,
                                   const PlacementAlternative *B) {
    return A->FieldSlots > B->FieldSlots;
  });

  {
    for (const PlacementAlternative *AltP : ByPosition) {
      const PlacementAlternative &Alt = *AltP;
      // Slot conflict (AIEBundle.h:98-100 OccupiedSlots & ConflictBits shape).
      if (S.OccupiedSlots & Alt.FieldSlots)
        continue;
      // Unit conflict — a SECOND axis, not implied by the slot check. Two
      // alternatives at different entry positions can name the same unit
      // (ADD32_P30_ALU0 and ADD32_P31_ALU0), and the hardware has one of each
      // unit. Trying the next alternative rather than rejecting the
      // instruction is the point: this is what makes "put it wherever its unit
      // is still free" the placement rule.
      //
      // Alt.Units is 0 when units are not modelled — Bundle128 always, and
      // format E whenever the caller had no MCInstrInfo to read the member
      // name from — and 0 conflicts with nothing, so the axis is inert rather
      // than wrong.
      if (S.OccupiedUnits & Alt.Units)
        continue;
      // Member must share a feasible FormatID with the cycle frontier.
      const uint64_t Allowed =
          S.FeasibleFormatMask & Alt.CompatibleFormatMask;
      if (Allowed == 0)
        continue;

      const SlotBits NewOcc = S.OccupiedSlots | Alt.FieldSlots;
      const uint64_t NewMask = coveringFormatMask(Table, NewOcc, Allowed);
      if (NewMask == 0)
        continue; // no FormatDesc covers NewOcc under allowed formats

      CycleMember M;
      M.LogicalOpcode = LogicalOpc;
      M.MemberOpcode = Alt.MemberOpcode;
      M.FieldSlots = Alt.FieldSlots;
      M.Units = Alt.Units;
      S.Members.push_back(M);
      S.OccupiedSlots = NewOcc;
      S.OccupiedUnits |= Alt.Units;
      S.FeasibleFormatMask = NewMask;
      (void)Snapshot; // accepted — Snapshot discarded
      return true;
    }
  }

  // Greedy could not place it, and that is NOT the same as infeasible. The
  // walk above commits the first alternative that fits and never reconsiders,
  // so an earlier member may be sitting on the only slot or unit this one can
  // use — or, more often, may have taken a P3x slot and thereby committed the
  // whole cycle to the 3-entry format, which excludes every instruction whose
  // wide immediate only fits a 2-entry entry. Re-solve the cycle before
  // rejecting (CB-147).
  //
  // Measured on gcc-c-torture before this existed: 1961 of 2507 rejections
  // (78%) were greedy-only, i.e. an assignment did exist.
  if (!S.Members.empty() && tryReassign(S, Fmts, Table, LogicalOpc, MII))
    return true;

  // Alternatives whose FieldSlots is not exactly one position are skipped
  // above; there are none today. Pure reject.
  S = Snapshot;
  return false;
}

/// tryAdd against the product FormatDesc table (BUNDLE128_FULL only).
inline bool tryAddProduct(CycleState &S, const HaydnMCFormats &Fmts,
                          unsigned LogicalOpc,
                          const MCInstrInfo *MII = nullptr) {
  return tryAdd(S, Fmts, productFormatTable(), LogicalOpc, MII);
}

/// Probe-only product tryAdd: true iff \p LogicalOpc can be accepted without
/// mutating \p S (Bundle.canAdd / HR getHazardType shape).
inline bool canTryAddProduct(const CycleState &S, const HaydnMCFormats &Fmts,
                             unsigned LogicalOpc,
                             const MCInstrInfo *MII = nullptr) {
  CycleState Probe = S;
  return tryAddProduct(Probe, Fmts, LogicalOpc, MII);
}

//===----------------------------------------------------------------------===//
// commit — FormatID selection → BundlePlan (no setDesc)
//===----------------------------------------------------------------------===//

/// Select among rows that are still in \p FeasibleFormatMask and cover
/// \p Occupied, using Priority ranking (selectFormatByPriority strengthen).
inline const FormatDesc *
selectFeasibleFormatByPriority(ArrayRef<FormatDesc> Table, SlotBits Occupied,
                               uint64_t FeasibleFormatMask) {
  const FormatDesc *Best = nullptr;
  for (const FormatDesc &F : Table) {
    if (!(FeasibleFormatMask & formatIDBit(F.FID)))
      continue;
    if (!F.covers(Occupied))
      continue;
    if (!Best || F.Priority < Best->Priority)
      Best = &F;
  }
  return Best;
}

/// Commit \p S to a BundlePlan. Empty members + zero occupancy → stall plan
/// (still a FormatID row: product Full NOP parcel / synthetic best Priority).
/// Does **not** setDesc or stamp MIR.
inline std::optional<BundlePlan> commit(const CycleState &S,
                                        ArrayRef<FormatDesc> Table) {
  SmallVector<unsigned, 3> Logicals;
  Logicals.reserve(S.Members.size());
  for (const CycleMember &M : S.Members)
    Logicals.push_back(M.LogicalOpcode);

  const FormatDesc *F = selectFeasibleFormatByPriority(
      Table, S.OccupiedSlots, S.FeasibleFormatMask);
  if (!F) {
    // Empty stall on product: FeasibleFormatMask still Full and covers 0.
    if (S.empty() && S.OccupiedSlots == 0) {
      if (const FormatDesc *Any = selectFormatByPriority(Table, 0))
        return makePlanFromFormatDesc(*Any, /*Occupied=*/0, Logicals);
    }
    return std::nullopt;
  }
  return makePlanFromFormatDesc(*F, S.OccupiedSlots, Logicals);
}

/// Commit against the product FormatDesc table.
inline std::optional<BundlePlan> commitProduct(const CycleState &S) {
  return commit(S, productFormatTable());
}

/// Convenience: tryAdd then return whether state advanced (unit sugar).
inline bool tryAddOrFalse(CycleState &S, const HaydnMCFormats &Fmts,
                          ArrayRef<FormatDesc> Table, unsigned LogicalOpc) {
  return tryAdd(S, Fmts, Table, LogicalOpc);
}

//===----------------------------------------------------------------------===//
// computeProductResMII (pure greedy bin-pack = post-RA tryAdd depth)
//===----------------------------------------------------------------------===//
//
// AIE peer: MachinePipeliner calculateResMIIDFA walks ResourceCycle
// canReserve/reserve (AIE AIEResourceCycle → Bundle.canAdd/add;
// AIEHazardRecognizer.cpp:173-214). Haydn strengthens the SMS model to the
// same live CycleState tryAddProduct path as post-RA HR.
//
// Left-to-right greedy: open a cycle, tryAddProduct each opcode; on reject
// close the cycle and retry on a fresh product CycleState. Unplaceable
// opcodes (no PlacementAlternatives) each consume a standalone cycle.
// Logical only — no setDesc / FormatID freeze.

/// Greedy product ResMII for \p Opcodes under BUNDLE128_FULL tryAddProduct.
/// \returns number of issue cycles needed (0 if \p Opcodes empty).
inline unsigned computeProductResMII(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.empty())
    return 0;

  HaydnMCFormats Fmts;
  unsigned Cycles = 0;
  CycleState S = makeProductCycleState();
  bool CycleHasMember = false;

  for (unsigned Opc : Opcodes) {
    if (canTryAddProduct(S, Fmts, Opc)) {
      (void)tryAddProduct(S, Fmts, Opc);
      CycleHasMember = true;
      continue;
    }

    // Current cycle cannot accept Opc — close it if non-empty and retry.
    if (CycleHasMember) {
      ++Cycles;
      S = makeProductCycleState();
      CycleHasMember = false;
    }

    if (tryAddProduct(S, Fmts, Opc)) {
      CycleHasMember = true;
      continue;
    }

    // No PlacementAlternatives / never placeable under product Full: one
    // standalone cycle (Bundle empty-escape peer for SMS ResMII accounting).
    ++Cycles;
    S = makeProductCycleState();
    CycleHasMember = false;
  }

  if (CycleHasMember)
    ++Cycles;
  return Cycles;
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H
