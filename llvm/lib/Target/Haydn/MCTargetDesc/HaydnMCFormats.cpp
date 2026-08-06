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
// GET_ALTERNATE_INST_OPCODE_FUNC regions are pulled in at namespace scope
// (function bodies for getSlotInfo/getFormatDescIndex/getAlternateInstsOpcode
// drop in here, declaring the very methods of HaydnMCFormats).
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
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>

namespace llvm {

#undef DEBUG_TYPE
#define DEBUG_TYPE "haydn-mcformats"

//===----------------------------------------------------------------------===//
// Generated format tables + member-function bodies (AIE pattern)
//===----------------------------------------------------------------------===//
//
// The GET_FORMATS_* includes define: HaydnSlots (slot descriptors)
// getSlotInfo / getFormatDescIndex / getAlternateInstsOpcode method bodies
// (these are the declarations on HaydnMCFormats in HaydnMCFormats.h), the
// packet-format tables, and the per-opcode Formats (returned by
// getMCFormats). Mirrors AIEMCFormats.cpp:18-29.

// GET_FORMATS_PACKETS_TABLE is CONSUMED. Product composites are Format E
// (BUNDLE_E96_*). Residual `_S*` members still contribute slot ConflictBits
// via their InstFormat Slot tags. getPacketFormats/getIsFormatAvailable return
// the GENERATED tables only.
#define GET_FORMATS_PACKETS_TABLE
#define GET_FORMATS_SLOTS_DEFS
#define GET_FORMATS_SLOTINFOS_MAPPING
#define GET_OPCODE_FORMATS_INDEX_FUNC
#define GET_ALTERNATE_INST_OPCODE_FUNC
// Materialize authority is AlternateInsts + setDesc; MC encode serializes
// member Desc (AIEBaseMCCodeEmitter.cpp:134-162).
#include "HaydnGenFormats.inc"

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
// getSlotInfo / getFormatDescIndex / getAlternateInstsOpcode are defined INSIDE
// this translation unit by HaydnGenFormats.inc (included above); their
// declarations live on HaydnMCFormats in HaydnMCFormats.h. The remaining
// overrides delegate to the generated / hand-authored tables.
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
  // Provisional product idle: Format E E2 envelope with indicator 111 and
  // both entry payloads zero. Zero entry windows decode as NOP under the
  // Format E inverse (empty entry → NOP); the header is never all-zero
  // (all-zero is not Format E). Golden GE96-01 still OPEN for named
  // dual-NOP / map-11 completion IDs — this is the minimal wire form that
  // satisfies writeNopData / bare-NOP / empty-composite without inventing
  // a non-Format-E pad. Replace when golden publishes a stronger vector.
  return true;
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
  // E2 header only: format_indicator=111, entry_num=0, reserved=00; payload 0.
  // EncodedBytes comes from the production registry (12), not a local literal.
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  Out.clear();
  Out.resize(Parcel, 0);
  Out[0] = static_cast<char>(haydnFormatEHeaderByte(
      haydn::format::FormatEEntryNumTwo));
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
