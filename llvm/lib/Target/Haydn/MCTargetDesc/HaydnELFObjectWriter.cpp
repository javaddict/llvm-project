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

// Peer of AIEELFObjectWriter.cpp:49-51 (ELF::EM_AIE, no second machine id)
// and AIE ELF.h:498-502 (public EF_AIE_*). Official ELF 259 is Kalray KVX.
// Stay on EM_HAYDN; distinguisher is ELF::EF_HAYDN_E96. Do not invent a
// replacement e_machine or extra e_flags image-version bits.
static_assert(ELF::EM_HAYDN == 259,
              "EM_HAYDN stays 259; do not invent a replacement (KVX collision)");
static_assert(ELF::EF_HAYDN_E96 == 0x1u,
              "EF_HAYDN_E96 stays 0x1; do not invent e_flags image-versioning");

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

  // Handle generic fixup kinds (data relocations).
  // PIC/JT EK_LabelDifference32 is `.long LBB - JT` (AsmPrinter). ELFObjectWriter
  // folds the subtract into IsPCRel + addend (ELFObjectWriter.cpp:1330-1356).
  // Peer: RISCVELFObjectWriter.cpp:77-83 FK_Data_4+IsPCRel → R_RISCV_32_PCREL;
  // HexagonELFObjectWriter.cpp:84 same for R_HEX_32_PCREL. Haydn emits
  // R_HAYDN_32_PCREL; never R_HAYDN_32 (absolute would be LBB, not LBB-JT).
  // This tree has no FK_PCRel_* kinds (PCRel is MCFixup::PCRel).
  if (Kind < FirstTargetFixupKind) {
    switch (Kind) {
    case FK_Data_4:
      return IsPCRel ? ELF::R_HAYDN_32_PCREL : ELF::R_HAYDN_32;
    case FK_Data_2:
      // 16-bit absolute DATA reloc (.short sym). Previously mapped to
      // R_HAYDN_SImm16 -- an instruction-field reloc whose 4-byte
      // read/mask/write handler clobbered the bytes following a 2-byte data
      // value. The dedicated R_HAYDN_16 handler does a 2-byte write only.
      // Peer: RISCVELFObjectWriter.cpp:77-83 IsPCRel only on FK_Data_4
      // (R_RISCV_32_PCREL); there is no R_HAYDN_16_PCREL — refuse rather
      // than silently emit absolute R_HAYDN_16 for a label difference.
      if (IsPCRel) {
        reportError(Fixup.getLoc(),
                    "16-bit PC-relative data relocations are not supported "
                    "on Haydn");
        return ELF::R_HAYDN_NONE;
      }
      return ELF::R_HAYDN_16;
    case FK_Data_1:
      // 8-bit absolute DATA reloc (.byte sym). See FK_Data_2.
      if (IsPCRel) {
        reportError(Fixup.getLoc(),
                    "8-bit PC-relative data relocations are not supported "
                    "on Haydn");
        return ELF::R_HAYDN_NONE;
      }
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
    // Same FK_Data_4 law: IsPCRel selects R_HAYDN_32_PCREL, never silent
    // absolute R_HAYDN_32 for a PC-relative 32-bit data word.
    return IsPCRel ? ELF::R_HAYDN_32_PCREL : ELF::R_HAYDN_32;

  case Haydn::FIXUP_HAYDN_SImm16:
    // 16-bit signed immediate relocation
    return ELF::R_HAYDN_SImm16;

  case Haydn::FIXUP_HAYDN_BranchSImm16:
    // PC-relative branch relocation. Byte PC+imm (ValueShift=0,
    // RelocTrans::None) in HaydnRelocLayout.
    return ELF::R_HAYDN_BranchSImm16;

  case Haydn::FIXUP_HAYDN_CallSImm20:
    // PC-relative call relocation. Same byte PC+imm law as branch.
    return ELF::R_HAYDN_CallSImm20;

  case Haydn::FIXUP_HAYDN_HI20:
    // Upper 20 bits of address (for LUI)
    return ELF::R_HAYDN_HI20;

  case Haydn::FIXUP_HAYDN_LO16:
    // Lower 16 bits of address (for ADDI with symbolic)
    return ELF::R_HAYDN_LO16;

  case Haydn::FIXUP_HAYDN_GOT_HI20:
    // Typed ELF 8. No PIC/GOT/PLT product ABI — keep the kind so a
    // producer cannot alias HI20/HI12. LLD getRelExpr/relocate refuse it
    // (same seat as TPREL). Do not rewrite as R_HAYDN_32.
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

  case Haydn::FIXUP_HAYDN_C_BranchSImm4:
  case Haydn::FIXUP_HAYDN_C_UImm4:
  case Haydn::FIXUP_HAYDN_C_BranchSImm10:
    // Compressed fixups are MC-only leftovers. No ELF kind — do not borrow
    // BranchSImm16 / UImm rows.
    reportError(Fixup.getLoc(),
                "compressed Haydn fixup has no ELF relocation");
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_LongBranchSImm20:
    // MC-only long-branch kind. Product JAL uses WIDE_CallSImm20.
    reportError(Fixup.getLoc(),
                "MC-only long-branch fixup has no ELF relocation");
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_S0LSOff4_2:
  case Haydn::FIXUP_HAYDN_S0LSOff4_3:
  case Haydn::FIXUP_HAYDN_S0LSOff2_0:
  case Haydn::FIXUP_HAYDN_S0LSOff3_0:
    // FI/spill scaled-imm fields resolve locally in the AsmBackend.
    reportError(Fixup.getLoc(),
                "MC-only FI/spill fixup has no ELF relocation");
    return ELF::R_HAYDN_NONE;

  case Haydn::FIXUP_HAYDN_HWLoopOffset:
    // Legacy 8-byte placeholder kind (MC-only; HaydnFixupKinds.h). No emitter
    // produces it: product SET_HWLOOP_* symbolic offsets use the typed
    // HWLoopOff1/Off2 kinds above. This case previously aliased to
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
    // Wide-imm pair: LUI I12 (absolute). FieldLsb is E2 e0 @32; E3 e0/e1/e2
    // via resolveFieldLsb. Specifier %hi12 selects this kind.
    return ELF::R_HAYDN_HI12;

  case Haydn::FIXUP_HAYDN_LO20:
    // Wide-imm pair: ADDI32 RI20 (absolute). Specifier %lo20. E2 e1 @65
    // via resolveFieldLsb.
    return ELF::R_HAYDN_LO20;

  case Haydn::FIXUP_HAYDN_PC_LO20:
    // Wide-imm pair: ADDI32 RI20 (PC-relative). Specifier %pc_lo20.
    return ELF::R_HAYDN_PC_LO20;

  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12:
    // I12 one-reg cond (BEQZ_W/…): E2 e0 imm12 @ parcel bits[32:43].
    // Byte PC+imm (ValueShift=0, RelocTrans::None).
    return ELF::R_HAYDN_WIDE_BranchSImm12;

  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
    // RI12 two-reg cond (BEQ_W/BNE_W/…): E2 e0 imm12 @ parcel bits[32:43].
    // Distinct from I12 so the linker does not overwrite rt/rs. Same byte
    // PC+imm law as other branch kinds.
    return ELF::R_HAYDN_WIDE_BranchSImm12_RI;

  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
    // I20 call (JAL family): E2 e0 @ parcel bits[31:50]. Byte PC+imm; no
    // dual call-scale product law.
    return ELF::R_HAYDN_WIDE_CallSImm20;

  case Haydn::FIXUP_HAYDN_LS_IMM:
    // Format E LOADSTORE0/LOAD1 RI6: 1:1 to R_HAYDN_LS_IMM (AIE dense
    // fixup→ELF map: AIEELFObjectWriter.cpp:60-63). Never alias SImm16.
    return ELF::R_HAYDN_LS_IMM;

  case Haydn::FIXUP_HAYDN_JALRSImm12:
    // R_HAYDN_JALRSImm12 (ELF 22). Distinct from the RI12 branch row.
    // Kind is selected by typed (row, entry, member) via
    // findFixupFromFixupFields (RI12 opc 1); FieldLsb is E2 e0 @32 /
    // E3 e0 @23 / E3 e1 @54. Call-indirect / JT dispatch (jalr rd, rs, 0)
    // bake a zero imm and never reach this mapping. PIC/JT table entries
    // are R_HAYDN_32_PCREL (FK_Data_4 + IsPCRel), not a second JALR kind.
    // Local targets still resolve in the AsmBackend; unresolved externals
    // emit ELF 22. Peer: AIE dense fixup->ELF map
    // (AIEELFObjectWriter.cpp:60-63); Haydn cannot be dense because
    // MC-only kinds sit after the shared ELF range.
    return ELF::R_HAYDN_JALRSImm12;

  case Haydn::FIXUP_HAYDN_CSR_UImm8:
    // R_HAYDN_CSR_UImm8 (ELF 23). Format E I8 uimm8 CSR address.
    // Distinct from R_HAYDN_8 (data-section 1-byte write). Local
    // constants still resolve in the AsmBackend; unresolved externals
    // emit this kind so reloc CSRW_W is not an untyped NONE fixup.
    return ELF::R_HAYDN_CSR_UImm8;
  }
}

bool HaydnELFObjectWriter::needsRelocateWithSymbol(const MCValue &,
                                                    unsigned Type) const {
  // Returning false let MCObjectStreamer drop the symbol from relocations
  // it treated as section-relative. Undefined externs then relocated
  // against symbol index 0 (R_HAYDN_CallSImm20 linked to address 0).
  // RISC-V ELFObjectWriter.cpp:needsRelocateWithSymbol returns true for
  // the same reason; AIEELFObjectWriter.cpp:37-40 is the conservative
  // VLIW peer. Keep the symbol so function and data references survive.
  return true;
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createHaydnELFObjectWriter() {
  return std::make_unique<HaydnELFObjectWriter>();
}
