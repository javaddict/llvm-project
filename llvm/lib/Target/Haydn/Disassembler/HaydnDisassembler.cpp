//===-- HaydnDisassembler.cpp - Disassembler for Haydn --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Product decode is Format E only (FE8). Cursor size is the production registry
// EncodedBytes for FormatE96 rows (via haydn::format typed APIs).
//
// Path:
//   1. tryDecodeFormatE — require a full product parcel; validate header
//      indicator/reserved/entry_num (fail-closed on true malformed); inverse-
//      resolve each entry via FormatEInverse + FormatEMembers; emit
//      BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY of logical children.
//   2. Soft-NOP individual entries only for reserved E2 map=11 / zero or
//      non-matching residual underfill — never invents logicals; never fails
//      the whole parcel for residual pad (objdump `<unknown>` rejects sim).
//      Non-zero unmatched payload is still a soft-NOP mnemonic, annotated
//      `<unresolved:0x…>` on the comment stream (T-MC9 auditor honesty).
//   3. Short residual (< product EncodedBytes) → Fail with Size = remaining
//      (no 2-byte NOP product path; all-zero is not Format E).
//
// Size contract: every Success/Fail that consumes a product parcel sets
// Size = product EncodedBytes so objdump lines are BundleSim-legal tokens.
// Malformed header/framing cases Fail with the same Size (forward progress).
// Bounds-safe InstPrinter `<?>` remains the anti-crash backstop.
//
//===----------------------------------------------------------------------===//

#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "haydn-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

//===----------------------------------------------------------------------===//
// Register decode helpers (used by TableGen-generated tables)
//===----------------------------------------------------------------------===//

// Decode a GPR32 register (4-bit encoding, R0-R15).
static DecodeStatus decodeGPR32(MCInst &Inst, uint32_t RegNo) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(Haydn::R0 + RegNo));
  return MCDisassembler::Success;
}

// Decode a DR64 register (4-bit encoding, D0-D15).
static DecodeStatus decodeDR64(MCInst &Inst, uint32_t RegNo) {
  if (RegNo >= 16)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(Haydn::D0 + RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPR32RegisterClass(MCInst &Inst, uint32_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  return decodeGPR32(Inst, RegNo);
}

// GPR32Lo (r0-r7, 3-bit encoding) — the low 8 GPRs. Reject RegNo >= 8.
// Retained because the generated decoder tables still reference it by name.
static DecodeStatus DecodeGPR32LoRegisterClass(MCInst &Inst, uint32_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo >= 8)
    return MCDisassembler::Fail;
  return decodeGPR32(Inst, RegNo);
}

static DecodeStatus DecodeDR64RegisterClass(MCInst &Inst, uint32_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  return decodeDR64(Inst, RegNo);
}

//===----------------------------------------------------------------------===//
// AIE-style scaled-immediate decoder for logical *_W / *_dr / hwloop_off*
// operand classes (Format E encode).
//
// Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes via tablegen
// `DecoderMethod`. Reverses the encoder's `getSImmOpValueXStepWide`: reads the
// raw N-bit field, sign/zero-extends per IsSigned, then shifts left by `Shift`
// to convert field units back to byte units (so the rendered disassembly shows
// byte offsets, matching RISCV's decodeSImmOperandAndLslN convention).
//
// Template parameters:
// N = encoded field width in bits
// Shift = left-shift applied after sign/zero-extension (1 for §5.14
// branch offsets in 2-byte units, 2 for §5.11/5.12 hwloop offsets
// in 4-byte units, 0 otherwise).
// IsSigned = 1 => sign-extend the N-bit field; 0 => zero-extend.
//===----------------------------------------------------------------------===//
template <unsigned N, unsigned Shift, bool IsSigned>
static DecodeStatus decodeSImmOperandXStepWide(MCInst &Inst, uint32_t Imm,
                                               int64_t Address,
                                               const MCDisassembler *Decoder) {
  // disassembler must NEVER abort on hostile.text (CLAUDE.md
  // "bounds-safe printOperand" philosophy: decode-or-degrade, never assert).
  // The generated decoder can call this DecoderMethod with an `Imm` whose
  // tablegen-aggregated field slice is WIDER than N bits — e.g. the LD slot
  // sub-trie reads an 8-bit slice for a `simm6` operand
  // (fieldFromInstruction(insn, 28, 8) → decodeSImmOperandXStepWide<6,0,1> on
  // cases 147/148/149/150 in HaydnGenDisassemblerTables.inc) because tblgen
  // merges the adjacent `reserved` field into the same decoded region. Bits
  // above N are reserved/zero on a correctly-encoded instruction, but a legacy
  // parcel stream or a straddle read can set them. Mask to N bits BEFORE the
  // sign/zero-extend so the assert can never fire and objdump renders the
  // decoded operand (the encoder only ever wrote N bits; masking recovers
  // exactly what was encoded). This mirrors the encoder's own `maskTrailingOnes`
  // contract in getSImmOpValueXStepWide.
  Imm &= maskTrailingOnes<uint32_t>(N);
  assert((Imm & ~maskTrailingOnes<uint32_t>(N)) == 0 &&
         "post-mask residual bits — mask logic broken");
  int64_t Extended = IsSigned ? SignExtend64<N>(Imm) : static_cast<int64_t>(Imm);
  Inst.addOperand(MCOperand::createImm(Extended << Shift));
  return MCDisassembler::Success;
}

//===----------------------------------------------------------------------===//
// Bitfield extraction helpers
//===----------------------------------------------------------------------===//

// Extract bits [Hi:Lo] (inclusive) from a value.
static uint32_t extractBits(uint64_t Word, unsigned Lo, unsigned Width) {
  return static_cast<uint32_t>((Word >> Lo) & ((1ULL << Width) - 1));
}

// Format E composite InstSlot decoders (BUNDLE_E96_* DecoderMethods).
// Defined after tables are included (need decodeInstruction). Declared here so
// decodeToMCInst template can see them at parse time.
static DecodeStatus decodeE2_0Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus decodeE2_1Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus decodeE3_0Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus decodeE3_1Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus decodeE3_2Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder);

