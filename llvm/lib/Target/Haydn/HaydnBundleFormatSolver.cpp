//===- HaydnBundleFormatSolver.cpp - Pure CycleState tryAdd/commit --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Out-of-line non-template Format E solver. Templates stay in the header.
// Peer: AIEBundle.h canAdd/add; AIE2InstrInfo.cpp:59-64 FormatInterface.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include <algorithm>
#include <cassert>
#include <string>

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace llvm {
namespace haydn {
namespace bundle {

/// True when \p Opcodes can be assigned injective Format E units under \p Mode
/// (0 = E2, 1 = E3). Format E units ≠ encoded entry identity: two LOADSTORE0
/// stores (D_SW_L_WITH_IMM, S_SB_WITH_IMM / ST8, ST32, ST64, …) cannot share a
/// cycle even when residual FieldSlots S0 vs S2 are free.
bool opcodesHaveFormatEUnitCoverForMode(ArrayRef<unsigned> Opcodes,
                                               uint8_t Mode) {
  if (Opcodes.size() < 2)
    return true;
  SmallVector<std::string, 3> Logs;
  Logs.reserve(Opcodes.size());
  for (unsigned Opc : Opcodes)
    Logs.push_back(format_e::peelLogicalOpcodeName(haydnOpcodeName(Opc)));
  return format_e::logicalsHaveUnitCoverForMode(Logs, Mode);
}
/// True when \p Opcodes have injective Format E units under E2 or E3.
/// One mechanism used by Bundle.canAdd, exactTryAddProduct, commitExact, and
/// verifyCommittedBundle (AIEBundle.h:62-105 canAdd overlay).
bool opcodesHaveFormatEUnitCover(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.size() < 2)
    return true;
  return opcodesHaveFormatEUnitCoverForMode(Opcodes, /*Mode=*/0) ||
         opcodesHaveFormatEUnitCoverForMode(Opcodes, /*Mode=*/1);
}
/// True iff sequential exact product packing accepts every opcode in order.
bool exactCanPackProductSequence(const HaydnMCFormats &Fmts,
                                        ArrayRef<unsigned> Opcodes) {
  CycleCandidateSet C =
      makeProductCandidateSet(Fmts.getPacketFormats());
  for (unsigned Opc : Opcodes) {
    if (!exactTryAddProduct(C, Fmts, Opc))
      return false;
  }
  return true;
}
/// Exhaustive <=3-member set oracle: true iff some permutation of \p Opcodes
/// packs under exact product matching (exit criteria small-oracle).
bool exactCanPackProductSet(const HaydnMCFormats &Fmts,
                                   ArrayRef<unsigned> Opcodes) {
  const unsigned N = Opcodes.size();
  if (N == 0)
    return true;
  if (N > Haydn::ISSUE_SLOT_COUNT)
    return false;
  // Heap's algorithm over a mutable copy (N <= 3).
  SmallVector<unsigned, 3> P(Opcodes.begin(), Opcodes.end());
  if (exactCanPackProductSequence(Fmts, P))
    return true;
  // Generate remaining permutations.
  SmallVector<unsigned, 3> C(N, 0);
  unsigned I = 0;
  while (I < N) {
    if (C[I] < I) {
      if ((I & 1) == 0)
        std::swap(P[0], P[I]);
      else
        std::swap(P[C[I]], P[I]);
      if (exactCanPackProductSequence(Fmts, P))
        return true;
      ++C[I];
      I = 0;
    } else {
      C[I] = 0;
      ++I;
    }
  }
  return false;
}
/// Single-state product tryAdd: exact one-step expand + preferred collapse.
/// Sequential multi-add on one CycleState can still dead-end (preferred freezes
/// the representative); Bundle/HR/SMS use exactTryAddProduct on a candidate set.
bool tryAddProduct(CycleState &S, const HaydnMCFormats &Fmts,
                          unsigned LogicalOpc) {
  const PacketFormats &Packets = Fmts.getPacketFormats();
  return tryAddWithCover(
      S, Fmts, LogicalOpc,
      [&](SlotBits NewOcc, uint64_t Allowed) {
        return coveringFormatMaskFromPackets(Packets, NewOcc, Allowed);
      });
}
/// Probe-only single-state product tryAdd (preferred-collapse probe).
bool canTryAddProduct(const CycleState &S, const HaydnMCFormats &Fmts,
                             unsigned LogicalOpc) {
  CycleCandidateSet Cands;
  Cands.push_back(S);
  return canExactTryAddProduct(Cands, Fmts, LogicalOpc);
}
/// Greedy product ResMII for \p Opcodes under generated Format E
/// exactTryAddProduct (PacketFormats authority, nondominated set).
/// \returns number of issue cycles needed (0 if \p Opcodes empty).
unsigned computeProductResMII(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.empty())
    return 0;

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  const PacketFormats &Packets = Fmts.getPacketFormats();
  unsigned Cycles = 0;
  CycleCandidateSet Cands = makeProductCandidateSet(Packets);
  bool CycleHasMember = false;

  for (unsigned Opc : Opcodes) {
    if (canExactTryAddProduct(Cands, Fmts, Opc)) {
      (void)exactTryAddProduct(Cands, Fmts, Opc);
      CycleHasMember = true;
      continue;
    }

    // Current cycle cannot accept Opc — close it if non-empty and retry.
    if (CycleHasMember) {
      ++Cycles;
      Cands = makeProductCandidateSet(Packets);
      CycleHasMember = false;
    }

    if (exactTryAddProduct(Cands, Fmts, Opc)) {
      CycleHasMember = true;
      continue;
    }

    // No PlacementAlternatives / never placeable under product Full: one
    // standalone cycle (Bundle empty-escape peer for SMS ResMII accounting).
    ++Cycles;
    Cands = makeProductCandidateSet(Packets);
    CycleHasMember = false;
  }

  if (CycleHasMember)
    ++Cycles;
  return Cycles;
}
/// True iff \p Opcodes (size ≤ ISSUE_SLOT_COUNT) form one legal product cycle
/// under exact matching, counting no-alt singletons as standalone-legal
/// (Bundle empty-escape peer — same ResMII accounting as computeProductResMII).
bool exactCanFormOneProductCycle(const HaydnMCFormats &Fmts,
                                        ArrayRef<unsigned> Opcodes) {
  const unsigned N = Opcodes.size();
  if (N == 0)
    return true;
  if (N > Haydn::ISSUE_SLOT_COUNT)
    return false;
  // Singleton always costs one standalone cycle (alts or empty-escape).
  if (N == 1)
    return true;
  // Multi-member: every opcode needs PlacementAlternatives; then set-oracle.
  for (unsigned Opc : Opcodes) {
    if (!hasPlacementAlternatives(Fmts, Opc))
      return false;
  }
  return exactCanPackProductSet(Fmts, Opcodes);
}
/// Left-to-right *preferred-collapse* product ResMII (tryAddProduct / single
/// CycleState). Weaker than computeProductResMII (exact candidate set). Used
/// only to prove SMS-RESMII can detect overestimate on first-fit dead-ends.
unsigned computePreferredProductResMII(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.empty())
    return 0;

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  unsigned Cycles = 0;
  CycleState S = makeProductCycleState(Fmts.getPacketFormats());
  bool CycleHasMember = false;

  for (unsigned Opc : Opcodes) {
    if (canTryAddProduct(S, Fmts, Opc)) {
      (void)tryAddProduct(S, Fmts, Opc);
      CycleHasMember = true;
      continue;
    }
    if (CycleHasMember) {
      ++Cycles;
      S = makeProductCycleState(Fmts.getPacketFormats());
      CycleHasMember = false;
    }
    if (tryAddProduct(S, Fmts, Opc)) {
      CycleHasMember = true;
      continue;
    }
    // No alts / unplaceable under preferred collapse: standalone cycle.
    ++Cycles;
    S = makeProductCycleState(Fmts.getPacketFormats());
    CycleHasMember = false;
  }
  if (CycleHasMember)
    ++Cycles;
  return Cycles;
}
/// Exhaustive product ResMII: minimum issue cycles to pack \p Opcodes under
/// generated Format E exact format matching, partitioning into cycles of
/// at most ISSUE_SLOT_COUNT members (SMS-RESMII exact oracle).
///
/// For N ≤ MaxExhaustiveProductResMIIOps: exact subset DP.
/// For larger N: returns computeProductResMII (greedy upper bound; not exact).
///
/// \returns 0 if empty; otherwise ≥ 1.
unsigned computeExhaustiveProductResMII(ArrayRef<unsigned> Opcodes) {
  const unsigned N = Opcodes.size();
  if (N == 0)
    return 0;
  if (N == 1)
    return 1;
  if (N > MaxExhaustiveProductResMIIOps)
    return computeProductResMII(Opcodes);

  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();
  const unsigned Full = 1u << N;

  // Packable[Mask]: ops in Mask form one legal issue cycle (size ≤ 3).
  SmallVector<uint8_t, 4096> Packable(Full, 0);
  Packable[0] = 1;
  for (unsigned Mask = 1; Mask < Full; ++Mask) {
    const unsigned Bits = llvm::popcount(Mask);
    if (Bits > Haydn::ISSUE_SLOT_COUNT)
      continue;
    SmallVector<unsigned, 3> Sub;
    Sub.reserve(Bits);
    for (unsigned I = 0; I < N; ++I)
      if (Mask & (1u << I))
        Sub.push_back(Opcodes[I]);
    Packable[Mask] = exactCanFormOneProductCycle(Fmts, Sub) ? 1 : 0;
  }

  // dp[Mask] = min cycles to cover exactly the ops in Mask.
  const unsigned Inf = N + 1;
  SmallVector<unsigned, 4096> DP(Full, Inf);
  DP[0] = 0;
  for (unsigned Mask = 1; Mask < Full; ++Mask) {
    // Enumerate nonempty submasks (standard SOS: O(3^N) total).
    for (unsigned Sub = Mask; Sub; Sub = (Sub - 1) & Mask) {
      if (!Packable[Sub])
        continue;
      const unsigned Prev = DP[Mask ^ Sub];
      if (Prev >= Inf)
        continue;
      DP[Mask] = std::min(DP[Mask], Prev + 1);
    }
  }
  assert(DP[Full - 1] < Inf && "every singleton is packable; cover exists");
  return DP[Full - 1];
}
/// Greedy (exactTryAdd) ResMII minus exhaustive oracle. Positive → overestimate
/// (SMS-RESMII fail for that multiset). Zero on product path when exact packing
/// is order-optimal for the body. Negative should not occur (greedy ≥ exact).
int productResMIIOverestimate(ArrayRef<unsigned> Opcodes) {
  const unsigned Greedy = computeProductResMII(Opcodes);
  const unsigned Exact = computeExhaustiveProductResMII(Opcodes);
  return static_cast<int>(Greedy) - static_cast<int>(Exact);
}
/// Preferred-collapse overestimate vs exhaustive oracle (diagnostic; proves the
/// gate detects first-fit dead-end inflation that exact matching fixes).
int preferredProductResMIIOverestimate(ArrayRef<unsigned> Opcodes) {
  const unsigned Pref = computePreferredProductResMII(Opcodes);
  const unsigned Exact = computeExhaustiveProductResMII(Opcodes);
  return static_cast<int>(Pref) - static_cast<int>(Exact);
}
/// True when format-dependent SMS product qualification must fail closed for
/// this opcode multiset: greedy exactTryAdd overestimates the exhaustive ≤3
/// format oracle on a body inside the exact bound (N ≤
/// MaxExhaustiveProductResMIIOps). Empty and N>bound never fail here — larger
/// bodies fall back to greedy so Over is always 0 (no false reject on an
/// inexact oracle). Pure predicate; no MIR mutation. SMS analyzeLoop (sibling)
/// may call this; pre-RA exposes the same surface via PreRASchedStrategy.
bool productResMIIFailsQualification(ArrayRef<unsigned> Opcodes) {
  if (Opcodes.empty() || Opcodes.size() > MaxExhaustiveProductResMIIOps)
    return false;
  return productResMIIOverestimate(Opcodes) > 0;
}
} // namespace bundle
} // namespace haydn
} // namespace llvm

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

