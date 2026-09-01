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
#include "HaydnRelocLayout.h"
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

bool haydnIsResidualFieldSlotName(StringRef Name) {
  // Leftover FieldSlot `*_S<digits>` (ABS64_S1). Occupancy must not recover a
  // catalog logical by stripping that suffix. AIE MultiSlot alts are generated
  // (AIEMCFormats.h:376-379) — no suffix table. Saturating public names
  // (ABS64S) do not end in `_S` + digits.
  StringRef Rest = Name;
  while (!Rest.empty() && Rest.back() >= '0' && Rest.back() <= '9')
    Rest = Rest.drop_back();
  if (Rest.size() == Name.size() || Rest.size() < 2)
    return false;
  return Rest.ends_with_insensitive("_S");
}

bool haydnIsGeneratedMemberName(StringRef Name) {
  // Parser/refusal law (W64 QW3): CASE-INSENSITIVE by design — this classifies
  // what a user TYPED (any case) so the AsmParser refuses member-form
  // spellings with the precise "private placement opcode" error. Opcode-name
  // member identity is the case-sensitive canonical
  // llvm::isGeneratedFormatEMemberName (HaydnFormatERecords.h); the two are
  // different laws and both are single-sourced.
  return Name.contains_insensitive("_E2_") ||
         Name.contains_insensitive("_E3_");
}