template <typename InsnType>
static DecodeStatus decodeInstruction(const uint8_t DecodeTable[], MCInst &MI,
                                      InsnType insn, uint64_t Address,
                                      const MCDisassembler *DisAsm,
                                      const MCSubtargetInfo &STI);

//===----------------------------------------------------------------------===//
// Main disassembler class
//===----------------------------------------------------------------------===//

class HaydnDisassembler : public MCDisassembler {
  std::unique_ptr<const MCInstrInfo> MCII;

public:
  HaydnDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                    MCInstrInfo const *MII)
      : MCDisassembler(STI, Ctx), MCII(MII) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  // Reset disassembler state at symbol boundaries. Prevents corrupted
  // bytes from cascading across function boundaries.
  Expected<bool> onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                               ArrayRef<uint8_t> Bytes,
                               uint64_t Address) const override;

  const MCInstrInfo &getMCII() const { return *MCII; }
};

} // end anonymous namespace

// Include auto-generated decoder tables (Format E entry namespaces + residual).
// Product decode uses DecoderTableE2E0* / E3E* on entry windows.
#define LLVM_DISASSEMBLER_HAYDN_DECODER_TABLES
#include "HaydnGenDisassemblerTables.inc"

// InsnBitWidth specializations for decodeInstruction Instantiation
// (same anonymous-namespace TU as the generated tables).
namespace {
template <> constexpr uint32_t InsnBitWidth<uint32_t> = 32;
// uint64_t carries 48-bit E2 entry containers (Size=6).
template <> constexpr uint32_t InsnBitWidth<uint64_t> = 48;
} // namespace

