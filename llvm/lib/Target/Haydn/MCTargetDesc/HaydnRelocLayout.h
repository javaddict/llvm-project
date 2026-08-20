//===-- HaydnRelocLayout.h - Single-source reloc bit-layout ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ONE definition of every Haydn relocation's bit geometry + value transform.
// Consumed by BOTH the MC backend (HaydnAsmBackend::applyFixup
// getFixupKindInfo) and the linker (lld/ELF/Arch/Haydn.cpp ::relocate
// getImplicitAddend). Reader and writer share the same rows, so they can
// never diverge: a writer÷4 / reader÷2 scale split or a flat HI20/LO16
// 0xFFFF mask is structurally impossible.
//
// Branch/call PC-relative kinds use the product RelocFieldInfo table
// (byte PC+imm for branches and calls; hwloop displacement <<2). FieldLsb
// is Format E E2 e0 absolute parcel bits with r_offset = parcel origin;
// E3 I12/RI12/I20 windows are resolved from the parcel at Loc.
// GE96-03: no extra shift on B*/JAL. RelocTrans::Unresolved
// remains for kinds without a published wire scale, and for unknown/
// Invalid kinds (rowFor never falls open to None). Hwloop Off1/Off2 retain
// ValueShift=2 from the explicit SET_HWLOOP displacement law.
//
// Lives in namespace llvm::HaydnReloc (distinct from the lld arch handler
// `class Haydn` and from the target's llvm::Haydn register/fixup namespace, so
// all three can coexist in lld/ELF/Arch/Haydn.cpp without ambiguity).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNRELOCLAYOUT_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNRELOCLAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm::HaydnReloc {

// Neutral relocation kind. Shared members (None..CSR_UImm8 0..23) have the
// SAME numeric values as the ELF `R_HAYDN_*` (ELFRelocs/Haydn.def), so lld
// indexes the table directly via `static_cast<RelocKind>(rel.type)`. MC
// maps its `MCFixupKind` through `mapFixupKind`. MC-only fixups (no ELF
// reloc) are appended after the shared range.
enum class RelocKind : uint16_t {
  // Shared with ELF R_HAYDN_* (values 0..23 must match Haydn.def)
  None = 0,
  Data32 = 1,
  SImm16 = 2,
  BranchSImm16 = 3,
  CallSImm20 = 4,
  HI20 = 5,
  LO16 = 6,
  Data32PCRel = 7,
  GOT_HI20 = 8,
  TPREL_HI20 = 9,
  TPREL_LO16 = 10,
  Data8 = 11,
  Data16 = 12,
  HI12 = 13,
  LO20 = 14,
  PC_LO20 = 15,
  HWLoopOff1 = 16,
  HWLoopOff2 = 17,
  WIDE_BranchSImm12 = 18,
  WIDE_CallSImm20 = 19,
  // RI12 two-reg cond (BEQ_W/BNE_W/…): imm12 @ s0 bits[19:8].
  // I12 form keeps WIDE_BranchSImm12 (ELF 18) @ bits[15:4]. Promoted from
  // MC-only so unresolved external targets emit a real ELF reloc.
  WIDE_BranchSImm12_RI = 20,
  // Format E LOADSTORE0/LOAD1 RI6 signed imm6 @ parcel bits[33:28].
  // Distinct from LO20 (ALU RI20 / retired WIDE LSOff20 @ bits[31:50]).
  LS_IMM = 21,
  // Format E JALR RI12 symbolic imm12 (ELF 22). Same field numbers as
  // WIDE_BranchSImm12_RI but distinct identity. Unresolved external
  // targets emit R_HAYDN_JALRSImm12.
  JALRSImm12 = 22,
  // Format E CSR I8 uimm8 (CSRW/CSRR, ELF 23). Absolute unsigned CSR
  // address; table FieldLsb is E2 e0 @ bits[39:32]. E3 windows via
  // resolveFieldLsb. Reloc CSRW_W uses this kind so the encoder does not
  // emit an untyped NONE fixup. Unresolved externals emit R_HAYDN_CSR_UImm8.
  CSR_UImm8 = 23,
  // MC-only fixups (never become ELF relocs)
  C_BranchSImm4 = 24,
  C_UImm4 = 25,
  C_BranchSImm10 = 26,
  HWLoopOffset = 27, // legacy placeholder (WIDE path uses HWLoopOff1/2)
  LongBranchSImm20 = 28,
  // s0 LS scaled-imm fields (MC-only — FI spill offsets are local).
  S0LSOff4_2 = 29, // LD32/ST32 word offset (÷4)
  S0LSOff4_3 = 30, // LD64/ST64 doubleword offset (÷8)
  S0LSOff2_0 = 31, // LD16/LDU16/LD8/LDU8 (unscaled)
  S0LSOff3_0 = 32, // ST16/ST8 (unscaled)
  Invalid = 0xFFFF,
};

