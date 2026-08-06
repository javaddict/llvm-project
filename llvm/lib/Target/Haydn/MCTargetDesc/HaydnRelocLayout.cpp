//===-- HaydnRelocLayout.cpp - Single-source reloc bit-layout ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the single-source Haydn relocation geometry table and the
// generic geometric patcher. See HaydnRelocLayout.h. Product FieldLsb is Format
// E E2 e0 absolute parcel bits (r_offset = parcel origin). Scales follow
// encoding_manual (branch/call halfword ÷2; CallSImm20 byte; hwloop ÷4).
// GE96-03 still open for golden formalization — product does not invent a
// second scale. RelocTrans::Unresolved remains for unpublished kinds.
// Both MC and lld delegate here.
//
//===----------------------------------------------------------------------===//

#include "HaydnRelocLayout.h"
#include "HaydnFixupKinds.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::HaydnReloc;

namespace llvm {
namespace HaydnReloc {
namespace {

// Per-kind geometry. Indexed via rowFor by RelocKind value.
struct Row {
  RelocKind Kind;
  RelocFieldInfo Info;
};

// Field LSB positions within the N-byte LE image starting at r_offset.
// Product Format E (E96, E2 e0 ALU0 primary freestanding placement):
//   I12 branch imm12         @ absolute parcel bits[24:35]  → FieldLsb=24
//   RI12 branch imm12        @ absolute parcel bits[28:39]  → FieldLsb=28
//   WIDE_Call / I20 (JAL)    @ absolute parcel bits[31:50]  → FieldLsb=31
//   LO20 / RI20 (ADDI32)     @ absolute parcel bits[31:50]  → FieldLsb=31
//   HI12 / LUI I12 imm12     @ absolute parcel bits[32:43]  → FieldLsb=32
//     Golden E2 e0 ALU0 I12: reg[23:20], reserved[31:24]=0, imm[43:32].
//     LUI shares the I12 type code with BEQZ/BNEZ/… but branch I12 sits at
//     [24:35]; LUI imm is after the reserved hole. FieldLsb=24 zeroed LUI
//     after link while LO20 applied → 0xfff9c340 for BSS ≥ 0x80000.
//     LO20 was FieldLsb=28 (bits[28:47]); golden ADDI32 RI20 is abs[50:31]
//     → FieldLsb=31. Off-by-3 wrote ring@0x15130 as 0x2a26 (misaligned).
// MC emits r_offset = parcel origin (byte 0) so P is the hardware PC and
// Align=2 range checks see even places. Residual s0 FieldLsb
// (bits[15:4]/bits[23:4]) is retired for these product kinds.
// HWLoopOff1/Off2 retain historical geometry until Format E hwloop lands.
constexpr Row Table[] = {
    {RelocKind::None, {0, 0, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::Data32, {4, 32, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::SImm16, {4, 16, 0, 0, 1, true, false, RelocTrans::None}},
    // Branch/call product scales (encoding_manual halfword ÷2; CallSImm20 byte).
    {RelocKind::BranchSImm16, {4, 16, 0, 1, 2, true, true, RelocTrans::None}},
    // CallSImm20: signed PC-relative BYTE offset (ValueShift=0). Used by
    // assembler-independent YAML thunk geometry tests; FieldLsb=4 is a
    // placeholder write window (tests use zero content).
    {RelocKind::CallSImm20, {4, 20, 4, 0, 1, true, true, RelocTrans::None}},
    {RelocKind::HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::LO16, {4, 16, 0, 0, 1, true, false, RelocTrans::LoMips}},
    {RelocKind::Data32PCRel, {4, 32, 0, 0, 1, true, true, RelocTrans::None}},
    {RelocKind::GOT_HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::TPREL_HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::TPREL_LO16, {4, 16, 0, 0, 1, true, false, RelocTrans::LoMips}},
    {RelocKind::Data8, {1, 8, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::Data16, {2, 16, 0, 0, 1, true, false, RelocTrans::None}},
    // Format E LUI I12: imm12 @ parcel bits[32:43] (golden E2 e0 ALU0).
    // Distinct from WIDE_BranchSImm12 I12 @ bits[24:35].
    // NBytes=8: field at bit 32 needs image width > 48 (old NBytes=6 truncated
    // high bits of RI20/I20 when FieldLsb+FieldSize > 48).
    {RelocKind::HI12, {8, 12, 32, 0, 1, false, false, RelocTrans::Hi12}},
    // Format E ADDI32 RI20: imm20 @ parcel bits[31:50] (golden abs[50:31]).
    {RelocKind::LO20, {8, 20, 31, 0, 1, false, false, RelocTrans::Lo20}},
    {RelocKind::PC_LO20, {8, 20, 31, 0, 1, false, true, RelocTrans::Lo20}},
    // SET_HWLOOP Off1/Off2: explicit displacement <<2 → ValueShift=2, Align=4.
    {RelocKind::HWLoopOff1, {6, 6, 1, 2, 4, false, true, RelocTrans::None}},
    {RelocKind::HWLoopOff2, {6, 12, 7, 2, 4, false, true, RelocTrans::None}},
    // Format E I12 one-reg branch (BEQZ/BNEZ): imm12 @ parcel bits[32:43]
    // (ALU0 entry0 window; same I12 field position as HI12/RI12 below).
    // FieldLsb was stale 24 (historical) — LLD wrote imm into golden-reserved
    // bits[24:31], the decoder's c-reserved constraint failed, and objdump
    // soft-NOP'd the entry. tblgen BEQZ_E2_E0_ALU0_I12 imm_1 @ entry[37:26]
    // = parcel bits[32:43] is the authority.
    {RelocKind::WIDE_BranchSImm12,
     {6, 12, 32, 1, 2, true, true, RelocTrans::None}},
    // Format E RI12 two-reg branch (BEQ/BNE): imm12 @ parcel bits[32:43]
    // (was stale 28; same field-position bug as the one-reg row above).
    {RelocKind::WIDE_BranchSImm12_RI,
     {6, 12, 32, 1, 2, true, true, RelocTrans::None}},
    // Format E I20 call (JAL): imm20 @ parcel bits[31:50] (golden abs[50:31]).
    // Byte PC-relative; NBytes=8 so bits[48:50] are not truncated (NBytes=6
    // only images [47:0] — negative offsets became 0x1Fxxxx garbage).
    {RelocKind::WIDE_CallSImm20,
     {8, 20, 31, 0, 1, true, true, RelocTrans::None}},
    {RelocKind::C_BranchSImm4, {2, 4, 0, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::C_UImm4, {2, 4, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::C_BranchSImm10, {2, 10, 4, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::HWLoopOffset, {4, 16, 0, 2, 4, true, true, RelocTrans::None}},
    {RelocKind::LongBranchSImm20,
     {4, 20, 0, 1, 2, true, true, RelocTrans::None}},
    // LS scaled-imm fields (FI/spill offsets) — width scaling, not branch.
    {RelocKind::S0LSOff4_2, {4, 4, 4, 2, 4, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff4_3, {4, 4, 4, 3, 8, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff2_0, {4, 2, 4, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff3_0, {4, 3, 4, 0, 1, false, false, RelocTrans::None}},
    // Format E LOADSTORE0 RI6: imm6 after rt/rs under dense packing @ bits[33:28]
    // (FieldLsb=28). Element index on the wire (codegen already ÷ width); no
    // additional ValueShift here. Residual LoWord bits[13:8] retired.
    {RelocKind::LS_IMM, {6, 6, 28, 0, 1, true, false, RelocTrans::None}},
};

const Row &rowFor(RelocKind R) {
  for (const Row &RowEntry : Table)
    if (RowEntry.Kind == R)
      return RowEntry;
  return Table[0]; // None fallback
}

// Shared diagnostic for kinds whose value transform is not product-closed.
constexpr const char *kTransformNotReady =
    "relocation transform not ready (branch/call wire scale unresolved)";

} // namespace

const RelocFieldInfo &getRelocFieldInfo(RelocKind R) {
  return rowFor(R).Info;
}

bool isRelocTransformReady(RelocKind R) {
  return getRelocFieldInfo(R).Trans != RelocTrans::Unresolved;
}

uint64_t readImage(const uint8_t *Loc, unsigned NBytes) {
  switch (NBytes) {
  default:
    return 0;
  case 1:
    return Loc[0];
  case 2:
    return Loc[0] | (uint64_t(Loc[1]) << 8);
  case 4:
    return uint64_t(Loc[0]) | (uint64_t(Loc[1]) << 8) |
           (uint64_t(Loc[2]) << 16) | (uint64_t(Loc[3]) << 24);
  case 6:
    // bits[31:0] in bytes 0..3, bits[47:32] in bytes 4..5 (lld 6-byte
    // read32le + read16le-at-+4 convention).
    return readImage(Loc, 4) | (readImage(Loc + 4, 2) << 32);
  case 8:
    // Full low 64 bits of a Format E parcel (enough for imm20 @ [31:50]).
    return readImage(Loc, 4) | (readImage(Loc + 4, 4) << 32);
  }
}

void writeImage(uint8_t *Loc, unsigned NBytes, uint64_t Value) {
  switch (NBytes) {
  default:
    break;
  case 1:
    Loc[0] = uint8_t(Value);
    break;
  case 2:
    Loc[0] = uint8_t(Value);
    Loc[1] = uint8_t(Value >> 8);
    break;
  case 4:
    Loc[0] = uint8_t(Value);
    Loc[1] = uint8_t(Value >> 8);
    Loc[2] = uint8_t(Value >> 16);
    Loc[3] = uint8_t(Value >> 24);
    break;
  case 6:
    writeImage(Loc, 4, Value & 0xFFFFFFFFULL);
    writeImage(Loc + 4, 2, (Value >> 32) & 0xFFFFULL);
    break;
  case 8:
    writeImage(Loc, 4, Value & 0xFFFFFFFFULL);
    writeImage(Loc + 4, 4, (Value >> 32) & 0xFFFFFFFFULL);
    break;
  }
}

void patchField(uint8_t *Loc, uint64_t FieldVal, unsigned NBytes,
                unsigned FieldSize, unsigned FieldLsb) {
  uint64_t Image = readImage(Loc, NBytes);
  uint64_t Mask = (FieldSize >= 64) ? ~0ULL : ((1ULL << FieldSize) - 1);
  Image = (Image & ~(Mask << FieldLsb)) | ((FieldVal & Mask) << FieldLsb);
  writeImage(Loc, NBytes, Image);
}

uint64_t readField(const uint8_t *Loc, unsigned NBytes, unsigned FieldSize,
                   unsigned FieldLsb) {
  uint64_t Image = readImage(Loc, NBytes);
  uint64_t Mask = (FieldSize >= 64) ? ~0ULL : ((1ULL << FieldSize) - 1);
  return (Image >> FieldLsb) & Mask;
}

RelocCompute computeRelocValue(RelocKind R, uint64_t Value) {
  const RelocFieldInfo &I = getRelocFieldInfo(R);
  RelocCompute Out;
  int64_t Sv = static_cast<int64_t>(Value);

  // Fail closed before any alignment or scale application when the transform
  // is not product-ready. Prevents halfword ÷2 from remaining acceptance law.
  if (I.Trans == RelocTrans::Unresolved) {
    Out.Err = kTransformNotReady;
    return Out;
  }

  // Align first so misaligned targets never look like range failures.
  // Hwloop rows use Align=4 (word displacement); other product rows use Align
  // from RelocFieldInfo.
  if (I.Align > 1 && (Sv & static_cast<int64_t>(I.Align - 1))) {
    Out.Err = "mis-aligned relocation target";
    return Out;
  }

  switch (I.Trans) {
  case RelocTrans::Unresolved:
    // Handled above; keep switch exhaustive.
    Out.Err = kTransformNotReady;
    return Out;
  case RelocTrans::HiMips: {
    uint64_t Hi = (Value + 0x8000) >> 16;
    if (Hi > 0xFFFF) {
      Out.Err = "HI20 value does not fit in 16 bits";
      return Out;
    }
    Out.FieldVal = Hi & 0xFFFF;
    break;
  }
  case RelocTrans::LoMips: {
    uint64_t Hi = ((Value + 0x8000) >> 16) & 0xFFFF;
    int64_t Lo = Sv - static_cast<int64_t>(Hi << 16);
    if (!isInt<16>(Lo)) {
      Out.Err = "LO16 value does not fit in 16 bits";
      return Out;
    }
    Out.FieldVal = static_cast<uint64_t>(Lo) & 0xFFFF;
    break;
  }
  case RelocTrans::Hi12: {
    // HI12 applies only to LUI. Product Format E places imm12 at parcel
    // bits[32:43] (golden E2 e0; see Table FieldLsb). Use the row's
    // FieldSize for the range check so an out-of-reach address fails
    // loudly rather than being silently truncated by patchField.
    // MIPS-style +0x80000 rounding pairs with LO20's sign-extended
    // reconstruction.
    unsigned FieldBits = I.FieldSize > 0 ? I.FieldSize : 12;
    uint64_t Hi = (Value + 0x80000) >> 20;
    if (Hi > ((1ULL << FieldBits) - 1)) {
      Out.Err = "HI12 value does not fit in the LUI ext field";
      return Out;
    }
    Out.FieldVal = Hi & ((1ULL << FieldBits) - 1);
    break;
  }
  case RelocTrans::Lo20: {
    // MIPS-style low-20 paired with HI12 (the +0x80000 rounding carries
    // across the 12+20 split so ADDI32_W's sign-extension reconstructs Val).
    uint64_t Hi = ((Value + 0x80000) >> 20) & 0xFFF;
    int64_t Lo = Sv - static_cast<int64_t>(Hi << 20);
    if (!isInt<20>(Lo)) {
      Out.Err = "LO20 value does not fit in 20 bits";
      return Out;
    }
    Out.FieldVal = static_cast<uint64_t>(Lo) & 0xFFFFF;
    break;
  }
  case RelocTrans::None: {
    // Field bounds after ValueShift. Product-ready effective windows:
    //   HWLoopOff1:             unsigned 6-bit after ÷4 → [0, 252]
    //   HWLoopOff2:             unsigned 12-bit after ÷4 → [0, 16380]
    //   HWLoopOffset (legacy):  signed 16-bit after ÷4 → [-131072, +131068]
    // Product branch/call and hwloop rows use this path. Consumers must not
    // keep a second isInt/isUInt width table — FieldSize + ValueShift + IsSigned
    // here are the sole acceptance authority for product-ready kinds.
    int64_t Shifted = Sv >> I.ValueShift; // arithmetic (signed)
    if (I.IsSigned ? !isIntN(I.FieldSize, Shifted)
                   : !isUIntN(I.FieldSize, static_cast<uint64_t>(Shifted))) {
      Out.Err = "relocation offset out of range";
      return Out;
    }
    Out.FieldVal = static_cast<uint64_t>(Shifted);
    break;
  }
  }
  Out.OK = true;
  return Out;
}

RelocAddend tryReadRelocAddend(RelocKind R, const uint8_t *Loc) {
  const RelocFieldInfo &I = getRelocFieldInfo(R);
  RelocAddend Out;

  if (I.Trans == RelocTrans::Unresolved) {
    Out.Err = kTransformNotReady;
    return Out;
  }

  uint64_t Field = readField(Loc, I.NBytes, I.FieldSize, I.FieldLsb);

  switch (I.Trans) {
  case RelocTrans::Unresolved:
    Out.Err = kTransformNotReady;
    return Out;
  case RelocTrans::HiMips:
  case RelocTrans::Hi12:
    Out.Value = static_cast<int64_t>(Field); // unsigned high halves
    Out.OK = true;
    return Out;
  case RelocTrans::Lo20:
    Out.Value = SignExtend64<20>(Field); // signed low-20 (pairs with HI12)
    Out.OK = true;
    return Out;
  case RelocTrans::LoMips:
    Out.Value = SignExtend64<16>(Field);
    Out.OK = true;
    return Out;
  case RelocTrans::None:
    break;
  }

  uint64_t Shifted = Field << I.ValueShift;
  if (!I.IsSigned)
    Out.Value = static_cast<int64_t>(Shifted);
  else
    Out.Value = SignExtend64(Shifted, I.FieldSize + I.ValueShift);
  Out.OK = true;
  return Out;
}

int64_t readRelocAddend(RelocKind R, const uint8_t *Loc) {
  RelocAddend A = tryReadRelocAddend(R, Loc);
  // Fail closed: do not invent a halfword or byte undo for Unresolved kinds.
  // Callers that need diagnostics should use tryReadRelocAddend.
  if (!A.OK)
    return 0;
  return A.Value;
}

// Map an MC target fixup kind (FIXUP_HAYDN_*, from HaydnFixupKinds.h enum
// `Fixups` in namespace llvm::Haydn) to the neutral relocation. Generic
// FK_Data_* kinds return Invalid and are handled by the caller's generic path.
RelocKind mapFixupKind(unsigned MCFixupKind) {
  switch (MCFixupKind) {
  case Haydn::FIXUP_HAYDN_NONE:
    return RelocKind::None;
  case Haydn::FIXUP_HAYDN_32:
    return RelocKind::Data32;
  case Haydn::FIXUP_HAYDN_SImm16:
    return RelocKind::SImm16;
  case Haydn::FIXUP_HAYDN_BranchSImm16:
    return RelocKind::BranchSImm16;
  case Haydn::FIXUP_HAYDN_CallSImm20:
    return RelocKind::CallSImm20;
  case Haydn::FIXUP_HAYDN_HI20:
    return RelocKind::HI20;
  case Haydn::FIXUP_HAYDN_LO16:
    return RelocKind::LO16;
  case Haydn::FIXUP_HAYDN_GOT_HI20:
    return RelocKind::GOT_HI20;
  case Haydn::FIXUP_HAYDN_TPREL_HI20:
    return RelocKind::TPREL_HI20;
  case Haydn::FIXUP_HAYDN_TPREL_LO16:
    return RelocKind::TPREL_LO16;
  case Haydn::FIXUP_HAYDN_32_PCREL:
    return RelocKind::Data32PCRel;
  case Haydn::FIXUP_HAYDN_C_BranchSImm4:
    return RelocKind::C_BranchSImm4;
  case Haydn::FIXUP_HAYDN_C_UImm4:
    return RelocKind::C_UImm4;
  case Haydn::FIXUP_HAYDN_C_BranchSImm10:
    return RelocKind::C_BranchSImm10;
  case Haydn::FIXUP_HAYDN_HWLoopOffset:
    return RelocKind::HWLoopOffset;
  case Haydn::FIXUP_HAYDN_HWLoopOff1:
    return RelocKind::HWLoopOff1;
  case Haydn::FIXUP_HAYDN_HWLoopOff2:
    return RelocKind::HWLoopOff2;
  case Haydn::FIXUP_HAYDN_LongBranchSImm20:
    return RelocKind::LongBranchSImm20;
  case Haydn::FIXUP_HAYDN_HI12:
    return RelocKind::HI12;
  case Haydn::FIXUP_HAYDN_LO20:
    return RelocKind::LO20;
  case Haydn::FIXUP_HAYDN_PC_LO20:
    return RelocKind::PC_LO20;
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12:
    return RelocKind::WIDE_BranchSImm12;
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
    return RelocKind::WIDE_BranchSImm12_RI;
  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
    return RelocKind::WIDE_CallSImm20;
  case Haydn::FIXUP_HAYDN_S0LSOff4_2:
    return RelocKind::S0LSOff4_2;
  case Haydn::FIXUP_HAYDN_S0LSOff4_3:
    return RelocKind::S0LSOff4_3;
  case Haydn::FIXUP_HAYDN_S0LSOff2_0:
    return RelocKind::S0LSOff2_0;
  case Haydn::FIXUP_HAYDN_S0LSOff3_0:
    return RelocKind::S0LSOff3_0;
  case Haydn::FIXUP_HAYDN_LS_IMM:
    return RelocKind::LS_IMM;
  default:
    return RelocKind::Invalid;
  }
}

} // namespace HaydnReloc
} // namespace llvm
