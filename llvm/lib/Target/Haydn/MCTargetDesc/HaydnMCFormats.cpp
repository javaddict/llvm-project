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
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <iterator>
#include <mutex>
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

/// Format E member at residual occupancy index \p Index whose MCInstrDesc
/// matches the logical (NumDefs + operands). One logical can have two golden
/// shapes at the same EntryIdx (SLT64 unary dest+src vs SFR-only 2-src;
/// X2SLT32 is the SFR-only shape). First-match and "smallest OperandCount"
/// pick the wrong one. No match → 0 so occupancy keeps the FieldSlot Fallback.
/// EntryIdx is not a new SLOT bit — occupancy stays the residual mask.
unsigned formatEMemberAtResidualIndex(unsigned LogicalOpc, unsigned Index) {
  const std::string Log = haydn::format_e::peelLogicalOpcodeName(
      occupancyOpcodeName(LogicalOpc), /*StripWide=*/false);
  const haydn::format_e::FormatEAltSpan *Span =
      haydn::format_e::findAltSpan(Log.c_str());
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
    if (MemDesc.getNumDefs() != LogDesc.getNumDefs())
      continue;
    const bool IsE2 = Mem.Mode == 0;
    if (MemDesc.getNumOperands() == LogDesc.getNumOperands() &&
        operandClassesMatch(LogDesc, MemDesc)) {
      if (IsE2 && ExactE2 == 0)
        ExactE2 = Opc;
      else if (!IsE2 && ExactE3 == 0)
        ExactE3 = Opc;
      continue;
    }
    // Tied-seed / trailing-use drop (X2MOVT32 3-op logical → 2-op member).
    if (MemDesc.getNumOperands() > 0 &&
        MemDesc.getNumOperands() < LogDesc.getNumOperands()) {
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
        else
          Cache[I][Slot] = Row.Fallback[Slot];
      }
    }
  });
  return &Cache[static_cast<size_t>(Idx)];
}

} // namespace

const std::vector<unsigned> *
HaydnMCFormats::getAlternateInstsOpcode(unsigned Opcode) const {
  return cachedMemberAlts(Opcode);
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

void haydnEmitFormatEParcelLE(const APInt &Word96, SmallVectorImpl<char> &CB) {
  haydn::format::EncodedBytes Parcel = haydnProductionParcelBytes();
  haydn::format::EncodedBits Bits = haydn::format::encodedBitsOrDie(
      haydn::format::BundleFormatRowID::E96TwoEntry);
  assert(Parcel.Value == 12u && Bits.Value == 96u &&
         "FE8: emit only Format E 12-byte / 96-bit parcels");
  assert(Word96.getBitWidth() == Bits.Value &&
         "Format E parcel APInt width must match production EncodedBits");
  assert(Parcel.Value * 8u == Bits.Value &&
         "production EncodedBytes must pack EncodedBits");
  // Little-endian: bit 0 in byte 0 … bit 95 in byte 11.
  const size_t Before = CB.size();
  for (unsigned Byte = 0; Byte < Parcel.Value; ++Byte) {
    uint64_t Chunk = Word96.extractBitsAsZExtValue(8, Byte * 8);
    CB.push_back(static_cast<char>(Chunk & 0xFF));
  }
  assert(CB.size() - Before == Parcel.Value &&
         "Format E emit must append exactly product EncodedBytes");
}

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
