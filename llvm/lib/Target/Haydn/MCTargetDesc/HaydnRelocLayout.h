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
// never diverge (: the lld writer ÷4 vs reader ÷2 split, and the flat
// HI20/LO16 0xFFFF mask, are structurally impossible).
//
// The geometric patcher is ported from AIE/Peano (lld/ELF/Arch/AIE.cpp
// patchNBytes / RelocationPatch). Per `encoding_manual.md`: branch offsets are
// ÷2 (§5.5/§5.14), hwloop offsets are ÷4 (§5.11/§5.12).
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

// Neutral relocation kind. Shared members (None..WIDE_BranchSImm12_RI
// 0..20) have the SAME numeric values as the ELF `R_HAYDN_*`
// (ELFRelocs/Haydn.def), so lld indexes the table directly via
// `static_cast<RelocKind>(rel.type)`. MC maps its `MCFixupKind` through
// `mapFixupKind`. MC-only fixups (no ELF reloc) are appended after the
// shared range.
enum class RelocKind : uint16_t {
  // Shared with ELF R_HAYDN_* (values 0..20 must match Haydn.def)
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
  // Bundle128 RI12 two-reg cond (BEQ/BNE/…): imm12 @ s0 bits[19:8].
  // I12 form keeps WIDE_BranchSImm12 (ELF 18) @ bits[15:4]. : promoted
  // from MC-only so unresolved external targets emit a real ELF reloc.
  WIDE_BranchSImm12_RI = 20,
  // MC-only fixups (never become ELF relocs)
  C_BranchSImm4 = 21,
  C_UImm4 = 22,
  C_BranchSImm10 = 23,
  HWLoopOffset = 24, // legacy placeholder (WIDE path uses HWLoopOff1/2)
  LongBranchSImm20 = 25,
  // s0 LS scaled-imm fields (MC-only — FI spill offsets are local).
  S0LSOff4_2 = 26, // LD32/ST32 word offset (÷4)
  S0LSOff4_3 = 27, // LD64/ST64 doubleword offset (÷8)
  S0LSOff2_0 = 28, // LD16/LDU16/LD8/LDU8 (unscaled)
  S0LSOff3_0 = 29, // ST16/ST8 (unscaled)
  // RISK-5 (reloc-side): s0 LS D_LD/S_LD/D_ST/S_ST imm6 field at
  // bits[13:8] of the Bundle128 LoWord. Distinct from LO20 (ADDI32_W/ORI32_W
  // imm20 @ bits[37:18]); the encoder currently conflates both under
  // FIXUP_HAYDN_LO20 (silent miscompilation of LS relocatable addresses).
  // MC-only until the encoder wires LD32/ST32/LD64/ST64 to this kind.
  LS_IMM = 30,
  Invalid = 0xFFFF,
};

// Value transform applied before the field bits are selected. Mirrors the
// MIPS-style HI/LO split used by the LUI+ADDI32 materialization pairs.
enum class RelocTrans : uint8_t {
  None,   // field = (signed value >> ValueShift); range-checked, sign/zero-extended
  HiMips, // (value + 0x8000) >> 16, 16-bit unsigned high half (LUI/ADDI32 pair)
  LoMips, // value - (HiMips(value) << 16), 16-bit signed low half
  Hi12,   // (value + 0x80000) >> 20, range-checked to FieldSize (12 bits
          // LUI_S0 imm12 at LoWord bits[15:4]; see Table).
  Lo20,   // value & 0xFFFFF, 20-bit low (ADDI32_W/ORI32_W imm20, §5.2)
};

// The complete bit-layout + semantics of one relocation. This struct is the
// single source of truth; both the MC writer/reader and the lld writer/reader
// consult it.
struct RelocFieldInfo {
  uint16_t NBytes;     // image width patched (1, 2, 4, or 6 bytes)
  uint8_t FieldSize;   // field bit width
  uint8_t FieldLsb;    // LSB position of the field within the N-byte LE image
  uint8_t ValueShift;  // input value pre-shift: 0, 1 (÷2 branch), 2 (÷4 hwloop)
  uint8_t Align;       // required input alignment (1, 2, 4)
  bool IsSigned;       // writer: isInt<FieldSize>; reader: sign-extend
  bool IsPCRel;        // informational (PC-relativity is resolved upstream)
  RelocTrans Trans;    // HI/LO transform (None for shifted-field kinds)
};