// Nested entry decode for BUNDLE_E96_* InstSlot operands (same anon NS).
namespace {
static DecodeStatus decodeFormatEEntrySlot(MCInst &MI, uint64_t EntryBits,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder,
                                           const uint8_t *Table, bool Use64) {
  MCContext &Ctx = Decoder->getContext();
  MCInst *Nested = Ctx.createMCInst();
  DecodeStatus S = MCDisassembler::Fail;
  if (Use64) {
    uint64_t Insn = EntryBits;
    S = decodeInstruction(Table, *Nested, Insn, Address, Decoder,
                          Decoder->getSubtargetInfo());
  } else {
    uint32_t Insn = static_cast<uint32_t>(EntryBits);
    S = decodeInstruction(Table, *Nested, Insn, Address, Decoder,
                          Decoder->getSubtargetInfo());
  }
  if (S == MCDisassembler::Fail) {
    Nested->clear();
    Nested->setOpcode(Haydn::NOP);
    S = MCDisassembler::Success;
  }
  MI.addOperand(MCOperand::createInst(Nested));
  return S;
}

static DecodeStatus decodeE2_0Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeFormatEEntrySlot(MI, Insn, Address, Decoder, DecoderTableE2E048,
                                /*Use64=*/true);
}
static DecodeStatus decodeE2_1Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeFormatEEntrySlot(MI, Insn, Address, Decoder, DecoderTableE2E148,
                                /*Use64=*/true);
}
static DecodeStatus decodeE3_0Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeFormatEEntrySlot(MI, Insn, Address, Decoder, DecoderTableE3E032,
                                /*Use64=*/false);
}
static DecodeStatus decodeE3_1Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeFormatEEntrySlot(MI, Insn, Address, Decoder, DecoderTableE3E132,
                                /*Use64=*/false);
}
static DecodeStatus decodeE3_2Slot(MCInst &MI, uint64_t Insn, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  return decodeFormatEEntrySlot(MI, Insn, Address, Decoder, DecoderTableE3E232,
                                /*Use64=*/false);
}
} // namespace

Expected<bool> HaydnDisassembler::onSymbolStart(SymbolInfoTy &Symbol,
                                                uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address) const {
  // At the start of a symbol, reset any in-progress state. This prevents
  // corrupted bytes from one function from cascading into the next.
  // Follows the Hexagon pattern of resetting at symbol boundaries.
  Size = 0;
  return true;
}

//===----------------------------------------------------------------------===//
// Format E product decode (registry EncodedBytes cursor)
//===----------------------------------------------------------------------===//

namespace {

/// Production parcel size for Format E (typed registry; both E2/E3 rows match).
/// FE8: non-12 sizes are not product — fail closed.
static unsigned productFormatEEncodedBytes() {
  using namespace haydn::format;
  EncodedBytes B = maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
  assert(B.Value == 12u && "product EncodedBytes must be Format E 12");
  assert(encodedBytesOrDie(BundleFormatRowID::E96TwoEntry) == B &&
         encodedBytesOrDie(BundleFormatRowID::E96ThreeEntry) == B &&
         "E2/E3 product EncodedBytes must agree");
  return B.Value;
}

/// Absolute bit extract from a little-endian Format E word (bit 0 = LSB).
static uint64_t extractFormatEBits(const APInt &Word, unsigned Lo,
                                   unsigned HiInclusive) {
  assert(HiInclusive >= Lo && "empty Format E field");
  unsigned Width = HiInclusive - Lo + 1;
  return Word.extractBitsAsZExtValue(Width, Lo);
}

/// Map a Format E logical catalog name to a live MC opcode when present.
static unsigned lookupLogicalOpcode(const MCInstrInfo &MII, StringRef Logical) {
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return Haydn::NOP;
  // Golden catalog logicals that only exist as residual wide/S0 public names.
  if (Logical.equals_insensitive("SET_HWLOOP_F2"))
    return Haydn::SET_HWLOOP_F2_W;
  if (Logical.equals_insensitive("SET_HWLOOP_REG"))
    return Haydn::SET_HWLOOP_REG_W;
  if (Logical.equals_insensitive("SET_HWLOOP"))
    return Haydn::SET_HWLOOP_W;
  for (unsigned Opc = 0, E = MII.getNumOpcodes(); Opc != E; ++Opc) {
    if (MII.getName(Opc).equals_insensitive(Logical))
      return Opc;
  }
  // Prefer wide public forms when bare name is a pseudo without encode path.
  std::string Wide = Logical.str() + "_W";
  for (unsigned Opc = 0, E = MII.getNumOpcodes(); Opc != E; ++Opc) {
    if (MII.getName(Opc).equals_insensitive(Wide))
      return Opc;
  }
  return 0;
}

/// GE96-03: cond-branch/JAL fields are byte PC+imm (no dump <<1).
/// SET_HWLOOP Off1/Off2 remain word scale (<<2) for dump bytes.
static unsigned formatEControlImmByteShift(StringRef Logical) {
  const std::string Peeled = haydn::format_e::peelLogicalOpcodeName(Logical);
  const StringRef Name = Peeled;

  // GE96-03: cond-branches and JAL print the field as bytes (ValueShift=0).
  // SET_HWLOOP begin/end offsets stay word scale (<<2).
  if (Name.equals_insensitive("SET_HWLOOP") ||
      Name.equals_insensitive("SET_HWLOOP_F2"))
    return 2u;

  // JALR and all other imms: field units == dump units (bytes / counts).
  return 0u;
}

/// One resolved Format E entry (inverse hit or soft-NOP underfill).
struct FormatEResolvedEntry {
  StringRef Logical;
  bool IsNop = true;
  int MemberId = -1; // >=0 only on inverse hit
  // Authoritative type layout from FormatEMembers[MemberId].LayoutId.
  const haydn::format_e::FormatETypeLayoutRec *Layout = nullptr;
  /// Non-zero unmatched entry payload. Soft-NOP still succeeds (BundleSim
  /// rejects `<unknown>`), but objdump annotates the junk for auditors.
  std::optional<uint64_t> UnresolvedPayload;
};

/// Resolve one entry via generated type layouts + FormatEInverse.
///
/// Fail-closed only on missing entry window (no MapLayout for Mode/EntryIdx).
/// Legal golden encodings inverse-hit and return MemberId + authoritative
/// LayoutId so operand recovery never zero-fills on a real member.
/// Soft-NOP (Logical=NOP, MemberId=-1) for: E2 reserved map=11, zero entry
/// underfill, and non-zero residual that does not inverse-match — never
/// invents a fake logical and never fails the whole parcel (objdump
/// `<unknown>` → BundleSim reject).
static bool resolveFormatEEntry(const APInt &Word, uint8_t Mode,
                                uint8_t EntryIdx, FormatEResolvedEntry &Out) {
  using namespace haydn::format_e;

  Out = FormatEResolvedEntry{};

  const FormatETypeLayoutRec *MapLayout = nullptr;
  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    const FormatETypeLayoutRec &L = FormatETypeLayouts[I];
    if (L.Mode == Mode && L.EntryIdx == EntryIdx) {
      MapLayout = &L;
      break;
    }
  }
  if (!MapLayout)
    return false;

