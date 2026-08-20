//===- HaydnMCFormats.cpp - Generated-format consumer + Haydn ext. --*- C++ -*-=
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the HaydnBaseMCFormats / HaydnMCFormats query interface as a
// CONSUMER of the generated HaydnGenFormats.inc (decision §9 Option A).
// It mirrors AIE's AIEMCFormats.cpp + AIEBaseMCFormats.cpp layout:
//
// GET_FORMATS_PACKETS_TABLE / GET_FORMATS_SLOTS_DEFS
// GET_FORMATS_SLOTINFOS_MAPPING / GET_OPCODE_FORMATS_INDEX_FUNC
// regions are pulled in at namespace scope (getSlotInfo /
// getFormatDescIndex). getAlternateInstsOpcode is occupancy + Format E
// members (HaydnGenAltOccupancy.inc), not the generated FieldSlot table.
// GET_FORMATS_FORMATS_DEFS region is pulled in inside namespace Haydn so
// the bare opcode enumerators (ADD32) resolve (AIE does the same
// inside namespace AIE). The generated Formats is returned by
// HaydnMCFormats::getMCFormats.
//
// Product PacketFormats are Format E composites only (FE8). Production
// ObjectEncodingProfile is E96 via the neutral registry in HaydnFormat.h.
// Alts-derived getLegalSlots is the slot legality authority for member tables.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCFormats.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

#include "HaydnFormat.h"
#include "HaydnFormatERecords.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();


extern const unsigned HaydnInstrNameIndices[];
extern const char HaydnInstrNameData[];

#undef DEBUG_TYPE
#define DEBUG_TYPE "haydn-mcformats"

//===----------------------------------------------------------------------===//
// Generated format tables + member-function bodies (AIE pattern)
//===----------------------------------------------------------------------===//
//
// The GET_FORMATS_* includes define: HaydnSlots (slot descriptors)
// getSlotInfo / getFormatDescIndex method bodies (declarations on
// HaydnMCFormats), the packet-format tables, and the per-opcode Formats
// (returned by getMCFormats). Mirrors AIEMCFormats.cpp:18-29.
// getAlternateInstsOpcode is occupancy + Format E members.

// GET_FORMATS_PACKETS_TABLE is CONSUMED. Product composites are Format E
// (BUNDLE_E96_*). PacketFormats / FormatAvailable are the generated identity;
// getPacketFormats/getIsFormatAvailable return those tables only. Residual
// FieldSlot names are not a second slot map (AIE AIEMCFormats.cpp:61-67).
#define GET_FORMATS_PACKETS_TABLE
#define GET_FORMATS_SLOTS_DEFS
#define GET_FORMATS_SLOTINFOS_MAPPING
#define GET_OPCODE_FORMATS_INDEX_FUNC
#include "HaydnGenFormats.inc"

#define GET_HAYDN_ALT_OCCUPANCY
#include "HaydnGenAltOccupancy.inc"

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

namespace {

StringRef occupancyOpcodeName(unsigned Opcode) {
  return StringRef(&HaydnInstrNameData[HaydnInstrNameIndices[Opcode]]);
}

/// Residual FieldSlot names are retired. Occupancy must not recover a
/// logical by stripping `_S0/_S1/_S2` (AIE uses generated MultiSlot alts,
/// AIEMCFormats.h:376-379 — no suffix table).
bool isResidualFieldSlotName(StringRef Name) {
  return Name.ends_with("_S0") || Name.ends_with("_S1") ||
         Name.ends_with("_S2");
}

/// True when register-operand classes match. Immediates compare kind only
/// (logical simm vs member uimm is still a setDesc-legal imm).
bool operandClassesMatch(const MCInstrDesc &A, const MCInstrDesc &B) {
  if (A.getNumOperands() != B.getNumOperands())
    return false;
  for (unsigned I = 0, E = A.getNumOperands(); I != E; ++I) {
    const MCOperandInfo &AO = A.operands()[I];
    const MCOperandInfo &BO = B.operands()[I];
    const bool AReg =
        AO.OperandType == MCOI::OPERAND_REGISTER || AO.RegClass >= 0;
    const bool BReg =
        BO.OperandType == MCOI::OPERAND_REGISTER || BO.RegClass >= 0;
    if (AReg != BReg)
      return false;
    if (AReg && AO.RegClass != BO.RegClass)
      return false;
  }
  return true;
}

/// Pad NOP occupancy: FormatEAltSpans is non-NOP only, so walk generated
/// NOP members at residual index. Prefer HINT (same class as the retired
/// FieldSlot pad) then any zero-operand member. E2 before E3; Finalize
/// rebinds to the committed row.
unsigned formatENopMemberAtIndex(unsigned Index) {
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  unsigned HintE2 = 0;
  unsigned HintE3 = 0;
  unsigned AnyE2 = 0;
  unsigned AnyE3 = 0;
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    const haydn::format_e::FormatEMemberRec &Mem =
        haydn::format_e::FormatEMembers[I];
    if (!Mem.IsNop || Mem.EntryIdx != static_cast<uint8_t>(Index))
      continue;
    if (I >= FormatEMemberOpcodeCount)
      continue;
    const unsigned Opc = FormatEMemberOpcodes[I];
    if (Opc == 0 || Opc >= MII.getNumOpcodes())
      continue;
    const bool IsE2 = Mem.Mode == 0;
    if (StringRef(Mem.TypeName).equals_insensitive("HINT")) {
      if (IsE2 && HintE2 == 0)
        HintE2 = Opc;
      else if (!IsE2 && HintE3 == 0)
        HintE3 = Opc;
      continue;
    }
    if (MII.get(Opc).getNumOperands() != 0)
      continue;
    if (IsE2 && AnyE2 == 0)
      AnyE2 = Opc;
    else if (!IsE2 && AnyE3 == 0)
      AnyE3 = Opc;
  }
  if (HintE2)
    return HintE2;
  if (HintE3)
    return HintE3;
  if (AnyE2)
    return AnyE2;
  return AnyE3;
}

