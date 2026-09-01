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
#include "HaydnBundlePortBudget.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/ADT/bit.h"
#include <algorithm>
#include <cassert>
#include <mutex>
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
  // Heap storage: Full is 2^N and N is caller-bounded (12), so the inline
  // buffer is a 4 KiB stack reservation per call — SmallVector default.
  SmallVector<uint8_t> Packable(Full, 0);
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
  SmallVector<unsigned> DP(Full, Inf);
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
namespace haydn {
namespace bundle {

unsigned formatEUnitTwinMember(unsigned Opc, uint32_t UsedUnits) {
  // GE96-11 (2026-08-21) commit-site twin rematch: golden unit-twin of \p Opc.
  // Same Logical + Mode + EntryIdx as \p Opc's own member record, Unit
  // disjoint from \p UsedUnits, lowest UnitMap first (the same deterministic
  // order assignFormatEMemberEntries uses). Fail closed (0): no twin → the
  // commit site keeps the residual pick and lets packing/verify reject.
  // NOTE: same-entry golden rows can carry different operand shapes
  // (X2SLT32@e0 ALU2 is SFR-only 2-src while ALU0 is unary dest+src), and
  // TypeCode is a unit-layout key (ALU1/ALU2 vs ALU0), NOT a shape oracle.
  // The caller gates every twin on memberDescCompatible before stamping.
  namespace fe = haydn::format_e;
  const fe::FormatEMemberRec *Self = nullptr;
  const fe::FormatEMemberRec *Best = nullptr;
  for (unsigned I = 0; I < fe::FormatEMemberCount; ++I) {
    const fe::FormatEMemberRec &M = fe::FormatEMembers[I];
    if (M.IsNop || M.Unit >= 32)
      continue;
    if (!Self) {
      if (M.MemberId < FormatEMemberOpcodeCount &&
          FormatEMemberOpcodes[M.MemberId] == Opc)
        Self = &M;
      continue;
    }
    if (StringRef(M.Logical) != StringRef(Self->Logical))
      continue;
    if (M.Mode != Self->Mode || M.EntryIdx != Self->EntryIdx)
      continue;
    if (UsedUnits & (1u << M.Unit))
      continue;
    if (!Best || M.UnitMap < Best->UnitMap)
      Best = &M;
  }
  if (!Self || !Best || Best->MemberId >= FormatEMemberOpcodeCount)
    return 0;
  return FormatEMemberOpcodes[Best->MemberId];
}

} // namespace bundle
} // namespace haydn
} // namespace llvm

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

