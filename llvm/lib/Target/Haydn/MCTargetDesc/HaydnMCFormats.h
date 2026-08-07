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
// `HaydnBaseMCFormats::getLegalSlots` is alts-derived: OR of non-zero sparse
// AlternateInsts indices. MC encodes member Desc as-is (AIE); residual hand-asm
// materializes via getAlternateInstsOpcode.
// HaydnBaseMCFormats::getMode0FormatDesc / getBundle128FormatDesc:
// Haydn-only format lookups; getBundle128FormatDesc delegates to the generated
// Formats table. The generated Formats (ADD32 today) is exposed via getMCFormats
// per the AIE contract.
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
#include <optional>
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
// Haydn's former FU-acceptance bitmask side table is gone; alts-derived
// getLegalSlots is the slot legality authority. AIE's MCSlotInfo carries no
// FU field, and so does Haydn's now.
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

  // AIE peer AIEInstFormat::hasSingleSlot / getSlot (AIEMCFormats.cpp:33-56):
  // a committed format-member (post-setDesc) has exactly one SlotsMap entry.
  // MultiSlot_Pseudo / MultiOpcode logicals have HasMultipleSlotOptions.
  bool hasSingleSlot() const {
    return SlotsMap.size() == 1 && !HasMultipleSlotOptions;
  }
  MCSlotKind getSingleSlotKind() const {
    assert(hasSingleSlot() && "not a single-slot format-member");
    return SlotsMap.begin()->SlotKind;
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
// delegates to the generated table and no longer reads the dormant row.
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
  // (only BUNDLE128_FULL is), so this query cannot yet delegate to
  // the generated table; retires when M0_64 migrates (post-P5).
  const MCFormatDesc &getMode0FormatDesc() const;

  // \returns the format-desc for one of the two format E composites,
  // \p CompositeOpcode being Haydn::BUNDLE_E2 or Haydn::BUNDLE_E3. Its
  // SlotsMap carries that composite's entry-window positions inside the 96-bit
  // bundle; use getSlotOffsetsHiBit(Kind) on the result to recover an entry's
  // MSB-indexed window.
  //
  // Unlike Bundle128's single BUNDLE128_FULL, WHICH composite applies is not a
  // constant — it follows from the entry count. Callers get it from
  // Bundle::getFormatOrNull()->Opcode, i.e. from the generated packet-format
  // table's coverage of the occupied slots, rather than deciding for
  // themselves. Delegates to getFormatDesc reading the GENERATED
  // Haydn::Formats table (single truth).
  const MCFormatDesc &getCompositeFormatDesc(unsigned CompositeOpcode) const;

  // \returns the MCInstrInfo this formats object was built with, or nullptr.
  //
  // The unit axis needs a member's NAME to read its unit off, and only
  // MCInstrInfo has names. Carrying it here rather than asking every Bundle
  // caller to pass it separately is what stops a caller silently losing the
  // unit check — HaydnMCCodeEmitter already constructed
  // HaydnMCFormatsWithMII and then dropped the MII on the floor when it built
  // its Bundle. See FORMAT-E-SWITCH-PLAN.md § 5.7.
  virtual const MCInstrInfo *getMCInstrInfo() const { return nullptr; }

  // \returns whether \p Opcode has an entry in the format-desc table.
  virtual bool isSupportedInstruction(unsigned Opcode) const;

  // AIE peer AIEBaseMCFormats::getSlotKind (AIEBaseMCFormats.cpp:66-75):
  // fixed slot of a committed format-member opcode after setDesc materialize.
  // Returns default/unknown MCSlotKind() for multi-slot logicals
  // (HasMultipleSlotOptions) and opcodes not in the Formats table — those
  // use PlacementAlternative / tryAddProduct instead.
  virtual MCSlotKind getSlotKind(unsigned Opcode) const;

  // \returns Format Description, index based on the opcode.
  virtual std::optional<unsigned>
  getFormatDescIndex(unsigned Opcode) const = 0;

  // \returns the member-opcode vector for a multi-slot logical / MultiSlot_Pseudo
  // (AIE AIEMCFormats.h:376-379 peer), or nullptr if \p Opcode has no
  // alternatives. Rows are sparse size-3: index == field, 0 for missing
  // members. PlacementAlternative FieldSlots = 1<<index for non-zero entries;
  // getLegalSlots ORs those indices.
  virtual const std::vector<unsigned> *
  getAlternateInstsOpcode(unsigned Opcode) const = 0;

  // Legal-slot bitmask (Haydn::SLOT: bit k = slot k). Derived from sparse
  // getAlternateInstsOpcode — bit k set iff Alts[k] != 0 (index == field).
  // Returns 0 with no alt row. Bundle/HR placement uses PlacementAlternative
  // tryAdd for alts-bearing logicals.
  virtual SlotBits getLegalSlots(unsigned Opc) const = 0;

  // \returns the slot descriptor for \p Kind, or nullptr if unknown.
  virtual const MCSlotInfo *getSlotInfo(const MCSlotKind Kind) const = 0;

  // \returns the base of the format-desc table (the GENERATED Formats).
  virtual const MCFormatDesc *getMCFormats() const = 0;

  // \returns the PacketFormats table from HaydnFormat.h (Module A).
  virtual const PacketFormats &getPacketFormats() const = 0;

  // \returns the FormatAvailable LUT from HaydnFormat.h.
  virtual ArrayRef<bool> getIsFormatAvailable() const = 0;

  // \returns whether \p SlotSet (a combination of Haydn::SLOT_P* masks) names
  // a packetable slot combination (true iff some format's SlotSet covers it).
  bool isFormatAvailable(uint64_t SlotSet) const;

  // \returns the packet format holding exactly \p NumEntries entries, or
  // nullptr. Format E has one row per entry count (BUNDLE_E2, BUNDLE_E3), so
  // this is how a caller that knows only "how many instructions are written
  // here" — the asm parser reading `{ a; b; c }` — recovers which slots those
  // positions name. Its getSlots() is in AsmString order, high entry first.
  const VLIWFormat *getFormatByEntryCount(unsigned NumEntries) const;

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

  // Alts-derived legal slots (OR of non-zero sparse alt indices).
  // Recognizes logical opcodes with a getAlternateInstsOpcode row. For a
  // member-opcode-aware query (MC encoder residual on already-_S* children),
  // use HaydnMCFormatsWithMII (strips `_S<k>` then consults alts / suffix).
  // MC encode serializes member Desc as-is (AIE).
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// member-opcode-aware formats subclass
//===----------------------------------------------------------------------===//
//
// The MC encoder wires `Haydn::Bundle<MCInst>` into `encodeBundleE` as the
// AIE-faithful shuffler. Bundle's `pickSlot` calls `getLegalSlots(Opc)` through
// the `HaydnBaseMCFormats*` interface. The base `HaydnMCFormats::getLegalSlots`
// only recognizes logical opcodes (getAlternateInstsOpcode rows). For an
// already-member child (AsmParser `.sN` / post-setDesc), `getLegalSlots`
// would return 0 without suffix strip.
//
// `HaydnMCFormatsWithMII` NORMALIZES a `_S<k>` member opcode to its logical
// base (via the name suffix) BEFORE consulting alts-derived getLegalSlots.
// It carries an `MCInstrInfo&` for the name lookup. The HR/scheduler path
// (no MII) keeps using base `HaydnMCFormats` and never sees member opcodes
// before setDesc.

// \returns the slot index (0/1/2) encoded in \p Opc's `_S<k>` name
// suffix, or -1 if \p Opc is not a format-member opcode.
int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII);