  uint64_t UnitMap =
      extractFormatEBits(Word, MapLayout->MapLo, MapLayout->MapHi);

  // E2 map=11 is reserved (not a unit). Soft-NOP residual; do not Fail the
  // parcel (legal product objects must never land here after encode fix).
  if (Mode == 0 && UnitMap == 3u) {
    Out.Logical = "NOP";
    Out.IsNop = true;
    return true;
  }

  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    const FormatETypeLayoutRec &L = FormatETypeLayouts[I];
    if (L.Mode != Mode || L.EntryIdx != EntryIdx || L.UnitMap != UnitMap)
      continue;

    // Type code sits immediately above the map field (golden placement).
    unsigned TypeLo = static_cast<unsigned>(L.MapHi) + 1u;
    unsigned TypeHi = TypeLo + L.TypeCodeWidth - 1u;
    if (TypeHi > L.EntryHi)
      continue;
    uint64_t TypeCode = extractFormatEBits(Word, TypeLo, TypeHi);
    if (TypeCode != L.TypeCode)
      continue;

    if (L.OpcodeHi < L.OpcodeLo)
      continue;
    uint64_t Opcode = extractFormatEBits(Word, L.OpcodeLo, L.OpcodeHi);
    // Mask to the layout opcode field width so sparse high bits cannot
    // poison inverse identity (legal encode zeroes them; hostile streams
    // still resolve the low field).
    unsigned OpcodeBits = static_cast<unsigned>(L.OpcodeHi - L.OpcodeLo + 1u);
    if (OpcodeBits < 64u)
      Opcode &= (uint64_t(1) << OpcodeBits) - 1u;

    int MemberId = findInverseMemberId(Mode, EntryIdx, L.Unit,
                                       static_cast<uint8_t>(TypeCode),
                                       static_cast<uint16_t>(Opcode));
    if (MemberId < 0)
      continue;