/// Format E member at residual occupancy index \p Index whose MCInstrDesc
/// matches the logical (NumDefs + operands). One logical can have two golden
/// shapes at the same EntryIdx (SLT64 unary dest+src vs SFR-only 2-src;
/// X2SLT32 is the SFR-only shape). First-match and "smallest OperandCount"
/// pick the wrong one. No match → 0 (fail closed). Residual FieldSlot
/// names are not occupancy identity — do not peel `_S*`.
/// Reloc `_W` logicals share the compact catalog span (ADDI32_W → ADDI32).
unsigned formatEMemberAtResidualIndex(unsigned LogicalOpc, unsigned Index) {
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  if (LogicalOpc >= MII.getNumOpcodes())
    return 0;
  const StringRef Raw = occupancyOpcodeName(LogicalOpc);
  if (isResidualFieldSlotName(Raw))
    return 0;
  // Exact catalog name, then reloc `_W` compact span. Do not recover a
  // logical by stripping `_S*` (AIE MultiSlot alts, AIEMCFormats.h:376-379).
  auto spanFor = [](StringRef Name) -> const haydn::format_e::FormatEAltSpan * {
    std::string Key = Name.str();
    if (const haydn::format_e::FormatEAltSpan *S =
            haydn::format_e::findAltSpan(Key.c_str()))
      return S;
    if (Name.ends_with("_W")) {
      Key = Name.drop_back(2).str();
      return haydn::format_e::findAltSpan(Key.c_str());
    }
    return nullptr;
  };
  if (Raw.equals_insensitive("NOP"))
    return formatENopMemberAtIndex(Index);
  const haydn::format_e::FormatEAltSpan *Span = spanFor(Raw);
  std::string Log = Raw.str();
  if (!Span) {
    // Public-logical aliases (LD32 → S_LW_WITH_IMM) share a catalog span.
    // peelLogicalOpcodeName is alias recovery only; FieldSlots already
    // returned 0 above.
    Log = haydn::format_e::peelLogicalOpcodeName(Raw, /*StripWide=*/false);
    if (StringRef(Log).equals_insensitive("NOP"))
      return formatENopMemberAtIndex(Index);
    Span = haydn::format_e::findAltSpan(Log.c_str());
    if (!Span || Span->Count == 0) {
      Log = haydn::format_e::peelLogicalOpcodeName(Raw, /*StripWide=*/true);
      if (StringRef(Log).equals_insensitive("NOP"))
        return formatENopMemberAtIndex(Index);
      Span = haydn::format_e::findAltSpan(Log.c_str());
    }
  }
  if (!Span || Span->Count == 0)
    return 0;
  const MCInstrDesc &LogDesc = MII.get(LogicalOpc);
  unsigned ExactE2 = 0;
  unsigned ExactE3 = 0;
  unsigned DropE2 = 0;
  unsigned DropE3 = 0;
  for (unsigned I = 0; I < Span->Count; ++I) {
    const uint16_t Mid = haydn::format_e::FormatEAltMemberIds[Span->Begin + I];
    if (Mid >= haydn::format_e::FormatEMemberCount ||
        Mid >= FormatEMemberOpcodeCount)
      continue;
    const haydn::format_e::FormatEMemberRec &Mem =
        haydn::format_e::FormatEMembers[Mid];
    if (Mem.IsNop || Mem.EntryIdx != Index)
      continue;
    const unsigned Opc = FormatEMemberOpcodes[Mid];
    if (Opc == 0 || Opc >= MII.getNumOpcodes())
      continue;
    const MCInstrDesc &MemDesc = MII.get(Opc);
    const bool IsE2 = Mem.Mode == 0;
    if (MemDesc.getNumDefs() == LogDesc.getNumDefs() &&
        MemDesc.getNumOperands() == LogDesc.getNumOperands() &&
        operandClassesMatch(LogDesc, MemDesc)) {
      if (IsE2 && ExactE2 == 0)
        ExactE2 = Opc;
      else if (!IsE2 && ExactE3 == 0)
        ExactE3 = Opc;
      continue;
    }
    // Closed keep-map (tied-acc, CB writeback, AR-UA POST). Not bag-sort.
    auto KeepKind = [&](unsigned OldI, unsigned NewI) {
      if (OldI >= LogDesc.getNumOperands() || NewI >= MemDesc.getNumOperands())
        return false;
      const MCOperandInfo &AO = LogDesc.operands()[OldI];
      const MCOperandInfo &BO = MemDesc.operands()[NewI];
      const bool AReg =
          AO.OperandType == MCOI::OPERAND_REGISTER || AO.RegClass >= 0;
      const bool BReg =
          BO.OperandType == MCOI::OPERAND_REGISTER || BO.RegClass >= 0;
      if (AReg != BReg)
        return false;
      if (AReg && AO.RegClass != BO.RegClass)
        return false;
      return true;
    };
    if (haydnFormatEKeepOperands(LogDesc, MemDesc, KeepKind)) {
      if (IsE2 && DropE2 == 0)
        DropE2 = Opc;
      else if (!IsE2 && DropE3 == 0)
        DropE3 = Opc;
      continue;
    }
    // Tied-seed / trailing-use drop (X2MOVT32 3-op logical → 2-op member).
    if (MemDesc.getNumOperands() > 0 &&
        MemDesc.getNumOperands() < LogDesc.getNumOperands() &&
        MemDesc.getNumDefs() == LogDesc.getNumDefs()) {
      if (IsE2 && DropE2 == 0)
        DropE2 = Opc;
      else if (!IsE2 && DropE3 == 0)
        DropE3 = Opc;
    }
  }
  // Occupancy is residual slot legality, not row identity. Prefer the E2
  // member when both Modes match the logical shape so a two-entry probe is
  // not silently E3-stamped; E3-only entries keep the E3 member. Finalize
  // rebinds to the committed row.
  if (ExactE2)
    return ExactE2;
  if (ExactE3)
    return ExactE3;
  if (DropE2)
    return DropE2;
  return DropE3;
}

