//===- HaydnMCFormats.h - Interfaces for the auto-generated Formats -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Utility classes to interface the generated Formats from CodeGenFormat with
// our C++ Haydn Backend. This is a port of AIE's AIEMCFormats.h: the generic
// class shapes (MCSlotKind, MCSlotInfo, MCFormatField, SlotFieldPair
// MCFormatDesc) are aligned to AIE so that HaydnGenFormats.inc (the output of
// the CodeGenFormat tablegen backend) is the SCHEMA OWNER — its emitted tables
// and function bodies slot in directly via the GET_FORMATS_* include guards.
//
// Haydn-specific extensions retained as ADDITIVE surface (not present in AIE):
// `HaydnBaseMCFormats::getLegalSlots` is the single slot
// authority (FlexMap-derived via getFlexVariant in the concrete subclass).
// The former hand-DClass `getAltSlotSet` / FU-acceptance side table
// `getSlotKind(unsigned)` were deleted in — the FlexMap is the.td
// ground truth and is already FU-aware.
// HaydnBaseMCFormats::getMode0FormatDesc / getBundle128FormatDesc:
// Haydn-only format lookups over the dormant hand-authored
// HaydnFormatDescs table; the generated Formats (ADD32 today) is
// exposed via getMCFormats per the AIE contract.
//
// Decision §9 (Option A): HaydnGenFormats.inc is the SCHEMA OWNER, not a
// translation target. Renamed classes to the generic AIE names; field layouts
// aligned. No wholesale AIEMCFormats port (deferred to post-P5).
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H

#include "HaydnFormat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <map>
#include <unordered_map>

namespace llvm {

using SlotBits = uint64_t;
class MCSlotInfo;

//===----------------------------------------------------------------------===//
// MCSlotKind — wrapper over the tablegen-generated Haydn_SLOT_* enum
//===----------------------------------------------------------------------===//

// Lightweight wrapper around a slot identifier. The enum values
// (Haydn_SLOT_S0/S1/S2) are emitted by HaydnGenFormats.inc
// (GET_FORMATS_SLOTKINDS region) as SEQUENTIAL ARRAY INDICES (0,1,2). This is
// DISTINCT from the Haydn::SLOT0/1/2 BITMASKS (1,2,4 in HaydnBaseInfo.h)
// which encode occupancy sets. MCSlotKind is the slot IDENTIFIER; SlotBits is
// the slot-occupancy BITMASK.
class MCSlotKind {
  // Kind of the slot (index into HaydnSlots).
  int Kind;

public:
  // For use in unordered_map.
  class Hasher {
  public:
    size_t operator()(MCSlotKind SK) const { return SK; }
  };

  // Sentinel for "no slot" / out-of-range (mirrors AIE).
  static const int SLOT_UNKNOWN = -1;

  // Tablegen-generated slot-kind enum values. The GET_FORMATS_SLOTKINDS region
  // of HaydnGenFormats.inc expands to `Haydn_SLOT_S0, Haydn_SLOT_S1
  // Haydn_SLOT_S2,` — these become the enumerators of this enum, in order
  // (0,1,2). They are the canonical MCSlotKind values.
  enum HaydnSlotKind : int {
#define GET_FORMATS_SLOTKINDS
#include "HaydnGenFormats.inc"
#undef GET_FORMATS_SLOTKINDS
  };

  // Ctor with SlotKind initialization.
  constexpr MCSlotKind(int Kind) : Kind(Kind) {}

  // Default constructor, initialize the value at UNKNOWN.
  constexpr MCSlotKind() : Kind(SLOT_UNKNOWN) {}

  // Copy/Move Ctor.
  constexpr MCSlotKind(const MCSlotKind &) = default;
  constexpr MCSlotKind(MCSlotKind &&) = default;

  inline bool operator==(const MCSlotKind &OtherKind) const {
    return Kind == OtherKind.Kind;
  }
  inline bool operator!=(const MCSlotKind &OtherKind) const {
    return !(*this == OtherKind);
  }
  inline MCSlotKind &operator=(const MCSlotKind &SlotKind) {
    Kind = SlotKind.Kind;
    return *this;
  }