    const FormatEMemberRec &M = FormatEMembers[MemberId];
    Out.Logical = M.Logical;
    Out.IsNop = M.IsNop != 0;
    Out.MemberId = MemberId;
    // Authoritative layout from the inverse-hit member — never re-scan.
    if (M.LayoutId < FormatETypeLayoutCount)
      Out.Layout = &FormatETypeLayouts[M.LayoutId];
    else
      Out.Layout = &L;
    return true;
  }

  // Entire entry payload zero with no inverse hit → empty/NOP entry.
  uint64_t EntryBits =
      extractFormatEBits(Word, MapLayout->EntryLo, MapLayout->EntryHi);
  if (EntryBits == 0) {
    Out.Logical = "NOP";
    Out.IsNop = true;
    return true;
  }
  // Non-zero payload with no inverse hit: soft-NOP underfill rather than
  // failing the whole parcel. BundleSim rejects any `<unknown>` line from
  // llvm-objdump; residual pads / map=11-adjacent junk must still advance.
  // Real ops that inverse-match still resolve above. Record the payload so
  // the comment stream can annotate `<unresolved:0x…>` (T-MC9).
  (void)UnitMap;
  Out.Logical = "NOP";
  Out.IsNop = true;
  Out.UnresolvedPayload = EntryBits;
  return true;
}

