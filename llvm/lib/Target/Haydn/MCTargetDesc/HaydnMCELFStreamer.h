//===- HaydnMCELFStreamer.h - Haydn subclass of MCELFStreamer ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn VLIW bundles (Haydn::BUNDLE) carry slot children as MCOperand::isInst
// sub-instructions. The base MCStreamer::emitInstruction only walks top-level
// isExpr operands, so symbols referenced *inside* a bundle child (e.g.
// `{ jal lr, main; nop; nop }`) are never registered with the assembler.
// Unregistered symbols are omitted from.symtab; the ELF reloc then points at
// symbol index 0 (ABS), and lld patches S=0 → a garbage PC-relative offset
// (: crt0 `jal lr, main` → `jal r0, 65535`).
//
// Mirror HexagonMCELFStreamer: before the base emit path, recursively visit
// used expressions in every bundle child so registerSymbol/setUsedInReloc run.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"
#include <memory>

namespace llvm {

class HaydnMCELFStreamer : public MCELFStreamer {
public:
  HaydnMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                     std::unique_ptr<MCObjectWriter> OW,
                     std::unique_ptr<MCCodeEmitter> Emitter);

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override;

private:
  // Recursively register symbols referenced by Inst (and any isInst children).
  void emitSymbolsInInst(const MCInst &Inst);
};

MCStreamer *createHaydnELFStreamer(const Triple &TT, MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> &&MAB,
                                   std::unique_ptr<MCObjectWriter> &&OW,
                                   std::unique_ptr<MCCodeEmitter> &&CE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H