  // NOTE: We use it to define a method DerivedClass::getSlotInfo with a
  // switch on an instance of the DerivedClass itself (AIE pattern).
  operator int() const { return Kind; }
};

//===----------------------------------------------------------------------===//
// MCSlotInfo — per-slot descriptor (5-field, AIE schema)
//===----------------------------------------------------------------------===//

// "Generic" Slot Information class implementation. Mirrors AIE's MCSlotInfo
// exactly: a 5-field constexpr ctor matching the initializer lists emitted by
// HaydnGenFormats.inc GET_FORMATS_SLOTS_DEFS:
// {const char* Name, unsigned Size, SlotBits SlotOccupancy
// SlotBits ConflictBits, unsigned NopOpc}
// Haydn's former FU-acceptance bitmask side table was removed
// the FlexMap `getLegalSlots` is the single slot authority and is already
// FU-aware. AIE's MCSlotInfo carries no FU field, and so does Haydn's now.
class MCSlotInfo {
private:
  // Name of the slot.
  // NOTE: a bit old-fashion but needed to guarantee the constexpr nature of
  // this class (AIE comment).
  const char *SlotName;
  // Size of the slot (in bits).
  const unsigned Size;
  // Bitset representing the occupancy of the slots.
  const SlotBits SlotOccupancy;
  // The closure of SlotOccupancy with the computed exclusions
  // e.g. XM implies X and M.
  const SlotBits ConflictBits;
  // Opcode of the NOP instruction attached to the slot.
  const unsigned NopOpc;

public:
  constexpr MCSlotInfo(const char *SlotName, unsigned Size, SlotBits Bits,
                       SlotBits ConflictBits, unsigned NopOpc)
      : SlotName(SlotName), Size(Size), SlotOccupancy(Bits),
        ConflictBits(ConflictBits), NopOpc(NopOpc) {}

  const char *getName() const { return SlotName; }
  SlotBits getSlotSet() const { return SlotOccupancy; }
  SlotBits getConflictSet() const { return ConflictBits; }
  unsigned getNOPOpcode() const { return NopOpc; }
  unsigned getSize() const { return Size; }
};

//===----------------------------------------------------------------------===//
// MCFormatField — a field of an instruction format
//===----------------------------------------------------------------------===//

// Describes where a single slot lives inside an encoded instruction/format
// word. Offsets are GLOBAL (word-scope) and indexed from the MSB (big-endian
// bit indexing), matching AIE's convention. Concretely:
// MSB LSB
// <------- getSize ------->
// 0 ==========> LeftOffset ==========> RightOffset FormatSize-1
class MCFormatField {
public:
  using GlobalOffsets = struct {
    unsigned LeftOffset;
    unsigned RightOffset;
  };

private:
  // nullopt for a composite (packet) format whose slot windows are described
  // only via the SlotsMap entries (see MCFormatDesc). For a standalone
  // (single-slot) format this is always populated.
  std::optional<const GlobalOffsets> LocInfos;
  // Kind of the slot.
  const MCSlotKind SlotKind;

public:
  constexpr MCFormatField(std::optional<const GlobalOffsets> LocInfos,
                          const MCSlotKind &Slot)
      : LocInfos(LocInfos), SlotKind(Slot) {}

  inline unsigned getSize() const {
    return LocInfos->RightOffset - LocInfos->LeftOffset + 1;
  }

  // Returns a couple of Offset (begin, end), indexed on the most significant
  // bit.
  inline const GlobalOffsets &getOffsets() const { return LocInfos.value(); }