const std::vector<unsigned> *cachedMemberAlts(unsigned Opcode) {
  const int Idx = haydnAltOccupancyIndex(Opcode);
  if (Idx < 0)
    return nullptr;
  static std::vector<std::vector<unsigned>> Cache;
  static std::once_flag Once;
  std::call_once(Once, [] {
    const size_t N = sizeof(HaydnAltOccupancy) / sizeof(HaydnAltOccupancy[0]);
    Cache.resize(N);
    for (size_t I = 0; I < N; ++I) {
      Cache[I].assign(3, 0);
      const HaydnAltOccupancyRow &Row = HaydnAltOccupancy[I];
      for (unsigned Slot = 0; Slot < 3; ++Slot) {
        if ((Row.Mask & (1u << Slot)) == 0)
          continue;
        if (unsigned Mem = formatEMemberAtResidualIndex(Row.LogicalOpc, Slot))
          Cache[I][Slot] = Mem;
        else if (Row.LogicalOpc == Haydn::WFI)
          report_fatal_error(
              "Haydn: WFI occupancy hole: mask bit " + Twine(Slot) +
                  " has no Format E HINT member",
              /*GenCrashDiag=*/false);
        else
          Cache[I][Slot] = 0;
      }
    }
  });
  return &Cache[static_cast<size_t>(Idx)];
}

/// Occupancy when LogicalMaterialize no longer lists a FieldSlot: walk the
/// generated alt span at residual indices 0..2. AIE getAlternateInstsOpcode
/// is TableGen MultiSlot only (AIEMCFormats.h:376-379); Haydn overlays
/// Format E members because EncodedBytes is not a slot bit.
const std::vector<unsigned> *cachedFormatEOnlyAlts(unsigned Opcode) {
  static std::mutex Mu;
  static DenseMap<unsigned, std::vector<unsigned>> Extra;
  std::lock_guard<std::mutex> Lock(Mu);
  auto It = Extra.find(Opcode);
  if (It != Extra.end())
    return It->second.empty() ? nullptr : &It->second;
  std::vector<unsigned> Alts(3, 0);
  bool Any = false;
  for (unsigned Slot = 0; Slot < 3; ++Slot) {
    if (unsigned Mem = formatEMemberAtResidualIndex(Opcode, Slot)) {
      Alts[Slot] = Mem;
      Any = true;
    }
  }
  auto Ins = Extra.try_emplace(Opcode, Any ? std::move(Alts)
                                           : std::vector<unsigned>{});
  return Ins.first->second.empty() ? nullptr : &Ins.first->second;
}

bool formatELogicalIsModeOnly(unsigned Opcode, uint8_t WantMode) {
  const StringRef Raw = occupancyOpcodeName(Opcode);
  if (isResidualFieldSlotName(Raw))
    return false;
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Raw.str().c_str());
  if (!Span || Span->Count == 0) {
    // Reloc `_W` compact span, then public-logical aliases. Residual `_S*`
    // already returned false — this is catalog occupancy, not FieldSlot
    // row recovery and not child-count identity.
    std::string Log = Raw.str();
    if (Raw.ends_with("_W"))
      Log = Raw.drop_back(2).str();
    Span = haydn::format_e::findAltSpan(Log.c_str());
    if (!Span || Span->Count == 0) {
      Log = haydn::format_e::peelLogicalOpcodeName(Raw);
      Span = haydn::format_e::findAltSpan(Log.c_str());
    }
  }
  if (!Span || Span->Count == 0)
    return false;
  bool SawWant = false;
  bool SawOther = false;
  for (unsigned I = 0; I < Span->Count; ++I) {
    const uint16_t Mid = haydn::format_e::FormatEAltMemberIds[Span->Begin + I];
    if (Mid >= haydn::format_e::FormatEMemberCount)
      continue;
    const haydn::format_e::FormatEMemberRec &M =
        haydn::format_e::FormatEMembers[Mid];
    if (M.IsNop)
      continue;
    if (M.Mode == WantMode)
      SawWant = true;
    else
      SawOther = true;
  }
  return SawWant && !SawOther;
}

} // namespace

const std::vector<unsigned> *
HaydnMCFormats::getAlternateInstsOpcode(unsigned Opcode) const {
  // Pre-lowering pseudos with no explicit AltOccupancy row never direct-
  // place: the golden member lookup would happily match the post-lowering
  // immediate shape (SET_HWLOOP pseudo 4-imm matches the HWLRIII member
  // exactly) and silently claim placeability before FixupHwLoops/
  // ExpandPseudos resolved the targets — dropping relocs. Parcel ops with
  // a real occupancy row (WFI) and lowered forms (SET_HWLOOP_W, CSRW_W)
  // have explicit rows and pass through unfiltered.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  if (Opcode < MII.getNumOpcodes() && MII.get(Opcode).isPseudo() &&
      haydnAltOccupancyIndex(Opcode) < 0)
    return nullptr;
  if (const std::vector<unsigned> *Cached = cachedMemberAlts(Opcode))
    return Cached;
  return cachedFormatEOnlyAlts(Opcode);
}

bool haydnFormatELogicalIsE3Only(unsigned Opcode) {
  return formatELogicalIsModeOnly(Opcode, /*WantMode=*/1);
}

bool haydnFormatELogicalIsE2Only(unsigned Opcode) {
  return formatELogicalIsModeOnly(Opcode, /*WantMode=*/0);
}

