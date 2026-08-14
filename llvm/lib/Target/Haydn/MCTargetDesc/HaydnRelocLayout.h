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

#include <cstdint>

namespace llvm::HaydnReloc {

// Neutral relocation kind. Shared members (None..LS_IMM 0..21) have the
// SAME numeric values as the ELF `R_HAYDN_*` (ELFRelocs/Haydn.def), so lld
// indexes the table directly via `static_cast<RelocKind>(rel.type)`. MC
// maps its `MCFixupKind` through `mapFixupKind`. MC-only fixups (no ELF
// reloc) are appended after the shared range.
enum class RelocKind : uint16_t {
  // Shared with ELF R_HAYDN_* (values 0..21 must match Haydn.def)
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
  // MC-only fixups (never become ELF relocs)
  C_BranchSImm4 = 22,
  C_UImm4 = 23,
  C_BranchSImm10 = 24,
  HWLoopOffset = 25, // legacy placeholder (WIDE path uses HWLoopOff1/2)
  LongBranchSImm20 = 26,
  // s0 LS scaled-imm fields (MC-only — FI spill offsets are local).
  S0LSOff4_2 = 27, // LD32/ST32 word offset (÷4)
  S0LSOff4_3 = 28, // LD64/ST64 doubleword offset (÷8)
  S0LSOff2_0 = 29, // LD16/LDU16/LD8/LDU8 (unscaled)
  S0LSOff3_0 = 30, // ST16/ST8 (unscaled)
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
  uint16_t NBytes;     // image width patched (1, 2, 4, or 6 bytes)
  uint8_t FieldSize;   // field bit width
  uint8_t FieldLsb;    // LSB position of the field within the N-byte LE image
  uint8_t ValueShift;  // input value pre-shift: 0 (byte), 2 (word/hwloop)
  uint8_t Align;       // required input alignment (1, 2, 4)
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
//   WIDE_BranchSImm12 / _RI — E3 I12/RI12 at e0 [23:34], e1 [54:65],
//     e2 I12 [81:92] (E2 e0 stays table FieldLsb=32)
//   HWLoopOff1/Off2 — E2 HWLRIII Off1/Off2 @ [13]/[36]; E3 F2 e0 @ [18]/[24],
//     e1 @ [49]/[55] (golden absolute parcel bits; table default is E2 F2
//     Off1@32 / Off2@38).
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
