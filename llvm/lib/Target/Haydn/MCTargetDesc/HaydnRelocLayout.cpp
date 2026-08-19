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
// E E2 e0 absolute parcel bits (r_offset = parcel origin). Scales follow the
// product RelocFieldInfo table (branch/call byte PC+imm; CallSImm20 byte;
// hwloop ÷4). GE96-03: B*/JAL field stores the byte displacement (no ÷2).
// RelocTrans::Unresolved remains for unpublished kinds and for unknown/
// Invalid kinds (rowFor is fail-closed). Both MC and lld delegate here.
//
//===----------------------------------------------------------------------===//

#include "HaydnRelocLayout.h"
#include "HaydnFixupKinds.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
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
//   WIDE_Call / I20 (JAL)    @ E2 e0 absolute parcel bits[31:50] → FieldLsb=31
//     E3 e0/e1 I20 positions differ (resolveFieldLsb): e0 [17:36], e1 [48:67].
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
// HWLoopOff1/Off2: Format E absolute parcel bits (r_offset = parcel origin).
// Table default is E2 e0 SET_HWLOOP_F2 (HWLRIIR); resolveFieldLsb covers
// E2 SET_HWLOOP (HWLRIII) and E3 e0/e1 F2 windows from golden layout.
constexpr Row Table[] = {
    {RelocKind::None, {0, 0, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::Data32, {4, 32, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::SImm16, {4, 16, 0, 0, 1, true, false, RelocTrans::None}},
    // Branch/call product scales (GE96-03: byte PC+imm; CallSImm20 byte).
    {RelocKind::BranchSImm16, {4, 16, 0, 0, 2, true, true, RelocTrans::None}},
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
    // RI20 is E2-only: e0 ALU0 @31 (table default), e1 ALU1 @ abs[84:65]
    // (resolveFieldLsb). NBytes=12 covers the e1 window past bit 63.
    {RelocKind::LO20, {12, 20, 31, 0, 1, false, false, RelocTrans::Lo20}},
    {RelocKind::PC_LO20, {12, 20, 31, 0, 1, false, true, RelocTrans::Lo20}},
    // SET_HWLOOP_F2 Off1/Off2 (Format E E2 e0 HWLRIIR golden absolute bits):
    //   uimm6  offset1 @ parcel bits[37:32] → FieldLsb=32
    //   uimm12 offset2 @ parcel bits[49:38] → FieldLsb=38
    // ValueShift=2 (byte offset ÷4), Align=4. NBytes=12 covers E3 e1 windows
    // past bit 63 via patchField bit-walk. resolveFieldLsb adjusts for
    // E2 HWLRIII and E3 e0/e1 placements.
    {RelocKind::HWLoopOff1, {12, 6, 32, 2, 4, false, true, RelocTrans::None}},
    {RelocKind::HWLoopOff2, {12, 12, 38, 2, 4, false, true, RelocTrans::None}},
    // Format E I12 one-reg branch (BEQZ/BNEZ): table FieldLsb is E2 e0
    // imm12 @ parcel bits[32:43]. E3 windows differ — resolveFieldLsb.
    // NBytes=12 covers E3 e1/e2 (imm past bit 48). GE96-03: ValueShift=0.
    {RelocKind::WIDE_BranchSImm12,
     {12, 12, 32, 0, 2, true, true, RelocTrans::None}},
    // Format E RI12 two-reg branch (BEQ/BNE): table FieldLsb is E2 e0
    // imm12 @ parcel bits[32:43] (same golden I12/RI12 position).
    {RelocKind::WIDE_BranchSImm12_RI,
     {12, 12, 32, 0, 2, true, true, RelocTrans::None}},
    // Format E I20 call (JAL): table FieldLsb=31 is E2 e0 (golden abs[50:31]).
    // E3 e0/e1 imm windows differ — resolveFieldLsb reads mode/entry at Loc.
    // Byte PC-relative; NBytes=12 covers E3 e1 imm @ bits[48:67].
    // Align=2 matches the branch path and MinBundleAddressAlignBytes (same
    // even-byte window the encoder already enforces for literal JAL).
    {RelocKind::WIDE_CallSImm20,
     {12, 20, 31, 0, 2, true, true, RelocTrans::None}},
    {RelocKind::C_BranchSImm4, {2, 4, 0, 0, 2, true, true, RelocTrans::None}},
    {RelocKind::C_UImm4, {2, 4, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::C_BranchSImm10, {2, 10, 4, 0, 2, true, true, RelocTrans::None}},
    {RelocKind::HWLoopOffset, {4, 16, 0, 2, 4, true, true, RelocTrans::None}},
    {RelocKind::LongBranchSImm20,
     {4, 20, 0, 0, 2, true, true, RelocTrans::None}},
    // LS scaled-imm fields (FI/spill offsets) — width scaling, not branch.
    {RelocKind::S0LSOff4_2, {4, 4, 4, 2, 4, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff4_3, {4, 4, 4, 3, 8, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff2_0, {4, 2, 4, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff3_0, {4, 3, 4, 0, 1, false, false, RelocTrans::None}},
    // Format E LOADSTORE0/LOAD1 RI6: imm6 after rt/rs under dense packing @
    // bits[33:28] (FieldLsb=28). Signed 6-bit, ValueShift=0. Distinct from
    // LO20 (ALU RI20 / retired WIDE LSOff20 @ bits[31:50]). Residual LoWord
    // bits[13:8] retired. NBytes=12 covers the non-e0 LOAD1 windows
    // (E2 e1 @72, E3 e0 @25, e1 @54, e2 @85) via resolveFieldLsb.
    {RelocKind::LS_IMM, {12, 6, 28, 0, 1, true, false, RelocTrans::None}},
    // Format E JALR RI12 imm12: same golden E2 e0 window as the RI12 branch
    // row (imm @ parcel bits[43:32], FieldLsb=32) but a distinct kind so a
    // JALR fixup never borrows the branch row (W27). Signed 12-bit byte
    // displacement from the parcel origin (ValueShift=0, Align=2 — GE96-03
    // no extra scale; execution semantics stay PC = rs + imm12). E3 e0/e1
    // windows resolve via resolveFieldLsb like the branch kinds.
    // ELF 22 (R_HAYDN_JALRSImm12) — M23 minted row, not MC-only.
    {RelocKind::JALRSImm12, {12, 12, 32, 0, 2, true, true, RelocTrans::None}},
};

static_assert(static_cast<unsigned>(RelocKind::WIDE_BranchSImm12_RI) ==
                  ELF::R_HAYDN_WIDE_BranchSImm12_RI,
              "shared RelocKind values must match ELF R_HAYDN_*");
static_assert(static_cast<unsigned>(RelocKind::LS_IMM) == ELF::R_HAYDN_LS_IMM,
              "LS_IMM RelocKind must match ELF R_HAYDN_LS_IMM");
static_assert(static_cast<unsigned>(RelocKind::JALRSImm12) ==
                  ELF::R_HAYDN_JALRSImm12,
              "JALRSImm12 RelocKind must match ELF R_HAYDN_JALRSImm12");

// Fail-closed sentinel: unknown / Invalid kinds are never product-ready.
// Returning Table[0] (None, Trans::None) used to make isRelocTransformReady
// succeed for RelocKind::Invalid and any out-of-table value.
constexpr Row kInvalidRow = {
    RelocKind::Invalid,
    {0, 0, 0, 0, 1, false, false, RelocTrans::Unresolved}};

const Row &rowFor(RelocKind R) {
  for (const Row &RowEntry : Table)
    if (RowEntry.Kind == R)
      return RowEntry;
  return kInvalidRow;
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
  case 12:
    // Low 64 only — callers that need bits[64:95] use bit-walk patch/read.
    return readImage(Loc, 8);
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
  case 12:
    // Preserve bytes[8:11]; only rewrite low 64 via the 8-byte path when the
    // field fits below bit 64. Fields past bit 63 use patchField bit-walk.
    writeImage(Loc, 8, Value);
    break;
  }
}

void patchField(uint8_t *Loc, uint64_t FieldVal, unsigned NBytes,
                unsigned FieldSize, unsigned FieldLsb) {
  // Bit-walk so Format E fields past bit 63 (E3 e1 I20 @ [48:67]) patch
  // correctly. uint64_t image math cannot represent FieldLsb+FieldSize > 64.
  if (FieldSize == 0 || NBytes == 0)
    return;
  uint64_t Mask = (FieldSize >= 64) ? ~0ULL : ((1ULL << FieldSize) - 1);
  FieldVal &= Mask;
  for (unsigned B = 0; B < FieldSize; ++B) {
    const unsigned Bit = FieldLsb + B;
    const unsigned ByteIdx = Bit / 8;
    if (ByteIdx >= NBytes)
      break;
    const uint8_t BitInByte = static_cast<uint8_t>(Bit % 8);
    const uint8_t M = static_cast<uint8_t>(1u << BitInByte);
    if ((FieldVal >> B) & 1ull)
      Loc[ByteIdx] = static_cast<uint8_t>(Loc[ByteIdx] | M);
    else
      Loc[ByteIdx] = static_cast<uint8_t>(Loc[ByteIdx] & ~M);
  }
}

uint64_t readField(const uint8_t *Loc, unsigned NBytes, unsigned FieldSize,
                   unsigned FieldLsb) {
  if (FieldSize == 0 || NBytes == 0)
    return 0;
  uint64_t Out = 0;
  const unsigned Cap = FieldSize >= 64 ? 64 : FieldSize;
  for (unsigned B = 0; B < Cap; ++B) {
    const unsigned Bit = FieldLsb + B;
    const unsigned ByteIdx = Bit / 8;
    if (ByteIdx >= NBytes)
      break;
    if ((Loc[ByteIdx] >> (Bit % 8)) & 1u)
      Out |= (1ull << B);
  }
  return Out;
}

unsigned resolveFieldLsb(RelocKind R, const uint8_t *Loc) {
  const RelocFieldInfo &I = getRelocFieldInfo(R);
  if (!Loc)
    return I.FieldLsb;

  auto GetBits = [&](unsigned Lo, unsigned Width) -> unsigned {
    unsigned V = 0;
    for (unsigned B = 0; B < Width; ++B) {
      const unsigned Bit = Lo + B;
      if ((Loc[Bit / 8] >> (Bit % 8)) & 1u)
        V |= (1u << B);
    }
    return V;
  };

  // Format E header: indicator bits[2:0]=7, entry_num bit[3] (0=E2, 1=E3).
  const unsigned Indicator = Loc[0] & 0x7u;
  const unsigned EntryNum = (Loc[0] >> 3) & 0x1u;

  // SET_HWLOOP_F2 / SET_HWLOOP Off1/Off2 — golden absolute parcel bits.
  // Table default is E2 e0 F2 (HWLRIIR): Off1@32, Off2@38.
  if (R == RelocKind::HWLoopOff1 || R == RelocKind::HWLoopOff2) {
    const bool IsOff1 = R == RelocKind::HWLoopOff1;
    if (Indicator != 0x7u)
      return I.FieldLsb;

    if (EntryNum == 0) {
      // E2 e0: type field at entry bits[6:2] → abs bits[12:8] (5b).
      // HWLRIIR (F2)=0x0c, HWLRIII (SET)=0x10 (generated Inst{} packing).
      const unsigned Type = GetBits(8, 5);
      if (Type == 0x10u)
        // SET_HWLOOP HWLRIII: Off1@bits[18:13], Off2@bits[47:36].
        return IsOff1 ? 13u : 36u;
      // SET_HWLOOP_F2 HWLRIIR (and unrecognized): table default.
      return IsOff1 ? 32u : 38u;
    }

    // E3: F2 only. map@entry+0 (2b)=2 (ALU0), type@entry+2 (4b)=0xc.
    // e0 @ abs[6:36] → Off1@18 Off2@24; e1 @ abs[37:67] → Off1@49 Off2@55.
    auto IsE3HwloopF2 = [&](unsigned EntryLo) -> bool {
      return GetBits(EntryLo, 2) == 2u && GetBits(EntryLo + 2, 4) == 0xcu;
    };
    if (IsE3HwloopF2(6))
      return IsOff1 ? 18u : 24u;
    if (IsE3HwloopF2(37))
      return IsOff1 ? 49u : 55u;
    return I.FieldLsb;
  }

  // HI12 / LUI I12. Table FieldLsb=32 is E2 e0 ALU0
  // (LUI_E2_E0_ALU0_I12: e0={c0:7, imm12, c1:8, rt:4, opc:3, c3:4, type:5=0xa,
  // map:2=0} → imm @ entry+26 → abs 32). The only other generated LUI
  // members are E3 e0:
  //   ALU2 I12 LUI_E3_E0_ALU2_I12: map=1, type=4, opc=1
  //     e0={c0:4, imm12, c1:4, rt:4, opc:1, type:4, map:2} → imm @ entry+15
  //     → abs 21 (golden I12 imm bit[32:21])
  //   ALU0 I12 LUI_E3_E0_ALU0_I12: map=2, type=0xa, opc=1
  //     e0={c0:2, imm12, rt:4, opc:3, c2:4, type:4, map:2} → imm @ entry+17
  //     → abs 23
  // Patching the E2 LSB on an E3 ALU2 LUI writes bit 32, which is only the
  // top bit of [21:32] — hi12=1 becomes executed imm 0x800 (plat_extras
  // freopen %hi12(stdout) / %hi12("w")).
  if (R == RelocKind::HI12) {
    if (Indicator != 0x7u)
      return I.FieldLsb;
    if (EntryNum == 0)
      return 32u; // E2 e0
    // E3 e0 @ abs [6:36].
    if (GetBits(6, 2) == 1u && GetBits(8, 4) == 4u && GetBits(12, 1) == 1u)
      return 21u;
    if (GetBits(6, 2) == 2u && GetBits(8, 4) == 0xau && GetBits(16, 3) == 1u)
      return 23u;
    return I.FieldLsb;
  }

  // LO20 / PC_LO20 — ALU RI20 (ADDI32/ORI32/…). RI20 is an E2-only type
  // (golden entry_num_0; E3 has no RI20 row). Table FieldLsb=31 is E2 e0
  // ALU0 (golden imm bit[50:31]). The e1 window is ALU1-only:
  //   E2 e1 ALU1 RI20: map[52:51]=0, 1-bit type @53=0, imm @ abs[84:65]
  // A symbolic ADDI32 placed at E2 e1 (e.g. `{ xor32; addi32 rt, rs, sym }`,
  // which packs as xor32@e0 ALU0 + addi32@e1 ALU1) previously patched 20
  // bits at LSB 31 — clobbering the e0 tail (rs/reg bits) and leaving the
  // real imm field zero (W25 / encoding F15 residual).
  // ALU1 I32 shares map=0 but has 2-bit opcode @ [55:54] and no imm20; the
  // full 1-bit type check @53 (=0) plus FieldSize=20 acceptance is the
  // golden discriminator (I32 imm sits at e1 [70:65] with opc≠RI20 range).
  if (R == RelocKind::LO20 || R == RelocKind::PC_LO20) {
    if (Indicator != 0x7u)
      return I.FieldLsb;
    if (EntryNum != 0u)
      return I.FieldLsb; // RI20 has no E3 member — fail to table default
    // Both E2 ALU units carry RI20 (map=0), so the entry is discriminated
    // by the type field: e0 ALU0 has a 5-bit type @8 (RI20=0x0f, golden
    // "01111"); e1 ALU1 has a 1-bit type @53 (RI20=0).
    // e0 ALU0 RI20: map[7:6]=0 + 5-bit type @8=0x0f → table window.
    if (GetBits(6, 2) == 0u && GetBits(8, 5) == 0x0fu)
      return I.FieldLsb; // E2 e0 ALU0 — table window
    // e1 ALU1 RI20: map[52:51]=0 + 1-bit type @53=0 → imm @65.
    if (GetBits(51, 2) == 0u && GetBits(53, 1) == 0u)
      return 65u;
    return I.FieldLsb;
  }

  // LS_IMM — LOADSTORE0/LOAD1 RI6 (S_LW_WITH_IMM et al.). Table FieldLsb=28
  // is E2 e0 LOADSTORE0 (golden imm bit[33:28]). Non-e0 windows (golden
  // absolute parcel bits; map values are LSB-first reads of the golden
  // "bit[hi:lo]" MSB notation, e.g. "10" reads as 2):
  //   E2 e1 LOAD1      map[52:51]=2, 2-bit type @53=1,     imm @ abs[77:72]
  //   E3 e0 LOADSTORE0 map[7:6]=3, 3-bit type @8=3,        imm @ abs[30:25]
  //   E3 e1 LOAD1      map[38:37]=3, 2-bit type @39=1,     imm @ abs[59:54]
  //   E3 e2 LOAD1      map[69:68]=3, 2-bit type @70=1,     imm @ abs[90:85]
  // ALU RI6 (shift-immediates SLLI64/…) shares the type name but has no
  // LS_IMM row (findFixupFromFixupFields RequireLSUnit) — the LOAD unit map
  // values below are exactly the LS units, so an ALU-unit RI6 site falls to
  // the table default (its producers never mint LS_IMM).
  if (R == RelocKind::LS_IMM) {
    if (Indicator != 0x7u)
      return I.FieldLsb;
    if (EntryNum == 0u) {
      // E2: e0 LOADSTORE0 map[7:6]=2 (golden "10") with 3-bit type @8=3;
      // e1 LOAD1 map[52:51]=2 (golden "10") with 2-bit type @53=1.
      if (GetBits(6, 2) == 2u && GetBits(8, 3) == 3u)
        return I.FieldLsb; // E2 e0 LOADSTORE0 — table window
      if (GetBits(51, 2) == 2u && GetBits(53, 2) == 1u)
        return 72u; // E2 e1 LOAD1
      return I.FieldLsb;
    }
    // E3: LOADSTORE0 @ e0 (map=3, 3b type=3), LOAD1 @ e1/e2 (map=3, 2b type=1).
    if (GetBits(6, 2) == 3u && GetBits(8, 3) == 3u)
      return 25u; // E3 e0 LOADSTORE0
    if (GetBits(37, 2) == 3u && GetBits(39, 2) == 1u)
      return 54u; // E3 e1 LOAD1
    if (GetBits(68, 2) == 3u && GetBits(70, 2) == 1u)
      return 85u; // E3 e2 LOAD1
    return I.FieldLsb;
  }

  const bool IsCall = R == RelocKind::WIDE_CallSImm20;
  const bool IsBrI12 = R == RelocKind::WIDE_BranchSImm12;
  const bool IsBrRI12 = R == RelocKind::WIDE_BranchSImm12_RI;
  const bool IsJalr = R == RelocKind::JALRSImm12;
  if (!IsCall && !IsBrI12 && !IsBrRI12 && !IsJalr)
    return I.FieldLsb;

  if (Indicator != 0x7u)
    return I.FieldLsb;

  // I12/RI12 cond-branch (and JALR — same golden RI12 type geometry):
  // table FieldLsb=32 is E2 e0 (golden abs[43:32]).
  // E3 generated Inst{} (MSB-first):
  //   e0/e1 31b I12  {pad2, imm12, reg4, opc3, pad4, type4=0xa, map2=2}
  //     map@entry+0, type@entry+2, imm@entry+17
  //     e0 @ [6:36] → abs 23; e1 @ [37:67] → abs 54
  //   e0/e1 31b RI12 {pad2, imm12, src4, rs4, opc3, type4=0xd, map2=2}
  //     same map/type/imm LSBs as I12 (type 0xd)
  //   e2 27b I12 {pad2, imm12, reg4, opc3, type4=0xa, map2=2}
  //     map@entry+0, type@entry+2, imm@entry+13 → abs 81
  if (IsBrI12 || IsBrRI12 || IsJalr) {
    if (EntryNum == 0)
      return 32u; // E2 e0
    auto IsE3BrI12 = [&](unsigned EntryLo) -> bool {
      return GetBits(EntryLo, 2) == 2u && GetBits(EntryLo + 2, 4) == 0xau;
    };
    auto IsE3BrRI12 = [&](unsigned EntryLo) -> bool {
      return GetBits(EntryLo, 2) == 2u && GetBits(EntryLo + 2, 4) == 0xdu;
    };
    if (IsE3BrI12(6) || IsE3BrRI12(6))
      return 23u;
    if (IsE3BrI12(37) || IsE3BrRI12(37))
      return 54u;
    if (IsE3BrI12(68) || IsE3BrRI12(68))
      return 81u;
    return I.FieldLsb;
  }

  if (EntryNum == 0)
    return 31u; // E2 e0 I20: Inst e0={imm20, c0, dest, …} → abs [31:50]

  // E3 I20 JAL: e0/e1 Inst = {imm20, dest4, opc1, type4, map2} (MSB-first).
  // map @ entry+0 (2b), type @ entry+2 (4b), opc @ entry+6 (1b), dest @
  // entry+7 (4b), imm @ entry+11 (20b). ALU0 UnitMap=2, TypeCode=14, Opc=1.
  auto IsE3JalI20 = [&](unsigned EntryLo) -> bool {
    return GetBits(EntryLo, 2) == 2u && GetBits(EntryLo + 2, 4) == 14u &&
           GetBits(EntryLo + 6, 1) == 1u;
  };

  // E3 e0 @ parcel bits[6:36] → imm abs [17:36]
  if (IsE3JalI20(6))
    return 17u;
  // E3 e1 @ parcel bits[37:67] → imm abs [48:67]
  if (IsE3JalI20(37))
    return 48u;

  // Unrecognized E3 call site: keep table default (E2 geometry) rather than
  // invent a third window.
  return I.FieldLsb;
}

RelocCompute computeRelocValue(RelocKind R, uint64_t Value) {
  const RelocFieldInfo &I = getRelocFieldInfo(R);
  RelocCompute Out;
  int64_t Sv = static_cast<int64_t>(Value);

  // Fail closed before any alignment or scale application when the transform
  // is not product-ready.
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

  const unsigned FieldLsb = resolveFieldLsb(R, Loc);
  uint64_t Field = readField(Loc, I.NBytes, I.FieldSize, FieldLsb);

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
  case Haydn::FIXUP_HAYDN_JALRSImm12:
    return RelocKind::JALRSImm12;
  default:
    return RelocKind::Invalid;
  }
}

unsigned mapRelocKindToFixup(RelocKind R) {
  switch (R) {
  case RelocKind::None:
    return Haydn::FIXUP_HAYDN_NONE;
  case RelocKind::Data32:
    return Haydn::FIXUP_HAYDN_32;
  case RelocKind::SImm16:
    return Haydn::FIXUP_HAYDN_SImm16;
  case RelocKind::BranchSImm16:
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  case RelocKind::CallSImm20:
    return Haydn::FIXUP_HAYDN_CallSImm20;
  case RelocKind::HI20:
    return Haydn::FIXUP_HAYDN_HI20;
  case RelocKind::LO16:
    return Haydn::FIXUP_HAYDN_LO16;
  case RelocKind::Data32PCRel:
    return Haydn::FIXUP_HAYDN_32_PCREL;
  case RelocKind::GOT_HI20:
    return Haydn::FIXUP_HAYDN_GOT_HI20;
  case RelocKind::TPREL_HI20:
    return Haydn::FIXUP_HAYDN_TPREL_HI20;
  case RelocKind::TPREL_LO16:
    return Haydn::FIXUP_HAYDN_TPREL_LO16;
  case RelocKind::Data8:
  case RelocKind::Data16:
    return Haydn::FIXUP_HAYDN_INVALID;
  case RelocKind::HI12:
    return Haydn::FIXUP_HAYDN_HI12;
  case RelocKind::LO20:
    return Haydn::FIXUP_HAYDN_LO20;
  case RelocKind::PC_LO20:
    return Haydn::FIXUP_HAYDN_PC_LO20;
  case RelocKind::HWLoopOff1:
    return Haydn::FIXUP_HAYDN_HWLoopOff1;
  case RelocKind::HWLoopOff2:
    return Haydn::FIXUP_HAYDN_HWLoopOff2;
  case RelocKind::WIDE_BranchSImm12:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  case RelocKind::WIDE_CallSImm20:
    return Haydn::FIXUP_HAYDN_WIDE_CallSImm20;
  case RelocKind::WIDE_BranchSImm12_RI:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case RelocKind::LS_IMM:
    return Haydn::FIXUP_HAYDN_LS_IMM;
  case RelocKind::JALRSImm12:
    return Haydn::FIXUP_HAYDN_JALRSImm12;
  case RelocKind::C_BranchSImm4:
    return Haydn::FIXUP_HAYDN_C_BranchSImm4;
  case RelocKind::C_UImm4:
    return Haydn::FIXUP_HAYDN_C_UImm4;
  case RelocKind::C_BranchSImm10:
    return Haydn::FIXUP_HAYDN_C_BranchSImm10;
  case RelocKind::HWLoopOffset:
    return Haydn::FIXUP_HAYDN_HWLoopOffset;
  case RelocKind::LongBranchSImm20:
    return Haydn::FIXUP_HAYDN_LongBranchSImm20;
  case RelocKind::S0LSOff4_2:
    return Haydn::FIXUP_HAYDN_S0LSOff4_2;
  case RelocKind::S0LSOff4_3:
    return Haydn::FIXUP_HAYDN_S0LSOff4_3;
  case RelocKind::S0LSOff2_0:
    return Haydn::FIXUP_HAYDN_S0LSOff2_0;
  case RelocKind::S0LSOff3_0:
    return Haydn::FIXUP_HAYDN_S0LSOff3_0;
  case RelocKind::Invalid:
    return Haydn::FIXUP_HAYDN_INVALID;
  }
  return Haydn::FIXUP_HAYDN_INVALID;
}

// Generated Format E type → published RelocKind. TypeOpcode ranges are the
// golden type-opcode column in FormatEMembers (not logical mnemonics).
// RI12 opcode 1 is JALR: dedicated JALRSImm12 row — never the RI12 branch
// row (W27); golden execution stays rs+imm12 (GE96-03).
struct TypeFixupSpec {
  const char *TypeName;
  uint16_t OpcodeLo;
  uint16_t OpcodeHi;
  RelocKind Kind;
  bool RequireLSUnit;
  unsigned RequireFieldSize; // 0 = any
};

constexpr TypeFixupSpec TypeFixupSpecs[] = {
    {"I12", 1, 1, RelocKind::HI12, false, 12},
    {"I12", 4, 7, RelocKind::WIDE_BranchSImm12, false, 12},
    {"RI12", 1, 1, RelocKind::JALRSImm12, false, 12},
    {"RI12", 2, 7, RelocKind::WIDE_BranchSImm12_RI, false, 12},
    {"I20", 1, 1, RelocKind::WIDE_CallSImm20, false, 20},
    {"RI20", 1, 7, RelocKind::LO20, false, 20},
    {"RI6", 0, 255, RelocKind::LS_IMM, true, 6},
    {"HWLRIIR", 0, 255, RelocKind::HWLoopOff1, false, 6},
    {"HWLRIIR", 0, 255, RelocKind::HWLoopOff2, false, 12},
    {"HWLRIII", 0, 255, RelocKind::HWLoopOff1, false, 6},
    {"HWLRIII", 0, 255, RelocKind::HWLoopOff2, false, 12},
};

RelocKind findFixupFromFixupFields(StringRef TypeName, unsigned TypeOpcode,
                                   ArrayRef<FixupField> Fields,
                                   unsigned FormatBytes, bool IsLSUnit) {
  if (TypeName.empty())
    return RelocKind::Invalid;

  unsigned FieldSize = 0;
  unsigned FieldLsb = kUnspecifiedFieldLsb;
  if (!Fields.empty()) {
    FieldSize = Fields[0].Size;
    FieldLsb = Fields[0].Offset;
  }
  // HWLoop Off1 default when the operand index is unknown (W37/W38 keep).
  if ((TypeName == "HWLRIIR" || TypeName == "HWLRIII") && FieldSize == 0)
    FieldSize = 6;

  RelocKind Hit = RelocKind::Invalid;
  unsigned Hits = 0;
  for (const TypeFixupSpec &S : TypeFixupSpecs) {
    if (TypeName != S.TypeName)
      continue;
    if (TypeOpcode < S.OpcodeLo || TypeOpcode > S.OpcodeHi)
      continue;
    if (S.RequireLSUnit && !IsLSUnit)
      continue;
    if (S.RequireFieldSize != 0 && FieldSize != 0 &&
        FieldSize != S.RequireFieldSize)
      continue;
    const RelocFieldInfo &I = getRelocFieldInfo(S.Kind);
    if (I.Trans == RelocTrans::Unresolved)
      continue;
    if (FieldLsb != kUnspecifiedFieldLsb && FieldLsb != I.FieldLsb)
      continue;
    if (FieldSize != 0 && FieldSize != I.FieldSize)
      continue;
    if (FormatBytes != 0 && I.NBytes > FormatBytes)
      continue;
    ++Hits;
    Hit = S.Kind;
  }
  if (Hits != 1)
    return RelocKind::Invalid;
  return Hit;
}

} // namespace HaydnReloc
} // namespace llvm
