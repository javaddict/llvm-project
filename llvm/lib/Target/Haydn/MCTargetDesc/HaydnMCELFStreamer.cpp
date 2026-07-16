//===- HaydnMCELFStreamer.cpp - Haydn subclass of MCELFStreamer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnMCELFStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

HaydnMCELFStreamer::HaydnMCELFStreamer(MCContext &Context,
                                       std::unique_ptr<MCAsmBackend> TAB,
                                       std::unique_ptr<MCObjectWriter> OW,
                                       std::unique_ptr<MCCodeEmitter> Emitter)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                    std::move(Emitter)) {}

void HaydnMCELFStreamer::emitSymbolsInInst(const MCInst &Inst) {
  // HexagonMCELFStreamer::EmitSymbol: walk operands for isExpr. Haydn extends
  // that with recursive isInst descent for Bundle128 BUNDLE children (AIE
  // composite / Hexagon packet both nest real ops under a container MI).
  for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (Op.isExpr())
      visitUsedExpr(*Op.getExpr());
    else if (Op.isInst() && Op.getInst())
      emitSymbolsInInst(*Op.getInst());
  }
}

void HaydnMCELFStreamer::emitInstruction(const MCInst &Inst,
                                         const MCSubtargetInfo &STI) {
  // base MCStreamer::emitInstruction only visits top-level isExpr
  // operands. Without this walk, `{ jal lr, main; nop; nop }` emitted a
  // R_HAYDN_CallSImm20 against symbol index 0 (lld patched S=0 → offset -PC).
  // Hexagon: HexagonMCELFStreamer::emitInstruction → EmitSymbol per packet
  // member before MCObjectStreamer::emitInstruction.
  emitSymbolsInInst(Inst);
  MCELFStreamer::emitInstruction(Inst, STI);
}

MCStreamer *llvm::createHaydnELFStreamer(const Triple & /*TT*/,
                                         MCContext &Context,
                                         std::unique_ptr<MCAsmBackend> &&MAB,
                                         std::unique_ptr<MCObjectWriter> &&OW,
                                         std::unique_ptr<MCCodeEmitter> &&CE) {
  return new HaydnMCELFStreamer(Context, std::move(MAB), std::move(OW),
                                std::move(CE));
}