// \returns the logical base opcode \p Opc was placed from, or \p Opc itself
// when it is already a logical. Callers that reason about an instruction's
// kind rather than its placement should fold through this so they name
// logicals only and stay independent of how members happen to be spelled.
unsigned getHaydnLogicalBaseOpcode(unsigned Opc, const MCInstrInfo &MII);

// Drop the placement suffix from a format-member instruction name, leaving the
// logical it was placed from. Two spellings are recognized:
//
//   Bundle128  `<logical>_S<k>`                 k = slot 0..2
//   format E   `<logical>_P<form><pos>_<unit>`  unit = the seven hardware units
//
// \returns the logical part, or std::nullopt when \p Name carries neither
// suffix and is therefore already a logical. Kept as a pure string operation
// so both spellings can be covered by unit tests while only one of them is
// live.
std::optional<StringRef> stripHaydnMemberSuffix(StringRef Name);

// The spelling of \p U, e.g. Unit::ALU0 -> "ALU0".
StringRef haydnUnitName(Haydn::Unit U);

// The hardware unit a format-member name names, or nullopt.
//
// Bundle128 members return nullopt, and that is the correct answer rather than
// a gap: its slot model pinned one unit per slot, so the slot and the unit were
// the same fact and the spelling never carried it. Format E decouples them —
// `ADD32_P30_ALU0` and `ADD32_P31_ALU0` are different entries on the SAME unit,
// and only one of them may be in a bundle.
//
// Kept a pure string operation, like stripHaydnMemberSuffix, so format E's
// spelling is unit-testable while Bundle128 is the one that is live.
std::optional<Haydn::Unit> haydnMemberUnitFromName(StringRef Name);

