//===-- HaydnAsmBackend.cpp - Haydn Assembler Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnAsmBackend.h"
#include "HaydnFixupKinds.h"
#include "HaydnRelocLayout.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// Map a compressed 16-bit opcode to its 32-bit equivalent.
// C_* compressed shells deleted (Bundle128-only). No opcode relaxes.
unsigned HaydnAsmBackend::getRelaxedOpcode(unsigned Opcode) const {
  return Opcode;
}

// Check whether the given instruction may need relaxation.
// no C_* shells — nothing relaxes at MC layer.
bool HaydnAsmBackend::mayNeedRelaxation(unsigned Opcode,
                                        ArrayRef<MCOperand> Operands,
                                        const MCSubtargetInfo &STI) const {
  (void)Opcode;
  (void)Operands;
  (void)STI;
  return false;
}

// G-MC-8: C_* compressed shells retired. mayNeedRelaxation is always
// false, so this override is never consulted for product emission. Keep a
// trivial override that never requests relaxation (do not re-open C_*).
bool HaydnAsmBackend::fixupNeedsRelaxationAdvanced(
    const MCFragment &, const MCFixup &, const MCValue &, uint64_t,
    bool) const {
  return false;
}

// Expand a compressed 16-bit instruction to its 32-bit equivalent.
// C_* shells deleted — no MC relaxation expansions remain.
void HaydnAsmBackend::relaxInstruction(MCInst &Inst,
                                       const MCSubtargetInfo &STI) const {
  (void)Inst;
  (void)STI;
}

// Apply a fixup value to the instruction/data at the given offset.
void HaydnAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                   const MCValue &Target, uint8_t *Data,
                                   uint64_t Value, bool IsResolved) {
  MCFixupKind Kind = Fixup.getKind();

  // Record relocations for unresolved fixups
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);

  // If this is a relocation (not a fixup), don't apply it now
  if (mc::isRelocation(Kind))
    return;

  // For fixups that need relocations, record them but don't apply
  if (!IsResolved) {
    // The linker will handle this
    return;
  }

  // === Single-source reloc table (HaydnRelocLayout). Both this MC writer and
  // lld's Haydn::relocate / getImplicitAddend delegate to the same geometry +
  // transform table, so reader and writer can never diverge (; the
  // ÷2/÷4 reader-writer split + flat HI20/LO16 mask are now impossible).
  //
  // Legacy exception: FIXUP_HAYDN_HWLoopOffset is the 8-byte *placeholder* form
  // whose loop_start/loop_end tag-compensation the geometric table
  // cannot express. Keep that logic verbatim; everything else is table-driven.
  if (Kind == Haydn::FIXUP_HAYDN_HWLoopOffset) {
    constexpr uint8_t HWLoopTagMask = 0x3;        // tag at bits[17:16] = byte2[1:0]
    constexpr uint64_t HWLoopEndWordCompensation = 4; // word1 sits at PC+4
    constexpr uint8_t HWLoopTagLoopEnd = 0b01;
    uint8_t Tag = 0b00;
    if (Fixup.getOffset() + 3 <= F.getSize())
      Tag = Data[2] & HWLoopTagMask;
    uint64_t Compensated = (Tag == HWLoopTagLoopEnd)
                               ? (Value + HWLoopEndWordCompensation)
                               : Value;
    if (Compensated & 0x3) {
      getContext().reportError(Fixup.getLoc(),
                               "hwloop offset must be 4-byte aligned");
      return;
    }
    int64_t Offset = static_cast<int64_t>(Compensated) >> 2;
    if (!isInt<16>(Offset)) {
      getContext().reportError(Fixup.getLoc(), "hwloop offset out of range");
      return;
    }
    if (Fixup.getOffset() + 4 > F.getSize()) {
      getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
      return;
    }
    uint64_t FieldVal =
        static_cast<uint64_t>(static_cast<int16_t>(Offset)) & 0xFFFF;
    HaydnReloc::patchField(Data, FieldVal, 4, 16, 0);
    return;
  }

  HaydnReloc::RelocKind R = HaydnReloc::mapFixupKind(static_cast<unsigned>(Kind));
  if (R != HaydnReloc::RelocKind::Invalid) {
    HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(R, Value);
    if (!Comp.OK) {
      getContext().reportError(Fixup.getLoc(), Comp.Err);
      return;
    }
    const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
    if (Fixup.getOffset() + FI.NBytes > F.getSize()) {
      getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
      return;
    }
    // Data is pre-adjusted to Fixup.getOffset (lesson): write at Data[0].
    HaydnReloc::patchField(Data, Comp.FieldVal, FI.NBytes, FI.FieldSize, FI.FieldLsb);
    return;
  }

  // Generic data fixups (FK_Data_* / FK_PCRel_*): raw little-endian byte write.
  if (Kind >= FirstTargetFixupKind)
    llvm_unreachable("Unknown target fixup kind!");
  MCFixupKindInfo Info = MCAsmBackend::getFixupKindInfo(Kind);
  unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;
  if (Fixup.getOffset() + NumBytes > F.getSize()) {
    getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
    return;
  }
  for (unsigned Idx = 0; Idx != NumBytes; ++Idx)
    Data[Idx] |= static_cast<uint8_t>((Value >> (Idx * 8)) & 0xFF);
}