namespace llvm {
namespace {

/// Format E members for setDesc; FieldSlots stay residual AlternateInsts
/// occupancy so Bundle.canAdd slot math does not change. EntryIdx is not a
/// new SLOT bit — suffix ≠ entry, and raw EntryIdx would expose e2 LOAD1
/// next to e1 LOAD1 for LD32. Residual index is the occupancy class;
/// a generated member at that entry is the setDesc target.
bool enumerateFormatEMemberAlts(const HaydnBaseMCFormats &Fmts,
                                unsigned LogicalOpc,
                                SmallVectorImpl<PlacementAlternative> &Out) {
  const std::string Log = haydn::format_e::peelLogicalOpcodeName(
      haydn::bundle::haydnOpcodeName(LogicalOpc), /*StripWide=*/false);
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Log.c_str());
  if (!Span || Span->Count == 0)
    return false;

  const std::vector<unsigned> *Residual =
      Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Residual || Residual->empty())
    return false;

  bool Any = false;
  for (unsigned Index = 0, E = static_cast<unsigned>(Residual->size());
       Index < E; ++Index) {
    if ((*Residual)[Index] == 0)
      continue;
    const uint64_t ResidualMask =
        residualAltCompatibleFormatMask(LogicalOpc, Index);
    if (ResidualMask == 0)
      continue;
    // Occupancy already picked the setDesc-shaped member (or FieldSlot
    // Fallback). Do not re-walk the alt span — first-match at EntryIdx
    // is the wrong golden signature for SLT64 / X2SLT32.
    Out.emplace_back((*Residual)[Index], ResidualMask,
                     fieldSlotsForAltIndex(Index));
    Any = true;
  }
  return Any;
}

