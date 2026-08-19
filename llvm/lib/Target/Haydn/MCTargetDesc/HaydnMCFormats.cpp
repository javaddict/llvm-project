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
// (BUNDLE_E96_*). Residual `_S*` members still contribute slot ConflictBits
// via their InstFormat Slot tags. getPacketFormats/getIsFormatAvailable return
// the GENERATED tables only.
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
/// pick the wrong one. No match → 0 so occupancy keeps the FieldSlot Fallback.
/// EntryIdx is not a new SLOT bit — occupancy stays the residual mask.
/// Reloc `_W` logicals share the compact catalog span (ADDI32_W → ADDI32).
unsigned formatEMemberAtResidualIndex(unsigned LogicalOpc, unsigned Index) {
  std::string Log = haydn::format_e::peelLogicalOpcodeName(
      occupancyOpcodeName(LogicalOpc), /*StripWide=*/false);
  if (StringRef(Log).equals_insensitive("NOP"))
    return formatENopMemberAtIndex(Index);
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Log.c_str());
  if (!Span || Span->Count == 0) {
    Log = haydn::format_e::peelLogicalOpcodeName(
        occupancyOpcodeName(LogicalOpc), /*StripWide=*/true);
    if (StringRef(Log).equals_insensitive("NOP"))
      return formatENopMemberAtIndex(Index);
    Span = haydn::format_e::findAltSpan(Log.c_str());
  }
  if (!Span || Span->Count == 0)
    return 0;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  if (LogicalOpc >= MII.getNumOpcodes())
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
  // E2/E3 is a bundle-level fact (child count). Occupancy defaults to E2
  // when both Modes match the logical shape; E3-only entries (ALU32 e1/e2)
  // keep the E3 member. Finalize rebinds to the committed row.
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
        else if (Row.Fallback[Slot] != 0)
          Cache[I][Slot] = Row.Fallback[Slot];
        else {
          // WFI: Mask bit 0 + Fallback {0,0,0}. A miss used to cache 0
          // (silent skip). Fail closed — HINT members must fill the hole.
          // Other retired families still skip (mask bit, no member).
          const std::string Log = haydn::format_e::peelLogicalOpcodeName(
              occupancyOpcodeName(Row.LogicalOpc));
          if (StringRef(Log).starts_with_insensitive("WFI"))
            report_fatal_error(
                "Haydn: WFI occupancy hole: mask bit " + Twine(Slot) +
                    " has no Format E HINT member and zero fallback",
                /*GenCrashDiag=*/false);
          Cache[I][Slot] = 0;
        }
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
  const std::string Log = haydn::format_e::peelLogicalOpcodeName(
      occupancyOpcodeName(Opcode));
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Log.c_str());
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
// Base getLegalSlots only has rows for logicals; strip `_S<k>` to recover the
// logical base before the alts query (member-aware MC / residual placement).

namespace {

// `_S<k>` name-suffix table — the slot digit is authoritative.
constexpr StringRef HaydnMemberSlotSuffix[3] = {"_S0", "_S1", "_S2"};

// \returns the slot index (0/1/2) encoded in \p Opc's `_S<k>` name
// suffix, or -1 if \p Opc is not a format-member opcode.
int getMemberSlotFromNameLocal(unsigned Opc, const MCInstrInfo &MII) {
  StringRef Name = MII.getName(Opc);
  for (int Slot = 0; Slot < 3; ++Slot) {
    StringRef Suffix = HaydnMemberSlotSuffix[Slot];
    if (Name.ends_with(Suffix))
      return Slot;
  }
  return -1;
}

// \returns the logical base opcode for \p Opc by stripping any `_S<k>`
// suffix, or \p Opc itself if it has no suffix. Base found by NAME lookup.
unsigned getLogicalBaseOpcode(unsigned Opc, const MCInstrInfo &MII) {
  StringRef Name = MII.getName(Opc);
  StringRef Base = Name;
  bool Stripped = false;
  for (StringRef Suf : HaydnMemberSlotSuffix) {
    if (Base.ends_with(Suf)) {
      Base = Base.drop_back(Suf.size());
      Stripped = true;
      break;
    }
  }
  if (!Stripped)
    return Opc; // already logical
  if (Base.empty())
    return 0;
  unsigned Num = MII.getNumOpcodes();
  for (unsigned Cand = 0; Cand < Num; ++Cand)
    if (MII.getName(Cand) == Base)
      return Cand;
  return 0;
}

} // end anonymous namespace

int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII) {
  return getMemberSlotFromNameLocal(Opc, MII);
}

SlotBits HaydnMCFormatsWithMII::getLegalSlots(unsigned Opc) const {
  // Normalize a member opcode to its logical base, then consult alts-derived
  // getLegalSlots. For a logical Opc this is a passthrough.
  unsigned BaseOpc = getLogicalBaseOpcode(Opc, MII);
  if (BaseOpc != 0) {
    SlotBits Bits = HaydnMCFormats::getLegalSlots(BaseOpc);
    if (Bits != 0)
      return Bits;
  }
  // Member opcodes whose stripped base is not a live logical (or has no alt
  // row): the `_S<k>` suffix is authoritative — that single slot is legal.
  int Slot = getHaydnFlexSlotFromName(Opc, MII);
  if (Slot >= 0)
    return SlotBits(1) << Slot;
  return 0;
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
  // Generated FormatAvailable LUT: SlotSet is available iff some product
  // Format E packet format covers it (or is a subset with NOP underfill).
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