SmallVector<unsigned, 3>
llvm::haydn::bundle::assignMemberOpcodesForSettledRow(
    ArrayRef<unsigned> LogicalOpcodes, uint8_t Mode) {
  // Bridge to the golden records DFS. Two traps this route avoids:
  // (1) LLVM logical NAMES (LD32/ST64) are not the golden record names
  //     (S_LW_WITH_IMM/D_SDW_WITH_IMM) — the golden name comes from any of
  //     the logical's member records, reached through the sparse alts
  //     vector and an opcode->record inverse;
  // (2) the sparse alts vector itself holds at most one row REPRESENTATIVE
  //     per residual slot, so it cannot serve as the candidate space for a
  //     row-constrained assignment — assignFormatEMemberEntries walks the
  //     full member records with entry+unit injectivity instead.
  const HaydnMCFormats &Fmts = haydnDefaultMCFormats();

  static SmallVector<int, 0> RecIdxByOpcode;
  static std::once_flag Once;
  std::call_once(Once, [] {
    const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
    RecIdxByOpcode.assign(MII.getNumOpcodes(), -1);
    // Both generated catalogs carry the same member count today (4294), but
    // they are independent headers — std::min keeps each bound authoritative
    // for its own table without a logical-operator tautology when the two
    // constants are equal (clang-tidy misc-redundant-expression).
    const unsigned MemberBound = std::min(haydn::format_e::FormatEMemberCount,
                                          FormatEMemberOpcodeCount);
    for (unsigned Mid = 0; Mid < MemberBound; ++Mid) {
      const unsigned Opc = FormatEMemberOpcodes[Mid];
      if (Opc != 0 && Opc < RecIdxByOpcode.size())
        RecIdxByOpcode[Opc] = static_cast<int>(Mid);
    }
  });

  SmallVector<unsigned, 3> Empty;
  const unsigned N = LogicalOpcodes.size();
  const unsigned Cap = bundleRowEntryCount(
      Mode ? haydn::format::BundleFormatRowID::E96ThreeEntry
           : haydn::format::BundleFormatRowID::E96TwoEntry);
  if (N == 0 || N > Cap)
    return Empty;

  SmallVector<std::string, 3> Logs;
  Logs.reserve(N);
  for (unsigned LogOpc : LogicalOpcodes) {
    const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(LogOpc);
    const char *Golden = nullptr;
    if (Alts) {
      for (unsigned MemberOpc : *Alts) {
        if (MemberOpc == 0 || MemberOpc >= RecIdxByOpcode.size() ||
            RecIdxByOpcode[MemberOpc] < 0)
          continue;
        const haydn::format_e::FormatEMemberRec &Rec =
            haydn::format_e::FormatEMembers[RecIdxByOpcode[MemberOpc]];
        if (!Rec.IsNop && Rec.Logical && Rec.Logical[0] != '\0') {
          Golden = Rec.Logical;
          break;
        }
      }
    }
    if (!Golden)
      return Empty;
    Logs.push_back(Golden);
  }

  auto Assign = haydn::format_e::assignFormatEMemberEntries(Logs, Mode);
  if (!Assign)
    return Empty;
  SmallVector<unsigned, 3> Out;
  Out.reserve(N);
  for (const haydn::format_e::FormatEEntryAssign &E : *Assign) {
    if (!E.Mem || E.Mem->MemberId >= FormatEMemberOpcodeCount)
      return Empty;
    const unsigned MemberOpc = FormatEMemberOpcodes[E.Mem->MemberId];
    if (MemberOpc == 0)
      return Empty;
    Out.push_back(MemberOpc);
  }
  return Out;
}

unsigned llvm::haydn::bundle::productSolveLogicalOpcode(
    unsigned Opc, const HaydnMCFormats &Fmts) {
  const unsigned MemberLog = format_e::logicalOpcodeOrSelf(Opc);
  if (hasPlacementAlternatives(Fmts, MemberLog))
    return MemberLog;
  if (hasPlacementAlternatives(Fmts, Opc))
    return Opc;

  // Residual codegen aliases (ST32_POST, LD32, …) peel to the catalog
  // logical that owns the AlternateInsts / Format E member span. Haydn
  // has one FormatInterface (haydnDefaultMCFormats); AIE has no alias
  // layer (AIEBaseMCFormats getAlternateInstsOpcode only).
  static StringMap<unsigned> AltsByName;
  static std::once_flag Once;
  std::call_once(Once, [] {
    const HaydnMCFormats &DefaultFmts = haydnDefaultMCFormats();
    const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
    for (unsigned O = 0, E = MII.getNumOpcodes(); O != E; ++O)
      if (DefaultFmts.getAlternateInstsOpcode(O))
        AltsByName[haydnOpcodeName(O)] = O;
  });
  const std::string Peeled = format_e::peelLogicalOpcodeName(
      haydnOpcodeName(Opc), /*StripWide=*/false);
  auto It = AltsByName.find(Peeled);
  if (It != AltsByName.end() && hasPlacementAlternatives(Fmts, It->second))
    return It->second;
  // Compact reloc span when the unsuffixed `_W` name owns AlternateInsts
  // (catalogOccupancyName / peelLogicalOpcodeName StripWide). JAL_W / BEQZ_W
  // FieldSlots are retired; Format E members live on JAL / BEQZ.
  const std::string Compact = format_e::peelLogicalOpcodeName(
      haydnOpcodeName(Opc), /*StripWide=*/true);
  if (Compact != Peeled) {
    auto ItW = AltsByName.find(Compact);
    if (ItW != AltsByName.end() &&
        hasPlacementAlternatives(Fmts, ItW->second))
      return ItW->second;
  }
  return MemberLog;
}