/// Product Format E decode. Full-parcel paths always set Size = EncodedBytes.
static DecodeStatus tryDecodeFormatE(MCInst &Instr, uint64_t &Size,
                                     ArrayRef<uint8_t> Bytes, uint64_t Address,
                                     const HaydnDisassembler &DisAsm,
                                     raw_ostream &CStream) {
  using namespace haydn::format;
  using namespace haydn::format_e;

  const unsigned ParcelBytes = productFormatEEncodedBytes();
  Size = ParcelBytes;

  if (Bytes.size() < ParcelBytes)
    return MCDisassembler::Fail;

  APInt Word(static_cast<unsigned>(
                 encodedBitsOrDie(BundleFormatRowID::E96TwoEntry).Value),
             0);
  for (unsigned I = 0; I < ParcelBytes; ++I)
    Word.insertBits(Bytes[I], I * 8, 8);

  uint64_t Indicator = extractFormatEBits(Word, 0, 2);
  uint64_t EntryNum = extractFormatEBits(Word, 3, 3);
  uint64_t Reserved = extractFormatEBits(Word, 4, 5);

  if (Indicator != FormatEIndicatorBits || Indicator != FormatEIndicator ||
      Reserved != FormatEHeaderReserved) {
    Instr = MCInst();
    return MCDisassembler::Fail;
  }

  const BundleFormatRowID Row =
      EntryNum == FormatEEntryNumThree ? BundleFormatRowID::E96ThreeEntry
                                       : BundleFormatRowID::E96TwoEntry;
  if (!productionProfilePermitsRow(Row)) {
    Instr = MCInst();
    return MCDisassembler::Fail;
  }
  const BundleFormatRowDesc *RowDesc = getBundleFormatRow(Row);
  if (!RowDesc) {
    Instr = MCInst();
    return MCDisassembler::Fail;
  }

  const uint8_t Mode = EntryNum == FormatEEntryNumThree ? 1 : 0;
  const unsigned EntryCount = RowDesc->EntryCount;

  // Fail-closed: only entry_num ∈ {0=E2, 1=E3} is product-legal. The header
  // field is 1 bit so other values cannot appear; keep the check explicit.
  if (EntryNum != FormatEEntryNumTwo && EntryNum != FormatEEntryNumThree) {
    Instr = MCInst();
    return MCDisassembler::Fail;
  }

  SmallVector<MCInst *, 3> Children;
  Children.reserve(EntryCount);
  bool AnyReal = false;

  for (unsigned E = 0; E < EntryCount; ++E) {
    FormatEResolvedEntry Resolved;
    if (!resolveFormatEEntry(Word, Mode, static_cast<uint8_t>(E), Resolved)) {
      Instr = MCInst();
      return MCDisassembler::Fail;
    }
    const StringRef Logical = Resolved.Logical;
    const bool IsNop = Resolved.IsNop;
    const FormatETypeLayoutRec *Lay = Resolved.Layout;
    if (Resolved.UnresolvedPayload) {
      CStream << "<unresolved:0x"
              << Twine::utohexstr(*Resolved.UnresolvedPayload) << ">\n";
    }

    MCInst *Child = DisAsm.getContext().createMCInst();
    if (IsNop || Logical.equals_insensitive("NOP") || !Lay) {
      Child->setOpcode(Haydn::NOP);
      Children.push_back(Child);
      continue;
    }

    // Entry-relative bits [EntryLo, EntryHi] → tblgen Inst low bits
    // (high pad zeros in Size*8 container).
    const uint64_t EntryBits =
        extractFormatEBits(Word, Lay->EntryLo, Lay->EntryHi);

    const uint8_t *Table = nullptr;
    bool Use64 = false;
    if (Mode == 0) {
      if (E == 0) {
        Table = DecoderTableE2E048;
        Use64 = true;
      } else if (E == 1) {
        Table = DecoderTableE2E148;
        Use64 = true;
      }
    } else {
      if (E == 0)
        Table = DecoderTableE3E032;
      else if (E == 1)
        Table = DecoderTableE3E132;
      else if (E == 2)
        Table = DecoderTableE3E232;
    }

    DecodeStatus DS = MCDisassembler::Fail;
    if (Table) {
      MCInst Decoded;
      if (Use64) {
        uint64_t Insn = EntryBits;
        DS = decodeInstruction(Table, Decoded, Insn, Address, &DisAsm,
                               DisAsm.getSubtargetInfo());
      } else {
        uint32_t Insn = static_cast<uint32_t>(EntryBits);
        DS = decodeInstruction(Table, Decoded, Insn, Address, &DisAsm,
                               DisAsm.getSubtargetInfo());
      }
      if (DS != MCDisassembler::Fail) {
        // Format E member DecoderMethods emit raw field units. Dump bytes:
        //   cond-branch / JAL: field is already PC+imm bytes (GE96-03)
        //   SET_HWLOOP / SET_HWLOOP_F2: Off1/Off2 word fields (ValueShift=2)
        //     → <<2 on operands 1 and 2 (sel, off1, off2, cnt|rs)
        const unsigned ByteShift = formatEControlImmByteShift(Logical);
        if (ByteShift == 2) {
          for (unsigned OI : {1u, 2u}) {
            if (OI >= Decoded.getNumOperands())
              break;
            MCOperand &T = Decoded.getOperand(OI);
            if (T.isImm())
              T.setImm(T.getImm() << 2);
          }
        }
        *Child = Decoded;
        AnyReal = true;
        Children.push_back(Child);
        continue;
      }
    }

    // Soft underfill: unknown entry → NOP (do not Fail the parcel).
    Child->setOpcode(Haydn::NOP);
    Children.push_back(Child);
  }

  // All-NOP / zero-entry envelope is the provisional product idle parcel
  // (header 111 + zero entries). Emit a single NOP for compact objdump lines;
  // real composites keep BUNDLE_E96_* with children.
  Instr = MCInst();
  if (!AnyReal) {
    Instr.setOpcode(Haydn::NOP);
    (void)Address;
    return MCDisassembler::Success;
  }

  // Prefer product Format E composite when entry count matches; otherwise
  // fall back to generic BUNDLE of logical children for printing.
  if (EntryCount == 2)
    Instr.setOpcode(Haydn::BUNDLE_E96_TWO_ENTRY);
  else if (EntryCount == 3)
    Instr.setOpcode(Haydn::BUNDLE_E96_THREE_ENTRY);
  else
    Instr.setOpcode(Haydn::BUNDLE);
  for (MCInst *C : Children)
    Instr.addOperand(MCOperand::createInst(C));

  (void)Address;
  return MCDisassembler::Success;
}

MCDisassembler::DecodeStatus HaydnDisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &CStream) const {
  // Product Format E decoder. Size is the registry EncodedBytes for a full
  // parcel so llvm-objdump hex tokens match BundleSim's product parser.
  using namespace haydn::format;
  const unsigned ParcelBytes =
      maxEncodedBytesInProfile(ObjectEncodingProfileID::E96).Value;
  if (Bytes.size() < ParcelBytes) {
    Instr = MCInst();
    Size = Bytes.size();
    return MCDisassembler::Fail;
  }
  return tryDecodeFormatE(Instr, Size, Bytes, Address, *this, CStream);
}

} // end anonymous namespace

static MCDisassembler *createHaydnDisassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new HaydnDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheHaydnTarget(),
                                         createHaydnDisassembler);
}