// Value transform applied before the field bits are selected. Mirrors the
// MIPS-style HI/LO split used by the LUI+ADDI32 materialization pairs.
enum class RelocTrans : uint8_t {
  None,   // field = (signed value >> ValueShift); range-checked, sign/zero-extended
  HiMips, // (value + 0x8000) >> 16, 16-bit unsigned high half (LUI/ADDI32 pair)
  LoMips, // value - (HiMips(value) << 16), 16-bit signed low half
  Hi12,   // (value + 0x80000) >> 20, range-checked to FieldSize (12 bits;
          // Format E LUI imm12 at parcel bits[36:47]; see Table).
  Lo20,   // value & 0xFFFFF, 20-bit low (ADDI32_W/ORI32_W imm20)
  // Reserved for kinds without a published wire scale (compute/read fail
  // closed). Product branch/call rows use RelocTrans::None + ValueShift.
  Unresolved,
};

// The complete bit-layout + semantics of one relocation. This struct is the
// single source of truth; both the MC writer/reader and the lld writer/reader
// consult it.
struct RelocFieldInfo {
  uint16_t NBytes;     // image width patched (1, 2, 4, 6, 8, or 12 bytes)
  uint8_t FieldSize;   // field bit width
  uint8_t FieldLsb;    // LSB position of the field within the N-byte LE image
  uint8_t ValueShift;  // input value pre-shift: 0 (byte), 2 (word/hwloop)
  uint8_t Align;       // required input alignment (1, 2, 4; WIDE call is 2)
  bool IsSigned;       // writer: isInt<FieldSize>; reader: sign-extend
  bool IsPCRel;        // informational (PC-relativity is resolved upstream)
  RelocTrans Trans;    // HI/LO transform, shifted-field, or Unresolved gate
};

// The geometry table. Indexed by RelocKind.
const RelocFieldInfo &getRelocFieldInfo(RelocKind R);

// True when computeRelocValue / readRelocAddend may apply a product transform.
// False only for RelocTrans::Unresolved rows.
bool isRelocTransformReady(RelocKind R);

// Map an MC target fixup kind (FIXUP_HAYDN_*) to the neutral relocation.
// Returns RelocKind::Invalid for non-target (generic FK_Data_*) kinds.
RelocKind mapFixupKind(unsigned MCFixupKind);

// Inverse of mapFixupKind. Returns FIXUP_HAYDN_INVALID when \p R has no
// MC target kind (Invalid / unmapped).
unsigned mapRelocKindToFixup(RelocKind R);

/// One relocatable window (AIE `FixupField` shape): LSB in the Format E
/// parcel plus bit width. Offset == kUnspecifiedFieldLsb means "size only".
inline constexpr unsigned kUnspecifiedFieldLsb = ~0u;
struct FixupField {
  unsigned Offset = kUnspecifiedFieldLsb;
  unsigned Size = 0;
};

/// AIE `findFixupfromFixupFields`: pick the unique published RelocKind whose
/// RelocFieldInfo matches generated Format E type geometry.
///
/// \p TypeName / \p TypeOpcode are Format E catalog fields (I12/RI12/I20/…),
/// not logical mnemonics. \p Fields[0].Size disambiguates HWLoop Off1 (6) vs
/// Off2 (12). \p IsLSUnit is required for RI6 (ALU RI6 has no LS_IMM row).
///
/// Returns Invalid when zero or >1 product-ready rows match. JALR is RI12
/// type-opcode 1 and maps to the dedicated JALRSImm12 row (never borrow
/// the RI12 branch row); execution stays PC = rs + imm12. I8 type-opcodes
/// 4/5 (CSRR/CSRW) map to CSR_UImm8; other I8 opcodes have no reloc row.
/// Fields[0].Offset, when set, must be a published window for that kind
/// (E2 e0 table FieldLsb or a typed E3/e1 member LSB) — AIE looks up by
/// the actual FixupField Offset (AIEMCFixupKinds.cpp:36-65); Haydn keeps
/// one ELF kind and accepts every published parcel-absolute LSB.
RelocKind findFixupFromFixupFields(StringRef TypeName, unsigned TypeOpcode,
                                   ArrayRef<FixupField> Fields,
                                   unsigned FormatBytes, bool IsLSUnit);

/// True when \p FieldLsb is the table default or a typed member window for
/// \p R (E3 e0/e1/e2 and E2 e1). Unknown LSB values are not published.
bool isPublishedFieldLsb(RelocKind R, unsigned FieldLsb);

