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

  // Branch PC-relative 16-bit field (byte PC+imm, RelocTrans::None).
  FIXUP_HAYDN_BranchSImm16,

  // Call PC-relative field (byte PC+imm, RelocTrans::None). Dual call-scale
  // tables must not be product law.
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

  // Long-branch JAL offset: MC-only kind. Product JAL uses WIDE_CallSImm20
  // (byte PC+imm). This kind must not introduce a second call-scale law.
  FIXUP_HAYDN_LongBranchSImm20,

  // Wide-imm pair relocations. Product FieldLsb is Format E parcel bits
  // (HaydnRelocLayout); residual s0 LoWord windows are retired.
  // HI12 -- LUI I12 imm12 @ parcel bits[32:43] (FieldLsb=32, FieldSize=12):
  // (val + 0x80000) >> 20. Generated members are LUI_E2_E0_ALU0_I12 / E3
  // ALU0/ALU1/ALU2 I12 (occupancy suffixes retired).
  FIXUP_HAYDN_HI12,
  // LO20 -- ADDI32_W/ORI32_W RI20 field (absolute): val & 0xFFFFF @ parcel
  // bits[31:50] (FieldLsb=31). E3 windows via resolveFieldLsb.
  FIXUP_HAYDN_LO20,
  // PC_LO20 -- ADDI32_W/ORI32_W RI20 field (PC-relative): (val) & 0xFFFFF.
  FIXUP_HAYDN_PC_LO20,

  // WIDE branch/call PC-rel fields. Byte PC+imm (ValueShift=0,
  // RelocTrans::None) in HaydnRelocLayout. Table FieldLsb is E2 e0; E3
  // windows via resolveFieldLsb.
  // I12 zero-compare form (BEQZ_W/BNEZ_W/…): imm12 @ parcel bits[32:43]
  // (FieldLsb=32).
  FIXUP_HAYDN_WIDE_BranchSImm12,
  // RI12 two-reg cond form (BEQ_W/BNE_W/…): imm12 @ parcel bits[32:43]
  // (FieldLsb=32). Distinct kind from I12 so a shared ELF row cannot
  // clobber rt/rs. Maps to ELF R_HAYDN_WIDE_BranchSImm12_RI.
  FIXUP_HAYDN_WIDE_BranchSImm12_RI,
  // WIDE call (JAL_W): I20 @ E2 e0 parcel bits[31:50] (FieldLsb=31). Byte
  // PC+imm, Align=2 — same even-byte law as WIDE branch.
  FIXUP_HAYDN_WIDE_CallSImm20,

  // MC-only FI/spill scaled-imm fields (reloc kind names kept). EncoderMethod
  // emits the fixup; AsmBackend patches with ValueShift scaling.
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
  // generated LS RI6 members here. Maps 1:1 to ELF R_HAYDN_LS_IMM — never
  // R_HAYDN_SImm16.
  FIXUP_HAYDN_LS_IMM,

  // Format E JALR RI12 symbolic imm12 (HaydnRelocLayout JALRSImm12): signed
  // 12-bit byte displacement from the parcel origin — E2 e0 @ parcel
  // bits[43:32] (FieldLsb=32), E3 e0/e1 via resolveFieldLsb, ValueShift=0,
  // Align=2. Execution is PC = rs + imm12; the kind types the assembler
  // symbol convention imm = target - parcel.
  // Distinct identity from FIXUP_HAYDN_WIDE_BranchSImm12_RI (same field
  // numbers) so a JALR fixup can never borrow the branch row.
  // Unresolved externals emit ELF R_HAYDN_JALRSImm12 (ELF 22). Call-indirect
  // and JT dispatch pass a literal 0 and do not emit this fixup.
  FIXUP_HAYDN_JALRSImm12,

  // Format E CSR I8 uimm8 (HaydnRelocLayout CSR_UImm8): unsigned 8-bit
  // CSR address — E2 e0 @ parcel bits[39:32] (FieldLsb=32), E3 windows
  // via resolveFieldLsb, ValueShift=0, Align=1, not PC-relative.
  // Reloc CSRW_W / CSRR I8 members use this kind so encode never emits
  // an untyped NONE fixup. Unresolved externals emit ELF R_HAYDN_CSR_UImm8
  // (ELF 23). Do not borrow R_HAYDN_8 / Data32 (those are data-section
  // 1-byte writes, not a parcel field).
  FIXUP_HAYDN_CSR_UImm8,

  // Marker - must be last
  FIXUP_HAYDN_INVALID,

  NumTargetFixupKinds = FIXUP_HAYDN_INVALID - FirstTargetFixupKind
};
} // namespace llvm::Haydn

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H