bool enumerateResidualFieldSlotAlts(const HaydnMCFormats &Fmts,
                                    unsigned LogicalOpc,
                                    SmallVectorImpl<PlacementAlternative> &Out) {
  const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Alts || Alts->empty())
    return false;
  bool Any = false;
  for (unsigned Index = 0, E = static_cast<unsigned>(Alts->size()); Index < E;
       ++Index) {
    const unsigned MemberOpc = (*Alts)[Index];
    if (MemberOpc == 0)
      continue;
    const uint64_t Mask = residualAltCompatibleFormatMask(LogicalOpc, Index);
    if (Mask == 0)
      continue;
    Out.emplace_back(MemberOpc, Mask, fieldSlotsForAltIndex(Index));
    Any = true;
  }
  return Any;
}

} // namespace

bool enumeratePlacementAlternatives(const HaydnMCFormats &Fmts,
                                    unsigned LogicalOpc,
                                    SmallVectorImpl<PlacementAlternative> &Out) {
  Out.clear();
  if (enumerateFormatEMemberAlts(Fmts, LogicalOpc, Out))
    return true;
  return enumerateResidualFieldSlotAlts(Fmts, LogicalOpc, Out);
}

bool hasPlacementAlternatives(const HaydnBaseMCFormats &Fmts,
                              unsigned LogicalOpc) {
  SmallVector<PlacementAlternative, 8> Alts;
  if (enumerateFormatEMemberAlts(Fmts, LogicalOpc, Alts))
    return true;
  const std::vector<unsigned> *Raw = Fmts.getAlternateInstsOpcode(LogicalOpc);
  if (!Raw)
    return false;
  return llvm::any_of(*Raw, [](unsigned M) { return M != 0; });
}

} // namespace llvm
