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
// The DORMANT hand-authored HaydnFormatDescs table (the legacy 16/32/48/64
// 128-bit slot geometries) is RETAINED as a Haydn extension to back
// getMode0FormatDesc — M0_64 is not yet a generated packet format (only
// BUNDLE128_FULL is). getBundle128FormatDesc delegates to the generated
// Haydn::Formats and no longer reads the dormant row; the dormant BUNDLE128
// row retires when M0_64 also delegates (post-P5).
//
// The Mode-0 slot bit positions (s0 at bits[23:4]/20b, s1 at
// bits[45:24]/22b, s2 at bits[63:46]/18b) are authoritative per
// encoding_manual.md §6 and are kept here as the spec-derived constants.
// Alts-derived `HaydnMCFormats::getLegalSlots` is the slot legality authority.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCFormats.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

#include "HaydnFormat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
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

// GET_FORMATS_PACKETS_TABLE is now CONSUMED. With BUNDLE128_FULL declared
// as a composite (HaydnCompositeFormats.td), the CodeGenFormat backend emits a
// real PacketFormats table (Formats), a FormatAvailable LUT, and — via
// computeSlotSets — real ConflictBits on every slot (self + slots no packet
// format combines with it). For Bundle128 (covers {S0,S1,S2}) every slot
// co-emits with every other, so ConflictBits degenerates to self-only (1/2/4)
// making the pickSlot ConflictBits clause in HaydnBundle.h a permissive no-op
// for valid combos while still rejecting hypothetical invalid combos.
// getPacketFormats/getIsFormatAvailable now return the GENERATED tables;
// the hand-authored HaydnFormats/HaydnPacketFormats/HaydnFormatAvailable
// (HaydnFormat.cpp) are retained as dormant backup but no longer back these
// two queries.
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
  // OR each member's OWN slot bit. This used to be `1 << Index`, which held
  // only while the alternates vector was indexed by slot. Format E indexes it
  // by placement — (entry position, unit) — so the index is not the slot and
  // several members can share one. Same correction as
  // fieldSlotsForMember in HaydnPlacementAlternative.h.
  SlotBits Bits = 0;
  for (unsigned MemberOpc : *Alts) {
    if (MemberOpc == 0)
      continue; // sparse hole
    MCSlotKind Kind = getSlotKind(MemberOpc);
    if (Kind != MCSlotKind())
      Bits |= SlotBits(1) << static_cast<unsigned>(Kind);
  }
  return Bits;
}

//===----------------------------------------------------------------------===//
// Slot bit-position conventions
//===----------------------------------------------------------------------===//
//
// Each slot's window is expressed as absolute bit indices in the encoded word
// (HiBit >= LoBit, MSB == width-1). (Historically these were documented in the
// deleted HaydnDClassInfo.h; the constants below are now the spec-derived
// source per encoding_manual.md §6.) The MCFormatDesc model uses MSB-indexed
// offsets (LeftOffset < RightOffset, both counted from the MSB), matching
// AIE's convention. For a word of `W` bits:
//
// LeftOffset = (W - 1) - HiBit / MSB-distance of the field's high end
// RightOffset = (W - 1) - LoBit / MSB-distance of the field's low end
//
// For Mode-0 (W=64): s0 HiBit=23,LoBit=4 -> L=40,R=59 (20b)
// s1 HiBit=45,LoBit=24 -> L=18,R=39 (22b)
// s2 HiBit=63,LoBit=46 -> L=0, R=17 (18b)
//
// For G32 (W=32): s0 HiBit=17,LoBit=4 -> L=14,R=27 (14b)
// s1 HiBit=31,LoBit=18 -> L=0, R=13 (14b)

//===----------------------------------------------------------------------===//
// Slot-bitmask / slot-index bridge helpers
//===----------------------------------------------------------------------===//

MCSlotKind haydnSlotMaskToKind(SlotBits Mask) {
  // A slot's occupancy mask is 1 << its MCSlotKind, by construction: the
  // generated HaydnSlots table stamps SlotOccupancy from the enumerator's
  // position. So the bridge is just "which bit", and it does not have to be
  // respelled when the slot set changes — this used to be a three-case switch
  // over Haydn::SLOT0/1/2 and would have needed five cases for format E.
  //
  // Only a single-slot mask names a kind; 0 and multi-bit sets do not.
  if (Mask == 0 || !isPowerOf2_64(Mask))
    return MCSlotKind(MCSlotKind::SLOT_UNKNOWN);
  return MCSlotKind(static_cast<int>(Log2_64(Mask)));
}