namespace {

StringRef occupancyOpcodeName(unsigned Opcode) {
  return StringRef(&HaydnInstrNameData[HaydnInstrNameIndices[Opcode]]);
}

bool isResidualFieldSlotName(StringRef Name) {
  return haydnIsResidualFieldSlotName(Name);
}

bool isGeneratedMemberName(StringRef Name) {
  return haydnIsGeneratedMemberName(Name);
}

/// Public mnemonic → golden catalog logical. Occupancy alias only — never
/// strips `_S0/_S1/_S2` or `_E2_/_E3_` (AIE MultiSlot alts,
/// AIEMCFormats.h:376-379). Empty = no alias.
std::string catalogPublicAlias(StringRef N) {
  auto is = [&](const char *A) { return N.equals_insensitive(A); };
  if (is("LD32") || is("LW"))
    return "S_LW_WITH_IMM";
  if (is("LD32_REG"))
    return "S_LW_WITH_REG";
  if (is("ST32") || is("SW"))
    return "S_SW_WITH_IMM";
  if (is("ST32_REG"))
    return "S_SW_WITH_REG";
  if (is("LD64"))
    return "D_LDW_WITH_IMM";
  if (is("LD64_REG"))
    return "D_LDW_WITH_REG";
  if (is("ST64"))
    return "D_SDW_WITH_IMM";
  if (is("ST64_REG"))
    return "D_SDW_WITH_REG";
  if (is("LD8") || is("LB"))
    return "S_LBS_WITH_IMM";
  if (is("LD8_REG"))
    return "S_LBS_WITH_REG";
  if (is("LDU8") || is("LBU"))
    return "S_LBU_WITH_IMM";
  if (is("LDU8_REG"))
    return "S_LBU_WITH_REG";
  if (is("ST8") || is("SB"))
    return "S_SB_WITH_IMM";
  if (is("ST8_REG"))
    return "S_SB_WITH_REG";
  if (is("LD16") || is("LH") || is("LHWS"))
    return "S_LHWS_WITH_IMM";
  if (is("LD16_REG"))
    return "S_LHWS_WITH_REG";
  if (is("LDU16") || is("LHU") || is("LHWU"))
    return "S_LHWU_WITH_IMM";
  if (is("LDU16_REG"))
    return "S_LHWU_WITH_REG";
  if (is("ST16") || is("SH") || is("SHW"))
    return "S_SHW_WITH_IMM";
  if (is("ST16_REG"))
    return "S_SHW_WITH_REG";
  if (is("LD32_POST") || is("LD32_POST_INC"))
    return "S_LW_POST_IMM";
  if (is("ST32_POST") || is("ST32_POST_INC"))
    return "S_SW_POST_IMM";
  if (is("LD32_PRE") || is("LD32_PRE_INC"))
    return "S_LW_PRE_IMM";
  if (is("ST32_PRE") || is("ST32_PRE_INC"))
    return "S_SW_PRE_IMM";
  if (is("LD64_POST"))
    return "D_LDW_POST_IMM";
  if (is("ST64_POST"))
    return "D_SDW_POST_IMM";
  if (is("SEXT_GPR32_TO_DR64") || is("SEXT32T64"))
    return "SEXT32T64";
  if (is("MOV_GPR_TO_DR64") || is("MOVE_GPR_TO_DR64") ||
      is("ZEXT_GPR32_TO_DR64"))
    return "SEXT32T64";
  if (is("RET"))
    return "JALR";
  if (is("PLDWWUA"))
    return "PLDWWUA_POST";
  if (is("WFI") || is("WFITBDTBDTBD"))
    return "WFI<TBD>";
  return {};
}

/// Catalog occupancy key. Refuses residual FieldSlots and generated members
/// (no `_S*` / `_E2_` name peel — AIE MultiSlot alts, AIEMCFormats.h:376-379).
/// Reloc `_W` compact span, MultiSlot `_MSP` compact name, then public aliases
/// (LD32 → S_LW_WITH_IMM). Empty = fail closed (do not invent TWO vs THREE
/// from an unknown name).
std::string catalogOccupancyName(StringRef Raw) {
  if (Raw.empty())
    return {};
  if (Raw.equals_insensitive("NOP"))
    return "NOP";
  if (isResidualFieldSlotName(Raw) || isGeneratedMemberName(Raw))
    return {};
  auto spanOf = [](StringRef N) -> const haydn::format_e::FormatEAltSpan * {
    if (N.empty())
      return nullptr;
    return haydn::format_e::findAltSpan(N.str().c_str());
  };
  if (spanOf(Raw))
    return Raw.str();

  StringRef Compact = Raw;
  if (Compact.ends_with("_MSP"))
    Compact = Compact.drop_back(4);
  // Residual public LS_REG shells still carry occupancy-class `_M*S*LS`
  // (HaydnInstrInfo.td LD32_REG_M0S0LS). That is the public matcher name,
  // not a FieldSlot `_S0/_S1/_S2` entry certification (AIE MultiSlot alts,
  // AIEMCFormats.h:376-379).
  for (StringRef Suf : {"_M0S0LS", "_M0S1LS", "_M0S2LS", "_M1S0LS",
                        "_M1S1LS", "_M1S2LS"}) {
    if (Compact.ends_with(Suf)) {
      Compact = Compact.drop_back(Suf.size());
      break;
    }
  }
  if (Compact.ends_with("_F2_W"))
    Compact = Compact.drop_back(2);
  else if (Compact.ends_with("_W"))
    Compact = Compact.drop_back(2);
  if (Compact.size() != Raw.size()) {
    if (isResidualFieldSlotName(Compact) || isGeneratedMemberName(Compact))
      return {};
    if (spanOf(Compact))
      return Compact.str();
  }

  auto acceptAlias = [&](std::string Aliased) -> std::string {
    if (Aliased.empty())
      return {};
    if (StringRef(Aliased).equals_insensitive("NOP"))
      return "NOP";
    if (isResidualFieldSlotName(Aliased) || isGeneratedMemberName(Aliased))
      return {};
    if (spanOf(Aliased))
      return Aliased;
    return {};
  };
  if (std::string A = acceptAlias(catalogPublicAlias(Raw)); !A.empty())
    return A;
  if (Compact.size() != Raw.size())
    return acceptAlias(catalogPublicAlias(Compact));
  return {};
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
  const std::string Log = catalogOccupancyName(Raw);
  if (Log.empty())
    return 0;
  if (StringRef(Log).equals_insensitive("NOP"))
    return formatENopMemberAtIndex(Index);
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Log.c_str());
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
  const std::string Log = catalogOccupancyName(occupancyOpcodeName(Opcode));
  if (Log.empty() || StringRef(Log).equals_insensitive("NOP"))
    return false;
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
  // generated Mode-only membership + assignFormatEMemberEntries (MemberId
  // + EntryIdx). Extra NOP pads are not occupancy.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  SmallVector<std::string, 3> Logs;
  bool AnyE3Only = false;
  bool AnyE2Only = false;
  for (unsigned Opc : LogicalOpcodes) {
    if (Opc == 0 || Opc == Haydn::NOP)
      continue;
    const StringRef Name = MII.getName(Opc);
    if (isResidualFieldSlotName(Name) || isGeneratedMemberName(Name) ||
        haydnFindFormatEMemberByOpcode(Opc))
      return 0;
    // Catalog occupancy / MemberId span. Empty is fail-closed — do not
    // treat an unknown name as unconstrained and then pick TWO vs THREE
    // from child count.
    std::string Log = catalogOccupancyName(Name);
    if (Log.empty())
      return 0;
    if (StringRef(Log).equals_insensitive("NOP"))
      continue;
    Logs.emplace_back(std::move(Log));
    if (haydnFormatELogicalIsE3Only(Opc))
      AnyE3Only = true;
    if (haydnFormatELogicalIsE2Only(Opc))
      AnyE2Only = true;
  }
  if (AnyE2Only && AnyE3Only)
    return 0;
  // All-NOP text is the generated E2 idle cycle (NOP in every E2 entry).
  // Not a child-count TWO vs THREE choice.
  if (Logs.empty())
    return haydnHasCanonicalIdleParcel() ? Haydn::BUNDLE_E96_TWO_ENTRY : 0;
  // Membership assignment is row identity. Do not admit E2 because N<=2
  // or E3 because N<=3 — assignFormatEMemberEntries fails closed when the
  // generated members cannot occupy that Mode's entries.
  const bool PlaceE2 =
      haydn::format_e::assignFormatEMemberEntries(Logs, /*Mode=*/0).has_value();
  const bool PlaceE3 =
      haydn::format_e::assignFormatEMemberEntries(Logs, /*Mode=*/1).has_value();
  if (AnyE3Only)
    return PlaceE3 ? Haydn::BUNDLE_E96_THREE_ENTRY : 0;
  if (AnyE2Only)
    return PlaceE2 ? Haydn::BUNDLE_E96_TWO_ENTRY : 0;
  // PacketFormats first-covering (AIE getFormat): smaller product row when
  // both Modes place.
  if (PlaceE2)
    return Haydn::BUNDLE_E96_TWO_ENTRY;
  if (PlaceE3)
    return Haydn::BUNDLE_E96_THREE_ENTRY;
  return 0;
}

