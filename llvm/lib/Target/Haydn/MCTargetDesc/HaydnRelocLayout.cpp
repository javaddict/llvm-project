//===-- HaydnRelocLayout.cpp - Single-source reloc bit-layout ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the single-source Haydn relocation geometry table and the
// generic geometric patcher. See HaydnRelocLayout.h. Field positions transcribe
// encoding_manual.md (branch ÷2 §5.5/§5.14; hwloop ÷4 §5.11/§5.12; HI/LO LUI
// pairs §5 Class 000). Both MC and lld delegate here.
//
//===----------------------------------------------------------------------===//

#include "HaydnRelocLayout.h"
#include "HaydnFixupKinds.h"
#include "llvm/ADT/ArrayRef.h"
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

// Field LSB positions (within the N-byte LE image) per encoding_manual.md:
// ALL emission now routes through Bundle128 (16-byte). Fields live
// in the s0 slot window (bits[47:0] of the LoWord). Legacy 48-bit-parcel
// positions (HWLoopOff1@bits[31:26], Off2@bits[25:14]) were transcribed from
// the earlier geometry and are NEVER emitted. The.td HaydnFU_ALU32_S0_HWLOOP
// _W/_F2_W classes (`s0 = {FU, opcode, reserved, rs/cnt, offset2, offset1
// sel}`) place the hwloop fields at offset1=s0 bits[6:1] and offset2=s0
// bits[18:7]; the.td HaydnFU_LS_S0_*_RI6 classes place the LS imm6 field at
// s0 bits[13:8]. See (LO20 28->18, WIDE_Branch 36->4) for the same
// Bundle128 cutover treatment.
// 32-bit instr: imm16@bits[15:0], call20@bits[19:0]
// LUI (Bundle128 s0 ALU32 FLEX, 16-byte parcel): full imm12 at
// LoWord bits[15:4] (HaydnFU_ALU32_S0_I12)
// Bundle128 s0 window:
// HI12 @ bits[15:4] (LUI imm12 I12 — FieldSize 5→12)
// HWLoopOff1 @ bits[6:1] (was [31:26] — stale legacy parcel pos)
// HWLoopOff2 @ bits[18:7] (was [25:14] — stale legacy parcel pos)
// LS_IMM @ bits[13:8] (dedicated kind — was conflated with LO20)
// WIDE_Branch @ bits[15:4] (BEQZ imm12 I12_ONE)
// WIDE_Call @ bits[23:4] (JAL imm20 I20)
// LO20 @ bits[37:18] (ADDI32_W/ORI32_W imm20 RI20)
// 16-bit compressed: imm4@bits[3:0], imm10@bits[13:4]
constexpr Row Table[] = {
    {RelocKind::None, {0, 0, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::Data32, {4, 32, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::SImm16, {4, 16, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::BranchSImm16, {4, 16, 0, 1, 2, true, true, RelocTrans::None}},
    // Bundle128-only JAL_S0 (HaydnFU_ALU32_S0_I20) layout is
    // s0 = {FU, opcode, reserved, imm20, rt} with rt at s0 bits[3:0] and
    // imm20 at s0 bits[23:4] (LoWord). The prior FieldLsb=0 + ValueShift=1
    // was legacy-parcel geometry: it wrote the 20-bit PC-rel value OVER rt
    // (and treated the call offset as halfword-scaled). That produced the
    // direct-ELF crt0 canary `jal r0, 65535` after R_HAYDN_CallSImm20 patch
    // (rd clobbered to 0, imm saturated). Match WIDE_CallSImm20 field
    // position (FieldLsb=4) but ValueShift=0 — calltarget_s0 stores the
    // signed PC-relative BYTE offset with no ÷2 (encoding_manual).
    {RelocKind::CallSImm20, {4, 20, 4, 0, 1, true, true, RelocTrans::None}},
    {RelocKind::HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::LO16, {4, 16, 0, 0, 1, true, false, RelocTrans::LoMips}},
    {RelocKind::Data32PCRel, {4, 32, 0, 0, 1, true, true, RelocTrans::None}},
    {RelocKind::GOT_HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::TPREL_HI20, {4, 16, 0, 0, 1, false, false, RelocTrans::HiMips}},
    {RelocKind::TPREL_LO16, {4, 16, 0, 0, 1, true, false, RelocTrans::LoMips}},
    {RelocKind::Data8, {1, 8, 0, 0, 1, true, false, RelocTrans::None}},
    {RelocKind::Data16, {2, 16, 0, 0, 1, true, false, RelocTrans::None}},
    // LUI is always emitted as Bundle128 s0 ALU32 FLEX
    // (LUI_S0 / HaydnFU_ALU32_S0_I12: s0={FU,opcode,reserved,imm12,rt}).
    // imm12 lives at s0 bits[15:4] of the LoWord (FieldLsb=4, FieldSize=12).
    // The prior FieldSize=5 was a Mode-0 residual from LUI_M0S0ALU's uimm5
    // at bits[8:4]; it silently truncated HI12>31 (addresses ≳32MB). FieldLsb=4
    // remains correct (rt still occupies bits[3:0] — same as WIDE_Branch
    // imm12 geometry). NBytes=4 covers bits[15:0] of the LoWord.
    {RelocKind::HI12, {4, 12, 4, 0, 1, false, false, RelocTrans::Hi12}},
    // Bundle128 cutover geometry. Legacy 48-bit parcel layouts (imm20 at
    // bits[47:28], imm12 at bits[47:36]) are NEVER emitted (cutover routes
    // ALL emission through encodeBundleE). The Bundle128 s0 slot window
    // places fields at DIFFERENT bit positions within the 16-byte LoWord:
    // ADDI32_W_S0 (HaydnFU_ALU32_S0_RI20): imm20 at s0 bits[37:18]
    // > LoWord bits[37:18] -> FieldLsb=18.
    // BEQZ_S0 (HaydnFU_ALU32_S0_I12_ONE): imm12 at s0 bits[15:4]
    // > LoWord bits[15:4] -> FieldLsb=4.
    // JAL_S0 (HaydnFU_ALU32_S0_I20): imm20 at s0 bits[23:4]
    // > LoWord bits[23:4] -> FieldLsb=4.
    // The OLD FieldLsb=28/36 wrote into the opcode+FU bits (bits[38:47])
    // clobbering the discriminators and producing <unknown> on disassembly.
    {RelocKind::LO20, {6, 20, 18, 0, 1, false, false, RelocTrans::Lo20}},
    {RelocKind::PC_LO20, {6, 20, 18, 0, 1, false, true, RelocTrans::Lo20}},
    // RISK-6 (real root): the OLD FieldLsb values (26 for Off1
    // 14 for Off2) were transcribed from the LEGACY 48-bit parcel geometry
    // (HWLoopOff1@bits[31:26], Off2@bits[25:14]) and never updated when
    // routed all emission through Bundle128. The.td HaydnFU_ALU32_S0_HWLOOP
    // _W / _F2_W classes (`s0 = {FU, opcode, reserved, cnt/rs, offset2
    // offset1, sel}`) place offset1 at s0 bits[6:1] (FieldLsb=1) and offset2
    // at s0 bits[18:7] (FieldLsb=7). With the stale FieldLsb=26, the patcher
    // wrote the 6-bit offset1 field into bits[31:26] of the LoWord — i.e. the
    // opcode+FU discriminator bits — so `set_hwloop_f2` overflows the field
    // for any offset > 0 (the encoding silently corrupts the opcode and
    // disassembles as `<unknown>`). The agent's IsSigned=false fix alone
    // was incomplete: the field RANGE was correct but the field POSITION was
    // wrong. ValueShift=2 (÷4 per §5.11/5.12 + haydn_instruction_db.json) and
    // IsSigned=false (uimm6/uimm12 per the.td UImmAsmOperand class) are both
    // retained; only FieldLsb changes.
    {RelocKind::HWLoopOff1, {6, 6, 1, 2, 4, false, true, RelocTrans::None}},
    {RelocKind::HWLoopOff2, {6, 12, 7, 2, 4, false, true, RelocTrans::None}},
    // I12 WIDE zero-compare branches (BEQZ/BNEZ/…):
    // HaydnFU_ALU32_S0_I12_ONE packs s0={FU,opc,reserved22,imm12,rs}
    // → imm12 at s0 bits[15:4] → FieldLsb=4.
    {RelocKind::WIDE_BranchSImm12,
     {4, 12, 4, 1, 2, true, true, RelocTrans::None}},
    // RI12 WIDE two-reg cond branches (BEQ/BNE/BGE/…):
    // HaydnFU_ALU32_S0_RI12 packs s0={FU,opc,reserved18,imm12,rt,rs}
    // → imm12 at s0 bits[19:8] → FieldLsb=8. Using FieldLsb=4 here
    // overwrote rt/rs (bits[7:0]) and produced invalid branch targets
    // (BundleSim reject / `bne_w r1, r8, 2` from `bne_w r1, r2, L`).
    {RelocKind::WIDE_BranchSImm12_RI,
     {4, 12, 8, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::WIDE_CallSImm20,
     {6, 20, 4, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::C_BranchSImm4, {2, 4, 0, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::C_UImm4, {2, 4, 0, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::C_BranchSImm10, {2, 10, 4, 1, 2, true, true, RelocTrans::None}},
    {RelocKind::HWLoopOffset, {4, 16, 0, 2, 4, true, true, RelocTrans::None}},
    {RelocKind::LongBranchSImm20,
     {4, 20, 0, 1, 2, true, true, RelocTrans::None}},
    // reloc-aware slot-OR: s0 LS scaled-imm fields (FI/spill offsets).
    // The LS _M0 variant's Inst bakes the off field at s0 ext bits[7:4] of
    // the LoWord (FmtM0_S0_LS_IMM4 Inst{7-4}=off), so FieldLsb=4, NBytes=4
    // (the LoWord of the Mode-0 bundle). ValueShift = data-width scaling (÷4
    // for LD32/ST32 words). MC-only — FI spill offsets are resolved locally by
    // the AsmBackend.
    {RelocKind::S0LSOff4_2, {4, 4, 4, 2, 4, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff4_3, {4, 4, 4, 3, 8, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff2_0, {4, 2, 4, 0, 1, false, false, RelocTrans::None}},
    {RelocKind::S0LSOff3_0, {4, 3, 4, 0, 1, false, false, RelocTrans::None}},
    // RISK-5 (reloc-side): s0 LS D_LD/S_LD/D_ST/S_ST imm6 field at
    // Bundle128 LoWord bits[13:8] (per HaydnFU_LS_S0_*_RI6: `s0 = {FU, opcode
    // reserved[24], imm6, rtd/rt, rs}` — 3+7+24=34 high bits, imm6 next).
    // Signed 6-bit byte offset (no scaling — the `simm6` operand stores the
    // raw byte value). The encoder (HaydnMCCodeEmitter::getExprFixupKind)
    // currently routes LD32/ST32/LD64/ST64 to FIXUP_HAYDN_LO20, which writes
    // the 20-bit field at bits[37:18] — a SILENT miscompilation of LS
    // relocatable addresses (the 6 LS bits of the LO20 patch land partly in
    // the imm6 field, partly in the rs/rtd fields). MC-only until a7a4e481
    // follow-up wires the encoder to FIXUP_HAYDN_LS_IMM.
    {RelocKind::LS_IMM, {6, 6, 8, 0, 1, true, false, RelocTrans::None}},
};

const Row &rowFor(RelocKind R) {
  for (const Row &RowEntry : Table)
    if (RowEntry.Kind == R)
      return RowEntry;
  return Table[0]; // None fallback
}
} // namespace

namespace {
// The generated geometry, as a flat table. See HaydnRelocGeometry.inc.
struct RelocGeomRow {
  uint8_t FieldSize, EntryCount, EntryIndex, Mapping, BundleLsb;
};
#define HAYDN_RELOC_GEOM_ROW_LIST(...) __VA_ARGS__
#define HAYDN_RELOC_GEOM_ROW(FS, EC, EI, MP, LSB) {FS, EC, EI, MP, LSB},
constexpr RelocGeomRow RelocGeom[] = {
#include "HaydnRelocGeometry.inc"
};
#undef HAYDN_RELOC_GEOM_ROW
#undef HAYDN_RELOC_GEOM_ROW_LIST

// Entry byte base and entry LSB bit, per composite. From the generated
// composites: BUNDLE_E2 entries start at bundle bits 6 and 51, BUNDLE_E3 at
// 6, 37 and 68 — none of them byte-aligned, which is the whole problem.
struct EntryGeom { uint8_t ByteBase, LsbBit; };
constexpr EntryGeom TwoEntry[] = {{0, 6}, {6, 51}};
constexpr EntryGeom ThreeEntry[] = {{0, 6}, {4, 37}, {8, 68}};

uint64_t readBundleBits(const uint8_t *Base, unsigned Lsb, unsigned Width) {
  uint64_t V = 0;
  for (unsigned I = 0; I != 12; ++I)
    if (I < 8)
      V |= uint64_t(Base[I]) << (8 * I);
  // Only the low 64 bits are needed: every mapping field sits below bit 70,
  // and the two whose entry starts at bit 68 still have their mapping within
  // reach of a second read.
  if (Lsb + Width <= 64)
    return (V >> Lsb) & ((uint64_t(1) << Width) - 1);
  uint64_t Hi = 0;
  for (unsigned I = 8; I != 12; ++I)
    Hi |= uint64_t(Base[I]) << (8 * (I - 8));
  return (Hi >> (Lsb - 64)) & ((uint64_t(1) << Width) - 1);
}
} // namespace

bool relocFieldBundleLsb(unsigned FieldSize, const uint8_t *BundleBase,
                         unsigned BundleByte, unsigned &OutBundleLsb) {
  // Header bit 3 is the entry count (§ 3). Bits[2:0] must be the format
  // indicator; if they are not, this is not a format E bundle and guessing a
  // geometry for it would patch arbitrary bytes.
  const uint8_t Header = BundleBase[0];
  if ((Header & 0x7) != 0x7)
    return false;
  const bool IsThreeEntry = (Header >> 3) & 1;
  ArrayRef<EntryGeom> Entries = IsThreeEntry ? ArrayRef<EntryGeom>(ThreeEntry)
                                             : ArrayRef<EntryGeom>(TwoEntry);

  // The entry whose byte base is the greatest at or below this byte. A fixup
  // is anchored at its entry's base, never partway into it, so this is exact.
  unsigned Index = 0;
  for (unsigned I = 0, E = Entries.size(); I != E; ++I)
    if (BundleByte >= Entries[I].ByteBase)
      Index = I;

  const unsigned Mapping =
      (unsigned)readBundleBits(BundleBase, Entries[Index].LsbBit, 2);
  const unsigned EntryCount = IsThreeEntry ? 3 : 2;
  for (const RelocGeomRow &R : RelocGeom)
    if (R.FieldSize == FieldSize && R.EntryCount == EntryCount &&
        R.EntryIndex == Index && R.Mapping == Mapping) {
      OutBundleLsb = R.BundleLsb;
      return true;
    }
  return false;
}

bool isInstructionFieldReloc(RelocKind R) {
  switch (R) {
  case RelocKind::None:
  case RelocKind::Data8:
  case RelocKind::Data16:
  case RelocKind::Data32:
  case RelocKind::Data32PCRel:
    return false;
  default:
    return true;
  }
}

bool patchRelocFieldInBundle(uint8_t *BundleBase, unsigned BundleByte,
                             const RelocFieldInfo &FI, uint64_t FieldVal) {
  unsigned BundleLsb = 0;
  if (!relocFieldBundleLsb(FI.FieldSize, BundleBase, BundleByte, BundleLsb))
    return false;

  const unsigned ByteLo = BundleLsb / 8;
  const unsigned BitLo = BundleLsb % 8;
  // patchField's image reader only knows widths 1, 2, 4 and 6 and silently
  // does NOTHING for anything else, so round up to one it supports. A 3-byte
  // window is how this first went wrong: the P32 branch patched correctly and
  // the P30/P31 ones wrote nothing at all, with no diagnostic.
  unsigned NBytes = (BitLo + FI.FieldSize + 7) / 8;
  for (unsigned W : {1u, 2u, 4u, 6u, 8u})
    if (W >= NBytes) {
      NBytes = W;
      break;
    }
  if (ByteLo + NBytes > 12)
    return false;

  patchField(BundleBase + ByteLo, FieldVal, NBytes, FI.FieldSize, BitLo);
  return true;
}

const RelocFieldInfo &getRelocFieldInfo(RelocKind R) {
  return rowFor(R).Info;
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

  if (I.Align > 1 && (Sv & static_cast<int64_t>(I.Align - 1))) {
    Out.Err = "mis-aligned relocation target";
    return Out;
  }

  switch (I.Trans) {
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
    // HI12 applies only to LUI (Bundle128 s0 ALU32 FLEX / LUI_S0).
    // Full imm12 at LoWord bits[15:4] (FieldSize=12). Use the row's
    // actual FieldSize for the range check so an out-of-reach address fails
    // loudly rather than being silently truncated by patchField. The
    // MIPS-style +0x80000 rounding is preserved (pairs with LO20's
    // sign-extended reconstruction).
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

int64_t readRelocAddend(RelocKind R, const uint8_t *Loc) {
  const RelocFieldInfo &I = getRelocFieldInfo(R);
  uint64_t Field = readField(Loc, I.NBytes, I.FieldSize, I.FieldLsb);

  switch (I.Trans) {
  case RelocTrans::HiMips:
  case RelocTrans::Hi12:
    return static_cast<int64_t>(Field); // unsigned high halves
  case RelocTrans::Lo20:
    return SignExtend64<20>(Field); // signed low-20 (pairs with HI12 rounding)
  case RelocTrans::LoMips:
    return SignExtend64<16>(Field);
  case RelocTrans::None:
    break;
  }

  uint64_t Shifted = Field << I.ValueShift;
  if (!I.IsSigned)
    return static_cast<int64_t>(Shifted);
  return SignExtend64(Shifted, I.FieldSize + I.ValueShift);
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