// Get information about a fixup kind.
MCFixupKindInfo HaydnAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  // Names only — geometry (TargetOffset/TargetSize) is derived from the
  // single-source HaydnRelocLayout table so getFixupKindInfo can never drift
  // from applyFixup (MC) or Haydn::relocate (lld). Order matches the
  // FIXUP_HAYDN_* enum in HaydnFixupKinds.h.
  static const char *const Names[] = {
      "FIXUP_HAYDN_NONE",         "FIXUP_HAYDN_32",
      "FIXUP_HAYDN_SImm16",       "FIXUP_HAYDN_BranchSImm16",
      "FIXUP_HAYDN_CallSImm20",   "FIXUP_HAYDN_HI20",
      "FIXUP_HAYDN_LO16",         "FIXUP_HAYDN_GOT_HI20",
      "FIXUP_HAYDN_TPREL_HI20",   "FIXUP_HAYDN_TPREL_LO16",
      "FIXUP_HAYDN_32_PCREL",     "FIXUP_HAYDN_C_BranchSImm4",
      "FIXUP_HAYDN_C_UImm4",      "FIXUP_HAYDN_C_BranchSImm10",
      "FIXUP_HAYDN_HWLoopOffset", "FIXUP_HAYDN_HWLoopOff1",
      "FIXUP_HAYDN_HWLoopOff2",   "FIXUP_HAYDN_LongBranchSImm20",
      "FIXUP_HAYDN_HI12",         "FIXUP_HAYDN_LO20",
      "FIXUP_HAYDN_PC_LO20",      "FIXUP_HAYDN_WIDE_BranchSImm12",
      "FIXUP_HAYDN_WIDE_BranchSImm12_RI",
      "FIXUP_HAYDN_WIDE_CallSImm20",
      "FIXUP_HAYDN_S0LSOff4_2",   "FIXUP_HAYDN_S0LSOff4_3",
      "FIXUP_HAYDN_S0LSOff2_0",   "FIXUP_HAYDN_S0LSOff3_0",
      "FIXUP_HAYDN_LS_IMM",
  };
  static_assert(std::size(Names) == Haydn::NumTargetFixupKinds,
                "Names[] must list every target fixup kind, in enum order");

  // Handle generic fixup kinds
  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  // Handle relocation fixups (from.reloc directive or assembler)
  if (mc::isRelocation(Kind))
    return {};

  assert(unsigned(Kind - FirstTargetFixupKind) < Haydn::NumTargetFixupKinds &&
         "Invalid fixup kind!");

  HaydnReloc::RelocKind R = HaydnReloc::mapFixupKind(Kind);
  const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
  // This LLVM tree stores PC-relativity on MCFixup::isPCRel (set by the
  // encoder), not on MCFixupKindInfo::Flags. Keep Flags=0 (matches RISCV).
  return MCFixupKindInfo{Names[Kind - FirstTargetFixupKind], FI.FieldLsb,
                         FI.FieldSize, 0};
}

// Write NOP data to the output stream.
std::unique_ptr<MCObjectTargetWriter>
HaydnAsmBackend::createObjectTargetWriter() const {
  return createHaydnELFObjectWriter();
}

bool HaydnAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                   const MCSubtargetInfo *) const {
  // Haydn is little-endian with 16-bit minimum instruction size
  // We'll emit 16-bit NOPs (0x0000) for alignment
  if ((Count % 2) != 0)
    return false;

  for (uint64_t Idx = 0; Idx < Count; Idx += 2)
    OS.write("\x00\x00", 2);

  return true;
}

// Create the Haydn assembly backend.
MCAsmBackend *llvm::createHaydnAsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  return new HaydnAsmBackend(STI, Options);
}
