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
// FE8: retired 128-bit format-desc / target-opcode helpers — product emit is
// Format E only via getObjectEncodingProfile / getBundleFormatRow.
// getMCFormats exposes the generated Formats per the AIE contract.
//
// Decision: HaydnGenFormats.inc is the SCHEMA OWNER for the live composite
// slot/member tables. The neutral ObjectEncodingProfile registry in
// HaydnFormat.h is the production profile/row/size authority.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H

#include "HaydnFormat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <map>
#include <optional>
#include <unordered_map>

namespace llvm {

namespace haydn {
namespace format_e {
struct FormatEMemberRec;
} // namespace format_e
} // namespace haydn

using SlotBits = uint64_t;
class MCRegisterInfo;
class MCSlotInfo;

// Standalone parse: generated-member Mode, not textual child count.
bool haydnFormatELogicalIsE3Only(unsigned Opcode);
bool haydnFormatELogicalIsE2Only(unsigned Opcode);

/// Standalone braced-bundle composite opcode from generated membership.
/// assignFormatEMemberEntries is row identity (MemberId + EntryIdx), not
/// child cardinality. PacketFormats first-covering is the smaller product
/// row when both Modes place (AIE PacketFormats::getFormat,
/// AIEMCFormats.cpp:29-36 / AIEBaseAsmParser.h:164-180 emitBundle). Extra
/// NOP pads are not occupancy. Returns 0 when no product row covers.
unsigned haydnSelectStandaloneFormatEOpcode(ArrayRef<unsigned> LogicalOpcodes);

/// RelocLayout ValueShift for Format E HWLR Off1/Off2 word fields.
/// 0 when \p MemberId is not HWLRIII/HWLRIIR. Dump bytes = field << shift.
/// AIE getSImmOpValueXStep (AIEBaseMCCodeEmitter.h:127-150) binds Shift on
/// the operand class; Haydn members use uimm so fill/decode consume this.
unsigned haydnFormatEHwloopImmFieldShift(int MemberId);

/// Closed FieldSlot / public-logical → generated member operand keep-map.
/// Same law for Finalize cutover and MC fill (AIE serializes typed members
/// as-is; Haydn overlays only these drop rules). Not a register-class
/// bag-sort. \p KindOk, when set, rejects a candidate whose operands do
/// not match the member Desc (trailing vs vestigial-first-ins).
/// Nullopt = fail closed.
std::optional<SmallVector<unsigned, 4>>
haydnFormatEKeepOperands(
    const MCInstrDesc &OldDesc, const MCInstrDesc &NewDesc,
    function_ref<bool(unsigned OldI, unsigned NewI)> KindOk = nullptr);

/// Private generated member for opcode \p Opc, or null. NOP multi-maps and
/// is excluded (product NOP is a completion pad, not a MemberId).
const haydn::format_e::FormatEMemberRec *
haydnFindFormatEMemberByOpcode(unsigned Opc);

/// Map a public logical / residual MCInst onto generated member \p Mem
/// (wire field order + reg classes from tblgen Desc). As-is copy when
/// the opcode already is the member; otherwise positional promote or the
/// closed keep-map. Not a register-class bag-sort. False = fail closed.
bool haydnFillFormatEMemberInst(const haydn::format_e::FormatEMemberRec &Mem,
                                const MCInst &Logical, const MCInstrInfo &MII,
                                const MCRegisterInfo &MRI, MCInst &Out);

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
// Generated Formats (HaydnGenFormats.inc) remain the live composite/member
// table. getAlternateInstsOpcode is occupancy + Format E members
// (HaydnGenAltOccupancy.inc). Production object-encoding identity is the
// neutral registry (haydn::format::ObjectEncodingProfileDesc, E96 only).
class HaydnBaseMCFormats {
public:
  virtual ~HaydnBaseMCFormats() = default;

  // \returns the format descriptor for \p Opcode. Asserts that the opcode is
  // in the table; callers that cannot guarantee this should test
  // isSupportedInstruction first.
  virtual const MCFormatDesc &getFormatDesc(unsigned Opcode) const;

  // \returns the immutable production ObjectEncodingProfile (E96).
  const haydn::format::ObjectEncodingProfileDesc &
  getObjectEncodingProfile() const;

  // \returns the row descriptor for \p Row, or nullptr if unknown.
  const haydn::format::BundleFormatRowDesc *
  getBundleFormatRow(haydn::format::BundleFormatRowID Row) const;

  // \returns EncodedBytes for a production/test row via the registry.
  haydn::format::EncodedBytes
  getEncodedBytes(haydn::format::BundleFormatRowID Row) const;

  // \returns EncodedBits for a production/test row via the registry.
  haydn::format::EncodedBits
  getEncodedBits(haydn::format::BundleFormatRowID Row) const;

  // \returns max EncodedBytes among rows permitted by the production profile.
  haydn::format::EncodedBytes getProductionMaxEncodedBytes() const;

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
  // alternatives. Rows are sparse size-3: index == residual occupancy class,
  // 0 for a hole. Non-zero entries are Format E members when a generated
  // member occupies that entry; otherwise the residual FieldSlot (WFI_S0,
  // SIMD *_S1 when no matching 0-def span). PlacementAlternative FieldSlots =
  // 1<<index;
  // getLegalSlots ORs those indices. Do not derive holes from raw EntryIdx.
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

