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

// Peer of AIEELFObjectWriter.cpp:49-51 (ELF::EM_AIE, no second machine id).
// Official ELF 259 is Kalray KVX. Stay on EM_HAYDN; distinguisher is
// EF_HAYDN_E96. Do not invent a replacement e_machine.
static_assert(ELF::EM_HAYDN == 259,
              "EM_HAYDN stays 259; do not invent a replacement (KVX collision)");

namespace {

class HaydnELFObjectWriter : public MCELFObjectTargetWriter {
public:
  // EM_HAYDN=259 is the in-tree experimental machine id. Official ELF
  // registry 259 is Kalray KVX — do not invent a replacement number here.
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
      // 64-bit data relocs are not a Haydn ELF kind (32-bit baremetal).
      // Diagnose; never llvm_unreachable — `.quad sym` is a user error.
      reportError(Fixup.getLoc(),
                  "64-bit data relocations not supported on 32-bit Haydn");
      return ELF::R_HAYDN_NONE;
    default:
      reportError(Fixup.getLoc(), "unsupported generic relocation kind");
      return ELF::R_HAYDN_NONE;
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
    // PC-relative branch relocation. Kind number is ABI-stable; value
    // transform is fail-closed in HaydnRelocLayout until wire scale lands.
    return ELF::R_HAYDN_BranchSImm16;

  case Haydn::FIXUP_HAYDN_CallSImm20:
    // PC-relative call relocation. Same fail-closed transform gate as branch.
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
    // Legacy 8-byte placeholder kind (MC-only; HaydnFixupKinds.h). No emitter
    // produces it: product SET_HWLOOP_* symbolic offsets use the typed
    // HWLoopOff1/Off2 kinds above. W37: this case previously aliased to
    // R_HAYDN_BranchSImm16, whose shared-layout row has ValueShift=0 while
    // HWLoopOffset has ValueShift=2 — an unresolved fixup of this kind would
    // link with the wrong scale (no <<2) and a wrong loop target. Fail
    // closed instead of aliasing to a differently-shifted row; if a producer
    // ever reappears it must mint a matching ELF reloc (FixupKinds + here +
    // RelocLayout row + lld) with ValueShift preserved, never borrow
    // BranchSImm16. Local (resolved) fixups never reach the writer — the
    // AsmBackend tag-compensation path owns those.
    reportError(Fixup.getLoc(),
                "legacy FIXUP_HAYDN_HWLoopOffset has no ELF relocation (no "
                "producer; refusing alias to differently-shifted "
                "R_HAYDN_BranchSImm16)");
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_HWLoopOff1:
    // Format E SET_HWLOOP_F2 uimm6_offset1 @ parcel bits[37:32] (E2 e0 F2).
    // 4-byte-unit PC-relative (ValueShift=2). Distinct from R_HAYDN_32 so LLD
    // patches only the offset field (Data32 would clobber the parcel).
    return ELF::R_HAYDN_HWLoopOff1;

  case Haydn::FIXUP_HAYDN_HWLoopOff2:
    // Format E SET_HWLOOP_F2 uimm12_offset2 @ parcel bits[49:38] (E2 e0 F2).
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
    // I12 one-reg cond (BEQZ_W/…): historical s0 imm12 @ bits[15:4].
    // ELF kind retained; layout transform is fail-closed until wire scale.
    return ELF::R_HAYDN_WIDE_BranchSImm12;

  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
    // RI12 two-reg cond (BEQ_W/BNE_W/…): historical s0 imm12 @ bits[19:8].
    // Distinct from I12 so the linker does not overwrite rt/rs. Transform
    // fail-closed with other branch kinds.
    return ELF::R_HAYDN_WIDE_BranchSImm12_RI;

  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
    // I20 call (JAL family): historical s0 imm20 @ bits[23:4]. Transform
    // fail-closed with other call kinds (no dual call-scale product law).
    return ELF::R_HAYDN_WIDE_CallSImm20;

  case Haydn::FIXUP_HAYDN_LS_IMM:
    // Format E LOADSTORE0/LOAD1 RI6: 1:1 to R_HAYDN_LS_IMM (AIE dense
    // fixup→ELF map: AIEELFObjectWriter.cpp:60-63). Never alias SImm16.
    return ELF::R_HAYDN_LS_IMM;

  case Haydn::FIXUP_HAYDN_JALRSImm12:
    // JALR RI12 symbolic imm12 is MC-only: no R_HAYDN_* kind is minted yet.
    // Local (same-section) targets resolve in the AsmBackend via the
    // JALRSImm12 RelocLayout row; a cross-object external target must fail
    // closed here rather than borrow a differently-identitied row (W37
    // HWLoopOffset precedent). Minting the ELF kind is an ABI decision
    // owned by the encoding topic.
    reportError(Fixup.getLoc(),
                "symbolic jalr to an external symbol has no Haydn ELF "
                "relocation (MC-only JALRSImm12); refusing alias to a "
                "branch/call row");
    return ELF::R_HAYDN_NONE;
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
