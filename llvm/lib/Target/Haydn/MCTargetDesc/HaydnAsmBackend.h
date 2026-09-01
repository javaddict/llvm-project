//===-- HaydnAsmBackend.h - Haydn Assembler Backend -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNASMBACKEND_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNASMBACKEND_H

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"

namespace llvm {

class HaydnAsmBackend : public MCAsmBackend {

public:
  HaydnAsmBackend(const MCSubtargetInfo &STI, const MCTargetOptions &OP)
      : MCAsmBackend(llvm::endianness::little) {}

  void applyFixup(const MCFragment &, const MCFixup &, const MCValue &Target,
                  uint8_t *Data, uint64_t Value, bool IsResolved) override;

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override;

  // evaluateFixup is deliberately NOT overridden (D1.28): the MC emitter is
  // the one parcel-origin authority (member fixups are re-based to ParcelBase
  // at emission), so default MC evaluation is correct for every kind. A
  // backend-wide PC-rel seeding hook would be a parallel mechanism that
  // silently perturbs any PCRel-flagged data word (R_HAYDN_32_PCREL,
  // S+A-P at any fragment offset). See HaydnAsmBackend.cpp.

  // Check whether the given instruction may need relaxation.
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override;

  // Format E has no MC-layer opcode relaxation.
  bool fixupNeedsRelaxationAdvanced(const MCFragment &, const MCFixup &,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override;

  // Format E has no MC-layer opcode relaxation.
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  /// Stamp production E96 ELF e_flags on every object (llvm-mc and llc).
  bool finishLayout() const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNASMBACKEND_H
