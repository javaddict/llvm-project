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

namespace llvm {

class LLVM_LIBRARY_VISIBILITY HaydnAsmPrinter : public AsmPrinter {
  HaydnMCInstLower MCInstLowering;


  /// Emit function-level `#<spill-kpi>` spill/reload observe summary.
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
  // flush labels only at the first real body MI — no streamer
  // emitCodeAlignment pad. Product Format E parcels are already
  // productParcelBytes() (12 B; HWLoopOff uses registry ValueShift);
  // Fixup/exact-commit owns any MIR pad cycles. MBB setAlignment is NOT used.
  DenseMap<const MachineBasicBlock *, SmallVector<MCSymbol *, 2>>
      PendingHwloopStartLabels;

  // JT/call soft-zero R0 is MIR (HaydnExpandPseudos), not printer state.

  // Stream a single already-concrete MCInst. Product encode is Format E
  // only (BUNDLE_E96_*). No printer re-slot, representation expand, or
  // logical-member bind.
  void emitWrappedInst(const MCInst &Inst);

 // : print-time fixed-R12 AT spill removed. VASTART/VACOPY expand via
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

public:
  // Inclusive START/END temp labels for SET_HWLOOP_{W,F2_W} (used by
  // HaydnMCInstLower for Desc-only BUNDLE/standalone Lower — not printer
 // expand). : flush on first/last real MI; no streamer alignment pad.
  MCSymbol *getOrCreateHwloopEndSym(MachineBasicBlock *Latch);
  MCSymbol *getOrCreateHwloopStartSym(MachineBasicBlock *LoopBody);

  explicit HaydnAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override { return "Haydn Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override;

  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;

  // Flush leftover HWLR_BEGIN/END temps if the MBB had no real instruction
  // (terminator-only or empty latch). AIE setPreInstrSymbol on the previous
  // bundle (AIEBaseAsmPrinter.cpp:64-70) unreachables when Prev is null;
  // Haydn overlay: never leave the MCInstLower temp undefined.
  void emitBasicBlockEnd(const MachineBasicBlock &MBB) override;

  void emitFunctionBodyStart() override;

  // Promote sh_addralign. Entry fill is HasFunctionAlignment=true, not a
  // post-label MachineAlignment pass; addPostBBSections is empty.
  void emitFunctionEntryLabel() override;

  bool runOnMachineFunction(MachineFunction &MF) override;

  // Pseudo instruction expansion (auto-generated)
  bool lowerPseudoInstExpansion(const MachineInstr *MI, MCInst &Inst);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNASMPRINTER_H
