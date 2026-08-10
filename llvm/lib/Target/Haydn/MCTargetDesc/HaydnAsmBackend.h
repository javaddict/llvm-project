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

  // Format E: PC is the parcel base, not a mid-parcel field byte offset.
  // Entry fields may sit at byte 6+; default MC P = frag+fixup_off mis-aligns
  // hwloop ÷4 and halfword branch checks. Compensate like Xtensa l32r.
  std::optional<bool> evaluateFixup(const MCFragment &, MCFixup &, MCValue &,
                                    uint64_t &Value) override;

  // Check whether the given instruction may need relaxation.
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override;

  // No MC-layer compression relaxation (retired C_* shells).
  bool fixupNeedsRelaxationAdvanced(const MCFragment &, const MCFixup &,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override;

  // Expand a compressed 16-bit instruction to its 32-bit equivalent.
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  /// Stamp production E96 ELF e_flags on every object (llvm-mc and llc).
  bool finishLayout() const override;

private:
  // Map a compressed opcode to its 32-bit equivalent.
  // Returns the original opcode if no relaxation mapping exists.
  unsigned getRelaxedOpcode(unsigned Opcode) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNASMBACKEND_H