  MCSlotKind getSlotKind() const { return SlotKind; }
};

// A (SlotKind, FormatField) pair used as the value type of a format's
// SlotsMap. Mirrors AIE's SlotFieldPair.
class SlotFieldPair {
public:
  const MCSlotKind SlotKind;
  const MCFormatField *FormatField;
};

//===----------------------------------------------------------------------===//
// MCFormatDesc — description of one bundle or standalone format (AIE schema)
//===----------------------------------------------------------------------===//

// Generic class representing a description of a format. The 6-field ctor
// matches the initializer list emitted by HaydnGenFormats.inc
// GET_FORMATS_FORMATS_DEFS:
// {unsigned Opcode, bool isComposite, bool hasMultipleSlotOptions
// ArrayRef<SlotFieldPair> SlotsFields
// OperandsFieldsMap {ArrayRef<const MCFormatField* const*> FieldRanges
// size_t Count}
// const MCFormatField* BaseField}
class MCFormatDesc {
public:
  enum MCFormatKind { FK_Packet, FK_Instr };

  // This stores a slice of a lookup table of SlotFieldPairs and emulates a
  // map.
  class SlotsFieldsMap {
  public:
    constexpr SlotsFieldsMap() : ValueSlice({}) {};
    constexpr SlotsFieldsMap(const llvm::ArrayRef<SlotFieldPair> Values)
        : ValueSlice(Values) {};

    SlotFieldPair const &at(const MCSlotKind &Key) const {
      auto Pair =
          llvm::find_if(ValueSlice, [Key](SlotFieldPair const &SlotField) {
            return SlotField.SlotKind == Key;
          });
      assert(Pair != ValueSlice.end());
      return *Pair;
    }

    SlotFieldPair const *begin() const { return ValueSlice.begin(); }
    SlotFieldPair const *end() const { return ValueSlice.end(); }

    size_t size() const { return ValueSlice.size(); }

  private:
    const llvm::ArrayRef<SlotFieldPair> ValueSlice;
  };

  // This behaves like an array of variable sized arrays.
  // All elements are consecutive in memory. The outer dimension is an array
  // of pointers into that memory array, which is passed in as \p Base.
  // An array's upper bound is the next array's lower bound. The pointer
  // table contains an extra element pointing beyond the last entry in the
  // contiguous memory. Hence we can create an array reference from two
  // adjacent pointers.
  class OperandsFieldsMap {

  public:
    constexpr OperandsFieldsMap(const MCFormatField *const *const *Base,
                                unsigned Size)
        : Base(Base), Size(Size) {}
    unsigned size() const { return Size; }
    ArrayRef<const MCFormatField *> at(unsigned Idx) const {
      assert(Idx < Size);
      return {Base[Idx], Base[Idx + 1]};
    }

  private:
    const MCFormatField *const *const *Base;
    unsigned Size;
  };

protected:
  // Kind of the Format, packet or subinstruction.
  const MCFormatKind Kind;
  // Is it a MultiSlotPseudo instruction ?
  const bool HasMultipleSlotOptions = false;
  // Opcode of the MCInst on which the format is attached.
  unsigned Opcode;
  // Container binding any Slot Kind with its corresponding Field in the
  // instruction (Composite or not).
  const SlotsFieldsMap SlotsMap;
  // Map between an MCOperand index and a vector of fields (possibly of size
  // n). For example in AIE1, the encoding of the 20-bit immediate of the
  // instructions MOV_U20/MOV_S20 is split in two parts:
  //imm{19-14}|reg{6}|imm{13-0}|...
  // (0) (1)
  // In this case, the set of fields has a size of 2 and has the order {0, 1}.
  const OperandsFieldsMap OpFormatMapper;
  // Pointers on the definition of the base field of the tree.
  // Currently used to recover the size of the instruction (Composite or not).
  const MCFormatField *const BaseField;

public:
  constexpr MCFormatDesc(unsigned Opcode, bool IsComposite,
                         bool HasMultipleSlotOptions,
                         const SlotsFieldsMap Slots,
                         OperandsFieldsMap OpFormatMapper,
                         const MCFormatField *BaseField)
      : Kind(IsComposite ? FK_Packet : FK_Instr),
        HasMultipleSlotOptions(HasMultipleSlotOptions), Opcode(Opcode),
        SlotsMap(Slots), OpFormatMapper(OpFormatMapper), BaseField(BaseField) {
    // IsComposite is set only when the instruction is used to define supported
    // VLIW type, whereas HasMultipleSlotOptions is set only when the Pseudo
    // instruction represents multiple target instruction. These conditions are
    // mutually exclusive.
    assert(!(IsComposite && HasMultipleSlotOptions));
  };

