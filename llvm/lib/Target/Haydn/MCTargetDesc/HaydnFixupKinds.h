//===-- HaydnFixupKinds.h - Haydn Specific Fixup Entries -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm::Haydn {
enum Fixups {
  // No fixup required
  FIXUP_HAYDN_NONE = FirstTargetFixupKind,

  // 32-bit absolute fixup
  FIXUP_HAYDN_32,

  // 16-bit signed immediate (I-type bits 15:0)
  FIXUP_HAYDN_SImm16,

  // Branch PC-relative field. Transform is not product-closed in
  // HaydnRelocLayout (fail closed until wire scale is published).
  FIXUP_HAYDN_BranchSImm16,

  // Call PC-relative field. Same fail-closed gate as branch kinds; dual
  // call-scale tables must not be product law.
  FIXUP_HAYDN_CallSImm20,

  // Upper 20 bits of address (for LUI/LOADI32 pair)
  FIXUP_HAYDN_HI20,

  // Lower 16 bits (second instruction of pair)
  FIXUP_HAYDN_LO16,

  // GOT entry high 20 bits
  FIXUP_HAYDN_GOT_HI20,

  // TLS offset high 20 bits
  FIXUP_HAYDN_TPREL_HI20,

  // TLS offset low 16 bits
  FIXUP_HAYDN_TPREL_LO16,

  // 32-bit PC-relative fixup
  FIXUP_HAYDN_32_PCREL,

  // Compressed branch: fail-closed with other branch/call kinds.
  FIXUP_HAYDN_C_BranchSImm4,

  // Compressed 16-bit unsigned 4-bit immediate (ALUImm4 format)
  FIXUP_HAYDN_C_UImm4,

  // Compressed branch: fail-closed with other branch/call kinds.
  FIXUP_HAYDN_C_BranchSImm10,

  // Legacy HWLoop placeholder: signed 16-bit field after ValueShift=2 (÷4).
  // Product SET_HWLOOP_W uses HWLoopOff1/Off2 below; this kind keeps the
  // tag-compensated applyFixup special case in the AsmBackend.
  FIXUP_HAYDN_HWLoopOffset,

  // SET_HWLOOP_W / SET_HWLOOP_F2_W offset fields (unsigned after ÷4).
  // Historical s0 layout `{FU, opcode, reserved, rs, offset2,
  // offset1, sel}`:
  //   offset1 (uimm6)  at LoWord bits[6:1]  (FieldLsb=1, Align=4)
  //   offset2 (uimm12) at LoWord bits[18:7] (FieldLsb=7, Align=4)
  // Effective byte windows: Off1 [0, 252], Off2 [0, 16380]. PC base is the
  // SET_HWLOOP parcel address. Geometry is sole-source in HaydnRelocLayout.
  FIXUP_HAYDN_HWLoopOff1,
  FIXUP_HAYDN_HWLoopOff2,

  // Long-branch JAL offset: fail-closed with other branch/call kinds.
  // MC-only; product transform regenerates after wire scale is published.
  FIXUP_HAYDN_LongBranchSImm20,

  // Wide-imm pair relocations.
  // LUI is emitted as s0 ALU32 FLEX (LUI_S0
  // HaydnFU_ALU32_S0_I12); full imm12 lives at LoWord bits[15:4].
  // ADDI32_W(imm20) carries a 20-bit field (sign-extended for ADDI32_W
  // zero-extended for ORI32_W) in the s0 window at bits[37:18]
  // (FieldLsb cutover).
  // HI12 -- LUI imm12: (val + 0x80000) >> 20, written to bits[15:4] of
  // the LoWord (FieldSize=12 via HaydnRelocLayout; fixed
  // Mode-0 residual FieldSize=5).
  FIXUP_HAYDN_HI12,
  // LO20 -- ADDI32_W/ORI32_W imm20 field (absolute): val & 0xFFFFF, written
  // to LoWord bits[37:18] (FieldLsb=18).
  FIXUP_HAYDN_LO20,
  // PC_LO20 -- ADDI32_W/ORI32_W imm20 field (PC-relative): (val) & 0xFFFFF.
  FIXUP_HAYDN_PC_LO20,

  // WIDE branch/call PC-rel fields. Geometry stubs may list historical s0
  // LoWord LSB positions; range/scale acceptance is fail-closed in
  // HaydnRelocLayout::computeRelocValue until wire scale is published.
  // I12 zero-compare form (BEQZ_W/BNEZ_W/…): imm12 at s0 bits[15:4]
  // (FieldLsb=4).
  FIXUP_HAYDN_WIDE_BranchSImm12,
  // RI12 two-reg cond form (BEQ_W/BNE_W/…): imm12 at s0 bits[19:8]
  // (FieldLsb=8). Distinct from the I12 kind above — sharing FieldLsb=4
  // clobbered rt/rs. Maps to ELF R_HAYDN_WIDE_BranchSImm12_RI.
  FIXUP_HAYDN_WIDE_BranchSImm12_RI,
  // WIDE call (JAL_W): imm20 at s0 bits[23:4] (FieldLsb=4). Same
  // fail-closed transform gate as other call kinds.
  FIXUP_HAYDN_WIDE_CallSImm20,

  // reloc-aware slot-OR: s0 LS scaled-imm fields (FI/spill offsets).
  // The LS _M0 variants (LD32_M0S0LS etc.) pack a scaled byte offset into the
  // s0 ext field. For a symbolic (frame-index/reloc) offset, the EncoderMethod
  // emits this fixup; the AsmBackend patches the field with ValueShift scaling.
  // S0LSOff4_2 — LD32/ST32: 4-bit field @ bits[7:4] of LoWord, ÷4 (<<2).
  // S0LSOff4_3 — LD64/ST64: 4-bit field @ bits[7:4] of LoWord, ÷8 (<<3).
  // S0LSOff2_0 — LD16/LDU16/LD8/LDU8: 2-bit field, unscaled.
  // S0LSOff3_0 — ST16/ST8: 3-bit field, unscaled.
  // MC-only (FI spill offsets are local — resolved by the AsmBackend).
  FIXUP_HAYDN_S0LSOff4_2,
  FIXUP_HAYDN_S0LSOff4_3,
  FIXUP_HAYDN_S0LSOff2_0,
  FIXUP_HAYDN_S0LSOff3_0,

  // s0 LS D_LD/S_LD/D_ST/S_ST imm6 field at LoWord bits[13:8]
  // (per HaydnFU_LS_S0_*_RI6 in HaydnFormatsLS.td:
  // `s0 = {FU, opcode, reserved[24], imm6, rtd/rt, rs}`). Signed 6-bit byte
  // offset (the `simm6` operand stores the raw value — no scaling). The
  // encoder (HaydnMCCodeEmitter::getExprFixupKind) currently routes
  // LD32/ST32/LD64/ST64 to FIXUP_HAYDN_LO20, which writes 20 bits at
  // bits[37:18] — that is the WRONG field for LS (the 6-bit imm6 lives at
  // bits[13:8], a different position from the ADDI32_W imm20). The result is
  // a silent miscompilation of LS relocatable addresses. MC-only
  // no ELF reloc; the encoder wiring maps LD32_S0 / ST32_S0 / LD64_S0 /
  // ST64_S0 to this kind instead of LO20. Defined + mapped here so the
  // encoder change is a one-line edit.
  FIXUP_HAYDN_LS_IMM,

  // Marker - must be last
  FIXUP_HAYDN_INVALID,

  NumTargetFixupKinds = FIXUP_HAYDN_INVALID - FirstTargetFixupKind
};
} // namespace llvm::Haydn

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H