const haydn::format::BundleFormatRowDesc *
haydnFormatERowForCompositeOpcode(unsigned Opcode) {
  using namespace haydn::format;
  if (Opcode == Haydn::BUNDLE_E96_TWO_ENTRY)
    return getBundleFormatRow(BundleFormatRowID::E96TwoEntry);
  if (Opcode == Haydn::BUNDLE_E96_THREE_ENTRY)
    return getBundleFormatRow(BundleFormatRowID::E96ThreeEntry);
  return nullptr;
}

std::string haydnCatalogOccupancyName(StringRef Raw) {
  return catalogOccupancyName(Raw);
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

  // AR unaligned POST load (2026-08-21 tied members): [rtd, rs1_wb, rs1,
  // rs2, ar_sel, dir_sel] → [dest1, dest2_wb, ar_sel, dest2]. The member
  // now declares the golden rs writeback (dest2_wb tied to dest2), so the
  // unencoded rs2/dir_sel drop keeps NumDefs aligned.
  if (OldDefs == 2 && NewDefs == 2 && OldN == 6 && NewN == 4) {
    SmallVector<unsigned, 4> K{0, 1, 4, 2};
    if (accept(K))
      return K;
  }
  // AR unaligned POST store (tied members): [rs1_wb, rtd, rs1, rs2,
  // ar_sel, dir_sel] → [dest2_wb, ar_sel, dest1, dest2].
  if (OldDefs == 1 && NewDefs == 1 && OldN == 6 && NewN == 4) {
    SmallVector<unsigned, 4> K{0, 4, 1, 2};
    if (accept(K))
      return K;
  }
  // Hand CB load imm swap (tied members): hand D_LDW_CB_IMM lists
  // [rtd, rs_wb, rs, cbr_sel, imm] while the tied member lists
  // [dest1, dest2_wb, cbr_sel, dest2, imm] — same classes, positions 2/3
  // swapped. Generated CB logicals match their members positionally and
  // never reach here.
  if (OldDefs == 2 && NewDefs == 2 && OldN == 5 && NewN == 5) {
    SmallVector<unsigned, 4> K{0, 1, 3, 2, 4};
    if (accept(K))
      return K;
  }
  // Hand shell omits a tied writeback the member declares (PLDWWUA vs
  // tied PLDWWUA_POST member): [ar_sel, rs] → [dest2_wb, ar_sel, dest2].
  // The member's tied use names the logical operand that also feeds the
  // synthetic wb def, so both map to the same old index.
  if (NewN == OldN + 1 && NewDefs == OldDefs + 1) {
    for (unsigned NewI = NewDefs; NewI != NewN; ++NewI) {
      const int Tie = NewDesc.getOperandConstraint(NewI, MCOI::TIED_TO);
      if (Tie < 0 || static_cast<unsigned>(Tie) >= NewDefs)
        continue;
      SmallVector<unsigned, 4> Keep(NewN, 0);
      unsigned OldUse = OldDefs;
      for (unsigned J = NewDefs; J != NewN; ++J)
        Keep[J] = OldUse++;
      Keep[static_cast<unsigned>(Tie)] = Keep[NewI];
      if (accept(Keep))
        return Keep;
    }
  }
  // WBARWUA: [rs, ar_sel, dir_sel] → [ar_sel, dest2].
  if (OldDefs == 0 && NewDefs == 0 && OldN == 3 && NewN == 2) {
    SmallVector<unsigned, 4> K{1, 0};
    if (accept(K))
      return K;
  }

  // CB load extra writeback: [dest, wb, base, sel, imm|rs] →
  // [dest, sel, base, imm|rs].
  // 2026-08-21: inert after the golden base-writeback tie cutover —
  // CB members now declare dest2_wb (defs align with their logicals and
  // the Exact/prefix paths bind them). Kept fail-closed for any future
  // no-wb member shape; no family currently matches.
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

unsigned haydnFormatEHwloopImmFieldShift(int MemberId) {
  using namespace haydn::format_e;
  if (MemberId < 0 || static_cast<unsigned>(MemberId) >= FormatEMemberCount)
    return 0;
  const FormatEMemberRec &Mem = FormatEMembers[MemberId];
  if (!Mem.TypeName)
    return 0;
  const StringRef TypeName = Mem.TypeName;
  if (TypeName != "HWLRIII" && TypeName != "HWLRIIR")
    return 0;
  // Off1 is 6 bits; Off2 is 12. Both layout rows share ValueShift=2.
  const HaydnReloc::FixupField Field{HaydnReloc::kUnspecifiedFieldLsb, 6};
  const HaydnReloc::RelocKind R = HaydnReloc::findFixupFromFixupFields(
      TypeName, Mem.Opcode, Field, /*FormatBytes=*/12,
      /*IsLSUnit=*/false);
  if (R == HaydnReloc::RelocKind::Invalid)
    return 0;
  return HaydnReloc::getRelocFieldInfo(R).ValueShift;
}

/// Logical hwloop_off operands are dump bytes; generated HWLR members encode
/// field units (uimm6/uimm12). Convert dump bytes >> ValueShift. Already-member
/// as-is fill must not convert (fields are already units).
static bool applyHwloopDumpBytesToMemberFields(
    const haydn::format_e::FormatEMemberRec &Mem, MCInst &Out) {
  const unsigned Shift = haydnFormatEHwloopImmFieldShift(Mem.MemberId);
  if (Shift == 0)
    return true;
  const unsigned Align = 1u << Shift;
  for (unsigned OI : {1u, 2u}) {
    if (OI >= Out.getNumOperands())
      break;
    MCOperand &Op = Out.getOperand(OI);
    if (!Op.isImm())
      continue;
    const int64_t Imm = Op.getImm();
    if (Imm < 0 || (static_cast<uint64_t>(Imm) & (Align - 1u)))
      return false;
    Op.setImm(Imm >> Shift);
  }
  return true;
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

bool haydnIsCompilerKeepMapExtraOp(
    const haydn::format_e::FormatEMemberRec &Mem, const MCInst &Logical,
    const MCInstrInfo &MII) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;
  const MCInstrDesc &LogDesc = MII.get(Logical.getOpcode());
  const MCInstrDesc &MemDesc = MII.get(MemberOpc);
  const unsigned Need = MemDesc.getNumOperands();
  const unsigned Have = Logical.getNumOperands();
  if (Have == Need || LogDesc.getNumOperands() <= Need)
    return false;

  // Compiler LUI vestigial $rs: extra register at first ins (index
  // NumDefs) where the member wants an imm. Runs before NumDefs
  // equality so dest-as-ins members (NumDefs 1->0) still fail closed.
  // Hand-asm omitted $rs is Imm 0 and stays in standalone Imm-0 fill.
  if (Have == Need + 1 && LogDesc.getNumDefs() >= 1) {
    const unsigned Mid = LogDesc.getNumDefs();
    if (Mid < Need && Mid < Have) {
      const MCOperandInfo &MemMid = MemDesc.operands()[Mid];
      const bool MemMidWantsReg =
          MemMid.OperandType == MCOI::OPERAND_REGISTER || MemMid.RegClass >= 0;
      if (Logical.getOperand(Mid).isReg() && !MemMidWantsReg)
        return true;
    }
    // dest-as-ins extra $rs at logical index 1 (member has no defs).
    if (LogDesc.getNumDefs() == 1 && MemDesc.getNumDefs() == 0 && Have > 1 &&
        Logical.getOperand(1).isReg())
      return true;
  }

  if (LogDesc.getNumDefs() != MemDesc.getNumDefs())
    return false;
  // 2026-08-21: a tied logical ins is only a compiler extra when the member
  // does NOT model the same tie. Since the golden base-writeback cutover
  // (UA `_POST` suffix + CB families), members declare dest2_wb tied to
  // dest2 exactly like their logicals; those fills belong to the standalone
  // keep-map, not to Finalize.
  bool MemberModelsAnyTie = false;
  for (unsigned I = MemDesc.getNumDefs(); I != MemDesc.getNumOperands(); ++I) {
    if (MemDesc.getOperandConstraint(I, MCOI::TIED_TO) >= 0) {
      MemberModelsAnyTie = true;
      break;
    }
  }
  if (!MemberModelsAnyTie) {
    for (unsigned I = LogDesc.getNumDefs(); I != LogDesc.getNumOperands();
         ++I) {
      if (LogDesc.getOperandConstraint(I, MCOI::TIED_TO) >= 0)
        return true;
    }
  }
  for (unsigned I = Need; I < Have; ++I) {
    if (Logical.getOperand(I).isReg())
      return true;
  }
  return false;
}

static bool formatEMemberPositionalKindsOk(const MCInstrDesc &Desc,
                                           const MCInst &Logical,
                                           const MCRegisterInfo &MRI) {
  const unsigned Need = Desc.getNumOperands();
  if (Logical.getNumOperands() != Need)
    return false;
  for (unsigned OI = 0; OI != Need; ++OI) {
    const MCOperand &MO = Logical.getOperand(OI);
    const MCOperandInfo &Info = Desc.operands()[OI];
    const bool WantReg =
        Info.OperandType == MCOI::OPERAND_REGISTER || Info.RegClass >= 0;
    if (WantReg) {
      if (!MO.isReg())
        return false;
      if (Info.RegClass >= 0 && MO.getReg() != Haydn::NoRegister &&
          !MRI.getRegClass(Info.RegClass).contains(MO.getReg()))
        return false;
    } else if (!MO.isImm() && !MO.isExpr()) {
      return false;
    }
  }
  return true;
}

bool haydnFillFormatEMemberInstPositional(
    const haydn::format_e::FormatEMemberRec &Mem, const MCInst &Logical,
    const MCInstrInfo &MII, const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;

  // FieldSlot, committed MemberId, and compiler extra-op never reconstruct.
  // AIE lowers as-is (AIEBaseAsmPrinter.cpp:166-177) and serializes typed
  // members as-is (AIEBaseMCCodeEmitter.cpp:45-68).
  const unsigned LogOpc = Logical.getOpcode();
  if (isResidualFieldSlotName(MII.getName(LogOpc)))
    return false;
  if (LogOpc == MemberOpc || haydnFindFormatEMemberByOpcode(LogOpc))
    return false;
  const MCInstrDesc &LogDesc = MII.get(LogOpc);
  if (Logical.getNumOperands() > LogDesc.getNumOperands())
    return false;
  if (haydnIsCompilerKeepMapExtraOp(Mem, Logical, MII))
    return false;

  const MCInstrDesc &Desc = MII.get(MemberOpc);
  const unsigned Need = Desc.getNumOperands();
  const unsigned Have = Logical.getNumOperands();
  if (Need == 0 && Have == 0) {
    Out.clear();
    Out.setOpcode(MemberOpc);
    return applyHwloopDumpBytesToMemberFields(Mem, Out);
  }
  if (!formatEMemberPositionalKindsOk(Desc, Logical, MRI))
    return false;
  Out.clear();
  Out.setOpcode(MemberOpc);
  for (unsigned OI = 0; OI != Need; ++OI)
    Out.addOperand(Logical.getOperand(OI));
  return applyHwloopDumpBytesToMemberFields(Mem, Out);
}

bool haydnFillFormatEMemberInst(const haydn::format_e::FormatEMemberRec &Mem,
                                const MCInst &Logical, const MCInstrInfo &MII,
                                const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;

  // FieldSlot, committed MemberId, and compiler extra-op never reconstruct.
  // Residual FieldSlots never recover occupancy here. AIE MultiSlot alts
  // (AIEMCFormats.h:376-379) are generated; suffix peel is not a fill.
  const unsigned LogOpc = Logical.getOpcode();
  if (isResidualFieldSlotName(MII.getName(LogOpc)))
    return false;
  if (LogOpc == MemberOpc || haydnFindFormatEMemberByOpcode(LogOpc))
    return false;
  const MCInstrDesc &LogDesc = MII.get(LogOpc);
  if (Logical.getNumOperands() > LogDesc.getNumOperands())
    return false;
  if (haydnIsCompilerKeepMapExtraOp(Mem, Logical, MII))
    return false;

  if (haydnFillFormatEMemberInstPositional(Mem, Logical, MII, MRI, Out))
    return true;

  auto finishLogicalFill = [&]() -> bool {
    return applyHwloopDumpBytesToMemberFields(Mem, Out);
  };

  // Standalone-only closed keep-map: Imm-0 hole, CB writeback, AR-UA POST,
  // WBARWUA, CSRW swap. Compiler extras (MOVE32 trailing, tied MAC, LUI
  // vestigial $rs) already returned false above.
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
      return finishLogicalFill();
    }
    // Parser may omit a tied writeback that is not in the AsmString
    // (d_lqhwua_post $rtd, $ar_sel, $rs1, $rs2, $dir_sel has no $rs1_wb).
    // 2026-08-21: members now model the golden rs writeback (dest2_wb
    // tied to dest2, incl. UA `_POST` suffix + CB families); the keep-map
    // branches in haydnFormatEKeepOperands reconstruct them.
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
          return finishLogicalFill();
      }
      if (auto Keep = haydnFormatEKeepOperands(OldDesc, NewDesc, kindOk)) {
        const unsigned OldDefs = OldDesc.getNumDefs();
        const unsigned NewDefs = NewDesc.getNumDefs();
        // Compiler LUI vestigial $rs keep-vector: drop the first ins after
        // defs. Hand-asm LUI is positional and never reaches here.
        if (OldDefs == NewDefs && OldN == NewN + 1 && OldDefs >= 1 &&
            OldDefs < NewN && Keep->size() == NewN) {
          bool DropsFirstIns = (*Keep)[0] == 0;
          for (unsigned NewI = OldDefs; NewI != NewN && DropsFirstIns; ++NewI)
            if ((*Keep)[NewI] != NewI + 1)
              DropsFirstIns = false;
          if (DropsFirstIns)
            return false;
        }
        // dest-as-ins extra $rs keep-vector: skip first ins after dest
        // (Keep = [0, 2, 3, ...]). CB store extra writeback is NumDefs
        // 1->0 with a reorder keep ({3,1,2} / {1,2,3,4}) and stays.
        if (OldDefs == 1 && NewDefs == 0 && OldN == NewN + 1 &&
            Keep->size() == NewN && (*Keep)[0] == 0) {
          bool DropsFirstIns = true;
          for (unsigned NewI = 1; NewI != NewN && DropsFirstIns; ++NewI)
            if ((*Keep)[NewI] != NewI + 1)
              DropsFirstIns = false;
          if (DropsFirstIns)
            return false;
        }
        if (emitKeep(*Keep))
          return finishLogicalFill();
      }
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
