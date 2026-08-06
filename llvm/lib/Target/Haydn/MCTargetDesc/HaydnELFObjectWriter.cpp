//===-- HaydnELFObjectWriter.cpp - Haydn ELF Writer ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class HaydnELFObjectWriter : public MCELFObjectTargetWriter {
public:
  HaydnELFObjectWriter(uint8_t OSABI = 0)
      : MCELFObjectTargetWriter(false, OSABI, ELF::EM_HAYDN, true){};
  ~HaydnELFObjectWriter() = default;

  unsigned getRelocType(const MCFixup &, const MCValue &,
                        bool IsPCRel) const override;
  bool needsRelocateWithSymbol(const MCValue &, unsigned Type) const override;
};

} // namespace

unsigned HaydnELFObjectWriter::getRelocType(const MCFixup &Fixup,
                                            const MCValue &Target,
                                            bool IsPCRel) const {
  // Determine the type of the relocation based on fixup kind
  MCFixupKind Kind = Fixup.getKind();

  // Handle generic fixup kinds (data relocations)
  if (Kind < FirstTargetFixupKind) {
    switch (Kind) {
    case FK_Data_4:
      return IsPCRel ? ELF::R_HAYDN_32_PCREL : ELF::R_HAYDN_32;
    case FK_Data_2:
      // 16-bit absolute DATA reloc (.short sym). /: previously mapped
      // to R_HAYDN_SImm16 -- an instruction-field reloc whose 4-byte
      // read/mask/write handler clobbered the bytes following a 2-byte data
      // value. The dedicated R_HAYDN_16 handler does a 2-byte write only.
      return ELF::R_HAYDN_16;
    case FK_Data_1:
      // 8-bit absolute DATA reloc (.byte sym). /: see FK_Data_2.
      return ELF::R_HAYDN_8;
    case FK_Data_8:
      // 64-bit data not supported for 32-bit architecture
      llvm_unreachable("64-bit data relocations not supported on 32-bit Haydn");
    default:
      llvm_unreachable("Invalid generic relocation kind");
    }
  }

  // Handle Haydn-specific fixup kinds
  switch (static_cast<unsigned>(Kind)) {
  default:
    // Prefer a normal diagnostic over llvm_unreachable so a future missing
    // mapping fails as "unsupported relocation" instead of aborting llvm-mc
    // (residual: was crash for WIDE_BranchSImm12_RI before it was wired).
    reportError(Fixup.getLoc(), "unsupported Haydn relocation type");
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_NONE:
    // No relocation needed
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_32:
    // 32-bit absolute relocation
    return ELF::R_HAYDN_32;

  case Haydn::FIXUP_HAYDN_SImm16:
    // 16-bit signed immediate relocation
    return ELF::R_HAYDN_SImm16;

  case Haydn::FIXUP_HAYDN_BranchSImm16:
    // PC-relative branch relocation (16-bit, word-aligned)
    return ELF::R_HAYDN_BranchSImm16;

  case Haydn::FIXUP_HAYDN_CallSImm20:
    // PC-relative call relocation (20-bit, halfword-aligned)
    return ELF::R_HAYDN_CallSImm20;

  case Haydn::FIXUP_HAYDN_HI20:
    // Upper 20 bits of address (for LUI)
    return ELF::R_HAYDN_HI20;

  case Haydn::FIXUP_HAYDN_LO16:
    // Lower 16 bits of address (for ADDI with symbolic)
    return ELF::R_HAYDN_LO16;

  case Haydn::FIXUP_HAYDN_GOT_HI20:
    // GOT entry high 20 bits
    return ELF::R_HAYDN_GOT_HI20;

  case Haydn::FIXUP_HAYDN_TPREL_HI20:
    // TLS TP-relative offset high 20 bits
    return ELF::R_HAYDN_TPREL_HI20;

  case Haydn::FIXUP_HAYDN_TPREL_LO16:
    // TLS TP-relative offset low 16 bits
    return ELF::R_HAYDN_TPREL_LO16;

  case Haydn::FIXUP_HAYDN_32_PCREL:
    // 32-bit PC-relative relocation
    return ELF::R_HAYDN_32_PCREL;

  case Haydn::FIXUP_HAYDN_HWLoopOffset:
    // Hardware-loop body start/end offset: 16-bit signed PC-relative field
    // word-aligned (encoded value = byteOffset >> 2). Same encoding semantics
    // as a PC-relative branch, so reuse the BranchSimm16 relocation. Emitted
    // by SET_HWLOOP_REG (two fixups: loop start + loop end). See.
    return ELF::R_HAYDN_BranchSImm16;

  case Haydn::FIXUP_HAYDN_HWLoopOff1:
    // BUG-2: WIDE SET_HWLOOP / SET_HWLOOP_F2 uimm6_offset1 at bits[31:26]
    // (encoding_manual.md §5.11/§5.12). 4-byte-unit PC-relative. Distinct
    // relocation so LLD writes the 6-bit field at the correct bit offset.
    return ELF::R_HAYDN_HWLoopOff1;

  case Haydn::FIXUP_HAYDN_HWLoopOff2:
    // BUG-2: WIDE SET_HWLOOP / SET_HWLOOP_F2 uimm12_offset2 at bits[25:14].
    return ELF::R_HAYDN_HWLoopOff2;

  case Haydn::FIXUP_HAYDN_HI12:
    // Wide-imm pair: LUI imm12 field (absolute)..
    return ELF::R_HAYDN_HI12;

  case Haydn::FIXUP_HAYDN_LO20:
    // Wide-imm pair: ADDI32_W/ORI32_W imm20 field (absolute)..
    return ELF::R_HAYDN_LO20;

  case Haydn::FIXUP_HAYDN_PC_LO20:
    // Wide-imm pair: ADDI32_W/ORI32_W imm20 field (PC-relative)..
    return ELF::R_HAYDN_PC_LO20;

  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12:
    // Bundle128 I12 one-reg cond (BEQZ/…): imm12 @ s0 bits[15:4], PC-rel ÷2.
    return ELF::R_HAYDN_WIDE_BranchSImm12;

  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
    // Bundle128 RI12 two-reg cond (BEQ/BNE/…): imm12 @ s0 bits[19:8]
    // PC-rel ÷2. Distinct from I12 so the linker does not overwrite rt/rs.
    return ELF::R_HAYDN_WIDE_BranchSImm12_RI;

  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
    // Bundle128 I20 call (JAL family): imm20 @ s0 bits[23:4], PC-rel.
    return ELF::R_HAYDN_WIDE_CallSImm20;
  }
}

bool HaydnELFObjectWriter::needsRelocateWithSymbol(const MCValue &,
                                                    unsigned Type) const {
  // M8-E2E: previously returned false unconditionally, which let the
  // MCObjectStreamer drop the symbol from relocations it considered
  // section-relative. For undefined extern symbols (no defining section)
  // this produced a relocation against symbol index 0 -- silently breaking
  // every cross-object function call (R_HAYDN_CallSImm20 against an extern
  // resolved to address 0 at link time). RISC-V takes the conservative
  // "return true" stance for the same reason; mirror it here so function
  // and data symbol references survive into the relocation's symbol field.
  // See ~/haydn-plans/lessons/m8-extern-call-null-reloc.md and
  // ~/haydn-plans/decisions/-m8-end-to-end-pipeline.md.
  return true;
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createHaydnELFObjectWriter() {
  return std::make_unique<HaydnELFObjectWriter>();
}
