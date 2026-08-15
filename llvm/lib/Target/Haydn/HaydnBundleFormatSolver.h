//===- HaydnBundleFormatSolver.h - Pure CycleState tryAdd/commit -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pure CycleState tryAdd/commit solver + Format E row-frontier mask:
//
//   CycleState          members + OccupiedSlots + FeasibleFormatMask
//   CycleCandidateSet   private nondominated SmallVector<CycleState>
//   exactTryAddProduct  expand all alts; prune dominated partial matchings
//   tryAddProduct       single-state preferred collapse (S2→S1→S0 tie-break)
//   commitProduct       Format E row + completion → BundlePlan (no setDesc)
//   productFeasibleFormatMask — Pre-RA / SMS occupancy → row-bit mask
//
// AIE peers (port structure; do not invent a parallel packing theory):
//
//   * AIEBundle.h:62-105  canAdd — format-available / slot conflict
//   * AIEBundle.h:110-145 add    — commits OccupiedSlots / SlotMap
//   * AIEBundle.h:150-156 getFormatOrNull — format-from-occupancy
//     (Haydn: exact PacketFormats cover; transitional occupancy resolves via
//     FeasibleFormatMask + productVLIWFormatForRow, not empty-cover rep)
//   * AIEHazardRecognizer.cpp:173-214 ResourceCycle —
//       getAlternateInstsOpcode + any_of / first canAdd AltOpcode
//   * AIEFormat.cpp:18-27 PacketFormats::getFormat first-covering
//
// Product identity is Format E (E96TwoEntry | E96ThreeEntry row mask).
// Product PacketFormats are Format E rows (BUNDLE_E96_*). EncodedBytes and
// durable MIR identity come from the registry via commitProduct / BundlePlan.
//
// : first-fit freeze (single S2→S1→S0 alt commit) is incomplete — a legal
// pack can dead-end when an early multi-slot op claims a scarce field. Bundle /
// HR / SMS / asm / verifier wire through exactTryAddProduct on a private
// nondominated CycleCandidateSet (plan §3.2). tryAddProduct remains the
// single-state preferred-collapse helper (materialize). Product is E2/E3 only.
// HR/ResourceCycle skip only true zero-resource meta (not blanket isPseudo).
// Post-RA pack reconstruction uses MachineBundle::isBundlePackSkippableOpcode
// (meta + alts; no-alt expand residuals remain skippable until residual
// expansion is guaranteed).
//
// Placement authority is PlacementAlternative + exact tryAdd. getLegalSlots is
// alts-derived (OR of non-zero sparse indices) for encode helpers only;
// Bundle/HR no-alt path does not consult it.
//
// Pre-RA / SMS expose a feasible Format E row *frontier* (bit mask) without
// freezing a row or setDesc (plan §7.1). Product mask is always
// ProductFormatMask when the generated row covers occupancy.
// productFeasibleFormatMask(Packets, Occupied) is the occupancy-only rebuild
// for Pre-RA adapters.
//
// SMS HaydnResourceCycle holds a *live* CycleCandidateSet (peer of post-RA HR
// CurrentCycleCandidates / commitPlacementForEmit exactTryAddProduct).
// FeasibleFormatMask and member field choices accumulate via exact expand —
// not OccupiedSlots-only rebuild. computeProductResMII greedy bin-packs with
// the same pure exactTryAddProduct depth (plan §7.1 Diff ResMII vs emit).
//
// SMS-RESMII: computeExhaustiveProductResMII is the bounded exact set
// oracle (partition into ≤3-member packable cycles). Compare greedy/DFA
// sequential ResMII against it; overestimate that worsens II blocks
// format-dependent SMS qualification (plan §2.5 / §5.2). Cannot replace
// ResourceManager::calculateResMIIDFA — this is a diagnostic/gate oracle.
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
#include "HaydnFormatERecords.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
/// Process-wide default FormatInterface. Haydn has one concrete variant.
/// AIE peer: AIE2InstrInfo.cpp:59-64 holds `const AIE2MCFormats AIE2Formats`
/// on TII::FormatInterface (`AIEBaseInstrInfo.h:885 getFormatInterface`).
/// Bundle/HR/SMS must pass the held HaydnBaseMCFormats*; pin/probe helpers
/// without a TII use this singleton instead of a local HaydnMCFormats.
inline const HaydnMCFormats &haydnDefaultMCFormats() {
  static const HaydnMCFormats Instance;
  return Instance;
}

