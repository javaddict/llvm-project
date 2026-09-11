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
// No extra shift on B*/JAL. RelocTrans::Unresolved remains for kinds
// without a published wire scale, and for unknown/Invalid kinds (rowFor
// never falls open to None). Hwloop Off1/Off2 retain ValueShift=2 from
// the explicit SET_HWLOOP displacement law.
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
  // Format E JALR RI12 imm12 (ELF 22). Same field numbers as
  // WIDE_BranchSImm12_RI but distinct identity — do not remint. Symbolic
  // JALR is ISA-69 fail-closed (no golden relocation base); this number
  // is residual identity, not a qualified object ABI.
  JALRSImm12 = 22,
  // Format E CSR I8 uimm8 (CSRW/CSRR, ELF 23). Absolute unsigned CSR
  // address; table FieldLsb is E2 e0 @ bits[39:32]. E3 windows via
  // resolveFieldLsb. Reloc CSRW_W uses this kind so the encoder does not
  // emit an untyped NONE fixup. Unresolved externals emit R_HAYDN_CSR_UImm8.
  CSR_UImm8 = 23,
  // Entry-qualified kinds (shared with ELF R_HAYDN_* 24..42). Same value
  // transform/scale/size as the base kind; FieldLsb is the typed window
  // (never sniffed). Emitted when a symbolic member's committed entry is
  // not the base kind's default window. D1.17 twins: HI12/CSR_UImm8 E3
  // e0/e1/e2 — map/type alone is not member-unique on those sites, so the
  // producer emits the typed kind and the sniff stays an opc-pinned
  // fallback only.
  LO20_E1 = 24,
  PC_LO20_E1 = 25,
  WIDE_CallSImm20_E3E1 = 26,
  WIDE_BranchSImm12_E3E0 = 27,
  WIDE_BranchSImm12_E3E1 = 28,
  WIDE_BranchSImm12_E3E2 = 29,
  WIDE_BranchSImm12_RI_E3E0 = 30,
  WIDE_BranchSImm12_RI_E3E1 = 31,
  JALRSImm12_E3E0 = 32,
  JALRSImm12_E3E1 = 33,
  HI12_E3E0_ALU2 = 34,
  HI12_E3E0_ALU0 = 35,
  HI12_E3E1 = 36,
  HI12_E3E2_ALU2 = 37,
  HI12_E3E2_ALU0 = 38,
  CSR_UImm8_E3E0_ALU2 = 39,
  CSR_UImm8_E3E0_ALU0 = 40,
  CSR_UImm8_E3E1 = 41,
  CSR_UImm8_E3E2 = 42,
  // MC-only fixups (never become ELF relocs)
  C_BranchSImm4 = 43,
  C_UImm4 = 44,
  C_BranchSImm10 = 45,
  HWLoopOffset = 46, // legacy placeholder (WIDE path uses HWLoopOff1/2)
  LongBranchSImm20 = 47,
  // s0 LS scaled-imm fields (MC-only — FI spill offsets are local).
  S0LSOff4_2 = 48, // LD32/ST32 word offset (÷4)
  S0LSOff4_3 = 49, // LD64/ST64 doubleword offset (÷8)
  S0LSOff2_0 = 50, // LD16/LDU16/LD8/LDU8 (unscaled)
  S0LSOff3_0 = 51, // ST16/ST8 (unscaled)
  Invalid = 0xFFFF,
};

/// Base (entry-default) kind for an entry-qualified kind; identity for
/// base kinds themselves. Qualified kinds share the base transform/scale.
constexpr RelocKind baseKindFor(RelocKind R) {
  switch (R) {
  case RelocKind::LO20_E1:
    return RelocKind::LO20;
  case RelocKind::PC_LO20_E1:
    return RelocKind::PC_LO20;
  case RelocKind::WIDE_CallSImm20_E3E1:
    return RelocKind::WIDE_CallSImm20;
  case RelocKind::WIDE_BranchSImm12_E3E0:
  case RelocKind::WIDE_BranchSImm12_E3E1:
  case RelocKind::WIDE_BranchSImm12_E3E2:
    return RelocKind::WIDE_BranchSImm12;
  case RelocKind::WIDE_BranchSImm12_RI_E3E0:
  case RelocKind::WIDE_BranchSImm12_RI_E3E1:
    return RelocKind::WIDE_BranchSImm12_RI;
  case RelocKind::JALRSImm12_E3E0:
  case RelocKind::JALRSImm12_E3E1:
    return RelocKind::JALRSImm12;
  case RelocKind::HI12_E3E0_ALU2:
  case RelocKind::HI12_E3E0_ALU0:
  case RelocKind::HI12_E3E1:
  case RelocKind::HI12_E3E2_ALU2:
  case RelocKind::HI12_E3E2_ALU0:
    return RelocKind::HI12;
  case RelocKind::CSR_UImm8_E3E0_ALU2:
  case RelocKind::CSR_UImm8_E3E0_ALU0:
  case RelocKind::CSR_UImm8_E3E1:
  case RelocKind::CSR_UImm8_E3E2:
    return RelocKind::CSR_UImm8;
  default:
    return R;
  }
}

/// True when \p R is one of the entry-qualified kinds (24..42).
constexpr bool isEntryQualifiedKind(RelocKind R) {
  return R >= RelocKind::LO20_E1 && R <= RelocKind::CSR_UImm8_E3E2;
}