//===----------------------------------------------------------------------===//
// shared Bundle128-target predicate. See HaydnMCFormats.h.
//===----------------------------------------------------------------------===//
//
// Shared helper so HaydnInstrInfo::getInstSizeInBytes mirrors the encoder's
// emit dispatch — BranchRelaxation must measure true Bundle128 16-byte emit
// widths. `getHaydnFlexSlotFromName` also backs member-aware
// `HaydnMCFormatsWithMII::getLegalSlots`.

bool isHaydnBundleTargetOpcode(unsigned Opc, const MCInstrInfo &MII) {
  // Answer for either spelling by asking the member-aware getLegalSlots, which
  // folds a member to its logical through getLogicalBaseOpcode and only then
  // falls back to the name suffix.
  //
  // The previous pair could not see a format E member at all. Its first arm
  // read the Bundle128 `_S<k>` suffix table, which cannot parse
  // `_P<form><pos>_<UNIT>`; its second built a PLAIN HaydnMCFormats, whose
  // rows are logicals only, and handed it the member opcode. So every format E
  // member answered "no". It stayed hidden because the AsmParser matches the
  // logical for almost everything — CSRW is isCodeGenOnly, so it is the one
  // mnemonic that had to arrive here as a member, and it aborted the encoder.
  // FORMAT-E-SWITCH-PLAN.md § 5.6: fold through the logical, never the
  // spelling.
  HaydnMCFormatsWithMII Formats(MII);
  return Formats.getLegalSlots(Opc) != 0;
}

//===----------------------------------------------------------------------===//
// member-opcode-aware helpers
//===----------------------------------------------------------------------===//
//
// The MC encoder's `Haydn::Bundle<MCInst>` shuffler (encodeBundleE) calls
// `getLegalSlots(Opc)` on children that may already be format members
// (AsmParser `.sN` or post-setDesc). Base getLegalSlots only has rows for
// logicals; strip `_S<k>` to recover the logical base before the alts query.

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

// \returns the logical base opcode for \p Opc by stripping any placement
// suffix, or \p Opc itself if it has none. Base found by NAME lookup.
unsigned getLogicalBaseOpcode(unsigned Opc, const MCInstrInfo &MII) {
  std::optional<StringRef> Stripped = stripHaydnMemberSuffix(MII.getName(Opc));
  if (!Stripped)
    return Opc; // already logical
  StringRef Base = *Stripped;
  if (Base.empty())
    return 0;
  unsigned Num = MII.getNumOpcodes();
  for (unsigned Cand = 0; Cand < Num; ++Cand)
    if (MII.getName(Cand) == Base)
      return Cand;
  return 0;
}

} // end anonymous namespace

// Unit spellings, indexed by Haydn::Unit. Order must match the enum; the
// static_assert below and haydnUnitName are the only things that know it.
static constexpr StringRef HaydnUnitNames[] = {
    "LOADSTORE0", "LOAD1", "ALU0", "ALU1", "ALU2", "MAC0", "MAC1"};
static_assert(sizeof(HaydnUnitNames) / sizeof(HaydnUnitNames[0]) ==
                  Haydn::UNIT_COUNT,
              "unit name table out of step with Haydn::Unit");

// Split `<logical>_P<form><pos>_<unit>` into its logical and its unit.
// \returns nullopt when \p Name carries no format E placement suffix.
static std::optional<std::pair<StringRef, Haydn::Unit>>
splitFormatEMember(StringRef Name) {
  // The unit set is closed, so matching it explicitly stops a logical that
  // merely ends in `_<word>` (ADD32_W, D_LDW_POST_IMM) from being mistaken for
  // a placed member.
  for (unsigned I = 0; I != Haydn::UNIT_COUNT; ++I) {
    StringRef UnitName = HaydnUnitNames[I];
    if (!Name.ends_with(UnitName))
      continue;
    StringRef Head = Name.drop_back(UnitName.size());
    if (!Head.ends_with("_"))
      continue;
    Head = Head.drop_back(1);
    // The placement token is `_P` followed by the form and the position.
    size_t Sep = Head.rfind("_P");
    if (Sep == StringRef::npos)
      continue;
    StringRef Digits = Head.substr(Sep + 2);
    if (Digits.size() != 2 ||
        !llvm::all_of(Digits, [](char C) { return C >= '0' && C <= '9'; }))
      continue;
    return std::make_pair(Head.substr(0, Sep), static_cast<Haydn::Unit>(I));
  }
  return std::nullopt;
}

std::optional<StringRef> stripHaydnMemberSuffix(StringRef Name) {
  // Bundle128: `<logical>_S<k>`.
  for (StringRef Suf : HaydnMemberSlotSuffix)
    if (Name.ends_with(Suf))
      return Name.drop_back(Suf.size());

  if (auto Split = splitFormatEMember(Name))
    return Split->first;

  return std::nullopt; // already logical
}

