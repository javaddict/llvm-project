//===-- HaydnAsmPrinter.h - Haydn implementation of AsmPrinter ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNASMPRINTER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNASMPRINTER_H

#include "HaydnMCInstLower.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include <optional>

namespace llvm {

class LLVM_LIBRARY_VISIBILITY HaydnAsmPrinter : public AsmPrinter {
  HaydnMCInstLower MCInstLowering;

  /// Emit HiFi-like `#<swps>` SMS bounds when this MBB is a recorded kernel.
  void emitSMSSWPSComments(const MachineBasicBlock &MBB);

  /// Emit function-level `#<spill-kpi>` spill/reload observe summary (B4.5).
  /// Counts MachineInstr::getSpillSize/getRestoreSize (and folded peers) over
  /// all real MIs including BUNDLE children. Observe-only; default ON.
  void emitSpillKPIComments();

  // Pending HWLOOP inclusive-END labels, keyed by the latch MBB. Per
  // the authoritative `fibonacci_hw_loop.asm`, HWLR_END is INCLUSIVE: the
  // address of the LAST instruction of the loop body (not the first
  // instruction after the body). chose ExitBB (exclusive) on the
  // since-disproven assumption that Haydn follows HiFi's exclusive
  // convention; this is exactly the follow-up risk recorded.
  // AIE-first: END address = last real body MI. Implemented as a
  // pending-vector flush in emitInstruction (pad ÷4, then emit labels)
  // not setPreInstrSymbol (parent would emit the label before our pad).
  // keyed MBB→VECTOR (one fresh MCSymbol PER hardware-loop instance).
  // Nested or shared-latch loops each get their own END symbol; all pending
  // symbols for a latch are emitted together at its last real instruction.
  // The previous MBB→single-MCSymbol map collided when two hwloops shared a
  // latch (the second clobbered the first's symbol → undefined symbol).
  DenseMap<const MachineBasicBlock *, SmallVector<MCSymbol *, 2>>
      PendingHwloopEndLabels;

  // Pending HWLOOP START labels, keyed by the loop-body MBB. Mirrors END:
  // flush on first real body MI after emitCodeAlignment(4) so HWLR_BEGIN is
  // ÷4-representable. MBB setAlignment is NOT used.
  DenseMap<const MachineBasicBlock *, SmallVector<MCSymbol *, 2>>
      PendingHwloopStartLabels;

  // JT/call soft-zero R0 is MIR (HaydnExpandPseudos), not printer state.

  // Emit a single MCInst wrapped in a 3-slot VLIW bundle with NOP padding.
  // Every Haydn instruction must be in a 64-bit D-class bundle — standalone
  // instructions are not supported by the hardware.
  void emitWrappedInst(const MCInst &Inst);

  // W1.2: print-time fixed-R12 AT spill removed. VASTART/VACOPY expand via
  // withPostRAScratch in ExpandPseudos (free GPR first; spill only if
  // needed). Printer is representation-only for that class.

  // Walk the operands of an MCInst (descending into nested sub-instructions
  // i.e. VLIW bundle children) and register every MCExpr operand with the
  // MCStreamer. This is required because \c MCStreamer::emitInstruction only
  // visits Expr operands on the *direct* operands of the MCInst it receives.
  // Haydn wraps every instruction in a BUNDLE MCInst whose children are
  // \c MCOperand::createInst operands (not Exprs), so the streamer's default
  // \c visitUsedExpr loop never reaches the children's Expr operands. Without
  // explicit registration, a referenced-but-undefined extern symbol (e.g. the
  // target of a JAL call) is never registered with the MCAssembler and is
  // therefore never written to <code>.symtab</code>; the resulting relocation
  // then references symbol index 0, silently breaking cross-object calls at
  // link/runtime. See.
  void registerSymbolicOperands(const MCInst &Inst) const;

  // Emit a single 48-bit WIDE hwloop setup instruction.
  // \param Sel HWLOOP selector (0=outer, 1=inner).
  // \param StartSym Symbol for the loop body start (HWLR_BEGIN target).
  // \param EndSym Symbol for the loop body end (HWLR_END target).
  // \param CntImm If present, emit SET_HWLOOP with this constant trip
  // count (§5.11 all-imm form). If absent, emit
  // SET_HWLOOP_F2 with the count in \p RsReg (§5.12).
  // \param RsReg GPR32 holding the trip count (used only when CntImm
  // is absent).
  void emitHWLoopWideInst(unsigned Sel, const MCSymbol *StartSym,
                          const MCSymbol *EndSym,
                          std::optional<int64_t> CntImm, unsigned RsReg = 0);

public:
  // Inclusive START/END temp labels for SET_HWLOOP_{W,F2_W} (used by
  // HaydnMCInstLower for Desc-only BUNDLE/standalone Lower — not printer
  // expand). Pending flush on first/last real MI after Bundle128 pad.
  MCSymbol *getOrCreateHwloopEndSym(MachineBasicBlock *Latch);
  MCSymbol *getOrCreateHwloopStartSym(MachineBasicBlock *LoopBody);

  explicit HaydnAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override { return "Haydn Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override;

  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;

  void emitFunctionBodyStart() override;

  bool runOnMachineFunction(MachineFunction &MF) override;

  // Pseudo instruction expansion (auto-generated)
  bool lowerPseudoInstExpansion(const MachineInstr *MI, MCInst &Inst);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNASMPRINTER_H