  // Return whether the format describes a Packet (Composite Instruction) or a
  // sub-instruction.
  MCFormatKind getKind() const { return Kind; }
  bool isPacket() const { return Kind == FK_Packet; }
  bool isSubInst() const { return Kind == FK_Instr; }
  bool hasMultipleSlotOptions() const { return HasMultipleSlotOptions; }

  // Return the opcode of the format.
  inline unsigned getOpcode() const { return Opcode; }

  // Returns the size of the encoding of the current format (in bits).
  // NOTE: Same as asking the size to the MCInstrDesc except the fact that the
  // MCInstrDesc returns the size in bytes.
  inline unsigned getFormatSize() const { return BaseField->getSize(); }

  // Returns a pair of Offset of the Slot Kind, indexed on the High bits:
  // From MSB/High = Big-endian indexing
  // MSB | Slot | LSB
  // >
  // <= numBits =>
  // 0 ============> L ==========> R FormatSize-1
  // GlobalOffsets = {L, R}
  MCFormatField::GlobalOffsets
  getSlotOffsetsHiBit(const MCSlotKind &Kind) const {
    assert(!HasMultipleSlotOptions);
    return SlotsMap.at(Kind).FormatField->getOffsets();
  }

  // Returns the list of field(s) covering the MCOperand Idx.
  // Pre-condition: Idx must be valid. Otherwise, it triggers an assertion.
  ArrayRef<const MCFormatField *> getFieldsCoveredByOpIdx(unsigned Idx) const {
    assert(!HasMultipleSlotOptions);
    return OpFormatMapper.at(Idx);
  }
};

//===----------------------------------------------------------------------===//
// HaydnBaseMCFormats — query interface
//===----------------------------------------------------------------------===//

// Query interface over the Haydn slot/format tables. Haydn's analogue of AIE's
// AIEBaseMCFormats. The single Haydn variant means there is one concrete
// subclass (HaydnMCFormats) and no per-variant split.
// Two coexisting format-desc tables back this interface:
// the GENERATED Formats (HaydnGenFormats.inc GET_FORMATS_FORMATS_DEFS)
// exposed via getMCFormats per the AIE contract. Today this carries the
// migrating Flex opcodes (ADD32) AND the BUNDLE128_FULL composite
// packet format, so it grows as opcodes/formats migrate onto
// the CodeGenFormat framework.
// the DORMANT hand-authored HaydnFormatDescs (HaydnMCFormats.cpp)
// backing the Haydn-specific getMode0FormatDesc lookup for the legacy
// 16/32/48/64-bit slot geometries. These stay until the generated table
// fully covers them (M0_64 migration, post-P5). getBundle128FormatDesc
// was DELEGATED to the generated table in GAP-MC1 and no longer
// reads the dormant row.
class HaydnBaseMCFormats {
public:
  virtual ~HaydnBaseMCFormats() = default;

  // \returns the format descriptor for \p Opcode. Asserts that the opcode is
  // in the table; callers that cannot guarantee this should test
  // isSupportedInstruction first.
  virtual const MCFormatDesc &getFormatDesc(unsigned Opcode) const;

  // \returns the Mode-0 bundle format-desc (the 3-slot 64-bit composite)
  // whose SlotsMap carries the s0/s1/s2 slot-window positions
  // (M0S0Field/M0S1Field/M0S2Field). Convenience for the encoder/decoder
  // which operate on Mode-0 bundles; use getSlotOffsetsHiBit(SLOT0/1/2) on
  // the result to recover each slot's MSB-indexed window in the 64b word.
  // First live consumer of the ported format-desc model (Gap 2).
  // HAYDN EXTENSION (not in AIE): backed by the dormant hand-authored
  // HaydnFormatDescs table. M0_64 is NOT yet a generated packet format
  // (only BUNDLE128_FULL is —), so this query cannot yet delegate to
  // the generated table; retires when M0_64 migrates (post-P5). GAP-MC1
  // conservative scope.
  const MCFormatDesc &getMode0FormatDesc() const;

