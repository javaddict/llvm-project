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

  // Branch offset (signed 16-bit, shifted left 2 for word alignment)
  FIXUP_HAYDN_BranchSImm16,

  // Call offset (signed 20-bit, J-type)
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

  // Compressed 16-bit branch offset (4-bit signed, shifted left 2)
  FIXUP_HAYDN_C_BranchSImm4,

  // Compressed 16-bit unsigned 4-bit immediate (ALUImm4 format)
  FIXUP_HAYDN_C_UImm4,

  // Slice 4 : §2.2 BRANCH pattern imm10 — 10-bit signed offset
  // shifted left 1 (2-byte units per §5.14 D1). Range +-512 words. Used by
  // C_J / C_JAL.
  FIXUP_HAYDN_C_BranchSImm10,

  // HWLoop body start/end offset (16-bit signed field, shifted left 2)
  FIXUP_HAYDN_HWLoopOffset,

  // WIDE SET_HWLOOP / SET_HWLOOP_F2 offset fields (encoding_manual.md §5.11
  // §5.12). (RISK-6): the actually-emitted def is the Bundle128 s0
  // variant SET_HWLOOP_F2_W_S0 (HaydnFormatsALU32.td), whose s0 slot
  // layout `{FU, opcode, reserved, rs, offset2, offset1, sel}` places:
  // offset1 (uimm6) at Bundle128 LoWord bits[6:1] (FieldLsb=1)
  // offset2 (uimm12) at Bundle128 LoWord bits[18:7] (FieldLsb=7)
  // The legacy Fmt48_WideSET_HWLOOP_F2 parcel positions (bits[31:26]
  // bits[25:14]) are NEVER emitted subsequent and were a stale transcription
  // that caused the real bug (off1 silently corrupted the rs/reserved
  // bits). Both use ÷4 (hwloop body start/end are 4-byte aligned per §5.14).
  // PC-relative base is the SET_HWLOOP_W parcel's own address.
  FIXUP_HAYDN_HWLoopOff1,
  FIXUP_HAYDN_HWLoopOff2,

  // Long-branch JAL offset (signed 20-bit, used by PseudoLongB* relaxation)
  FIXUP_HAYDN_LongBranchSImm20,

  // Wide-imm pair relocations (; Bundle128 cutover).
  // LUI is emitted as Bundle128 s0 ALU32 FLEX (LUI_S0
  // HaydnFU_ALU32_S0_I12); full imm12 lives at LoWord bits[15:4].
  // ADDI32_W(imm20) carries a 20-bit field (sign-extended for ADDI32_W
  // zero-extended for ORI32_W) in the Bundle128 s0 window at bits[37:18]
  // (FieldLsb cutover).
  // HI12 -- LUI imm12: (val + 0x80000) >> 20, written to bits[15:4] of
  // the LoWord (FieldSize=12 via HaydnRelocLayout; fixed
  // Mode-0 residual FieldSize=5).
  FIXUP_HAYDN_HI12,
  // LO20 -- ADDI32_W/ORI32_W imm20 field (absolute): val & 0xFFFFF, written
  // to Bundle128 LoWord bits[37:18] (FieldLsb=18).
  FIXUP_HAYDN_LO20,
  // PC_LO20 -- ADDI32_W/ORI32_W imm20 field (PC-relative): (val) & 0xFFFFF.
  FIXUP_HAYDN_PC_LO20,

  // WIDE 48-bit branch/call fixups (encoding_manual.md §5.5, ÷2 per §5.14 D1).
  // The 32-bit FIXUP_HAYDN_BranchSImm16 / FIXUP_HAYDN_CallSImm20 write the
  // bits[15:0] / bits[19:0] window of a 32-bit instruction word, which is
  // the WRONG field for the 48-bit WIDE BR class:
  // WIDE BEQ/BEQZ family: offset12 lives at bits[47:36] (TargetOffset=36).
  // WIDE JAL_W: offset20 lives at bits[47:28] (TargetOffset=28).
  // Both are PC-relative with ÷2 granularity. The dedicated kinds route the
  // byte-OR loop (via Info.TargetOffset) to the correct high-bit field, so
  // the rt/rs/opcode/spare bytes below are not clobbered.
  FIXUP_HAYDN_WIDE_BranchSImm12,
  // Bundle128 two-register WIDE cond branch (BEQ/BNE/… RI12).
  // Imm12 sits at s0 bits[19:8] (FieldLsb=8). Distinct from
  // FIXUP_HAYDN_WIDE_BranchSImm12 which patches the 1-reg I12 form
  // (BEQZ/BNEZ/…) at s0 bits[15:4] (FieldLsb=4). Sharing one
  // FieldLsb for both forms made RI12 fixups clobber rt/rs (e.g.
  // `bne_w r1, r2, L` → `bne_w r1, r8, 2`). MC-only until an ELF
  // reloc is allocated; local labels resolve at assemble time.
  FIXUP_HAYDN_WIDE_BranchSImm12_RI,
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

  // RISK-5 (reloc-side): s0 LS D_LD/S_LD/D_ST/S_ST imm6 field at
  // Bundle128 LoWord bits[13:8] (per HaydnFU_LS_S0_*_RI6 in HaydnFormatsLS.td:
  // `s0 = {FU, opcode, reserved[24], imm6, rtd/rt, rs}`). Signed 6-bit byte
  // offset (the `simm6` operand stores the raw value — no scaling). The
  // encoder (HaydnMCCodeEmitter::getExprFixupKind) currently routes
  // LD32/ST32/LD64/ST64 to FIXUP_HAYDN_LO20, which writes 20 bits at
  // bits[37:18] — that is the WRONG field for LS (the 6-bit imm6 lives at
  // bits[13:8], a different position from the ADDI32_W imm20). The result is
  // a silent miscompilation of LS relocatable addresses. MC-only
  // no ELF reloc; the encoder wiring (a7a4e481 / follow-up) maps LD32_S0
  // ST32_S0 / LD64_S0 / ST64_S0 to this kind instead of
  // LO20. Defined + mapped here so the encoder change is a one-line edit.
  FIXUP_HAYDN_LS_IMM,

  // Marker - must be last
  FIXUP_HAYDN_INVALID,

  NumTargetFixupKinds = FIXUP_HAYDN_INVALID - FirstTargetFixupKind
};
} // namespace llvm::Haydn

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNFIXUPKINDS_H