  // \returns whether \p SlotSet (a combination of Haydn::SLOT* masks) names a
  // packetable slot combination (true iff some format's SlotSet covers it).
  bool isFormatAvailable(uint64_t SlotSet) const;

protected:
  // Check if the Instruction is indeed into the Tables (AIE pattern).
  void checkInstructionIsSupported(unsigned Opcode) const;
};

// Concrete subclass. getSlotInfo / getFormatDescIndex come from
// HaydnGenFormats.inc (GET_FORMATS_SLOTINFOS_MAPPING /
// GET_OPCODE_FORMATS_INDEX_FUNC). getAlternateInstsOpcode is occupancy +
// Format E members (HaydnGenAltOccupancy.inc).
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
  // member-opcode-aware query (post-setDesc Format E member), use
  // HaydnMCFormatsWithMII (EntryIdx from FormatEMemberRec, AIE
  // AIEBaseMCFormats.cpp:66-75 slot identity). MC encode serializes member
  // Desc as-is (AIE).
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// member-opcode-aware formats subclass
//===----------------------------------------------------------------------===//
//
// Bundle's `pickSlot` calls `getLegalSlots(Opc)` through the
// `HaydnBaseMCFormats*` interface. The base `HaydnMCFormats::getLegalSlots`
// only recognizes logical opcodes (getAlternateInstsOpcode rows). For an
// already-member child (post-setDesc), `getLegalSlots` would return 0
// without member identity.
//
// `HaydnMCFormatsWithMII` uses generated FormatEMemberRec EntryIdx (AIE
// AIEBaseMCFormats.cpp:66-75), not a FieldSlot name suffix. Residual `_S*`
// FieldSlots are retired (0 defs). It carries an `MCInstrInfo&` for the
// name lookup. The HR/scheduler path (no MII) keeps using base
// `HaydnMCFormats` and never sees member opcodes before setDesc.

// \returns the generated Format E EntryIdx for member opcode \p Opc, or -1
// if \p Opc is not a Format E member.
int getHaydnFlexSlotFromName(unsigned Opc, const MCInstrInfo &MII);

// `HaydnMCFormats` subclass that maps generated members to their EntryIdx
// before consulting alts-derived getLegalSlots. Constructed by the MC
// encoder (holds MCInstrInfo). The HR/scheduler path keeps using the base
// `HaydnMCFormats` (logical-only).
class HaydnMCFormatsWithMII : public HaydnMCFormats {
  const MCInstrInfo &MII;

public:
  HaydnMCFormatsWithMII(const MCInstrInfo &MII) : MII(MII) {}

  // Member-opcode-aware legal-slot query. Generated members occupy
  // EntryIdx; logicals use alts-derived getLegalSlots.
  SlotBits getLegalSlots(unsigned Opc) const override;
};

//===----------------------------------------------------------------------===//
// Haydn slot bitmask helpers
//===----------------------------------------------------------------------===//

// \returns the MCSlotKind corresponding to a Haydn::SLOT* bitmask (1/2/4), or
// SLOT_UNKNOWN if \p Mask is not a single-slot Haydn mask.
MCSlotKind haydnSlotMaskToKind(SlotBits Mask);

/// Residual S0/S1/S2 MCSlotKind → Haydn::SLOT0/1/2 FieldSlots bit.
/// PlacementAlternative FieldSlots use issue bits (1/2/4); residual S* kinds
/// sit after E2/E3 entry kinds in the generated enum. Returns 0 if \p Kind is
/// not a residual issue slot.
SlotBits residualSlotKindToFieldSlots(MCSlotKind Kind);

//===----------------------------------------------------------------------===//
// Format E product parcel helpers (MC encode / pad)
//===----------------------------------------------------------------------===//
//
// Parcel length always comes from the production registry row EncodedBytes
// (both E2 and E3 assert the same current golden size). Call sites must not
// spell bare 12/16/96/128 widths. Canonical idle/completion wire bytes are
// gated: when golden has not registered a product idle form, pad and empty
// parcels fail closed rather than inventing indicator/payload/top-pad bits.

/// Production max EncodedBytes for profile E96 (current rows share one size).
haydn::format::EncodedBytes haydnProductionParcelBytes();

/// True iff a product-approved canonical idle/completion wire form is
/// registered for executable padding. False while idle remains undefined.
bool haydnHasCanonicalIdleParcel();

/// Header byte bits [5:0]: format_indicator=0b111, entry_num, reserved=0b00.
/// \p EntryNum is 0 (two-entry) or 1 (three-entry). Other values assert.
uint8_t haydnFormatEHeaderByte(unsigned EntryNum);

/// Append one Format E parcel (production EncodedBytes) from an APInt whose
/// width is the generated EncodedBits, as little-endian host bytes (bit 0
/// in byte 0). Asserts width and size against the registry row.
void haydnEmitFormatEParcelLE(const APInt &Word, SmallVectorImpl<char> &CB);

/// If a product idle parcel exists, append one copy into \p Out and return
/// true. Otherwise leave \p Out unchanged and return false (fail closed).
bool haydnTryGetCanonicalIdleParcel(SmallVectorImpl<char> &Out);

/// Generated Format E entry window for (\p Mode, \p EntryIdx): width and
/// absolute parcel LSB from FormatETypeLayouts (EntryHi/EntryLo). False
/// when no layout row exists. Replaces the hand E2/E3 width/LSB switch.
bool haydnFormatEEntryWindow(uint8_t Mode, unsigned EntryIdx, unsigned &Width,
                             unsigned &LSB);

/// Write executable pad of \p CountBytes as whole production-size idle
/// parcels. Returns false when CountBytes is not a multiple of the production
/// EncodedBytes, or when no product-approved idle form is registered.
bool haydnWriteCanonicalIdlePad(raw_ostream &OS, uint64_t CountBytes);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCFORMATS_H