  // \returns the Bundle128 format-desc (the 3-slot 128-bit single composite
  // from). SlotsMap carries the s0/s1/s2 slot-window positions;
  // use getSlotOffsetsHiBit(SLOT0/1/2) on the result to recover each slot's
  // MSB-indexed window in the 128b word:
  // s0 = [0,47] (48b window; FU(3b) at offset 0)
  // s1 = [48,87] (40b window; FU(3b) at offset 48)
  // s2 = [88,127] (40b window; FU(3b) at offset 88)
  // Stage-1 vertical slice: ADD64 only.
  // (GAP-MC1): now DELEGATES to getFormatDesc(Haydn::BUNDLE128_FULL)
  // reading the GENERATED Haydn::Formats table (single truth). The dormant
  // HaydnFormatDescs[FDI_BUNDLE128] row + B128S0/S1/S2Field are retained
  // (table shape uniformity) but no longer back this query.
  const MCFormatDesc &getBundle128FormatDesc() const;

  // \returns whether \p Opcode has an entry in the format-desc table.
  virtual bool isSupportedInstruction(unsigned Opcode) const;

  // \returns Format Description, index based on the opcode.
  virtual std::optional<unsigned>
  getFormatDescIndex(unsigned Opcode) const = 0;

  // \returns a set of opcode for a given multi-slot pseudo intr, for an
  // unsupported opcode it returns an empty set.
  virtual const std::vector<unsigned> *
  getAlternateInstsOpcode(unsigned Opcode) const = 0;

  // single-authority legal-slot query (: promoted to the base
  // interface so HaydnBundle / HaydnResourceCycle can call it through the
  // base pointer). \returns a bitmask (bit k = slot k, Haydn::SLOT
  // convention: SLOT0=1<<0) of the slots opcode \p Opc can occupy, DERIVED
  // solely from the FlexMap (the tblgen ground truth): slot k is legal iff
  // a `_S<k>` variant exists. This is the one slot authority the
  // scheduler / HR / SMS consult; it replaces the former
  // `getAltSlotSet` (legal ∩ FU-acceptance) whose hand-DClass derivation
  // disagreed with the encoder (Bug1). Returns 0 for opcodes with no
  // flex family (standalone WIDE, pseudo) — callers treat 0 as "not a
  // bundle-slot op" and skip the slot auction.
  virtual SlotBits getLegalSlots(unsigned Opc) const = 0;

  // \returns the slot descriptor for \p Kind, or nullptr if unknown.
  virtual const MCSlotInfo *getSlotInfo(const MCSlotKind Kind) const = 0;

  // \returns the base of the format-desc table (the GENERATED Formats).
  virtual const MCFormatDesc *getMCFormats() const = 0;

  // \returns the PacketFormats table from HaydnFormat.h (Module A).
  virtual const PacketFormats &getPacketFormats() const = 0;

  // \returns the FormatAvailable LUT from HaydnFormat.h.
  virtual ArrayRef<bool> getIsFormatAvailable() const = 0;