/// True for JALRSImm12 and its entry-qualified twins. ELF numbers stay
/// (do not remint). Symbolic use is ISA-69 fail-closed — not a published
/// relocation-base ABI and not a reason to return R_PC.
constexpr bool isSymbolicJalrReloc(RelocKind R) {
  return baseKindFor(R) == RelocKind::JALRSImm12;
}

/// Named diagnostic for assembly, object emission, and linking.
inline constexpr const char *kUnsupportedSymbolicJalrDiag =
    "Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base); "
    "refusing silent PC-relative R_HAYDN_JALRSImm12";

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
/// the RI12 branch row). Execution is golden rs+imm12; symbolic JALR is
/// ISA-69 fail-closed (this row is schema identity, not a relocation ABI).
/// I8 type-opcodes 4/5 (CSRR/CSRW) map to CSR_UImm8; other I8 opcodes
/// have no reloc row.
/// Unspecified I8 FieldSize defaults to 8 (AIE always supplies Size).
/// Fields[0].Offset, when set, must be a published window for that kind
/// (E2 e0 table FieldLsb or a typed E3/e1 member LSB) — AIE looks up by
/// the actual FixupField Offset (AIEMCFixupKinds.cpp:36-65); Haydn keeps
/// one ELF kind and accepts every published parcel-absolute LSB.
RelocKind findFixupFromFixupFields(StringRef TypeName, unsigned TypeOpcode,
                                   ArrayRef<FixupField> Fields,
                                   unsigned FormatBytes, bool IsLSUnit);

/// True when \p FieldLsb is the table default or a typed member window for
/// \p R (E3 e0/e1/e2 and E2 e1). Unknown LSB values are not published.
/// Typed windows are generated FieldLsbSites / ExtraPublishedLsb
/// (HaydnGenRelocFieldLsb.inc); Loc sniffing stays tryResolveFieldLsb.
bool isPublishedFieldLsb(RelocKind R, unsigned FieldLsb);

/// FieldLsb from typed (mode, entry, unit) membership. Mode 0=E2, 1=E3.
/// Unit is the Format E unit index (ALU0=0, ALU1=1, ALU2=2, LOAD1=3,
/// LOADSTORE0=4); ~0u means unknown unit. Prefers an exact unit match,
/// then a unit-wildcard site, then the E2 e0 table default. Does not sniff
/// Loc bytes — that remains tryResolveFieldLsb for MC applyFixup / lld.
unsigned resolveFieldLsbForMember(RelocKind R, unsigned Mode, unsigned EntryIdx,
                                  unsigned Unit = ~0u);

/// Entry-qualified RelocKind for base family \p R at generated
/// (Mode, EntryIdx, Unit). AIE peer: findFixupfromFixupFields looks up the
/// unique kind whose FixupField Offset matches the translated window
/// (AIEMCFixupKinds.cpp:36-65; AIEBaseMCCodeEmitter.cpp:189-232). Haydn
/// overlay: match the generated FieldLsbSites window against RelocFieldInfo
/// of the 24..42 twins (baseKindFor + unique FieldLsb). No new site struct.
/// Returns \p R at the base window; the unique twin when FieldLsb matches;
/// \p R itself when a non-HI12/CSR family has a non-default window with no
/// minted twin (WIDE_Call E3 e0 keeps the base kind + sniff); Invalid when
/// HI12/CSR has a non-default window with no twin, or when two twins share
/// one FieldLsb (uniqueness failure).
RelocKind qualifyRelocKindForMember(RelocKind R, unsigned Mode,
                                    unsigned EntryIdx, unsigned Unit = ~0u);

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
// live Format E mode/entry at Loc — the fallible successor of the retired
// infallible resolveFieldLsb (D1.42). Nullptr return == success and \p Lsb
// carries the window; a non-null return is a NAMED, kind-qualified error
// string the caller must report (MC applyFixup reportError / lld Err(ctx)).
//
// Authority:
//   HWLoopOff1/Off2 — generated HwLoopSniffSites ONLY (golden parcel map/
//     type windows + Off LSBs from HaydnGenRelocFieldLsb.inc). An unknown
//     hwloop site (wrong indicator, non-hwloop type at E2 e0, no F2 at E3,
//     an E3 e2 site) FAILS CLOSED with a named error naming the kind —
//     never a silent patch of the base-row E2 F2 window (32/38). The
//     header parse uses named constants (FormatEIndicatorBits /
//     FormatEEntryNumBit), not numeric masks.
//   Entry-qualified kinds (24..42) early-return their typed row — the
//     window rides the kind, never a sniff.
//   Remaining families (WIDE_Call/Branch, JALR, LO20/PC_LO20, LS_IMM,
//     HI12, CSR_UImm8) return their windows via resolveFieldLsbForMember
//     on the generated FieldLsbSites rows and keep their documented
//     opc/map/type pin predicates and fail-through to the table default
//     (INV6 ratchet: unit test SniffMatchesGeneratedSites).
//
// E2 e0 vs e1 discrimination is positional (the header encodes only mode,
// not entry index): the sniff alone is never proof of entry identity —
// producer-side placement stays the typed authority.
//
// A null \p Loc keeps returning the table row (no product caller passes
// null; noted for completeness).
const char *tryResolveFieldLsb(RelocKind R, const uint8_t *Loc, unsigned &Lsb);

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
