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

  // SET_HWLOOP / SET_HWLOOP_F2 offset fields (unsigned after ÷4).
  // Format E product geometry (r_offset = parcel origin; HaydnRelocLayout):
  //   Off1 (uimm6)  @ E2 e0 F2 absolute parcel bits[37:32] (FieldLsb=32)
  //   Off2 (uimm12) @ E2 e0 F2 absolute parcel bits[49:38] (FieldLsb=38)
  // ValueShift=2, Align=4. Effective byte windows: Off1 [0, 252],
  // Off2 [0, 16380]. PC base is the SET_HWLOOP parcel address. resolveFieldLsb
  // covers E2 HWLRIII and E3 e0/e1 F2 windows.
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

  // Format E LOADSTORE0/LOAD1 RI6 signed imm6 @ parcel bits[33:28]
  // (HaydnRelocLayout LS_IMM). Distinct from FIXUP_HAYDN_LO20 (ALU RI20 /
  // retired WIDE LSOff20 @ bits[31:50]). Encoder getExprFixupKind routes
  // generated LS RI6 members (and peeled LD32/ST32/LD64/ST64 → S_LW_WITH_IMM
  // etc.) here. Maps 1:1 to ELF R_HAYDN_LS_IMM — never R_HAYDN_SImm16.
  FIXUP_HAYDN_LS_IMM,

  // Marker - must be last
  FIXUP_HAYDN_INVALID,

  NumTargetFixupKinds = FIXUP_HAYDN_INVALID - FirstTargetFixupKind
};
} // namespace llvm::Haydn

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H