unsigned haydnSelectStandaloneFormatEOpcode(ArrayRef<unsigned> LogicalOpcodes) {
  // AIE emitBundle (AIEBaseAsmParser.h:164-180) takes Format->Opcode from
  // getFormatOrNull / PacketFormats::getFormat first-covering (smallest
  // row that covers occupancy), not child cardinality. Haydn overlay:
  // generated Mode-only membership + unit cover + family EntryCapacity.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const haydn::format_e::FamilyRecords Fam =
      haydn::format_e::getDefaultFamilyRecords();
  SmallVector<std::string, 3> Logs;
  bool AnyE3Only = false;
  bool AnyE2Only = false;
  for (unsigned Opc : LogicalOpcodes) {
    if (Opc == 0 || Opc == Haydn::NOP)
      continue;
    const StringRef Name = MII.getName(Opc);
    if (isResidualFieldSlotName(Name) || haydnFindFormatEMemberByOpcode(Opc))
      return 0;
    // Catalog occupancy name for unit cover. Residual FieldSlot already
    // returned 0 — this is not `_S*` row recovery and not child count.
    Logs.emplace_back(haydn::format_e::peelLogicalOpcodeName(Name));
    if (haydnFormatELogicalIsE3Only(Opc))
      AnyE3Only = true;
    if (haydnFormatELogicalIsE2Only(Opc))
      AnyE2Only = true;
  }
  if (AnyE2Only && AnyE3Only)
    return 0;
  // N is generated EntryCapacity occupancy, not TWO vs THREE identity.
  // AnyE3Only below refuses size≤1→E2 for an E3-only logical. When both
  // Modes cover, PacketFormats first-covering (AIE getFormat) is the
  // smaller product row.
  const unsigned N = Logs.size();
  if (AnyE2Only && N > Fam.E2EntryCapacity)
    return 0;
  if (AnyE3Only && N > Fam.E3EntryCapacity)
    return 0;
  const bool CoverE2 =
      haydn::format_e::logicalsHaveUnitCoverForMode(Logs, /*Mode=*/0);
  const bool CoverE3 =
      haydn::format_e::logicalsHaveUnitCoverForMode(Logs, /*Mode=*/1);
  const bool FitsE2 = !AnyE3Only && CoverE2 && N <= Fam.E2EntryCapacity;
  const bool FitsE3 = !AnyE2Only && CoverE3 && N <= Fam.E3EntryCapacity;
  if (AnyE3Only) {
    if (!FitsE3)
      return 0;
    return Haydn::BUNDLE_E96_THREE_ENTRY;
  }
  if (AnyE2Only) {
    if (!FitsE2)
      return 0;
    return Haydn::BUNDLE_E96_TWO_ENTRY;
  }
  if (FitsE2)
    return Haydn::BUNDLE_E96_TWO_ENTRY;
  if (FitsE3)
    return Haydn::BUNDLE_E96_THREE_ENTRY;
  return 0;
}

std::optional<SmallVector<unsigned, 4>>
haydnFormatEKeepOperands(
    const MCInstrDesc &OldDesc, const MCInstrDesc &NewDesc,
    function_ref<bool(unsigned, unsigned)> KindOk) {
  const unsigned OldN = OldDesc.getNumOperands();
  const unsigned NewN = NewDesc.getNumOperands();
  const unsigned OldDefs = OldDesc.getNumDefs();
  const unsigned NewDefs = NewDesc.getNumDefs();

  if (OldN == 0 && NewN == 0)
    return SmallVector<unsigned, 4>{};
  if (OldN == 0 || NewN == 0)
    return std::nullopt;

  auto accept = [&](ArrayRef<unsigned> Keep) -> bool {
    if (Keep.size() != NewN)
      return false;
    if (!KindOk)
      return true;
    for (unsigned NewI = 0; NewI != NewN; ++NewI)
      if (!KindOk(Keep[NewI], NewI))
        return false;
    return true;
  };
  auto prefix = [&](unsigned N) {
    SmallVector<unsigned, 4> K;
    for (unsigned I = 0; I != N; ++I)
      K.push_back(I);
    return K;
  };

  if (OldN == NewN && OldDefs == NewDefs) {
    auto K = prefix(NewN);
    if (accept(K))
      return K;
  }
  // Catalog role `reg` dest-as-ins (CSRR / ZERO_GPR / MOVESFR2GPR).
  if (OldDefs == 1 && NewDefs == 0 && OldN == NewN) {
    auto K = prefix(NewN);
    if (accept(K))
      return K;
  }

  if (OldDefs == NewDefs && OldN > NewN) {
    SmallVector<unsigned, 4> Keep;
    bool DroppedTied = false;
    for (unsigned I = 0; I != OldN; ++I) {
      const int Tie = OldDesc.getOperandConstraint(I, MCOI::TIED_TO);
      if (Tie >= 0 && static_cast<unsigned>(Tie) < OldDefs) {
        DroppedTied = true;
        continue;
      }
      Keep.push_back(I);
    }
    if (DroppedTied && accept(Keep))
      return Keep;
  }

  // AR unaligned POST load: [rtd, wb, rs1, rs2, ar_sel, dir_sel] →
  // [dest1, ar_sel, dest2]. Golden AR window has dest/ar_sel/rs only.
  if (OldDefs == 2 && NewDefs == 1 && OldN == 6 && NewN == 3) {
    SmallVector<unsigned, 4> K{0, 4, 2};
    if (accept(K))
      return K;
  }
  // AR unaligned POST store: [wb, rtd, rs1, rs2, ar_sel, dir_sel] →
  // [ar_sel, dest1, dest2].
  if (OldDefs == 1 && NewDefs == 0 && OldN == 6 && NewN == 3) {
    SmallVector<unsigned, 4> K{4, 1, 2};
    if (accept(K))
      return K;
  }
  // WBARWUA: [rs, ar_sel, dir_sel] → [ar_sel, dest2].
  if (OldDefs == 0 && NewDefs == 0 && OldN == 3 && NewN == 2) {
    SmallVector<unsigned, 4> K{1, 0};
    if (accept(K))
      return K;
  }

  // CB load extra writeback: [dest, wb, base, sel, imm|rs] →
  // [dest, sel, base, imm|rs].
  if (OldDefs == NewDefs + 1 && NewDefs == 1 && OldN == NewN + 1 &&
      NewN >= 3) {
    SmallVector<unsigned, 4> Keep{0, 3, 2};
    for (unsigned I = 4; I < OldN && Keep.size() < NewN; ++I)
      Keep.push_back(I);
    if (accept(Keep))
      return Keep;
  }
  // CB load extra writeback, generated operand order (S2b golden logicals):
  // [dest, wb, sel, base, rs2] → [dest, sel, base, rs2]. The generated
  // D_LDW_CB_REG logical emits uimm1 cbr_sel as the FIRST ins operand
  // (HaydnInstrInfoGolden.td.inc), unlike the hand order above.
  if (OldDefs == NewDefs + 1 && NewDefs == 1 && OldN == NewN + 1 &&
      NewN >= 3) {
    SmallVector<unsigned, 4> Keep{0, 2, 3, 4};
    for (unsigned I = 5; I < OldN && Keep.size() < NewN; ++I)
      Keep.push_back(I);
    if (accept(Keep))
      return Keep;
  }
  // CB store extra writeback: [wb, data, base, sel, imm|rs] →
  // [sel, data, base, imm|rs].
  if (OldDefs == 1 && NewDefs == 0 && OldN == NewN + 1 && NewN >= 3) {
    SmallVector<unsigned, 4> Keep{3, 1, 2};
    for (unsigned I = 4; I < OldN && Keep.size() < NewN; ++I)
      Keep.push_back(I);
    if (accept(Keep))
      return Keep;
  }
  // CB store extra writeback, generated operand order (S2b golden logicals):
  // [wb, sel, data, base, imm] → [sel, data, base, imm]. Mirrors the
  // generated load order above (uimm cbr_sel first in ins).
  if (OldDefs == 1 && NewDefs == 0 && OldN == NewN + 1 && NewN >= 3) {
    SmallVector<unsigned, 4> Keep{1, 2, 3, 4};
    for (unsigned I = 5; I < OldN && Keep.size() < NewN; ++I)
      Keep.push_back(I);
    if (accept(Keep))
      return Keep;
  }

  // dest-as-ins skip first ins (LUI vestigial $rs).
  if (OldDefs == 1 && NewDefs == 0 && OldN == NewN + 1 && NewN >= 1) {
    SmallVector<unsigned, 4> Keep;
    Keep.push_back(0);
    for (unsigned NewI = 1; NewI != NewN; ++NewI)
      Keep.push_back(NewI + 1);
    if (accept(Keep))
      return Keep;
  }

  if (OldDefs == NewDefs && OldN > NewN) {
    bool AnyTiedUse = false;
    for (unsigned I = OldDefs; I != OldN; ++I) {
      const int Tie = OldDesc.getOperandConstraint(I, MCOI::TIED_TO);
      if (Tie >= 0 && static_cast<unsigned>(Tie) < OldDefs) {
        AnyTiedUse = true;
        break;
      }
    }
    if (!AnyTiedUse) {
      bool TrailingUses = true;
      for (unsigned I = NewN; I != OldN; ++I)
        if (I < OldDefs) {
          TrailingUses = false;
          break;
        }
      if (TrailingUses) {
        auto K = prefix(NewN);
        if (accept(K))
          return K;
      }
    }
  }

  if (OldDefs == NewDefs && OldN == NewN + 1 && OldDefs >= 1 &&
      OldDefs < NewN) {
    SmallVector<unsigned, 4> Keep;
    for (unsigned I = 0; I != OldDefs; ++I)
      Keep.push_back(I);
    for (unsigned NewI = OldDefs; NewI != NewN; ++NewI)
      Keep.push_back(NewI + 1);
    if (accept(Keep))
      return Keep;
  }

  // CSRW catalog is (uimm8, rs); some generated members list (rs, uimm8).
  // Closed two-op swap, not a class bag-sort.
  if (OldDefs == 0 && NewDefs == 0 && OldN == 2 && NewN == 2) {
    SmallVector<unsigned, 4> K{1, 0};
    if (accept(K))
      return K;
  }

  return std::nullopt;
}