// haydnMemberUnitFromName as an occupancy bit, or 0 for "no unit modelled".
Haydn::UnitBits haydnMemberUnitBits(StringRef Name);

// `HaydnMCFormats` subclass that normalizes member opcodes before consulting
// alts-derived getLegalSlots. Constructed by the MC encoder (holds MCInstrInfo).
// The HR/scheduler path keeps using the base `HaydnMCFormats` (logical-only).
class HaydnMCFormatsWithMII : public HaydnMCFormats {
  const MCInstrInfo &MII;

public:
  HaydnMCFormatsWithMII(const MCInstrInfo &MII) : MII(MII) {}

  const MCInstrInfo *getMCInstrInfo() const override { return &MII; }

  // Member-opcode-aware legal-slot query. Strips the `_S<k>` suffix to
  // recover the logical base, then queries alts-derived getLegalSlots.
  // For a logical opcode this is identical to the base implementation.
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// Haydn slot bitmask helpers
//===----------------------------------------------------------------------===//

// \returns the MCSlotKind corresponding to a Haydn::SLOT* bitmask (1/2/4), or
// SLOT_UNKNOWN if \p Mask is not a single-slot Haydn mask.
MCSlotKind haydnSlotMaskToKind(SlotBits Mask);

//===----------------------------------------------------------------------===//
// shared Bundle128-target predicate (Bundle128 16-byte emit path).
//===----------------------------------------------------------------------===//
//
// The MC encoder (`HaydnMCCodeEmitter::encodeInstruction` / `encodeBundleE`)
// routes BOTH standalone Bundle128-target opcodes AND formed BUNDLEs whose
// real children are all Bundle128-target through `encodeBundleE`, emitting a
// 128-bit (16-byte) Bundle128 composite word via `emitBundleWord`.
//
// A `_S<k>` opcode is SELF-DESCRIBING: its name suffix carries the slot
// digit (0/1/2 = S0/S1/S2). A logical opcode with PlacementAlternative members
// also routes through the Bundle128 path (encode materializes the member via
// getAlternateInstsOpcode / setDesc). Both cases return true here.
//
// SHARED so that `HaydnInstrInfo::getInstSizeInBytes` (consulted by upstream
// BranchRelaxation's `computeBlockSize`) mirrors the encoder's emit dispatch
// exactly — the size model MUST predict the Bundle128 16-byte emit width, or
// BranchRelaxation undercounts branch distances (~8x), deems every branch in
// range, never relaxes, and the WIDE conditional branch overflows
// `FIXUP_HAYDN_WIDE_BranchSImm12` (±4 KB) at MC-fixup time on fns > ~4 KB
// (adddf3/divdf3). It also prevents the `BranchRelaxation::verify` BlockSize
// assert (BlockInfo.Size vs post-relax recompute).

// \returns true iff \p Opc is emitted via the 128-bit Bundle128 path:
// a `_S<k>` opcode (self-describing name suffix), OR
// a logical opcode with PlacementAlternative members (encode materializes).
// In both cases the emitted width is 16 bytes. This mirrors the MC encoder's
// `isBundleTargetOpcode` gate (`HaydnMCCodeEmitter.cpp`).
bool isHaydnBundleTargetOpcode(unsigned Opc, const MCInstrInfo &MII);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H
