//===-- HaydnFormatERecords.h - Inert Format E generated tables -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Include surface for generated Format E placement / inverse / setDesc ledger
// tables. Product disassembler (tryDecodeFormatE) and MC encode placement
// consume the inverse / type-layout / member tables; unit tests pin counts.
//
// Regenerate / A0 gate (T-TII2):
//   FormatE/generate_format_e_records.py
//   FormatE/generate_format_e_records.py --check
//     [--json PATH --xlsx PATH --canonical-vectors PATH]
//   ninja HaydnFormatERecordsCheck
// --check fail-closes on generated-file drift, XLSX↔JSON type-layout parity,
// td-vs-golden imm width/signedness, and canonical-vector ledger round-trip.
// It does not drive llvm-mc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace llvm {
namespace haydn {
namespace format_e {

// ---------------------------------------------------------------------------
// Golden pins + geometry
// ---------------------------------------------------------------------------
#define GET_FORMAT_E_GOLDEN_PINS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_ENUMS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_TYPE_LAYOUTS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_MEMBERS
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_ALTERNATIVES
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_INVERSE
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_UNIT_INJECTIVITY
#include "HaydnGenFormatERecords.inc"

#define GET_FORMAT_E_SETDESC_LEDGER
#include "HaydnGenFormatESetDescLedger.inc"

// ---------------------------------------------------------------------------
// Family handle (inert; E96 only)
// ---------------------------------------------------------------------------
// Numeric member identity will later be (family, row, entry, unit, type).
// This wave only adds the handle so later waves can thread it. E96 tables
// stay registered under the current FormatE* names. Generated
// FormatEMemberRec / FormatESetDescLedgerRec carry Family (E96=0).
// Consumers must not string-parse family/entry/unit out of member def names.
// Callers that need family-scoped facts go through this handle so a second
// family can land as an additive table, not a second pipeline.

enum class BundleFamily : uint8_t {
  E96 = 0,
};

inline constexpr BundleFamily kAdmittedFamily = BundleFamily::E96;

/// Family-scoped view of the generated Format E tables plus the family's
/// published slot/coissue facts (Shared Unit names + E2/E3 entry capacity).
/// Today E96 is the only admitted family; the tables remain the existing
/// FormatE* globals. Slot law is these generated capacities and units —
/// not a second itinerary table and not a second family.
/// Peer: AIE ProcessorItineraries FuncUnits list
/// (AIE2PGenSchedule.td ProcessorItineraries) + MemInstrItinData
/// (AIETarget.td:22-47). Overlay is family-handle, not a sibling pipeline.
struct FamilyRecords {
  BundleFamily Family;
  unsigned E2EntryCapacity;
  unsigned E3EntryCapacity;
  ArrayRef<const char *> SharedUnits;
};

#define GET_HAYDN_FAMILY_SCHED
#include "HaydnGenMemoryCycles.inc"

inline FamilyRecords getFamilyRecords(BundleFamily Family) {
  if (Family != BundleFamily::E96)
    llvm_unreachable("Haydn: no admitted bundle-format family besides E96");
  return FamilyRecords{
      Family, GeneratedFamilyE2EntryCapacity, GeneratedFamilyE3EntryCapacity,
      ArrayRef<const char *>(GeneratedFamilySharedUnits)};
}

inline FamilyRecords getDefaultFamilyRecords() {
  return getFamilyRecords(kAdmittedFamily);
}

/// Alternative span for a golden logical name. The generated FormatEAltSpans
/// table is sorted by `strcmp` key order — binary search, never a linear walk
/// (enumeratePlacementAlternatives consults this per probe on the post-RA
/// auction / HR / solver hot path).
inline const FormatEAltSpan *findAltSpan(const char *Logical) {
  const auto *Begin = FormatEAltSpans;
  const auto *End = Begin + FormatENonNopLogicalCount;
  const auto *It = std::lower_bound(
      Begin, End, Logical, [](const FormatEAltSpan &Row, const char *Key) {
        return std::strcmp(Row.Logical, Key) < 0;
      });
  if (It != End && std::strcmp(It->Logical, Logical) == 0)
    return It;
  return nullptr;
}

/// Inverse lookup by placement key. Returns MemberId or -1.
/// FormatEInverse is independently sorted by (Mode, EntryIdx, Unit, TypeCode,
/// Opcode, MemberId) — binary search, never a linear walk. Peer: AIE
/// getFormatDescIndex opcode switch (CodeGenFormat.cpp:132;
/// AIEMCFormats.h:373-374) and getAlternateInstsOpcode
/// (CodeGenFormat.cpp:155-163; AIEMCFormats.h:376-379). Overlay is the
/// generated inverse table, not a sibling planner. Same lower_bound shape
/// as findAltSpan in this file.
inline int findInverseMemberId(uint8_t Mode, uint8_t EntryIdx, uint8_t Unit,
                               uint8_t TypeCode, uint16_t Opcode) {
  const FormatEInverseRec *Begin = FormatEInverse;
  const FormatEInverseRec *End = Begin + FormatEMemberCount;
  const FormatEInverseRec Probe{Mode, EntryIdx, Unit, TypeCode, Opcode, 0,
                                nullptr, nullptr};
  const FormatEInverseRec *It = std::lower_bound(
      Begin, End, Probe,
      [](const FormatEInverseRec &A, const FormatEInverseRec &B) {
        if (A.Mode != B.Mode)
          return A.Mode < B.Mode;
        if (A.EntryIdx != B.EntryIdx)
          return A.EntryIdx < B.EntryIdx;
        if (A.Unit != B.Unit)
          return A.Unit < B.Unit;
        if (A.TypeCode != B.TypeCode)
          return A.TypeCode < B.TypeCode;
        return A.Opcode < B.Opcode;
      });
  if (It != End && It->Mode == Mode && It->EntryIdx == EntryIdx &&
      It->Unit == Unit && It->TypeCode == TypeCode && It->Opcode == Opcode)
    return static_cast<int>(It->MemberId);
  return -1;
}

/// Exact inverse hit for a generated member record (MemberId + placement key).
inline bool inverseCoversMember(const FormatEMemberRec &M) {
  if (M.IsNop)
    return true;
  int Hit = findInverseMemberId(M.Mode, M.EntryIdx, M.Unit, M.TypeCode, M.Opcode);
  return Hit >= 0 && static_cast<unsigned>(Hit) == M.MemberId;
}

/// Peel residual `_S*` / Format E member / public mnemonic names to the golden
/// catalog logical used by FormatEMembers. Shared by MC encode, Bundle canAdd,
/// HR/solver tryAdd, and commitExact unit cover — one map, not a second table.
/// This is occupancy-name recovery, not a product alternate source: suffix
/// `_S*` name discovery does not emit typed alternates.
/// \p StripWide drops reloc `_W` / `_F2_W`. Occupancy tries the compact
/// catalog span when the unsuffixed `_W` name has no alt row (ADDI32_W →
/// ADDI32). CSRW_W peels to CSRW; reloc CSRW_W cutovers to the member and
/// encode refuses an untyped CSR fixup kind.
inline std::string peelLogicalOpcodeName(StringRef Name,
                                         bool StripWide = true) {
  StringRef Base = Name;
  auto peel = [&](StringRef Suf) {
    if (Base.ends_with(Suf))
      Base = Base.drop_back(Suf.size());
  };
  // Mode is the earliest `_E2_` / `_E3_`. Searching `_E2_` first would
  // treat E3-e2 members (ADD32_E3_E2_ALU2_RR) as logical `ADD32_E3`.
  size_t ModeAt = StringRef::npos;
  for (StringRef Marker : {"_E2_", "_E3_"}) {
    const size_t Idx = Base.find(Marker);
    if (Idx != StringRef::npos && (ModeAt == StringRef::npos || Idx < ModeAt))
      ModeAt = Idx;
  }
  if (ModeAt != StringRef::npos)
    Base = Base.take_front(ModeAt);
  for (int Pass = 0; Pass < 3; ++Pass) {
    StringRef Before = Base;
    for (StringRef Suf :
         {"_S0", "_S1", "_S2", "_LD_S0", "_LD_S1", "_LD_S2", "_M0S0LS",
          "_M0S1LS", "_M0S2LS", "_M1S0LS", "_M1S1LS", "_M1S2LS"})
      peel(Suf);
    if (Base == Before)
      break;
  }
  // MultiSlot_Pseudo pin (ADD32_MSP) materializes as the catalog logical.
  if (Base.ends_with("_MSP"))
    Base = Base.drop_back(4);
  if (Base.equals_insensitive("PLDWWUA"))
    Base = "PLDWWUA_POST";
  if (StripWide) {
    if (Base.ends_with("_F2_W"))
      Base = Base.drop_back(2);
    else if (Base.ends_with("_W"))
      Base = Base.drop_back(2);
  }

  if (Base.equals_insensitive("LD32") || Base.equals_insensitive("LW") ||
      Base.equals_insensitive("LD32_REG"))
    return Base.equals_insensitive("LD32_REG") ? "S_LW_WITH_REG"
                                               : "S_LW_WITH_IMM";
  if (Base.equals_insensitive("ST32") || Base.equals_insensitive("SW") ||
      Base.equals_insensitive("ST32_REG"))
    return Base.equals_insensitive("ST32_REG") ? "S_SW_WITH_REG"
                                               : "S_SW_WITH_IMM";
  if (Base.equals_insensitive("LD64") || Base.equals_insensitive("LD64_REG"))
    return Base.equals_insensitive("LD64_REG") ? "D_LDW_WITH_REG"
                                               : "D_LDW_WITH_IMM";
  if (Base.equals_insensitive("ST64") || Base.equals_insensitive("ST64_REG"))
    return Base.equals_insensitive("ST64_REG") ? "D_SDW_WITH_REG"
                                               : "D_SDW_WITH_IMM";
  if (Base.equals_insensitive("LD8") || Base.equals_insensitive("LB") ||
      Base.equals_insensitive("LD8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LBS_WITH_REG"
                                             : "S_LBS_WITH_IMM";
  if (Base.equals_insensitive("LDU8") || Base.equals_insensitive("LBU") ||
      Base.equals_insensitive("LDU8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LBU_WITH_REG"
                                             : "S_LBU_WITH_IMM";
  if (Base.equals_insensitive("ST8") || Base.equals_insensitive("SB") ||
      Base.equals_insensitive("ST8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_SB_WITH_REG"
                                             : "S_SB_WITH_IMM";
  if (Base.equals_insensitive("LD16") || Base.equals_insensitive("LH") ||
      Base.equals_insensitive("LHWS") || Base.equals_insensitive("LD16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LHWS_WITH_REG"
                                             : "S_LHWS_WITH_IMM";
  if (Base.equals_insensitive("LDU16") || Base.equals_insensitive("LHU") ||
      Base.equals_insensitive("LHWU") || Base.equals_insensitive("LDU16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LHWU_WITH_REG"
                                             : "S_LHWU_WITH_IMM";
  if (Base.equals_insensitive("ST16") || Base.equals_insensitive("SH") ||
      Base.equals_insensitive("SHW") || Base.equals_insensitive("ST16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_SHW_WITH_REG"
                                             : "S_SHW_WITH_IMM";
  if (Base.equals_insensitive("LD32_POST") ||
      Base.equals_insensitive("LD32_POST_INC"))
    return "S_LW_POST_IMM";
  if (Base.equals_insensitive("ST32_POST") ||
      Base.equals_insensitive("ST32_POST_INC"))
    return "S_SW_POST_IMM";
  if (Base.equals_insensitive("LD32_PRE") ||
      Base.equals_insensitive("LD32_PRE_INC"))
    return "S_LW_PRE_IMM";
  if (Base.equals_insensitive("ST32_PRE") ||
      Base.equals_insensitive("ST32_PRE_INC"))
    return "S_SW_PRE_IMM";
  if (Base.equals_insensitive("LD64_POST"))
    return "D_LDW_POST_IMM";
  if (Base.equals_insensitive("ST64_POST"))
    return "D_SDW_POST_IMM";
  if (Base.equals_insensitive("SEXT_GPR32_TO_DR64") ||
      Base.equals_insensitive("SEXT32T64"))
    return "SEXT32T64";
  if (Base.equals_insensitive("MOV_GPR_TO_DR64") ||
      Base.equals_insensitive("MOVE_GPR_TO_DR64") ||
      Base.equals_insensitive("ZEXT_GPR32_TO_DR64"))
    return "SEXT32T64";
  if (Base.equals_insensitive("RET"))
    return "JALR";
  // Catalog token is WFI<TBD>; TableGen member symbol is WFITBDTBDTBD_*
  // (angle brackets are not ident). Same span as the generated HINT members.
  if (Base.equals_insensitive("WFI") ||
      Base.equals_insensitive("WFITBDTBDTBD"))
    return "WFI<TBD>";
  return Base.str();
}

/// Resolve a Format E member for (logical, mode, entry). Prefers lower UnitMap
/// among candidates whose Unit is not in \p UsedUnitMask. Shared by MC encode
/// and Finalize FieldSlot→MemberId cutover.
inline const FormatEMemberRec *findFormatEMember(StringRef Logical, uint8_t Mode,
                                                 uint8_t EntryIdx,
                                                 uint32_t UsedUnitMask) {
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return nullptr;
  const FormatEMemberRec *Fallback = nullptr;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop || M.Mode != Mode || M.EntryIdx != EntryIdx)
      continue;
    if (!Logical.equals_insensitive(M.Logical))
      continue;
    if (M.Unit < 32 && (UsedUnitMask & (1u << M.Unit)))
      continue;
    if (!Fallback || M.UnitMap < Fallback->UnitMap)
      Fallback = &M;
  }
  return Fallback;
}

/// One kid's Format E entry + generated member after bounded assignment.
/// Parallel to the input logical-name list; FieldSlot suffixes never appear.
struct FormatEEntryAssign {
  uint8_t EntryIdx = 0;
  const FormatEMemberRec *Mem = nullptr;
};

/// Assign each golden logical a distinct (EntryIdx, member) under \p Mode.
/// Tries every legal unit at an entry (lower UnitMap first) with unit
/// injectivity. Used by Finalize FieldSlot cutover and by standalone
/// hand-asm placement; compiler MC must not call this for residual
/// FieldSlot composites (those cut over before encode).
inline std::optional<SmallVector<FormatEEntryAssign, 3>>
assignFormatEMemberEntries(ArrayRef<std::string> Logs, uint8_t Mode) {
  const unsigned N = Logs.size();
  const unsigned EntryCount = Mode ? 3u : 2u;
  if (N == 0 || N > EntryCount)
    return std::nullopt;

  SmallVector<int, 3> EntryOf(N, -1);
  SmallVector<const FormatEMemberRec *, 3> MemOf(N, nullptr);
  uint32_t UsedUnits = 0;
  uint8_t UsedEntries = 0;

  auto dfs = [&](auto &&Self, unsigned Kid) -> bool {
    if (Kid == N)
      return true;
    StringRef Log = Logs[Kid];
    for (uint8_t Entry = 0; Entry < EntryCount; ++Entry) {
      if (UsedEntries & (1u << Entry))
        continue;
      SmallVector<const FormatEMemberRec *, 4> Cands;
      for (unsigned I = 0; I < FormatEMemberCount; ++I) {
        const FormatEMemberRec &M = FormatEMembers[I];
        if (M.IsNop || M.Mode != Mode || M.EntryIdx != Entry)
          continue;
        if (!Log.equals_insensitive(M.Logical))
          continue;
        if (M.Unit < 32 && (UsedUnits & (1u << M.Unit)))
          continue;
        Cands.push_back(&M);
      }
      for (unsigned I = 1; I < Cands.size(); ++I) {
        const FormatEMemberRec *X = Cands[I];
        unsigned J = I;
        while (J > 0 && Cands[J - 1]->UnitMap > X->UnitMap) {
          Cands[J] = Cands[J - 1];
          --J;
        }
        Cands[J] = X;
      }
      for (const FormatEMemberRec *Mem : Cands) {
        EntryOf[Kid] = static_cast<int>(Entry);
        MemOf[Kid] = Mem;
        UsedEntries |= static_cast<uint8_t>(1u << Entry);
        if (Mem->Unit < 32)
          UsedUnits |= (1u << Mem->Unit);
        if (Self(Self, Kid + 1))
          return true;
        if (Mem->Unit < 32)
          UsedUnits &= ~(1u << Mem->Unit);
        UsedEntries &= static_cast<uint8_t>(~(1u << Entry));
        EntryOf[Kid] = -1;
        MemOf[Kid] = nullptr;
      }
    }
    return false;
  };

  if (!dfs(dfs, 0))
    return std::nullopt;

  SmallVector<FormatEEntryAssign, 3> Out;
  Out.reserve(N);
  for (unsigned K = 0; K != N; ++K)
    Out.push_back({static_cast<uint8_t>(EntryOf[K]), MemOf[K]});
  return Out;
}

/// Golden Format E unit bit-mask for \p Logical under \p Mode (0=E2, 1=E3).
/// Stores such as D_SW_L_WITH_IMM / S_SB_WITH_IMM are LOADSTORE0-only (e0).
/// Backing store is one process-wide StringMap per Mode built from the
/// immutable generated FormatEMembers table (exact-case keys — generated
/// names are uppercase; the old linear scan's equals_insensitive tolerated
/// only case variants that never occur). Probing the map is O(1) instead of
/// a 3686-row string scan; solver tryApplyAlt calls this per (candidate ×
/// alt) member on the post-RA auction path.
inline uint32_t unitMaskForLogical(StringRef Logical, uint8_t Mode) {
  static StringMap<uint32_t> Maps[2] = {
      StringMap<uint32_t>(),
      StringMap<uint32_t>(),
  };
  static std::once_flag Once;
  std::call_once(Once, [] {
    for (unsigned I = 0; I < FormatEMemberCount; ++I) {
      const FormatEMemberRec &M = FormatEMembers[I];
      if (M.IsNop != 0 || M.Unit >= 32)
        continue;
      uint32_t &Mask = Maps[M.Mode][M.Logical];
      Mask |= (1u << M.Unit);
    }
  });
  const auto It = Maps[Mode].find(Logical);
  return It == Maps[Mode].end() ? 0 : It->second;
}

/// True when \p Logs can be assigned injective Format E units under \p Mode.
/// Unknown logicals (mask 0) stay unconstrained so residual names that are
/// not in the golden catalog do not false-reject; mapped stores are exclusive.
inline bool logicalsHaveUnitCoverForMode(ArrayRef<std::string> Logs,
                                         uint8_t Mode) {
  if (Logs.size() < 2)
    return true;

  SmallVector<uint32_t, 3> Masks;
  Masks.reserve(Logs.size());
  bool AnyConstrained = false;
  for (const std::string &L : Logs) {
    uint32_t M = unitMaskForLogical(L, Mode);
    if (M == 0)
      M = ~0u;
    else
      AnyConstrained = true;
    Masks.push_back(M);
  }
  if (!AnyConstrained)
    return true;

  auto assignable = [&](ArrayRef<uint32_t> UnitMasks) -> bool {
    const unsigned N = UnitMasks.size();
    if (N == 0)
      return true;
    if (N == 1)
      return UnitMasks[0] != 0;
    if (N == 2) {
      for (unsigned U0 = 0; U0 < 32; ++U0) {
        if (!(UnitMasks[0] & (1u << U0)))
          continue;
        for (unsigned U1 = 0; U1 < 32; ++U1) {
          if (U0 == U1)
            continue;
          if (UnitMasks[1] & (1u << U1))
            return true;
        }
      }
      return false;
    }
    for (unsigned U0 = 0; U0 < 32; ++U0) {
      if (!(UnitMasks[0] & (1u << U0)))
        continue;
      for (unsigned U1 = 0; U1 < 32; ++U1) {
        if (U1 == U0 || !(UnitMasks[1] & (1u << U1)))
          continue;
        for (unsigned U2 = 0; U2 < 32; ++U2) {
          if (U2 == U0 || U2 == U1)
            continue;
          if (UnitMasks[2] & (1u << U2))
            return true;
        }
      }
    }
    return false;
  };
  return assignable(Masks);
}

/// True when \p Logs have injective Format E units under E2 or E3.
/// Invariant: Format E units ≠ encoded entry identity (LOADSTORE0 is unique).
inline bool logicalsHaveUnitCover(ArrayRef<std::string> Logs) {
  if (Logs.size() < 2)
    return true;
  return logicalsHaveUnitCoverForMode(Logs, /*Mode=*/0) ||
         logicalsHaveUnitCoverForMode(Logs, /*Mode=*/1);
}

/// Golden unit for a generated member symbol (`ADD32_E3_E0_ALU2_RR`).
/// \returns nullopt when \p Symbol is not a catalog member (logical leftover).
/// Same memoized-lookup contract as unitMaskForLogical: exact-case keys over
/// the immutable generated table; memberSymbolsHaveInjectiveUnits calls this
/// per member inside every solver tryApplyAlt probe.
inline std::optional<uint8_t> unitForMemberSymbol(StringRef Symbol) {
  static StringMap<uint8_t> Map;
  static std::once_flag Once;
  std::call_once(Once, [] {
    for (unsigned I = 0; I < FormatEMemberCount; ++I)
      Map.try_emplace(FormatEMembers[I].MemberSymbol,
                      FormatEMembers[I].Unit);
  });
  const auto It = Map.find(Symbol);
  if (It == Map.end())
    return std::nullopt;
  return It->second;
}

/// True when chosen member symbols have injective Format E units.
/// Logical cover (`logicalsHaveUnitCover`) is existence-only — three ADD32
/// logicals can sit on ALU0/ALU1/ALU2, but first-at-entry members at e0 and
/// e2 can both be ALU2. Unknown symbols stay unconstrained.
inline bool memberSymbolsHaveInjectiveUnits(ArrayRef<StringRef> Symbols) {
  uint32_t Used = 0;
  for (StringRef Symbol : Symbols) {
    const std::optional<uint8_t> Unit = unitForMemberSymbol(Symbol);
    if (!Unit)
      continue;
    const uint32_t Bit = 1u << *Unit;
    if ((Used & Bit) != 0)
      return false;
    Used |= Bit;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Member opcode → logical opcode (TII branch / SMS inverse)
// ---------------------------------------------------------------------------
// AIE peer: inverse of AIEMCFormats::getAlternateInstsOpcode
// (AIEMCFormats.h:376-379; generated switch in CodeGenFormat.cpp:155-163).
// Hexagon packet children keep the architectural opcode
// (HexagonInstrInfo.cpp:390-397 bundle walk; HexagonMCInstrInfo.cpp:423 getType).
// Haydn residual `_S*` plus Format E members overlay that shape with generated
// enum cases (HaydnGenFormatEMemberOpcodes.inc GET_FORMAT_E_MEMBER_TO_LOGICAL).
//
// lookupGeneratedMemberToLogical returns 0 when \p Opcode is absent
// (fail-closed: never returns the member itself). logicalOpcodeOrSelf maps
// that 0 to identity for already-logical opcodes.

unsigned lookupGeneratedMemberToLogical(unsigned Opcode);

inline unsigned logicalOpcodeOrSelf(unsigned Opcode) {
  if (unsigned Logical = lookupGeneratedMemberToLogical(Opcode))
    return Logical;
  return Opcode;
}

} // namespace format_e
} // namespace haydn
} // namespace llvm

// MemberId / logical indexes over the independently sorted FormatEInverse
// table. Included at global scope because the .inc opens its own namespaces.
// Peer: AIE getAlternateInstsOpcode generated switch
// (CodeGenFormat.cpp:155-163). Overlay is FormatEInverse, not a planner.
#define GET_FORMAT_E_INVERSE_INDEX
#include "HaydnGenFormatEInverse.inc"

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFORMATERECORDS_H