const haydn::format_e::FormatEMemberRec *
haydnFindFormatEMemberByOpcode(unsigned Opc) {
  if (Opc == 0 || Opc == Haydn::NOP)
    return nullptr;
  for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
    if (FormatEMemberOpcodes[I] != Opc)
      continue;
    if (I >= haydn::format_e::FormatEMemberCount)
      return nullptr;
    const haydn::format_e::FormatEMemberRec &M =
        haydn::format_e::FormatEMembers[I];
    if (M.IsNop)
      return nullptr;
    return &M;
  }
  return nullptr;
}

bool haydnFillFormatEMemberInst(const haydn::format_e::FormatEMemberRec &Mem,
                                const MCInst &Logical, const MCInstrInfo &MII,
                                const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;

  // Residual FieldSlots never recover occupancy here. AIE MultiSlot alts
  // (AIEMCFormats.h:376-379) are generated; suffix peel is not a fill.
  {
    const StringRef LogName = MII.getName(Logical.getOpcode());
    if (LogName.ends_with("_S0") || LogName.ends_with("_S1") ||
        LogName.ends_with("_S2"))
      return false;
  }

  // Typed as-is path: SubInst is already the private Format E member opcode
  // with wire-shaped operands — copy Desc operands without bag-sort rebuild.
  if (Logical.getOpcode() == MemberOpc) {
    const MCInstrDesc &Desc = MII.get(MemberOpc);
    if (Logical.getNumOperands() < Desc.getNumOperands())
      return false;
    Out.clear();
    Out.setOpcode(MemberOpc);
    for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI)
      Out.addOperand(Logical.getOperand(OI));
    return true;
  }

  // Residual positional promote: when residual/slot-member operands already
  // match the private member Desc in count, order, and operand kind, copy
  // without bag-sort. Shape-mismatched residual falls through to keep-map.
  {
    const MCInstrDesc &Desc = MII.get(MemberOpc);
    const unsigned Need = Desc.getNumOperands();
    if (Logical.getNumOperands() == Need) {
      bool PosOk = true;
      for (unsigned OI = 0; OI != Need; ++OI) {
        const MCOperand &MO = Logical.getOperand(OI);
        const MCOperandInfo &Info = Desc.operands()[OI];
        const bool WantReg = Info.OperandType == MCOI::OPERAND_REGISTER ||
                             Info.RegClass >= 0;
        if (WantReg) {
          if (!MO.isReg()) {
            PosOk = false;
            break;
          }
          if (Info.RegClass >= 0 && MO.getReg() != Haydn::NoRegister &&
              !MRI.getRegClass(Info.RegClass).contains(MO.getReg())) {
            PosOk = false;
            break;
          }
        } else if (!MO.isImm() && !MO.isExpr()) {
          PosOk = false;
          break;
        }
      }
      if (PosOk) {
        Out.clear();
        Out.setOpcode(MemberOpc);
        for (unsigned OI = 0; OI != Need; ++OI)
          Out.addOperand(Logical.getOperand(OI));
        return true;
      }
    }
  }

  // Compiler extra-op cutover is Finalize keep-map, not this fill:
  //   * MOVE32/ABS32 trailing rs2 (3-op logical vs 2-op member)
  //   * tied MAC/MOVT acc ins when the logical carries more ops than
  //     the generated member
  // Hand-asm omitted rs2 is Imm 0; AR-UA POST / CB writeback change
  // NumDefs and stay in the closed keep-map below. Peer: AIE serializes
  // typed members as-is (AIEBaseMCCodeEmitter.cpp:45-68).
  {
    const MCInstrDesc &LogDesc = MII.get(Logical.getOpcode());
    const MCInstrDesc &MemDesc = MII.get(MemberOpc);
    const unsigned Need = MemDesc.getNumOperands();
    const unsigned Have = Logical.getNumOperands();
    if (LogDesc.getNumDefs() == MemDesc.getNumDefs() && Have != Need &&
        LogDesc.getNumOperands() > Need) {
      bool AnyTied = false;
      for (unsigned I = LogDesc.getNumDefs(); I != LogDesc.getNumOperands();
           ++I) {
        if (LogDesc.getOperandConstraint(I, MCOI::TIED_TO) >= 0) {
          AnyTied = true;
          break;
        }
      }
      bool TrailingExtraReg = false;
      for (unsigned I = Need; I < Have; ++I) {
        if (Logical.getOperand(I).isReg()) {
          TrailingExtraReg = true;
          break;
        }
      }
      if (AnyTied || TrailingExtraReg)
        return false;
    }
  }

  // Closed keep-map (same law as Finalize fieldSlotKeepOperands):
  // identity, tied-acc drop, trailing extra uses, dest-as-ins, CB
  // writeback, AR-UA POST (rs2/dir_sel unencoded). Not a class bag-sort.
  {
    const MCInstrDesc &OldDesc = MII.get(Logical.getOpcode());
    const MCInstrDesc &NewDesc = MII.get(MemberOpc);
    const unsigned OldN = OldDesc.getNumOperands();
    const unsigned NewN = NewDesc.getNumOperands();
    const unsigned Have = Logical.getNumOperands();
    auto kindOk = [&](unsigned OldI, unsigned NewI) -> bool {
      if (OldI >= Have)
        return false;
      const MCOperand &MO = Logical.getOperand(OldI);
      const MCOperandInfo &Info = NewDesc.operands()[NewI];
      const bool WantReg = Info.OperandType == MCOI::OPERAND_REGISTER ||
                           Info.RegClass >= 0;
      if (WantReg) {
        if (!MO.isReg())
          return false;
        if (Info.RegClass >= 0 && MO.getReg() != Haydn::NoRegister &&
            !MRI.getRegClass(Info.RegClass).contains(MO.getReg()))
          return false;
        return true;
      }
      return MO.isImm() || MO.isExpr();
    };
    auto emitKeep = [&](ArrayRef<unsigned> Keep) -> bool {
      if (Keep.size() != NewN)
        return false;
      for (unsigned NewI = 0; NewI != NewN; ++NewI)
        if (!kindOk(Keep[NewI], NewI))
          return false;
      Out.clear();
      Out.setOpcode(MemberOpc);
      for (unsigned NewI = 0; NewI != NewN; ++NewI)
        Out.addOperand(Logical.getOperand(Keep[NewI]));
      return true;
    };
    if (OldN == 0 && NewN == 0 && Have == 0) {
      Out.clear();
      Out.setOpcode(MemberOpc);
      return true;
    }
    // Parser may omit a tied writeback that is not in the AsmString
    // (d_lqhwua_post $rtd, $ar_sel, $rs1, $rs2, $dir_sel has no $rs1_wb).
    // kindOk already rejects keep indices past Have.
    if (OldN > 0 && NewN > 0 && Have > 0) {
      // AsmString may omit a logical ins register (LUI $rs, CSRR $rs).
      // The parser then defaults that slot to Imm 0. Drop those holes
      // before trailing-use so the real imm/expr is kept.
      if (OldDesc.getNumDefs() == NewDesc.getNumDefs() && OldN > NewN) {
        SmallVector<unsigned, 4> Keep;
        bool DroppedHole = false;
        for (unsigned I = 0; I != OldN; ++I) {
          const MCOperandInfo &OldInfo = OldDesc.operands()[I];
          const bool OldWantsReg =
              OldInfo.OperandType == MCOI::OPERAND_REGISTER ||
              OldInfo.RegClass >= 0;
          if (OldWantsReg && I < Have && !Logical.getOperand(I).isReg()) {
            DroppedHole = true;
            continue;
          }
          Keep.push_back(I);
        }
        if (DroppedHole && emitKeep(Keep))
          return true;
      }
      if (auto Keep = haydnFormatEKeepOperands(OldDesc, NewDesc, kindOk))
        if (emitKeep(*Keep))
          return true;
    }
  }

  // Class-bag reconstruction is deleted. AIE serializes typed members as-is
  // (AIEBaseMCCodeEmitter.cpp:45-68). Shape mismatch fails closed.
  return false;
}