/// FieldLsb from typed (mode, entry, unit) membership. Mode 0=E2, 1=E3.
/// Unit is the Format E unit index (ALU0=0, ALU1=1, ALU2=2, LOAD1=3,
/// LOADSTORE0=4); ~0u means unknown unit. Prefers an exact unit match,
/// then a unit-wildcard site, then the E2 e0 table default. Does not sniff
/// Loc bytes — that remains resolveFieldLsb for MC applyFixup / lld.
unsigned resolveFieldLsbForMember(RelocKind R, unsigned Mode, unsigned EntryIdx,
                                  unsigned Unit = ~0u);

// Read an N-byte little-endian image (N in {1,2,4,6}) as a uint64_t.
uint64_t readImage(const uint8_t *Loc, unsigned NBytes);
// Write an N-byte little-endian image (N in {1,2,4,6}).
void writeImage(uint8_t *Loc, unsigned NBytes, uint64_t Value);

// Geometric bit patch (AIE patchNBytes): clear then set FieldSize bits of
// FieldVal (already aligned to bit 0) at FieldLsb in the N-byte image.
// Supports FieldLsb+FieldSize beyond 64 (Format E E3 e1 I20 @ bits[48:67]).
void patchField(uint8_t *Loc, uint64_t FieldVal, unsigned NBytes,
                unsigned FieldSize, unsigned FieldLsb);

// Extract FieldSize bits at FieldLsb from the N-byte image (inverse of patchField).
uint64_t readField(const uint8_t *Loc, unsigned NBytes, unsigned FieldSize,
                   unsigned FieldLsb);

// Resolve FieldLsb for kinds whose absolute parcel bit position depends on the
// live Format E mode/entry at Loc. Table FieldLsb is E2 e0 authority:
//   WIDE_CallSImm20 — E3 JAL I20 at e0 [17:36] or e1 [48:67]
//   WIDE_BranchSImm12 / _RI / JALRSImm12 — E3 I12/RI12 at e0 [23:34],
//     e1 [54:65], e2 I12 [81:92] (E2 e0 stays table FieldLsb=32).
//     JALR generated members are E2 e0 / E3 e0 / E3 e1 ALU0 only.
//   HWLoopOff1/Off2 — E2 HWLRIII Off1/Off2 @ [13]/[36]; E3 F2 e0 @ [18]/[24],
//     e1 @ [49]/[55] (golden absolute parcel bits; table default is E2 F2
//     Off1@32 / Off2@38).
//   LO20/PC_LO20 — ALU RI20 (E2-only type): e0 ALU0 @31 (table default);
//     e1 ALU1 @65 (golden imm bit[84:65]).
//   LS_IMM — LS RI6: E2 e0 LOADSTORE0 @28 (table default); E2 e1 LOAD1 @72;
//     E3 e0 LOADSTORE0 @25; E3 e1 LOAD1 @54; E3 e2 LOAD1 @85.
//   CSR_UImm8 — I8 uimm8: E2 e0 @32 (table default); E3 e0 ALU2 @27 /
//     ALU0 @23; E3 e1 @54; E3 e2 @85.
// Returns the table default when Loc is not a recognizable Format E site.
unsigned resolveFieldLsb(RelocKind R, const uint8_t *Loc);

// Result of computing the field value from a relocation input.
struct RelocCompute {
  uint64_t FieldVal = 0;
  bool OK = false;
  const char *Err = nullptr; // diagnostic when !OK (alignment / range / gate)
};
// Apply Trans + ValueShift, range/alignment-check. Returns the field value to
// patch, or an error string. This is the sole range/scale authority for MC
// applyFixup and lld relocate / inBranchRange — consumers must not keep
// parallel isInt field-width tables. Unresolved kinds fail closed; product
// rows (branch/call byte PC+imm, hwloop ÷4, data) follow RelocFieldInfo.
RelocCompute computeRelocValue(RelocKind R, uint64_t Value);

// Result of reading an encoded addend (inverse of computeRelocValue).
struct RelocAddend {
  int64_t Value = 0;
  bool OK = false;
  const char *Err = nullptr;
};

// Read the encoded addend: extract field, undo ValueShift, sign/zero-extend
// per IsSigned. Fails closed for Unresolved kinds.
RelocAddend tryReadRelocAddend(RelocKind R, const uint8_t *Loc);

// Convenience wrapper: returns tryReadRelocAddend().Value when OK, else 0.
// Prefer tryReadRelocAddend when diagnostics matter.
int64_t readRelocAddend(RelocKind R, const uint8_t *Loc);

} // namespace llvm::HaydnReloc

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNRELOCLAYOUT_H