// MCInstrInfo name tables (HaydnMCTargetDesc.cpp GET_INSTRINFO_MC_DESC).
extern const unsigned HaydnInstrNameIndices[];
extern const char HaydnInstrNameData[];

namespace haydn {
namespace bundle {

/// Opcode mnemonic from the generated MC name tables (no TII required).
inline StringRef haydnOpcodeName(unsigned Opcode) {
  return StringRef(&HaydnInstrNameData[HaydnInstrNameIndices[Opcode]]);
}

/// True when \p Opc is a generated Format E member at encoded entry \p EntryIdx.
/// Mode marker is `_E2_`/`_E3_`; entry is the following `_E0_`/`_E1_`/`_E2_`.
inline bool formatEMemberOccupiesEntry(unsigned Opc, unsigned EntryIdx) {
  const StringRef Name = haydnOpcodeName(Opc);
  switch (EntryIdx) {
  case 0:
    return Name.contains("_E2_E0_") || Name.contains("_E3_E0_");
  case 1:
    return Name.contains("_E2_E1_") || Name.contains("_E3_E1_");
  case 2:
    return Name.contains("_E2_E2_") || Name.contains("_E3_E2_");
  default:
    return false;
  }
}

/// True when \p Opcodes can be assigned injective Format E units under \p Mode
/// (0 = E2, 1 = E3). Format E units ≠ encoded entry identity: two LOADSTORE0
/// stores (D_SW_L_WITH_IMM, S_SB_WITH_IMM / ST8, ST32, ST64, …) cannot share a
/// cycle even when residual FieldSlots S0 vs S2 are free.
bool opcodesHaveFormatEUnitCoverForMode(ArrayRef<unsigned> Opcodes,
                                               uint8_t Mode);


/// True when \p Opcodes have injective Format E units under E2 or E3.
/// One mechanism used by Bundle.canAdd, exactTryAddProduct, commitExact, and
/// verifyCommittedBundle (AIEBundle.h:62-105 canAdd overlay).
bool opcodesHaveFormatEUnitCover(ArrayRef<unsigned> Opcodes);


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
};

/// Transactional packing state for one architectural issue cycle.
/// Pure data — no MachineInstr / AltDesc / MCFlags side effects.
struct CycleState {
  SmallVector<CycleMember, 3> Members;
  SlotBits OccupiedSlots = 0;
  /// Intersection of Format E rows still covering OccupiedSlots and compatible
  /// with every accepted member (bit = formatRowBit(BundleFormatRowID)).
  uint64_t FeasibleFormatMask = ProductFormatMask;