namespace Haydn {
#define GET_FORMATS_FORMATS_DEFS
#include "HaydnGenFormats.inc"
} // end namespace Haydn

SlotBits HaydnMCFormats::getLegalSlots(unsigned Opc) const {
  // Alts-derived legality (AIE-shaped). OR of non-zero sparse alt indices
  // from getAlternateInstsOpcode — vector index == field/slot. Bundle/HR
  // placement is PlacementAlternative + tryAdd. Returns 0 when no alt table
  // row (standalone / pseudo) — callers treat 0 as "not a bundle-slot op".
  const std::vector<unsigned> *Alts = getAlternateInstsOpcode(Opc);
  if (!Alts)
    return 0;
  SlotBits Bits = 0;
  for (unsigned Index = 0, E = static_cast<unsigned>(Alts->size()); Index < E;
       ++Index)
    if ((*Alts)[Index] != 0)
      Bits |= (SlotBits(1) << Index);
  return Bits;
}

//===----------------------------------------------------------------------===//
// Slot-bitmask / slot-index bridge helpers
//===----------------------------------------------------------------------===//
//
// Residual S0/S1/S2 slot kinds map from Haydn::SLOT* FieldSlots bits.
// Product Format E entry kinds use E2_*/E3_* MCSlotKind values.

MCSlotKind haydnSlotMaskToKind(SlotBits Mask) {
  switch (Mask) {
  case Haydn::SLOT0:
    return MCSlotKind::Haydn_SLOT_S0;
  case Haydn::SLOT1:
    return MCSlotKind::Haydn_SLOT_S1;
  case Haydn::SLOT2:
    return MCSlotKind::Haydn_SLOT_S2;
  default:
    return MCSlotKind(MCSlotKind::SLOT_UNKNOWN);
  }
}