  // \returns whether \p SlotSet (a combination of Haydn::SLOT* masks) names a
  // packetable slot combination (true iff some format's SlotSet covers it).
  bool isFormatAvailable(uint64_t SlotSet) const;

protected:
  // Check if the Instruction is indeed into the Tables (AIE pattern).
  void checkInstructionIsSupported(unsigned Opcode) const;
};

// Concrete subclass. The generated function bodies for getSlotInfo
// getFormatDescIndex, and getAlternateInstsOpcode are provided by
// HaydnGenFormats.inc (GET_FORMATS_SLOTINFOS_MAPPING
// GET_OPCODE_FORMATS_INDEX_FUNC / GET_ALTERNATE_INST_OPCODE_FUNC); the
// declarations here match those signatures exactly.
class HaydnMCFormats : public HaydnBaseMCFormats {
public:
  const std::vector<unsigned> *
  getAlternateInstsOpcode(unsigned Opcode) const override;
  std::optional<unsigned>
  getFormatDescIndex(unsigned Opcode) const override;
  const MCSlotInfo *getSlotInfo(const MCSlotKind Kind) const override;
  const MCFormatDesc *getMCFormats() const override;
  ArrayRef<bool> getIsFormatAvailable() const override;
  const PacketFormats &getPacketFormats() const override;

  // \returns the `_<base>_S<Slot>` opcode variant of the legacy opcode
  // \p LegacyOpc, or 0 if no such variant exists. \p Slot must be 0, 1, or 2
  // (Haydn slot index). The mapping is.td-driven, emitted into
  // HaydnGenFormats.inc (GET_LEGACY_TO_FLEX_MAP region) by the CodeGenFormat
  // backend. Replaces the former runtime name-scan in
  // HaydnFlexMaterialize.
  unsigned getFlexVariant(unsigned LegacyOpc, unsigned Slot) const;

  // single-authority legal-slot query (override). \returns a bitmask
  // (bit k = slot k, Haydn::SLOT convention: SLOT0=1<<0) of the slots opcode
  // \p Opc can occupy, DERIVED solely from the FlexMap (the tblgen ground
  // truth): slot k is legal iff a `_S<k>` variant exists
  // (`getFlexVariant(Opc, k) != 0`). This is the one slot authority the
  // scheduler/HR consults — it replaces the hand-maintained HaydnDClass
  // `getLegalSlots`/`getAltSlotSet` tables whose disagreement with the
  // encoder caused Bug1 (split authority: the itinerary `Slot012_ALU`
  // claimed all 3 slots for 48-bit ops that are physically S0-only).
  // Returns 0 for opcodes with no flex family (standalone WIDE, pseudo)
  // callers treat 0 as "not a bundle-slot op" and skip the slot auction.
  // NOTE: this override only recognizes LEGACY opcodes (the generated
  // `getFlexVariant` switch has `case XOR32:` but no `case XOR32_S0:`).
  // For a flex-opcode-aware query (needed by the MC encoder's Bundle
  // shuffler), use `HaydnMCFormatsWithMII` (below) or the free helper
  // `getHaydnFlexVariantForSlot`.
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// flex-opcode-aware formats subclass + free helper (GAP-MC2)
//===----------------------------------------------------------------------===//
//
// The MC encoder wires `Haydn::Bundle<MCInst>` into `encodeBundle128` as the
// AIE-faithful shuffler. Bundle's `pickSlot` calls `getLegalSlots(Opc)` through
// the `HaydnBaseMCFormats*` interface. The base `HaydnMCFormats::getLegalSlots`
// only recognizes LEGACY opcodes (the generated `getFlexVariant` switch misses
// flex opcodes like `XOR32_S0`). For an already-flex child (produced when
// the AsmParser matches a `.sN` mnemonic suffix, or when a prior spread
// rewrites the opcode), `getLegalSlots` returns 0 and `pickSlot` fails
// 's root.
//
// `HaydnMCFormatsWithMII` is the encoder-side subclass that NORMALIZES a flex
// opcode to its legacy base (via the `_S<k>` name suffix) BEFORE consulting
// the generated FlexMap. It carries an `MCInstrInfo&` for the name lookup. The
// HR/scheduler path (no MII available) keeps using the base `HaydnMCFormats`
// and never sees flex opcodes (the post-RA packetizer commits legacy opcodes
// and only the AsmPrinter/encoder materializes the flex variant). This keeps
// the two consumers on consistent authorities without broadening the generated
// table.

// \returns the `_S<Slot>` variant opcode of \p Opc for \p Slot, or 0 if
// none exists. \p Opc may be a LEGACY opcode (e.g. XOR32) OR an already-flex
// opcode (e.g. XOR32_S0) — the `_S{0,1,2}` suffix is stripped first
// to recover the base mnemonic, then `Base + "_S<Slot>"` is looked up in
// \p MII. This is the uniform suffix-strip mechanism (lifted from the
// `findFlexVariantForSlot` helper that lived in HaydnMCCodeEmitter.cpp).
unsigned getHaydnFlexVariantForSlot(unsigned Opc, unsigned Slot,
                                    const MCInstrInfo &MII);

// \returns the slot index (0/1/2) encoded in \p Opc's `_S<k>` name
// suffix, or -1 if \p Opc is not a flex opcode.
int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII);

// `HaydnMCFormats` subclass that normalizes flex opcodes before consulting
// the FlexMap. Constructed by the MC encoder (which holds an MCInstrInfo).
// The HR/scheduler path keeps using the base `HaydnMCFormats` (legacy-only).
class HaydnMCFormatsWithMII : public HaydnMCFormats {
  const MCInstrInfo &MII;

public:
  HaydnMCFormatsWithMII(const MCInstrInfo &MII) : MII(MII) {}