  bool empty() const { return Members.empty(); }
  unsigned memberCount() const {
    return static_cast<unsigned>(Members.size());
  }
};

/// Product CycleState seeded from PacketFormats coverage.
/// FeasibleFormatMask = ProductFormatMask (E2|E3) when any row covers empty.
inline CycleState makeProductCycleState(const PacketFormats &Packets) {
  CycleState S;
  S.FeasibleFormatMask =
      productCovers(Packets, /*Occupied=*/0) ? ProductFormatMask : 0;
  assert(S.FeasibleFormatMask == ProductFormatMask &&
         "PacketFormats missing product row coverage for empty occupancy");
  return S;
}

/// Product CycleState via a temporary formats view (Bundle/SMS conveniences).
inline CycleState makeProductCycleState() {
  return makeProductCycleState(haydnDefaultMCFormats().getPacketFormats());
}

//===----------------------------------------------------------------------===//
// tryAdd — transactional pure accept + exact candidate set
//===----------------------------------------------------------------------===//

/// Product covering mask from PacketFormats coverage (E2|E3 frontier).
/// AIE peer: PacketFormats::getFormat first-covering (AIEFormat.cpp:18-27)
/// strengthened to a row *mask*. Rows that cannot hold the occupancy are
/// stripped: Format E E2 has two entries, E3 has three. Residual
/// PlacementAlternative FieldSlots are one issue bit per member (SLOT0/1/2),
/// so popcount(NewOcc) is the real member count on that path.
inline uint64_t coveringFormatMaskFromPackets(const PacketFormats &Packets,
                                              SlotBits NewOcc,
                                              uint64_t AllowedMask) {
  if (!(AllowedMask & ProductFormatMask))
    return 0;
  if (!productCovers(Packets, NewOcc))
    return 0;
  uint64_t Mask = AllowedMask & ProductFormatMask;
  // Entry capacity: refuse E2 when three members share a cycle, and refuse
  // every product row beyond three.
  const unsigned OccCount = llvm::popcount(NewOcc);
  if (OccCount > 2)
    Mask &= ~formatRowBit(BundleFormatRowID::E96TwoEntry);
  if (OccCount > 3)
    Mask &= ~formatRowBit(BundleFormatRowID::E96ThreeEntry);
  return Mask;
}

/// Product Format E row frontier for Pre-RA / SMS from generated
/// PacketFormats coverage (BUNDLE_E96_*).
///
/// AIE returns a single VLIWFormat* from occupancy (AIEBundle.h:150-156
/// getFormatOrNull → PacketFormats::getFormat). Haydn keeps a *mask* of still-
/// feasible Format E rows until post-RA freeze (plan §7.1): never setDesc /
/// never freeze a row on this path. Empty and any row-covering occupancy
/// both yield ProductFormatMask.
inline uint64_t productFeasibleFormatMask(const PacketFormats &Packets,
                                          SlotBits Occupied = 0) {
  return coveringFormatMaskFromPackets(Packets, Occupied, ProductFormatMask);
}

/// Convenience: product frontier via temporary formats view.
inline uint64_t productFeasibleFormatMask(SlotBits Occupied = 0) {
  return productFeasibleFormatMask(haydnDefaultMCFormats().getPacketFormats(), Occupied);
}

/// Rebuild a product CycleState from occupied slots only (no member history).
/// Occupancy-only frontier rebuild (tests / scoreboard probes). Live Bundle /
/// HR / SMS retain CycleCandidateSet member history so rematching is
/// possible — do not rebuild-from-occupied on those paths.
inline CycleState
makeProductCycleStateFromOccupied(const PacketFormats &Packets,
                                  SlotBits Occupied) {
  CycleState S = makeProductCycleState(Packets);
  S.OccupiedSlots = Occupied;
  S.FeasibleFormatMask = productFeasibleFormatMask(Packets, Occupied);
  return S;
}

inline CycleState makeProductCycleStateFromOccupied(SlotBits Occupied) {
  return makeProductCycleStateFromOccupied(haydnDefaultMCFormats().getPacketFormats(), Occupied);
}

/// Map a single Haydn::SLOT* FieldSlots bit to issue-slot index 0/1/2.
/// \returns nullopt if \p Field is not exactly one of SLOT0/1/2.
inline std::optional<unsigned> fieldSlotsToIndex(SlotBits Field) {
  for (unsigned S = 0; S < Haydn::ISSUE_SLOT_COUNT; ++S) {
    if (Field == (SlotBits(1) << S))
      return S;
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// — nondominated CycleCandidateSet (exact partial matchings)
//===----------------------------------------------------------------------===//
//
// Plan §3.2: a Format E row mask is insufficient — two partial assignments
// under the same row can leave different fields free. Retain nondominated
// complete partial matchings as a private SmallVector of CycleState (no public
// CycleFrontier class). Bound is rows × field alts (E2/E3 only: tiny).

/// Private bounded set of nondominated packing states for one issue cycle.
/// Bundle / HR / SMS hold this; not a durable MIR side-map.
using CycleCandidateSet = SmallVector<CycleState, 8>;

/// Architectural issue-slot universe for dominance (S0|S1|S2).
inline constexpr SlotBits kIssueSlotUniverse =
    static_cast<SlotBits>(Haydn::SLOT_ALL);

/// True iff every future pure slot/format packing legal under \p B is also
/// legal under \p A (A is at least as flexible as B).
/// Occupied(A) ⊆ Occupied(B) and Feasible(A) ⊇ Feasible(B).
inline bool packingDominates(const CycleState &A, const CycleState &B) {
  if ((A.OccupiedSlots & ~B.OccupiedSlots) != 0)
    return false;
  if ((B.FeasibleFormatMask & ~A.FeasibleFormatMask) != 0)
    return false;
  return true;
}

inline bool packingEquivalent(const CycleState &A, const CycleState &B) {
  return A.OccupiedSlots == B.OccupiedSlots &&
         A.FeasibleFormatMask == B.FeasibleFormatMask;
}

/// Deterministic materialize preference among packing-equivalent states:
/// lexicographic higher FieldSlots bit-index per member in order (S2→S1→S0
/// first-fit shape when progressive choices remain free).
/// \returns true if \p A is strictly preferred over \p B.
inline bool isPreferredCandidate(const CycleState &A, const CycleState &B) {
  const unsigned N = std::min(A.memberCount(), B.memberCount());
  for (unsigned I = 0; I < N; ++I) {
    const auto IA = fieldSlotsToIndex(A.Members[I].FieldSlots);
    const auto IB = fieldSlotsToIndex(B.Members[I].FieldSlots);
    const int SA = IA ? static_cast<int>(*IA) : -1;
    const int SB = IB ? static_cast<int>(*IB) : -1;
    if (SA != SB)
      return SA > SB;
    if (A.Members[I].MemberOpcode != B.Members[I].MemberOpcode)
      return A.Members[I].MemberOpcode < B.Members[I].MemberOpcode;
  }
  if (A.memberCount() != B.memberCount())
    return A.memberCount() > B.memberCount();
  // Stable: prefer higher OccupiedSlots bit pattern as last resort.
  return A.OccupiedSlots > B.OccupiedSlots;
}

/// Insert \p S into \p Cands, dropping packing-dominated states. Packing-
/// equivalent states keep only the preferred materialize representative.
inline void insertNondominatedCandidate(CycleCandidateSet &Cands,
                                        CycleState S) {
  for (const CycleState &E : Cands) {
    if (!packingDominates(E, S))
      continue;
    if (packingEquivalent(E, S)) {
      // Equivalent occupancy/frontier — keep preferred only.
      if (!isPreferredCandidate(S, E))
        return;
      continue;
    }
    // E strictly dominates S.
    return;
  }

  // Drop states dominated by S (and packing-equivalent that S prefers).
  CycleCandidateSet Kept;
  Kept.reserve(Cands.size() + 1);
  for (CycleState &E : Cands) {
    if (packingDominates(S, E)) {
      if (packingEquivalent(S, E))
        continue; // S preferred (or equal — we keep S)
      if (!packingDominates(E, S))
        continue; // S strictly dominates E
    }
    Kept.push_back(std::move(E));
  }
  Kept.push_back(std::move(S));
  Cands = std::move(Kept);
}

/// Seed product candidate set: one empty Full-covering CycleState.
inline CycleCandidateSet
makeProductCandidateSet(const PacketFormats &Packets) {
  CycleCandidateSet C;
  C.push_back(makeProductCycleState(Packets));
  return C;
}

inline CycleCandidateSet makeProductCandidateSet() {
  return makeProductCandidateSet(haydnDefaultMCFormats().getPacketFormats());
}

/// Member opcodes (parallel to \p LogicalOpcodes) bound onto ONE settled row
/// (\p Mode: 0 = E2, 1 = E3) with entry/unit injectivity — the shared
/// assignFormatEMemberEntries DFS on the golden member records. Empty on
/// failure. Out-of-line: needs the per-TU member-opcode table and the shared
/// MCInstrInfo for logical names.
SmallVector<unsigned, 3>
assignMemberOpcodesForSettledRow(ArrayRef<unsigned> LogicalOpcodes,
                                 uint8_t Mode);

/// Golden register-file port budgets over one candidate cycle (4R2W GPR,
/// 7R3W DR, 2R2W AR, 2R1W SFR; HaydnPortModel counters). Out-of-line so the
/// header consumers do not inherit the port model's enum includes.
bool cycleMembersRespectPortBudgets(ArrayRef<class MachineInstr *> Instrs);

/// Preferred representative among \p Cands (non-empty). Deterministic
/// S2→S1→S0 materialize / SlotMap / AltDesc selection.
inline const CycleState &
selectPreferredCandidate(const CycleCandidateSet &Cands) {
  assert(!Cands.empty() && "empty CycleCandidateSet");
  const CycleState *Best = &Cands.front();
  for (unsigned I = 1, E = Cands.size(); I != E; ++I)
    if (isPreferredCandidate(Cands[I], *Best))
      Best = &Cands[I];
  return *Best;
}

/// Apply one legal alt onto a copy of \p S; nullopt if conflict / no cover.
/// CompatibleFormatMask already carries residual E2-only / E3-only clamps
/// (residualAltCompatibleFormatMask); intersecting with FeasibleFormatMask
/// keeps E3-only singletons from surviving under an E2-only frontier.
/// Overlay: Format E unit injectivity (AIEBundle.h:62-105 canAdd is slot +
/// isFormatAvailable; Haydn units ≠ encoded entry identity).
template <typename CoverFn>
inline std::optional<CycleState>
tryApplyAlt(const CycleState &S, unsigned LogicalOpc,
            const PlacementAlternative &Alt, CoverFn CoverMask) {
  if (Alt.FieldSlots == 0)
    return std::nullopt;
  if (S.OccupiedSlots & Alt.FieldSlots)
    return std::nullopt;
  SmallVector<unsigned, 4> UnitOps;
  UnitOps.reserve(S.Members.size() + 1);
  for (const CycleMember &Mem : S.Members)
    UnitOps.push_back(Mem.LogicalOpcode);
  UnitOps.push_back(LogicalOpc);
  if (!opcodesHaveFormatEUnitCover(UnitOps))
    return std::nullopt;
  const uint64_t Allowed = S.FeasibleFormatMask & Alt.CompatibleFormatMask;
  if (Allowed == 0)
    return std::nullopt;
  const SlotBits NewOcc = S.OccupiedSlots | Alt.FieldSlots;
  const uint64_t NewMask = CoverMask(NewOcc, Allowed);
  if (NewMask == 0)
    return std::nullopt;

  CycleState N = S;
  CycleMember M;
  M.LogicalOpcode = LogicalOpc;
  M.MemberOpcode = Alt.MemberOpcode;
  M.FieldSlots = Alt.FieldSlots;
  N.Members.push_back(M);
  N.OccupiedSlots = NewOcc;
  N.FeasibleFormatMask = NewMask;
  return N;
}

/// Exact expand: every candidate × every PlacementAlternative under \p CoverMask;
/// replace \p Cands with the nondominated successor set.
/// \returns true and mutates \p Cands on accept; false leaves \p Cands unchanged.
template <typename CoverFn>
inline bool exactTryAddWithCover(CycleCandidateSet &Cands,
                                 const HaydnMCFormats &Fmts,
                                 unsigned LogicalOpc, CoverFn CoverMask) {
  if (Cands.empty())
    return false;

  SmallVector<PlacementAlternative, 4> Alts;
  if (!enumeratePlacementAlternatives(Fmts, LogicalOpc, Alts))
    return false;
  CycleCandidateSet Next;
  for (const CycleState &S : Cands) {
    for (const PlacementAlternative &Alt : Alts) {
      if (auto N = tryApplyAlt(S, LogicalOpc, Alt, CoverMask))
        insertNondominatedCandidate(Next, std::move(*N));
    }
  }
  if (Next.empty())
    return false;
  Cands = std::move(Next);
  return true;
}

/// Probe-only exact expand: true iff some candidate accepts \p LogicalOpc.
template <typename CoverFn>
inline bool canExactTryAddWithCover(ArrayRef<CycleState> Cands,
                                    const HaydnMCFormats &Fmts,
                                    unsigned LogicalOpc, CoverFn CoverMask) {
  if (Cands.empty())
    return false;
  SmallVector<PlacementAlternative, 4> Alts;
  if (!enumeratePlacementAlternatives(Fmts, LogicalOpc, Alts))
    return false;
  for (const CycleState &S : Cands) {
    for (const PlacementAlternative &Alt : Alts) {
      if (tryApplyAlt(S, LogicalOpc, Alt, CoverMask))
        return true;
    }
  }
  return false;
}

/// Exact product expand (Format E PacketFormats authority). Production
/// Bundle / HR / SMS / verifier path.
inline bool exactTryAddProduct(CycleCandidateSet &Cands,
                               const HaydnMCFormats &Fmts,
                               unsigned LogicalOpc) {
  const PacketFormats &Packets = Fmts.getPacketFormats();
  return exactTryAddWithCover(
      Cands, Fmts, LogicalOpc,
      [&](SlotBits NewOcc, uint64_t Allowed) {
        return coveringFormatMaskFromPackets(Packets, NewOcc, Allowed);
      });
}

/// Probe-only exact product expand (does not mutate \p Cands).
inline bool canExactTryAddProduct(ArrayRef<CycleState> Cands,
                                  const HaydnMCFormats &Fmts,
                                  unsigned LogicalOpc) {
  const PacketFormats &Packets = Fmts.getPacketFormats();
  return canExactTryAddWithCover(
      Cands, Fmts, LogicalOpc,
      [&](SlotBits NewOcc, uint64_t Allowed) {
        return coveringFormatMaskFromPackets(Packets, NewOcc, Allowed);
      });
}

/// True iff sequential exact product packing accepts every opcode in order.
bool exactCanPackProductSequence(const HaydnMCFormats &Fmts,
                                        ArrayRef<unsigned> Opcodes);


/// Exhaustive <=3-member set oracle: true iff some permutation of \p Opcodes
/// packs under exact product matching (exit criteria small-oracle).
bool exactCanPackProductSet(const HaydnMCFormats &Fmts,
                                   ArrayRef<unsigned> Opcodes);


/// First free field assignment for \p LogicalOpc that keeps a covering format.
/// Prefer higher slots first (S2 → S1 → S0) — single-state preferred collapse.
/// Production multi-step packing uses exactTryAddProduct.
///
/// Port of AIE alt try (AIEHazardRecognizer.cpp:174-214) strengthened: enumerate
/// all legal alts on a singleton candidate set, keep nondominated successors,
/// then collapse to selectPreferredCandidate (S2→S1→S0 materialize order).
///
/// \returns true and mutates \p S on accept; false leaves \p S unchanged.
template <typename CoverFn>
inline bool tryAddWithCover(CycleState &S, const HaydnMCFormats &Fmts,
                            unsigned LogicalOpc, CoverFn CoverMask) {
  CycleCandidateSet Cands;
  Cands.push_back(S);
  if (!exactTryAddWithCover(Cands, Fmts, LogicalOpc, CoverMask))
    return false;
  S = selectPreferredCandidate(Cands);
  return true;
}

/// Single-state product tryAdd: exact one-step expand + preferred collapse.
/// Sequential multi-add on one CycleState can still dead-end (preferred freezes
/// the representative); Bundle/HR/SMS use exactTryAddProduct on a candidate set.
bool tryAddProduct(CycleState &S, const HaydnMCFormats &Fmts,
                          unsigned LogicalOpc);


/// Probe-only single-state product tryAdd (preferred-collapse probe).
bool canTryAddProduct(const CycleState &S, const HaydnMCFormats &Fmts,
                             unsigned LogicalOpc);


//===----------------------------------------------------------------------===//
// commit — Format E row selection → BundlePlan (no setDesc)
//===----------------------------------------------------------------------===//

/// Commit against product Format E: planFromPacketFormats supplies registry
/// EncodedBytes; row selection uses the surviving FeasibleFormatMask so an
/// E3-only frontier is never collapsed to E2 by member count alone.
/// residualAltCompatibleFormatMask stamps E3-only alts as E96ThreeEntry so
/// FeasibleFormatMask after tryApply is sole-row E3 for LOG2/EXP2/… .
/// Does **not** setDesc or stamp MIR.
inline std::optional<BundlePlan>
commitProduct(const CycleState &S, const PacketFormats &Packets) {
  SmallVector<unsigned, 3> Logicals;
  Logicals.reserve(S.Members.size());
  for (const CycleMember &M : S.Members)
    Logicals.push_back(M.LogicalOpcode);

  if (!(S.FeasibleFormatMask & ProductFormatMask))
    return std::nullopt;
  // Stall / non-empty: transitional composite must cover OccupiedSlots.
  return planFromPacketFormats(Packets, S.OccupiedSlots, Logicals,
                               S.FeasibleFormatMask);
}

/// Convenience commit via temporary formats view.
inline std::optional<BundlePlan> commitProduct(const CycleState &S) {
  return commitProduct(S, haydnDefaultMCFormats().getPacketFormats());
}

//===----------------------------------------------------------------------===//
// computeProductResMII (pure greedy bin-pack = post-RA tryAdd depth)
//===----------------------------------------------------------------------===//
//
// AIE peer: MachinePipeliner calculateResMIIDFA walks ResourceCycle
// canReserve/reserve (AIE AIEResourceCycle → Bundle.canAdd/add;
// AIEHazardRecognizer.cpp:173-214). Haydn strengthens the SMS model to the
// same live CycleCandidateSet exactTryAddProduct path as post-RA HR.
//
// Left-to-right greedy: open a cycle, exactTryAddProduct each opcode; on
// reject close the cycle and retry on a fresh product candidate set.
// Unplaceable opcodes (no PlacementAlternatives) each consume a standalone
// cycle. Logical only — no setDesc / no row freeze.

/// Greedy product ResMII for \p Opcodes under generated Format E
/// exactTryAddProduct (PacketFormats authority, nondominated set).
/// \returns number of issue cycles needed (0 if \p Opcodes empty).
unsigned computeProductResMII(ArrayRef<unsigned> Opcodes);


//===----------------------------------------------------------------------===//
// SMS-RESMII — exhaustive ≤3 format ResMII oracle vs greedy / preferred
//===----------------------------------------------------------------------===//
//
// Plan §2.5 / §5.2 / exit #9: compare left-to-right greedy ResMII (the same
// exactTryAddProduct depth ResourceCycle / calculateResMIIDFA walks) against
// an exact partition oracle over ≤3-member cycles. Preferred-collapse
// sequential packing is retained as a *weaker* diagnostic baseline that
// deliberately overestimates on first-fit dead-ends (ADD32+2×ADD64) so the
// oracle can detect overestimate when the live SMS path regresses.
//
// Exhaustive bound: N ≤ MaxExhaustiveProductResMIIOps (12). Larger bodies
// fall back to greedy (upper bound only — not a lower-bound proof).

/// Max multiset size for exact subset-partition ResMII (2^12 DP + 3^N submasks).
constexpr unsigned MaxExhaustiveProductResMIIOps = 12;

/// True iff \p Opcodes (size ≤ ISSUE_SLOT_COUNT) form one legal product cycle
/// under exact matching, counting no-alt singletons as standalone-legal
/// (Bundle empty-escape peer — same ResMII accounting as computeProductResMII).
bool exactCanFormOneProductCycle(const HaydnMCFormats &Fmts,
                                        ArrayRef<unsigned> Opcodes);


/// Left-to-right *preferred-collapse* product ResMII (tryAddProduct / single
/// CycleState). Weaker than computeProductResMII (exact candidate set). Used
/// only to prove SMS-RESMII can detect overestimate on first-fit dead-ends.
unsigned computePreferredProductResMII(ArrayRef<unsigned> Opcodes);


/// Exhaustive product ResMII: minimum issue cycles to pack \p Opcodes under
/// generated Format E exact format matching, partitioning into cycles of
/// at most ISSUE_SLOT_COUNT members (SMS-RESMII exact oracle).
///
/// For N ≤ MaxExhaustiveProductResMIIOps: exact subset DP.
/// For larger N: returns computeProductResMII (greedy upper bound; not exact).
///
/// \returns 0 if empty; otherwise ≥ 1.
unsigned computeExhaustiveProductResMII(ArrayRef<unsigned> Opcodes);


/// Greedy (exactTryAdd) ResMII minus exhaustive oracle. Positive → overestimate
/// (SMS-RESMII fail for that multiset). Zero on product path when exact packing
/// is order-optimal for the body. Negative should not occur (greedy ≥ exact).
int productResMIIOverestimate(ArrayRef<unsigned> Opcodes);


/// Preferred-collapse overestimate vs exhaustive oracle (diagnostic; proves the
/// gate detects first-fit dead-end inflation that exact matching fixes).
int preferredProductResMIIOverestimate(ArrayRef<unsigned> Opcodes);


/// True when format-dependent SMS product qualification must fail closed for
/// this opcode multiset: greedy exactTryAdd overestimates the exhaustive ≤3
/// format oracle on a body inside the exact bound (N ≤
/// MaxExhaustiveProductResMIIOps). Empty and N>bound never fail here — larger
/// bodies fall back to greedy so Over is always 0 (no false reject on an
/// inexact oracle). Pure predicate; no MIR mutation. SMS analyzeLoop (sibling)
/// may call this; pre-RA exposes the same surface via PreRASchedStrategy.
bool productResMIIFailsQualification(ArrayRef<unsigned> Opcodes);


} // namespace bundle
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLEFORMATSOLVER_H