// Residual S0/S1/S2 MCSlotKind → Haydn::SLOT* FieldSlots bit. PlacementAlternative
// FieldSlots and PackingCandidates use SLOT0/1/2 (1/2/4); residual S* kinds sit
// at higher enum indices after E2/E3 entry kinds (5/6/7). Map explicitly so
// hints / ForceSlot do not compare 1<<Kind against FieldSlots.
SlotBits residualSlotKindToFieldSlots(MCSlotKind Kind) {
  if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_S0))
    return Haydn::SLOT0;
  if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_S1))
    return Haydn::SLOT1;
  if (Kind == MCSlotKind(MCSlotKind::Haydn_SLOT_S2))
    return Haydn::SLOT2;
  return 0;
}

//===----------------------------------------------------------------------===//
// member-opcode-aware helpers
//===----------------------------------------------------------------------===//
//
// AIE AIEBaseMCFormats.cpp:67-75: post-setDesc members use generated format
// desc slot identity, not a name suffix. Haydn overlay: Format E members
// publish EntryIdx on FormatEMemberRec (EncodedBytes is not a slot bit).
// Residual `_S*` FieldSlots are retired (0 defs); do not recover a slot
// from a name suffix.

int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII) {
  (void)MII;
  if (const haydn::format_e::FormatEMemberRec *Mem =
          haydnFindFormatEMemberByOpcode(Opc))
    return static_cast<int>(Mem->EntryIdx);
  return -1;
}

SlotBits HaydnMCFormatsWithMII::getLegalSlots(unsigned Opc) const {
  (void)MII;
  if (const haydn::format_e::FormatEMemberRec *Mem =
          haydnFindFormatEMemberByOpcode(Opc))
    return SlotBits(1) << Mem->EntryIdx;
  return HaydnMCFormats::getLegalSlots(Opc);
}

//===----------------------------------------------------------------------===//
// HaydnBaseMCFormats — base implementations (port of AIEBaseMCFormats.cpp)
//===----------------------------------------------------------------------===//

void HaydnBaseMCFormats::checkInstructionIsSupported(unsigned Opcode) const {
  assert(isSupportedInstruction(Opcode) && "Unsupported instruction");
  (void)Opcode;
}

const MCFormatDesc &
HaydnBaseMCFormats::getFormatDesc(unsigned Opcode) const {
  if (auto const TableIdx = getFormatDescIndex(Opcode)) {
    unsigned int TableIdxVal = TableIdx.value();
    const MCFormatDesc *Formats = getMCFormats();
    assert(Formats[TableIdxVal].getOpcode() == Opcode);
    return Formats[TableIdxVal];
  }
  // Trigger an unreachable if the data isn't available.
  LLVM_DEBUG(dbgs() << "Unsupported instruction: " << Opcode << "\n"
                    << "please verify that it isn't Pseudo/CodeGenOnly\n");
  llvm_unreachable("[HaydnMCFormats] Unsupported instruction");
}

bool HaydnBaseMCFormats::isSupportedInstruction(unsigned Opcode) const {
  // an opcode "participates in the slot/format model" iff EITHER:
  // (a) it has an entry in the GENERATED Formats table (format-members after
  // setDesc, MultiSlot_Pseudo logicals, Format E composites, …), OR
  // (b) it has PlacementAlternative legal slots
  // (`getLegalSlots(Opcode) != 0`) — logical multi-slot public opcodes.
  //
  // NOTE: getFormatDesc(Opcode) stays strict (generated table only). Bundle
  // uses getSlotKind for committed members and tryAddProduct for logicals.
  if (getFormatDescIndex(Opcode).has_value())
    return true;
  return getLegalSlots(Opcode) != 0;
}

MCSlotKind HaydnBaseMCFormats::getSlotKind(unsigned Opcode) const {
  // AIE AIEBaseMCFormats.cpp:66-75 — fixed slot of a single-slot format
  // member (post-setDesc identity). Multi-slot logicals / MultiSlot_Pseudo
  // return unknown so Bundle/HR use PlacementAlternative tryAdd instead.
  auto TableIdx = getFormatDescIndex(Opcode);
  if (!TableIdx)
    return MCSlotKind();
  const MCFormatDesc &Desc = getMCFormats()[*TableIdx];
  if (!Desc.hasSingleSlot())
    return MCSlotKind();
  return Desc.getSingleSlotKind();
}

const haydn::format::ObjectEncodingProfileDesc &
HaydnBaseMCFormats::getObjectEncodingProfile() const {
  return haydn::format::getProductionObjectEncodingProfile();
}

const haydn::format::BundleFormatRowDesc *
HaydnBaseMCFormats::getBundleFormatRow(
    haydn::format::BundleFormatRowID Row) const {
  return haydn::format::getBundleFormatRow(Row);
}

haydn::format::EncodedBytes HaydnBaseMCFormats::getEncodedBytes(
    haydn::format::BundleFormatRowID Row) const {
  return haydn::format::encodedBytesOrDie(Row);
}

haydn::format::EncodedBits HaydnBaseMCFormats::getEncodedBits(
    haydn::format::BundleFormatRowID Row) const {
  return haydn::format::encodedBitsOrDie(Row);
}

haydn::format::EncodedBytes
HaydnBaseMCFormats::getProductionMaxEncodedBytes() const {
  return haydn::format::maxEncodedBytesInProfile(
      haydn::format::ObjectEncodingProfileID::E96);
}

bool HaydnBaseMCFormats::isFormatAvailable(uint64_t SlotSet) const {
  ArrayRef<bool> Avail = getIsFormatAvailable();
  return SlotSet < Avail.size() && Avail[SlotSet];
}