// The geometry table. Indexed by RelocKind.
// Bundle-bit LSB of the relocatable immediate a fixup patches, derived from
// the bundle image. \returns false when the geometry cannot be resolved, in
// which case the caller MUST report an error rather than patch.
//
// A relocation names a BYTE, but format E entry windows do not start on byte
// boundaries and the field's position inside an entry depends on the
// placement, so neither `byte*8` nor a per-kind FieldLsb is right — the two
// errors are independent and both are silent. See FORMAT-E-SWITCH-PLAN.md
// § 5.8; getting it wrong mis-links quietly, and in the one loud case
// overwrites the bundle header.
//
// Everything the lookup needs is in the image, which is what lets MC and lld
// share one answer even though neither knows the member:
//   * FieldSize      — the relocation's own RelocFieldInfo
//   * entry count    — header bit 3
//   * entry index    — which entry \p BundleByte falls in
//   * mapping value  — read out of that entry, and what distinguishes e.g.
//                      LUI on ALU2 from a branch on ALU0 at the same entry
//
// \p BundleBase points at the first byte of the 12-byte bundle and
// \p BundleByte is the fixup's offset from it.
bool relocFieldBundleLsb(unsigned FieldSize, const uint8_t *BundleBase,
                         unsigned BundleByte, unsigned &OutBundleLsb);

// Patch a relocatable immediate into its format E bundle. \returns false when
// the geometry cannot be resolved or the window would leave the bundle; the
// caller MUST report an error rather than proceed.
//
// This is the whole operation, not a helper, so MC and lld share every step —
// the geometry lookup, the byte-window arithmetic, and the width rounding.
// Each of those has already produced a silent wrong answer once (§ 5.8), and a
// reader/writer split is exactly what this file exists to prevent.
//
// \p BundleBase points at the first byte of the 12-byte bundle; \p BundleByte
// is the relocation's offset from it.
// Whether \p R patches a field inside an INSTRUCTION, as opposed to a plain
// word in .data / .rodata.
//
// Only the instruction ones live inside a format E bundle and need the
// placement-dependent geometry; a data relocation patches its bytes directly
// and must not go anywhere near the bundle header. Getting this wrong is how
// the first attempt broke every data relocation in lld — it failed safely,
// because non-bundle bytes do not carry the 0b111 format indicator, but it
// should never have been asked.
bool isInstructionFieldReloc(RelocKind R);

bool patchRelocFieldInBundle(uint8_t *BundleBase, unsigned BundleByte,
                             const RelocFieldInfo &FI, uint64_t FieldVal);

const RelocFieldInfo &getRelocFieldInfo(RelocKind R);

// Map an MC target fixup kind (FIXUP_HAYDN_*) to the neutral relocation.
// Returns RelocKind::Invalid for non-target (generic FK_Data_*) kinds.
RelocKind mapFixupKind(unsigned MCFixupKind);

// Read an N-byte little-endian image (N in {1,2,4,6}) as a uint64_t.
uint64_t readImage(const uint8_t *Loc, unsigned NBytes);
// Write an N-byte little-endian image (N in {1,2,4,6}).
void writeImage(uint8_t *Loc, unsigned NBytes, uint64_t Value);

// Geometric bit patch (AIE patchNBytes): clear then set FieldSize bits of
// FieldVal (already aligned to bit 0) at FieldLsb in the N-byte image.
void patchField(uint8_t *Loc, uint64_t FieldVal, unsigned NBytes,
                unsigned FieldSize, unsigned FieldLsb);

// Extract FieldSize bits at FieldLsb from the N-byte image (inverse of patchField).
uint64_t readField(const uint8_t *Loc, unsigned NBytes, unsigned FieldSize,
                   unsigned FieldLsb);

// Result of computing the field value from a relocation input.
struct RelocCompute {
  uint64_t FieldVal = 0;
  bool OK = false;
  const char *Err = nullptr; // diagnostic when !OK (alignment / range)
};
// Apply Trans + ValueShift, range/alignment-check. Returns the field value to
// patch, or an error string.
RelocCompute computeRelocValue(RelocKind R, uint64_t Value);

// Read the encoded addend: extract field, undo ValueShift, sign/zero-extend
// per IsSigned. Matches computeRelocValue inversely (reader/writer symmetry).
int64_t readRelocAddend(RelocKind R, const uint8_t *Loc);

} // namespace llvm::HaydnReloc

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNRELOCLAYOUT_H