StringRef haydnUnitName(Haydn::Unit U) {
  unsigned I = static_cast<unsigned>(U);
  assert(I < Haydn::UNIT_COUNT && "unit out of range");
  return HaydnUnitNames[I];
}

std::optional<Haydn::Unit> haydnMemberUnitFromName(StringRef Name) {
  // Bundle128 members carry no unit: its slot model pinned each unit to one
  // slot, so the slot WAS the unit and the spelling never had to say. Returning
  // nullopt is the truthful answer, not a failure — callers treat "no unit
  // modelled" as "no unit constraint", which is what makes the unit axis inert
  // while Bundle128 is live.
  if (auto Split = splitFormatEMember(Name))
    return Split->second;
  return std::nullopt;
}

Haydn::UnitBits haydnMemberUnitBits(StringRef Name) {
  if (auto U = haydnMemberUnitFromName(Name))
    return Haydn::unitBit(*U);
  return 0;
}

int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII) {
  return getMemberSlotFromNameLocal(Opc, MII);
}

unsigned getHaydnLogicalBaseOpcode(unsigned Opc, const MCInstrInfo &MII) {
  return getLogicalBaseOpcode(Opc, MII);
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

// R5: dormant hand-authored HaydnFormatDescs (C16/G32/MOVEI48
// WIDE48/M0_64/M3_64/Bundle128) DELETED. Bundle128 geometry comes solely
// from CodeGenFormat-generated Haydn::Formats (BUNDLE128_FULL).


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
  // setDesc, MultiSlot_Pseudo logicals, BUNDLE128_FULL, …), OR
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

const MCFormatDesc &
HaydnBaseMCFormats::getMode0FormatDesc() const {
  // Mode-0 is retired. Bundle128 is the sole packet format.
  // Callers must use getBundle128FormatDesc / generated Haydn::Formats.
  report_fatal_error(
      "Haydn: getMode0FormatDesc is retired (Bundle128-only); use"
      "getBundle128FormatDesc()");
}

const MCFormatDesc &
HaydnBaseMCFormats::getCompositeFormatDesc(unsigned CompositeOpcode) const {
  assert((CompositeOpcode == Haydn::BUNDLE_E2 ||
          CompositeOpcode == Haydn::BUNDLE_E3) &&
         "not a format E composite");
  // Sole packet-format authority: generated CodeGenFormat table.
  return getFormatDesc(CompositeOpcode);
}

// The generated slot enum and the hand-written masks in HaydnBaseInfo.h are two
// spellings of the same thing (see haydnSlotMaskToKind). Tie them here so a
// regenerated encoding that adds, drops or reorders a slot fails the build
// rather than silently shifting every occupancy mask underneath the packer.
static_assert(MCSlotKind::Haydn_SLOT_P20 == 0, "slot enum order changed");
static_assert(MCSlotKind::Haydn_SLOT_P21 == 1, "slot enum order changed");
static_assert(MCSlotKind::Haydn_SLOT_P30 == 2, "slot enum order changed");
static_assert(MCSlotKind::Haydn_SLOT_P31 == 3, "slot enum order changed");
static_assert(MCSlotKind::Haydn_SLOT_P32 == 4, "slot enum order changed");

bool HaydnBaseMCFormats::isFormatAvailable(uint64_t SlotSet) const {
  ArrayRef<bool> Avail = getIsFormatAvailable();
  return SlotSet < Avail.size() && Avail[SlotSet];
}

const VLIWFormat *
HaydnBaseMCFormats::getFormatByEntryCount(unsigned NumEntries) const {
  return getPacketFormats().getFormatByEntryCount(NumEntries);
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
  // GET_FORMATS_PACKETS_TABLE region). AIE-faithful model: the table is
  // derived from the defined composite (packet) formats — currently
  // BUNDLE128_FULL (HaydnCompositeFormats.td), which covers {S0,S1,S2}.
  return Formats;
}

ArrayRef<bool> HaydnMCFormats::getIsFormatAvailable() const {
  // return the GENERATED FormatAvailable LUT (HaydnGenFormats.inc
  // GET_FORMATS_PACKETS_TABLE region). Computed by the CodeGenFormat backend:
  // SlotSet is "available" iff some packet format covers it (or it is a subset
  // of one, with NOPs filling the unused slots). With Bundle128 covering all 3
  // slots, every combo 0..7 is available (matches the prior hand-authored LUT
  // exactly — behavior-preserving).
  return ArrayRef<bool>(FormatAvailable, SlotSetSize);
}

} // namespace llvm