//===----------------------------------------------------------------------===//
// HaydnMCFormats — concrete subclass
//===----------------------------------------------------------------------===//
//
// getSlotInfo / getFormatDescIndex are defined INSIDE this translation
// unit by HaydnGenFormats.inc. getAlternateInstsOpcode is occupancy +
// Format E members (HaydnGenAltOccupancy.inc). Declarations live on
// HaydnMCFormats in HaydnMCFormats.h.
//
// getSlotKind (AIE AIEBaseMCFormats.cpp:66-75): after setDesc materialize,
// Bundle canAdd/verify packs by fixed member slot — not tryAddProduct on
// logicals.

const MCFormatDesc *HaydnMCFormats::getMCFormats() const {
  return Haydn::Formats;
}

const PacketFormats &HaydnMCFormats::getPacketFormats() const {
  // Return the GENERATED PacketFormats table (HaydnGenFormats.inc
  // GET_FORMATS_PACKETS_TABLE region). Product rows are Format E composites
  // (BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY) only.
  return Formats;
}

ArrayRef<bool> HaydnMCFormats::getIsFormatAvailable() const {
  // Generated FormatAvailable LUT (AIE AIEMCFormats.cpp:65-67). SlotSet
  // identity is PacketFormats, not a child-count or underfill map.
  return ArrayRef<bool>(FormatAvailable, SlotSetSize);
}

//===----------------------------------------------------------------------===//
// Format E product parcel helpers
//===----------------------------------------------------------------------===//

haydn::format::EncodedBytes haydnProductionParcelBytes() {
  haydn::format::EncodedBytes B = haydn::format::maxEncodedBytesInProfile(
      haydn::format::ObjectEncodingProfileID::E96);
  // FE8: sole product is Format E 12-byte; non-E96 dual sizes are retired.
  assert(B.Value == 12u && "production EncodedBytes must be Format E 12");
  return B;
}

bool haydnHasCanonicalIdleParcel() {
  return !haydn::format::canonicalFullSlotIdleParcel().empty();
}

uint8_t haydnFormatEHeaderByte(unsigned EntryNum) {
  assert((EntryNum == haydn::format::FormatEEntryNumTwo ||
          EntryNum == haydn::format::FormatEEntryNumThree) &&
         "Format E entry_num must be 0 (E2) or 1 (E3)");
  // bits[2:0]=indicator 111, bit[3]=entry_num, bits[5:4]=reserved 00.
  return static_cast<uint8_t>(
      (haydn::format::FormatEIndicatorBits & 0x7u) |
      ((EntryNum & 0x1u) << 3));
}

void haydnEmitFormatEParcelLE(const APInt &Word, SmallVectorImpl<char> &CB) {
  haydn::format::EncodedBytes Parcel = haydnProductionParcelBytes();
  haydn::format::EncodedBits Bits = haydn::format::encodedBitsOrDie(
      haydn::format::BundleFormatRowID::E96TwoEntry);
  assert(Word.getBitWidth() == Bits.Value &&
         "Format E parcel APInt width must match production EncodedBits");
  assert(Parcel.Value * 8u == Bits.Value &&
         "production EncodedBytes must pack EncodedBits");
  const size_t Before = CB.size();
  for (unsigned Byte = 0; Byte < Parcel.Value; ++Byte) {
    uint64_t Chunk = Word.extractBitsAsZExtValue(8, Byte * 8);
    CB.push_back(static_cast<char>(Chunk & 0xFF));
  }
  assert(CB.size() - Before == Parcel.Value &&
         "Format E emit must append exactly product EncodedBytes");
}

bool haydnFormatEEntryWindow(uint8_t Mode, unsigned EntryIdx, unsigned &Width,
                             unsigned &LSB) {
  using namespace haydn::format_e;
  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    const FormatETypeLayoutRec &L = FormatETypeLayouts[I];
    if (L.Mode != Mode || L.EntryIdx != static_cast<uint8_t>(EntryIdx))
      continue;
    if (L.EntryHi < L.EntryLo)
      return false;
    Width = static_cast<unsigned>(L.EntryHi) - L.EntryLo + 1u;
    LSB = L.EntryLo;
    return true;
  }
  return false;
}

namespace {
constexpr bool formatEEntryWindowIs(uint8_t Mode, unsigned EntryIdx,
                                    unsigned ExpectW, unsigned ExpectL) {
  using namespace haydn::format_e;
  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    const FormatETypeLayoutRec &L = FormatETypeLayouts[I];
    if (L.Mode != Mode || L.EntryIdx != static_cast<uint8_t>(EntryIdx))
      continue;
    return (static_cast<unsigned>(L.EntryHi) - L.EntryLo + 1u) == ExpectW &&
           L.EntryLo == ExpectL;
  }
  return false;
}
static_assert(formatEEntryWindowIs(0, 0, 45, 6), "E2 e0 generated window");
static_assert(formatEEntryWindowIs(0, 1, 41, 51), "E2 e1 generated window");
static_assert(formatEEntryWindowIs(1, 0, 31, 6), "E3 e0 generated window");
static_assert(formatEEntryWindowIs(1, 1, 31, 37), "E3 e1 generated window");
static_assert(formatEEntryWindowIs(1, 2, 27, 68), "E3 e2 generated window");
} // namespace

bool haydnTryGetCanonicalIdleParcel(SmallVectorImpl<char> &Out) {
  if (!haydnHasCanonicalIdleParcel())
    return false;
  ArrayRef<uint8_t> Idle = haydn::format::canonicalFullSlotIdleParcel();
  if (Idle.empty() || Idle.size() != haydnProductionParcelBytes().Value)
    return false;
  Out.clear();
  Out.reserve(Idle.size());
  for (uint8_t B : Idle)
    Out.push_back(static_cast<char>(B));
  return true;
}

bool haydnWriteCanonicalIdlePad(raw_ostream &OS, uint64_t CountBytes) {
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  assert(Parcel != 0 && "production EncodedBytes must be non-zero");
  if (CountBytes % Parcel != 0)
    return false;
  if (CountBytes == 0)
    return true;
  if (!haydnHasCanonicalIdleParcel())
    return false;

  SmallVector<char, 16> Idle;
  if (!haydnTryGetCanonicalIdleParcel(Idle))
    return false;
  assert(Idle.size() == Parcel && "idle parcel size must match EncodedBytes");
  for (uint64_t Off = 0; Off < CountBytes; Off += Parcel)
    OS.write(Idle.data(), Idle.size());
  return true;
}

} // namespace llvm
