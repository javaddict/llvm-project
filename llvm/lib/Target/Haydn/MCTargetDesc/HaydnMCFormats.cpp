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
// BUNDLE128_FULL is). getBundle128FormatDesc was DELEGATED to the
// generated Haydn::Formats in GAP-MC1 and no longer reads the dormant
// row; the dormant BUNDLE128 row is retained only for table-shape uniformity
// and retires when M0_64 also delegates (post-P5).
//
// The Mode-0 slot bit positions (s0 at bits[23:4]/20b, s1 at
// bits[45:24]/22b, s2 at bits[63:46]/18b) were previously documented as
// mirroring `HaydnDClassInfo.h::M0Rows`. That file was deleted in (the
// FlexMap `HaydnMCFormats::getLegalSlots` is now the single slot authority).
// The bit positions themselves are still authoritative per encoding_manual.md
// §6 and are kept here as the spec-derived constants.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCFormats.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

#include "HaydnFormat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
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
#define GET_LEGACY_TO_FLEX_MAP
#include "HaydnGenFormats.inc"

namespace Haydn {
#define GET_FORMATS_FORMATS_DEFS
#include "HaydnGenFormats.inc"
} // end namespace Haydn

SlotBits HaydnMCFormats::getLegalSlots(unsigned Opc) const {
  // single-authority legal-slot query. Derived solely from the FlexMap
  // (the tblgen ground truth generated above via GET_LEGACY_TO_FLEX_MAP): slot
  // k is legal iff a `_S<k>` variant exists. This is the one slot
  // authority the HR/scheduler consults; it replaces the hand HaydnDClass
  // getLegalSlots/getAltSlotSet tables. Returns 0 for opcodes with no flex
  // family (standalone WIDE / pseudo) — callers treat 0 as "not a bundle-slot
  // op" and skip the slot auction.
  SlotBits Bits = 0;
  for (unsigned Slot = 0; Slot < 3; ++Slot)
    if (getFlexVariant(Opc, Slot) != 0)
      Bits |= (SlotBits(1) << Slot);
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

namespace {

//===----------------------------------------------------------------------===//
// Haydn FU-acceptance side table — REMOVED.
//
// The former FU-acceptance bitmask (`HaydnSlotFUAccept`) and its free
// helpers (`getHaydnSlotFUAcceptBits`, `haydnSlotAcceptsFU`
// `haydnSlotKindToMask`) were deleted in. They had a single production
// consumer — the hand-DClass `getAltSlotSet`, also deleted in — and the
// unittest that asserted their values against M0Rows was deleted alongside.
// The FlexMap (`HaydnMCFormats::getLegalSlots`) is the single slot authority
// subsequent and is already FU-aware (slot k is legal iff a `_S<k>`
// variant exists, and the.td FU/slot assignment produces that variant).
//===----------------------------------------------------------------------===//

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Slot-bitmask / slot-index bridge helpers
//===----------------------------------------------------------------------===//

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

//===----------------------------------------------------------------------===//
// shared Bundle128-target predicate. See HaydnMCFormats.h.
//===----------------------------------------------------------------------===//
//
// The `_S{0,1,2}` name-suffix table + getFlexSlotFromName logic lived as
// a file-static in HaydnMCCodeEmitter.cpp. It is promoted to this shared
// helper (single source of truth) so HaydnInstrInfo::getInstSizeInBytes can
// mirror the encoder's emit dispatch — required for BranchRelaxation to
// measure true post-FLEX-materialize 16-byte emit widths.
// The shared `getHaydnFlexSlotFromName` also backs the flex-aware
// `HaydnMCFormatsWithMII::getLegalSlots` (GAP-MC2).

bool isHaydnBundle128TargetOpcode(unsigned Opc, const MCInstrInfo &MII) {
  // Slot-variant opcodes (self-describing via _S<k> name suffix) pass directly.
  if (getHaydnFlexSlotFromName(Opc, MII) >= 0)
    return true;
  // Legacy opcodes that have a slot variant in ANY slot pass — the encoder
  // maps them to the slot's `_S<k> before encoding. The former
  // `getFlexVariant(Opc, 0)` check only recognized S0-legal ops, falsely
  // rejecting S1/S2-only ops (e.g. SEXT_GPR32_TO_DR64, FlexMap `{0,S1,0}`) as
  // "no Bundle128 form" — Bug1 fallout. Use the single-authority getLegalSlots
  // (FlexMap-derived) so any slot with a variant qualifies.
  HaydnMCFormats Formats;
  return Formats.getLegalSlots(Opc) != 0;
}

//===----------------------------------------------------------------------===//
// flex-opcode-aware helpers (GAP-MC2)
//===----------------------------------------------------------------------===//
//
// The MC encoder's `Haydn::Bundle<MCInst>` shuffler (encodeBundle128) calls
// `getLegalSlots(Opc)` on children that may be ALREADY-flex opcodes (produced
// when the AsmParser matches a `.sN` mnemonic suffix, or when a prior spread
// rewrites the opcode). The generated `getFlexVariant` switch only has cases
// for LEGACY opcodes (`case XOR32:`), so it returns 0 for a flex opcode and
// `getLegalSlots` reports no legal slots — `Bundle::pickSlot` fails (
// root). These helpers normalize a flex opcode to its legacy base before the
// FlexMap lookup.

namespace {

// `_S<k>` name-suffix table — the slot digit is authoritative.
constexpr StringRef HaydnFlexSlotSuffix2[3] = {"_S0", "_S1",
                                               "_S2"};

// \returns the slot index (0/1/2) encoded in \p Opc's `_S<k>` name
// suffix, or -1 if \p Opc is not a flex opcode.
int getFlexSlotFromNameLocal(unsigned Opc, const MCInstrInfo &MII) {
  StringRef Name = MII.getName(Opc);
  for (int Slot = 0; Slot < 3; ++Slot) {
    StringRef Suffix = HaydnFlexSlotSuffix2[Slot];
    if (Name.ends_with(Suffix))
      return Slot;
  }
  return -1;
}

// \returns the legacy base opcode for \p Opc by stripping any `_S<k>`
// suffix, or 0 if \p Opc IS the base (no suffix) or the base name does not
// resolve to an opcode. The base is found by NAME lookup in \p MII (the
// opcode enum values are not derivable from the suffix alone).
unsigned getFlexBaseOpcode(unsigned Opc, const MCInstrInfo &MII) {
  // Legacy opcode: no suffix → it IS its own base.
  StringRef Name = MII.getName(Opc);
  StringRef Base = Name;
  bool Stripped = false;
  for (StringRef Suf : HaydnFlexSlotSuffix2) {
    if (Base.ends_with(Suf)) {
      Base = Base.drop_back(Suf.size());
      Stripped = true;
      break;
    }
  }
  if (!Stripped)
    return Opc; // already legacy
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
  return getFlexSlotFromNameLocal(Opc, MII);
}

unsigned getHaydnFlexVariantForSlot(unsigned Opc, unsigned Slot,
                                    const MCInstrInfo &MII) {
  if (Slot >= 3)
    return 0;

  // Prefer the generated FlexMap (tblgen single authority). One materialize
  // step: logical → encode form (e.g. LD32 → LD32_S1 Bundle128). No intermediate
  // logical twin.
  HaydnMCFormats Formats;
  unsigned BaseOpc = getFlexBaseOpcode(Opc, MII);
  if (BaseOpc == 0)
    BaseOpc = Opc;
  if (unsigned Var = Formats.getFlexVariant(BaseOpc, Slot))
    return Var;
  if (unsigned Var = Formats.getFlexVariant(Opc, Slot))
    return Var;

  // Name fallback: strip `_S<k>` then look up `Base + "_S<Slot>"`.
  StringRef Name = MII.getName(Opc);
  StringRef Base = Name;
  for (StringRef Suf : HaydnFlexSlotSuffix2) {
    if (Base.ends_with(Suf)) {
      Base = Base.drop_back(Suf.size());
      break;
    }
  }
  if (Base.empty())
    return 0;
  SmallString<64> Target;
  (Base + "_S" + Twine(Slot)).toVector(Target);
  StringRef TargetRef(Target);
  unsigned Num = MII.getNumOpcodes();
  for (unsigned Cand = 0; Cand < Num; ++Cand)
    if (MII.getName(Cand) == TargetRef)
      return Cand;
  return 0;
}

SlotBits HaydnMCFormatsWithMII::getLegalSlots(unsigned Opc) const {
  // Normalize a flex opcode to its legacy base, then consult the generated
  // FlexMap (which only has legacy-opcode cases). For a legacy Opc this is a
  // passthrough (getFlexBaseOpcode returns Opc itself).
  unsigned BaseOpc = getFlexBaseOpcode(Opc, MII);
  if (BaseOpc != 0) {
    SlotBits Bits = HaydnMCFormats::getLegalSlots(BaseOpc);
    if (Bits != 0)
      return Bits;
  }
  // already-flex opcodes whose stripped base is NOT a live opcode (or
  // has no FlexMap entry) used to report legal-slots=0. Bundle::add then
  // accepted them on an empty bundle (standalone escape) but never reserved a
  // slot — encodeBundle128 saw an empty SlotMap and emitted 3×NOP (16×0x00).
  // logical REG bases (LD32_REG_M0S0LS …) are FlexMap-paired to private
  // *_S0 peers; this branch still covers asm-matched bare `_S0` encode forms
  // and any residual peer with no live logical base. The `_S<k>` suffix is
  // authoritative — that single slot is legal.
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
  // (a) it has an entry in the GENERATED Formats table (the migrated Flex
  // opcodes — ADD32 today, growing as opcodes move onto the
  // CodeGenFormat framework per), OR
  // (b) it has a `_S<k>` variant in some slot — i.e. the FlexMap-derived
  // `getLegalSlots(Opcode) != 0`. Post-+ this is the single
  // FlexMap ground truth (the.td-driven superset of the former
  // hand-DClass `getEncOpcodeMap+hasEncMapping` check).
  //
  // NOTE: getFormatDesc(Opcode) is NOT broadened — it stays strict (generated
  // table only) and would assert on a legacy opcode. Bundle::add never calls
  // getFormatDesc; it calls getSlotInfo(slotKind) and getLegalSlots(Opcode)
  // both of which work for legacy opcodes via the FlexMap. The only strict
  // consumer of getFormatDesc is the packet-format encoder path, which only
  // runs on migrated opcodes.
  if (getFormatDescIndex(Opcode).has_value())
    return true;
  return getLegalSlots(Opcode) != 0;
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
HaydnBaseMCFormats::getBundle128FormatDesc() const {
  // Sole packet-format authority: generated CodeGenFormat table.
  return getFormatDesc(Haydn::BUNDLE128_FULL);
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
// The former HaydnBaseMCFormats::getSlotKind(unsigned Opcode) and
// getAltSlotSet(unsigned Opcode) were DELETED — they had zero production
// callers after the Bundle/HR/ResourceCycle cutover to the FlexMap
// `getLegalSlots` authority. getSlotKind(unsigned) was dead code;
// getAltSlotSet's sole live consumer (HaydnBundle::pickSlotForOccupied) now
// calls getLegalSlots directly.

const MCFormatDesc *HaydnMCFormats::getMCFormats() const {
  return Haydn::Formats;
}

const PacketFormats &HaydnMCFormats::getPacketFormats() const {
  // return the GENERATED PacketFormats table (HaydnGenFormats.inc
  // GET_FORMATS_PACKETS_TABLE region). This is the AIE-faithful model: the
  // table is derived from the defined composite (packet) formats — currently
  // BUNDLE128_FULL (HaydnCompositeFormats.td), which covers {S0,S1,S2}. The
  // hand-authored HaydnPacketFormats retired (G-MC-9)
  // backup but no longer backs this query.
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