  // Flex-opcode-aware legal-slot query. Strips the `_S<k>` suffix to
  // recover the legacy base, then queries the base's legal slots via the
  // generated FlexMap. For a legacy opcode this is identical to the base
  // implementation; for a flex opcode it returns the SAME legal-slot set as
  // the legacy base (the flex variants are slot-specific, so the union of
  // variant slots IS the legal set).
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// Haydn slot bitmask helpers
//===----------------------------------------------------------------------===//

// \returns the MCSlotKind corresponding to a Haydn::SLOT* bitmask (1/2/4), or
// SLOT_UNKNOWN if \p Mask is not a single-slot Haydn mask.
MCSlotKind haydnSlotMaskToKind(SlotBits Mask);

//===----------------------------------------------------------------------===//
// shared Bundle128-target predicate (Flex-encodable opcodes).
//===----------------------------------------------------------------------===//
//
// The MC encoder (`HaydnMCCodeEmitter::encodeSingleInstruction`
// `encodeBundle128`) routes BOTH standalone Flex-target opcodes AND formed
// BUNDLEs whose real children are all Flex-target through `encodeBundle128`
// emitting a 128-bit (16-byte) Bundle128 composite word via `emitBundle128Word`.
//
// A `_S<k>` opcode is SELF-DESCRIBING: its name suffix carries the slot
// digit (0/1/2 = S0/S1/S2). A legacy opcode that HAS a Flex variant also
// routes through the Bundle128 path (the encoder maps it to its `_S<k>`
// form before encoding). Both cases return true here.
//
// SHARED so that `HaydnInstrInfo::getInstSizeInBytes` (consulted by upstream
// BranchRelaxation's `computeBlockSize`) mirrors the encoder's emit dispatch
// exactly — the size model MUST predict the post-FLEX-materialize 16-byte
// emit width, or BranchRelaxation undercounts branch distances (~8x), deems
// every branch in range, never relaxes, and the WIDE conditional branch
// overflows `FIXUP_HAYDN_WIDE_BranchSImm12` (±4 KB) at MC-fixup time on fns
// > ~4 KB (adddf3/divdf3 —). It also prevents the
// `BranchRelaxation::verify` BlockSize assert (BlockInfo.Size vs post-relax
// recompute). See (size model) + (FLEX-materialize
// regression of the size model).

// \returns true iff \p Opc is emitted via the 128-bit Bundle128 path:
// a `_S<k>` opcode (self-describing name suffix), OR
// a legacy opcode that has a `_S<k>` variant (encoder maps it).
// In both cases the emitted width is 16 bytes. This mirrors the MC encoder's
// `isBundle128TargetOpcode` gate (`HaydnMCCodeEmitter.cpp`).
bool isHaydnBundle128TargetOpcode(unsigned Opc, const MCInstrInfo &MII);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H