bool llvm::haydn::bundle::cycleMembersRespectPortBudgets(
    ArrayRef<MachineInstr *> Instrs) {
  unsigned GR = 0, GW = 0, DRr = 0, DRw = 0, ARr = 0, ARw = 0, SR = 0,
           SW = 0;
  for (const MachineInstr *MI : Instrs) {
    auto [R, W] = countGPRPorts(*MI);
    GR += R;
    GW += W;
    auto [DR2, DW2] = countDRPorts(*MI);
    DRr += DR2;
    DRw += DW2;
    auto [AR2, AW2] = countARPorts(*MI);
    ARr += AR2;
    ARw += AW2;
    // SFR: count LIVE writes only. countSFRPorts charges every def "dead
    // or live" (PackLegality rule 3), but the golden entry menus seat two
    // or three ALU ops in one bundle and the product emits such bundles
    // (dual-ADDI32 pairs all over the corpus) — current members carry NO
    // $sfr operand at all (the old implicit dead $sfr modeling is gone;
    // CB-161 2026-08-21 audit), so this liveness carve-out no longer
    // gates any live corpus pack. A live SFR write (CSRW, a consumed
    // compare) still is exclusive-port traffic, and countSFRPorts now
    // also charges member-shape anonymous $sfr operands (their generated
    // descs dropped the logical's Uses/Defs=[SFR] naming). The HR-side
    // counter still applies rule 3 to dead defs — that asymmetry is part
    // of the CB-153b co-issue story and stays flagged for the owner.
    auto [SR2, SW2] = countSFRPorts(*MI);
    SR += SR2;
    unsigned LiveSFRWrites = 0;
    for (const MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.isDef() && !MO.isDead() &&
          isHaydnSFRPortReg(MO.getReg()))
        ++LiveSFRWrites;
    }
    SW += std::min(SW2, LiveSFRWrites);
  }
  return GR <= HAYDN_GPR_READ_PORTS && GW <= HAYDN_GPR_WRITE_PORTS &&
         DRr <= HAYDN_DR_READ_PORTS && DRw <= HAYDN_DR_WRITE_PORTS &&
         ARr <= HAYDN_AR_READ_PORTS && ARw <= HAYDN_AR_WRITE_PORTS &&
         SR <= HAYDN_SFR_READ_PORTS && SW <= HAYDN_SFR_WRITE_PORTS;
}

bool llvm::haydn::bundle::cycleMembersExceedPortBudget(
    ArrayRef<MachineInstr *> Instrs) {
  // P7: commit-side wrapper over the shared port-budget predicate. Kept
  // beside cycleMembersRespectPortBudgets (CB-153b; live-SFR-aware caps)
  // so later working callers can share haydnCycleMembersExceedPortBudget
  // with verifyCommittedBundle.
  return haydnCycleMembersExceedPortBudget(Instrs);
}


unsigned llvm::haydn::bundle::countKernelIssueParcels(
    const MachineBasicBlock &MBB) {
  // G002 II-parity shared counter (declaration: HaydnBundle.h). Mirrors the
  // AsmPrinter AchievedII filters exactly: one parcel per BUNDLE root, one
  // per bare real MI; meta/debug/CFI/implicit-def/kill/inline-asm never
  // issue. Callers: HaydnAsmPrinter::emitSMSSWPSComments and
  // Historical: the deleted post-RA host shared this walk — never a second walk.
  // G004: the ZOL terminator pseudo (PseudoLoopEnd) never issues either —
  // FixupHwLoops consumes it as the HWLR END marker. At the AsmPrinter seat
  // it is already gone (no change); at the multistage seat (pre-Fixup) it
  // must not count, else every Form-B kernel reports realized-II = II + 1
  // and rolls back.
  unsigned Parcels = 0;
  for (const MachineInstr &MI : MBB) {
    if (MI.isBundle())
      ++Parcels;
    else if (!MI.isMetaInstruction() && !MI.isDebugInstr() &&
             !MI.isCFIInstruction() && !MI.isImplicitDef() && !MI.isKill() &&
             !MI.isInlineAsm() && MI.getOpcode() != Haydn::PseudoLoopEnd)
      ++Parcels;
  }
  return Parcels;
}
