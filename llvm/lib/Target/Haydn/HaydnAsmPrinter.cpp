//===-- HaydnAsmPrinter.cpp - Haydn LLVM assembly writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the Haydn assembly language.
//
//===----------------------------------------------------------------------===//

#include "HaydnAsmPrinter.h"
#include "Haydn.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnInstPrinter.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-asm-printer"

// residual: packer still emits occasional oversubscribed BUNDLEs
// (27 NatureDSP kernels). AIE fails closed at format assert; Haydn currently
// splits for product green. Strict mode refuses the silent split.
// Track: re-enable default-strict after PostRA placement is complete.
// After PostRA cycleCanFormLegalBundle guard, oversub should be rare.
// Default ON (AIE-style fail-closed). Escape: -haydn-asmprinter-strict-bundles=0.
static cl::opt<bool> HaydnAsmPrinterStrictBundles(
    "haydn-asmprinter-strict-bundles", cl::Hidden, cl::init(true),
    cl::desc(" fatal on oversubscribed Bundle128 instead of"
             "emergency multi-bundle split (default ON)"));

STATISTIC(NumAsmPrinterBundleSplits,
          "HaydnAsmPrinter emergency splits of oversubscribed bundles");

HaydnAsmPrinter::HaydnAsmPrinter(llvm::TargetMachine &TM,
                                std::unique_ptr<llvm::MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), MCInstLowering(OutContext, *this) {}

bool HaydnAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // soft-zero R0 after JT/call is inserted in MIR by
  // HaydnExpandPseudos::insertSoftZeroR0Maintenance (before PostRA pack).
  // AsmPrinter no longer injects R0 re-zero (representation-only).
  // drop any leftover hwloop label state from a prior function.
  PendingHwloopEndLabels.clear();
  PendingHwloopStartLabels.clear();
  return AsmPrinter::runOnMachineFunction(MF);
}

void HaydnAsmPrinter::registerSymbolicOperands(const MCInst &Inst) const {
  // register every Expr operand (recursing into nested
  // sub-instructions, e.g. BUNDLE children) with the streamer so the
  // MCAssembler records the symbol. MCStreamer::emitInstruction visits Expr
  // operands only on the *direct* operands of the MCInst it receives; Haydn
  // wraps every instruction in a BUNDLE whose children are MCOperand::createInst
  // operands, so the streamer's default visit loop never reaches the child
  // Expr operands. Without this, an undefined extern referenced by a child
  // (e.g. the JAL call target) is never registered -> never emitted to
  // symtab -> its relocation references symbol index 0.
  for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (Op.isExpr()) {
      OutStreamer->visitUsedExpr(*Op.getExpr());
    } else if (Op.isInst() && Op.getInst()) {
      registerSymbolicOperands(*Op.getInst());
    }
  }
}

void HaydnAsmPrinter::emitWrappedInst(const MCInst &Inst) {
  // Product encode path is Bundle128 only (16 B parcels). The MCInst is
  // streamed; HaydnMCCodeEmitter routes through encodeBundle128 (FlexMap
  // materializes private slot peers at MCInst). No multi-width Mode-0/1/3
  // product path. registerSymbolicOperands registers Expr fixups.
  //
  // History (retired): force-BUNDLE wrap + NOP-pad and variable-width
  // EW_16/32/48/64 bare emit were pre-Bundle128 cutover experiments.
  registerSymbolicOperands(Inst);
  EmitToStreamer(*OutStreamer, Inst);
}

// W1.2: print-time fixed-R12 AT helpers removed. VASTART/VACOPY expand via
// withPostRAScratch (free GPR first; PostRAScratchFI only if spill needed).

// Emit a single Bundle128 SET_HWLOOP setup (Phase B2).
// Per encoding_manual.md §5.11-§5.13 there are three WIDE forms (logical
// opcodes; encode-time Flex materializes the S0 slot variant into a
// Bundle128 parcel):
// SET_HWLOOP_W : uimm16_cnt + uimm6_off1 + uimm12_off2 + hwlr_sel
// SET_HWLOOP_F2_W : rs(count) + uimm6_off1 + uimm12_off2 + hwlr_sel
// SET_HWLOOP_REG_W : rs1(begin) + rs2(end) + rs3(count) + hwlr_sel
// Offsets are MCSymbolRefExpr operands; the emitter attaches
// FIXUP_HAYDN_HWLoopOff1/Off2 (÷4, Bundle128 FieldLsb —).
// Spec §HW Loop setup timing (VLIW_Engine_Compiler_Constraints):
// if HWLR_BEGIN is bundle `t`, SET must issue at or before `t-3`.
// Preferred fill: real preheader work after SET (trip compute stays before
// SET because it defines the count; other independent precompute is moved
// after SET by HaydnHardwareLoops). HaydnFixupHwLoops (post-BR) inserts only
// the *deficit* idle NOPs when useful work is short. This emitter does NOT
// always spray 3 NOPs — that burned cycles when the preheader already had
// address/setup arithmetic that could sit in the commit window.
// HWLR_BEGIN/END 4-byte pads remain at the body's first/last real instr
// (PendingHwloopStartLabels / PendingHwloopEndLabels).
void HaydnAsmPrinter::emitHWLoopWideInst(unsigned Sel,
                                          const MCSymbol *StartSym,
                                          const MCSymbol *EndSym,
                                          std::optional<int64_t> CntImm,
                                          unsigned RsReg) {
  const MCExpr *StartExpr = MCSymbolRefExpr::create(StartSym, OutContext);
  const MCExpr *EndExpr = MCSymbolRefExpr::create(EndSym, OutContext);

  MCInst HWInst;
  // MCInst operand order matches the.td / encoder contract: 
  // SET_HWLOOP_W: (sel, offset1, offset2, cnt)
  // SET_HWLOOP_F2_W: (sel, offset1, offset2, rs)
  // For the constant-count form we pass the symbol expressions for the
  // offsets and the materialized count as an immediate. For the register
  // form we pass rs as the count source. hwlr_sel = Sel (1 = innermost ZOL).
  if (CntImm) {
    HWInst.setOpcode(Haydn::SET_HWLOOP_W);
    HWInst.addOperand(MCOperand::createImm(Sel & 0x1));  // sel (uimm1)
    HWInst.addOperand(MCOperand::createExpr(StartExpr)); // offset1 (brtarget)
    HWInst.addOperand(MCOperand::createExpr(EndExpr));   // offset2 (brtarget)
    HWInst.addOperand(MCOperand::createImm(*CntImm));    // cnt (uimm16)
  } else {
    HWInst.setOpcode(Haydn::SET_HWLOOP_F2_W);
    HWInst.addOperand(MCOperand::createImm(Sel & 0x1));  // sel (uimm1)
    HWInst.addOperand(MCOperand::createExpr(StartExpr)); // offset1 (brtarget)
    HWInst.addOperand(MCOperand::createExpr(EndExpr));   // offset2 (brtarget)
    HWInst.addOperand(MCOperand::createReg(RsReg));      // rs (GPR32 count)
  }
  // align the SET parcel's OWN address to 4 bytes BEFORE emitting it.
  // The hwloop offset fields are PC-relative to the SET address
  // (FIXUP_HAYDN_HWLoopOff1/2, ÷4); under Bundle128-only every parcel is
  // already 16-byte aligned, so this is a safety no-op that stays for any
  // residual non-16 path.
  OutStreamer->emitCodeAlignment(Align(4), &getSubtargetInfo());
  EmitToStreamer(*OutStreamer, HWInst);
  // Setup-gap NOPs: HaydnFixupHwLoops (addPreEmit after BranchRelaxation).
}

// Find the last "real" instruction of \p MBB — the last instruction that is
// neither a terminator, nor meta, nor a debug/pseudo instruction that emits
// no bytes. This is the instruction whose start address is the inclusive
// HWLR_END target per the Haydn spec (HWLR_END = address of the LAST
// instruction of the loop body). For short ZOL bodies, HaydnFixupHwLoops
// Body trailing pads (if any) may leave this last real as
// a pad — useful LD/MAC/ST then appear before the END label while still
// executing inside the inclusive [BEGIN, END] window. Returns nullptr for an
// empty/terminator-only block. Uses MachineBasicBlock::iterator (NOT
// instr_iterator) so a VLIW BUNDLE is treated as a single unit (the BUNDLE
// header is what AsmPrinter receives in emitInstruction).
static MachineInstr *getLastRealInstr(MachineBasicBlock *MBB) {
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    MachineInstr &MI = *I;
    if (MI.isTerminator() || MI.isMetaInstruction())
      continue;
    if (MI.isDebugInstr())
      continue;
    // Skip pseudos that emit no bytes (e.g. LoopStart in a preheader, or
    // PseudoLoopEnd in the latch — the latter is also isMeta and caught
    // above). A BUNDLE header is not a pseudo even though its children may be
    // keep it. NOTE: some Haydn pseudos DO emit bytes: SETCBR_BEGIN
    // SETCBR_END are pseudos that this AsmPrinter expands to a real `csrw`
    // (see the SETCBR case in emitInstruction), so they must NOT be skipped
    // otherwise, if one were the loop body's last instruction, the END label
    // would land before an earlier instruction (inclusive END off) or never
    // emit at all (undefined-symbol error).
    if (!MI.isBundle() && MI.isPseudo() &&
        MI.getOpcode() != Haydn::SETCBR_BEGIN &&
        MI.getOpcode() != Haydn::SETCBR_END)
      continue;
    return &MI;
  }
  return nullptr;
}

static const MachineInstr *getLastRealInstr(const MachineBasicBlock *MBB) {
  return getLastRealInstr(const_cast<MachineBasicBlock *>(MBB));
}

// Create a fresh inclusive-END MCSymbol for ONE hardware-loop instance whose
// latch is \p Latch. /AIE semantics: END address = last real body MI.
// Haydn implements that via PendingHwloopEndLabels flushed in emitInstruction
// *after* emitCodeAlignment(4) immediately before that last real MI — not via
// setPreInstrSymbol (which would emit the label before the ÷4 pad).
// per-instance symbols (vector) for shared-latch nested loops.
MCSymbol *
HaydnAsmPrinter::getOrCreateHwloopEndSym(MachineBasicBlock *Latch) {
  MCSymbol *Sym = OutContext.createTempSymbol("Lhwloop_end", true);
  PendingHwloopEndLabels[Latch].push_back(Sym);
  Latch->setLabelMustBeEmitted();
  return Sym;
}

// Create a fresh per-instance START symbol for the hardware loop whose body
// (HWLR_BEGIN target) is \p LoopBody. : BEGIN = first real body MI after
// ÷4 pad (pending flush in emitInstruction).
MCSymbol *
HaydnAsmPrinter::getOrCreateHwloopStartSym(MachineBasicBlock *LoopBody) {
  MCSymbol *Sym = OutContext.createTempSymbol("Lhwloop_start", true);
  PendingHwloopStartLabels[LoopBody].push_back(Sym);
  LoopBody->setLabelMustBeEmitted();
  return Sym;
}

void HaydnAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // HWLR_BEGIN alignment : if one or more hardware loops are waiting on a
  // START label for this MI's parent block, and this MI is the block's first
  // emitted real instruction, pad the stream to 4-byte alignment and THEN emit
  // all pending START symbols. The hwloop offset fields are ÷4 (§5.11/§5.14);
  // variable-width Haydn parcels (2/4/6/8 bytes) leave the stream at 2-mod-4
  // whenever an ODD count of {2,6}-byte parcels precedes the body — e.g. the
  // 6-byte WIDE SET_HWLOOP parcel itself, OR a hoisted 6-byte WIDE
  // constant-materialization parcel placed between SET and body. Without this
  // pad the fixup resolver rejects "hwloop offset must be 4-byte aligned"
  // (HaydnAsmBackend.cpp:597) and -filetype=obj aborts. The pad is a NOP bundle
  // inside the loop body/preheader — harmless. emitCodeAlignment needs the
  // MCSubtargetInfo arg (2nd param, no default) for NOP encoding; pass
  // &getSubtargetInfo (AsmPrinter base). MBB setAlignment is NOT used: LLVM
  // elides alignment for fallthrough blocks, so the streamer call is the
  // only reliable mechanism. Fires once per (block, hwloop) — the list is
  // cleared after emission so a re-visited block can't double-define a symbol.
  // Block-entry detection: skip leading meta/debug that emit no bytes, fire
  // on the first REAL instruction (incl. byte-emitting Haydn pseudos).
  if (!PendingHwloopStartLabels.empty()) {
    const MachineBasicBlock *Parent = MI->getParent();
    auto It = PendingHwloopStartLabels.find(Parent);
    if (It != PendingHwloopStartLabels.end() && !It->second.empty() &&
        !MI->isMetaInstruction() && !MI->isDebugInstr()) {
      // Pad first, then labels (÷4). Do not use setPreInstrSymbol for these
      // symbols — parent AsmPrinter would emit PreInstr before this pad.
      OutStreamer->emitCodeAlignment(Align(4), &getSubtargetInfo());
      for (MCSymbol *Sym : It->second)
        OutStreamer->emitLabel(Sym);
      It->second.clear();
    }
  }

  // Inclusive-END: if one or more hardware loops are waiting on an end-label
  // for this MI's parent block, and this MI is the block's last real body
  // instruction, emit ALL of them now so each resolves to this instruction's
  // start address. This makes HWLR_END point at the LAST body instruction
  // (inclusive) rather than the first instruction after the body (exclusive
  // the old behavior). getLastRealInstr treats a VLIW BUNDLE header as
  // the unit (matching what AsmPrinter receives here), so a direct ==
  // comparison suffices for both bundled and non-bundled blocks.
  //
  // HWLR_END is ÷4 too. Pad the stream to 4-byte alignment BEFORE
  // emitting the END label(s) so the latch's last instruction (and thus the
  // END fixup value) is 4-aligned regardless of the body's parcel composition
  // (e.g. a body containing its own 6-byte WIDE parcel). Same rationale as the
  // START pad above; same NOP-bundle mechanism.
  //
  if (!PendingHwloopEndLabels.empty()) {
    const MachineBasicBlock *Parent = MI->getParent();
    auto It = PendingHwloopEndLabels.find(Parent);
    if (It != PendingHwloopEndLabels.end()) {
      const MachineInstr *LastReal = getLastRealInstr(Parent);
      if (LastReal == MI) {
        // Pad once before END label(s), then clear (no double-define).
        OutStreamer->emitCodeAlignment(Align(4), &getSubtargetInfo());
        for (MCSymbol *Sym : It->second)
          OutStreamer->emitLabel(Sym);
        It->second.clear();
      }
    }
  }
  // Handle VLIW bundles: create an MC-level BUNDLE with embedded sub-instructions
  // following Hexagon's pattern. Each child MachineInstr is lowered into a
  // heap-allocated MCInst (via OutContext.createMCInst) and added as an
  // MCOperand::createInst operand. The MC layer (InstPrinter, CodeEmitter)
  // then handles the bundle as a unit.
  //
  // (single authority = Flags/placement, not opcode suffix): emit children
  // in SLOT order (s0, s1, s2). Primary slot source after MCInstLower is
  // HaydnMCFlags (from AltDescs placement). Suffix `_S<k>` is interim
  // fallback only for PEI/Frame ops still emitted as Flex. Encoder materializes
  // Flex variants at encode time; logical opcodes stay logical on MIR.
  if (MI->isBundle()) {
    MCInst MCB;
    MCB.setOpcode(Haydn::BUNDLE);

    // Lower each real child, then place it into its committed slot. Slots is
    // indexed 0=S0, 1=S1, 2=S2.
    const MCInstrInfo *MCInfo = TM.getMCInstrInfo();
    MCInst *Slots[3] = {nullptr, nullptr, nullptr};
    SmallVector<MCInst *, 3> UnslottedChildren; // no Flags and no Flex suffix
    // Over-subscription: AIE-style fail closed. Silent split after
    // pack invalidates branch/hwloop sizes. Opt-in emergency escape below.
    SmallVector<MCInst *, 2> Overflow;
    MachineBasicBlock::const_instr_iterator I = MI->getIterator();
    ++I; // Skip the BUNDLE instruction itself
    for (MachineBasicBlock::const_instr_iterator E = MI->getParent()->instr_end();
         I != E && I->isInsideBundle(); ++I) {
      // Skip debug instructions, implicit defs, and pseudos within the bundle.
      // These are not real instructions and should not be encoded.
      if (I->isDebugInstr() || I->isImplicitDef())
        continue;
      if (I->isPseudo())
        continue;

      // Allocate via MCContext so the child MCInst has stable lifetime.
      // The MCContext owns the memory and frees it at destruction.
      MCInst *ChildInst = OutContext.createMCInst();
      MCInstLowering.Lower(&*I, *ChildInst);

      // primary = HaydnMCFlags (placement); fallback = Flex suffix.
      unsigned SlotIdx = 3; // sentinel: no known placement
      if (auto FlagSlot = HaydnMCFlags::getHaydnSlot(*ChildInst)) {
        SlotIdx = *FlagSlot;
      } else if (MCInfo) {
        StringRef Name = MCInfo->getName(ChildInst->getOpcode());
        if (Name.ends_with("_S0"))
          SlotIdx = 0;
        else if (Name.ends_with("_S1"))
          SlotIdx = 1;
        else if (Name.ends_with("_S2"))
          SlotIdx = 2;
      }
      // True if Opc has a Bundle128 encode form in slot K (legal placement).
      auto FlexVarInSlot = [&](unsigned Opc, unsigned K) -> unsigned {
        return MCInfo ? getHaydnFlexVariantForSlot(Opc, K, *MCInfo) : 0;
      };

      if (SlotIdx < 3) {
        if (!Slots[SlotIdx] && FlexVarInSlot(ChildInst->getOpcode(), SlotIdx)) {
          // Ensure Flags carry placement even when only suffix was known.
          HaydnMCFlags::setHaydnSlot(*ChildInst, SlotIdx);
          Slots[SlotIdx] = ChildInst;
        } else {
          // Collision / illegal preferred slot: free LEGAL slot + re-Flag.
          // Rewrite opcode only when already Flex (PEI/Frame interim);
          // logical ops keep opcode (encoder materializes).
          StringRef Name =
              MCInfo ? MCInfo->getName(ChildInst->getOpcode()) : StringRef();
          bool IsFlex = Name.ends_with("_S0") || Name.ends_with("_S1") ||
                        Name.ends_with("_S2");
          unsigned Placed = 3;
          for (unsigned K = 0; K < 3 && Placed == 3; ++K) {
            if (Slots[K])
              continue;
            unsigned Var = FlexVarInSlot(ChildInst->getOpcode(), K);
            if (!Var)
              continue; // not legal in this free slot
            if (IsFlex)
              ChildInst->setOpcode(Var);
            HaydnMCFlags::setHaydnSlot(*ChildInst, K);
            Placed = K;
          }
          if (Placed >= 3) {
            Overflow.push_back(ChildInst);
          } else {
            Slots[Placed] = ChildInst;
          }
        }
      } else {
        UnslottedChildren.push_back(ChildInst);
      }
    }

    // Place unslotted children in the first free LEGAL slot and stamp Flags.
    // Encode strips NOP pads — Flags are the public placement authority.
    for (MCInst *Child : UnslottedChildren) {
      unsigned Placed = 3;
      for (unsigned K = 0; K < 3; ++K) {
        if (Slots[K])
          continue;
        if (!MCInfo ||
            !getHaydnFlexVariantForSlot(Child->getOpcode(), K, *MCInfo))
          continue;
        Placed = K;
        break;
      }
      if (Placed >= 3) {
        // No free legal slot (e.g. second S0-only) → separate single-op bundle.
        Overflow.push_back(Child);
        continue;
      }
      HaydnMCFlags::setHaydnSlot(*Child, Placed);
      Slots[Placed] = Child;
    }

    // Push children in s0/s1/s2 order; NOP-pad empty slots to ISSUE_SLOT_COUNT
    // (3). Unused slots must be NOP.
    for (unsigned K = 0; K < llvm::Haydn::ISSUE_SLOT_COUNT; ++K) {
      if (Slots[K]) {
        MCB.addOperand(MCOperand::createInst(Slots[K]));
      } else {
        MCInst *NopInst = OutContext.createMCInst();
        NopInst->setOpcode(Haydn::NOP);
        MCB.addOperand(MCOperand::createInst(NopInst));
      }
    }

    // Register each child's Expr operands (e.g. JAL call targets) so undefined
    // extern symbols make it into.symtab. See registerSymbolicOperands /.
    registerSymbolicOperands(MCB);

    EmitToStreamer(*OutStreamer, MCB);

    // Over-subscription: strict mode fail-closed; default = emergency split
    // (NatureDSP residual) with STATISTIC for packer debt tracking.
    if (!Overflow.empty() && HaydnAsmPrinterStrictBundles) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnAsmPrinter: oversubscribed Bundle128 (slot collision) after "
            "pack — refuse silent split (strict). Fix PostRA"
            "placement or drop -haydn-asmprinter-strict-bundles. Bundle MIR:\n";
      MI->print(OS);
      report_fatal_error(Twine(OS.str()));
    }
    if (!Overflow.empty()) {
      ++NumAsmPrinterBundleSplits;
      LLVM_DEBUG(dbgs() << "HaydnAsmPrinter: emergency split of oversubscribed "
                           "bundle ("
                        << Overflow.size() << " overflow child(ren))\n");
    }

    // Emergency split path: emit each deferred child alone.
    for (MCInst *OverChild : Overflow) {
      unsigned OverSlot = 3;
      if (auto FlagSlot = HaydnMCFlags::getHaydnSlot(*OverChild))
        OverSlot = *FlagSlot;
      else if (MCInfo) {
        StringRef Nm = MCInfo->getName(OverChild->getOpcode());
        if (Nm.ends_with("_S0"))
          OverSlot = 0;
        else if (Nm.ends_with("_S1"))
          OverSlot = 1;
        else if (Nm.ends_with("_S2"))
          OverSlot = 2;
      }
      if (OverSlot >= 3 || !MCInfo ||
          !getHaydnFlexVariantForSlot(OverChild->getOpcode(), OverSlot,
                                      *MCInfo)) {
        OverSlot = 0;
        if (MCInfo) {
          for (unsigned K = 0; K < 3; ++K)
            if (getHaydnFlexVariantForSlot(OverChild->getOpcode(), K, *MCInfo)) {
              OverSlot = K;
              break;
            }
        }
      }
      HaydnMCFlags::setHaydnSlot(*OverChild, OverSlot);
      MCInst *SlotsO[3] = {nullptr, nullptr, nullptr};
      SlotsO[OverSlot] = OverChild;
      MCInst OverMCB;
      OverMCB.setOpcode(Haydn::BUNDLE);
      for (unsigned K = 0; K < llvm::Haydn::ISSUE_SLOT_COUNT; ++K) {
        if (SlotsO[K])
          OverMCB.addOperand(MCOperand::createInst(SlotsO[K]));
        else {
          MCInst *NopInst = OutContext.createMCInst();
          NopInst->setOpcode(Haydn::NOP);
          OverMCB.addOperand(MCOperand::createInst(NopInst));
        }
      }
      registerSymbolicOperands(OverMCB);
      EmitToStreamer(*OutStreamer, OverMCB);
    }
    return;
  }

  // Check if this is a pseudo instruction that needs expansion
  MCInst TmpInst;
  if (lowerPseudoInstExpansion(MI, TmpInst)) {
    emitWrappedInst(TmpInst);
    return;
  }

  // Handle target-specific pseudo instructions that aren't auto-generated
  switch (MI->getOpcode()) {
  default:
    break;
  case Haydn::B: {
    // Expand B pseudo to BEQZ_W R0, target (R0 is always zero, so this
    // always branches). Phase 1b : the WIDE 48-bit form per
    // encoding_manual.md §5.5 (opcode 0x2C) replaces the legacy Haydn32 BEQZ.
    MCInst Tmp;
    Tmp.setOpcode(Haydn::BEQZ_W);
    // BEQZ_W: operand 0 = rs (GPR32), operand 1 = offset (brtarget_wide_i12)
    Tmp.addOperand(MCOperand::createReg(Haydn::R0));
    // Get the MBB target
    for (unsigned Idx = 0; Idx < MI->getNumOperands(); ++Idx) {
      if (MI->getOperand(Idx).isMBB()) {
        const MCSymbol *Sym = MI->getOperand(Idx).getMBB()->getSymbol();
        const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);
        Tmp.addOperand(MCOperand::createExpr(Expr));
        emitWrappedInst(Tmp);
        return;
      }
    }
    llvm_unreachable("B pseudo has no MBB operand");
  }
  case Haydn::RET: {
    // Expand RET pseudo to JALR_W R0, R15, 0
    // JALR_W rd, rs, target: jump to rs + target, store return address in rd.
    // rd = R0 (discard link address), rs = R15 (LR), target = 0 (no offset).
    // Phase 1a: route to the 48-bit WIDE form (JALR_W
    // encoding_manual.md §5.5 Class 001). The legacy Haydn32 FmtJR parcel is
    // being purged from CodeGen selection.
    MCInst Tmp;
    Tmp.setOpcode(Haydn::JALR_W);
    Tmp.addOperand(MCOperand::createReg(Haydn::R0));  // rd = R0 (discard)
    Tmp.addOperand(MCOperand::createReg(Haydn::R15)); // rs = R15 (LR)
    Tmp.addOperand(MCOperand::createImm(0));           // target = 0 (no offset)
    emitWrappedInst(Tmp);
    return;
  }
  case Haydn::BR_JT: {
    // Expand BR_JT pseudo to JALR_W R0, $addr, 0
    // BR_JT carries (GPR32:$addr, i32imm:$jt). The $addr operand holds the
    // loaded jump table entry (target address). $jt is the jump table index
    // used only for MCInst lowering / relocation; we emit JALR_W with the
    // register operand directly. / Phase 1a: 48-bit WIDE form.
    Register AddrReg = MI->getOperand(0).getReg();
    MCInst Tmp;
    Tmp.setOpcode(Haydn::JALR_W);
    Tmp.addOperand(MCOperand::createReg(Haydn::R0));     // rd = R0 (discard link)
    Tmp.addOperand(MCOperand::createReg(AddrReg));       // rs = target address
    Tmp.addOperand(MCOperand::createImm(0));             // offset = 0
    emitWrappedInst(Tmp);
    return;
  }
  case Haydn::PseudoCALLIndirect: {
    // Expand PseudoCALLIndirect (function-pointer call) to JALR_W R15, $rs, 0:
    // jump to the address in $rs, store the return address in R15 (LR).
    //
    // Kept as a pseudo through every MIR pass and lowered here at MC level
    // (mirrors RET/BR_JT) because raw JALR_W is isTerminator=1 (its RET use)
    // which would make the machine verifier reject a call — instructions
    // (ADJCALLSTACKUP, the return,...) follow a call in the same block.
    // a reg callee previously went through JAL, whose calltarget
    // operand cannot hold a register, so the printer dropped the target and
    // the ISS saw r0=0 -> self-loop on every fnptr call. PseudoCALLIndirect
    // carries (outs GPR32:$rd = R15), (ins GPR32:$rs = fnptr).
    // Phase 1a: 48-bit WIDE form.
    Register Rs = MI->getOperand(1).getReg();
    MCInst Tmp;
    Tmp.setOpcode(Haydn::JALR_W);
    Tmp.addOperand(MCOperand::createReg(Haydn::R15)); // rd = R15 (link)
    Tmp.addOperand(MCOperand::createReg(Rs));         // rs = function pointer
    Tmp.addOperand(MCOperand::createImm(0));          // offset = 0
    emitWrappedInst(Tmp);
    // Soft-zero after PseudoCALLIndirect is MIR (ExpandPseudos); no inject.
    return;
  }
  case Haydn::LOADI32: {
    // Expand LOADI32 pseudo. The operand-1 kind selects the expansion:
    // MO.isImm -> HaydnMatInt constant sequence (the original path).
    // MO.isMBB -> LUI + ADDI32_W block-address materialization.
    //
    // the isMBB path is produced by BranchRelaxation via
    // HaydnInstrInfo::insertIndirectBranch, which emits
    // LOADI32 scratch, <dest_addr>.addMBB(&NewDestBB)
    // JALR_W R0, scratch, 0
    // to relax an out-of-range branch into an indirect jump. The previous
    // implementation handled only MO.isImm and silently dropped the MBB
    // operand — the address load emitted NOTHING, so scratch held a stale
    // value and the jalr jumped to garbage (e.g. 0xFFFD0). The size model
    // (getInstSizeInBytes LOADI32 case) already assumes 12 bytes
    // (LUI+ADDI32_W) for non-imm operands, so the printer emitting 0 bytes
    // also broke the BranchRelaxation byte-accounting invariant.
    // The fix mirrors LOAD_ADDR's MBB-address expansion: emit the MBB's
    // block-label symbol via the canonical LUI (HI12) + ADDI32_W (LO20)
    // pair; the MC emitter attaches FIXUP_HAYDN_HI12 / FIXUP_HAYDN_LO20
    // (getExprFixupKind), so the reloc resolves at link/object time.
    Register DstReg = MI->getOperand(0).getReg();
    const MachineOperand &MO = MI->getOperand(1);

    if (MO.isImm()) {
      int64_t Imm = MO.getImm();
      HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(Imm);

      // Emit each instruction in the sequence. The first instruction uses
      // R0 as source; subsequent instructions use the destination register
      // (chaining). SLLI32/ORI32 also chain from the destination.
      for (size_t Idx = 0; Idx < Seq.size(); ++Idx) {
        const HaydnMatInt::Inst &Inst = Seq[Idx];
        MCInst Tmp;
        Tmp.setOpcode(Inst.Opc);

        switch (Inst.Opc) {
        default:
          // ADDI32, LUI, ADDI32_W: (rd, rs, imm). ADDI32_W is the 48-bit
          // wide-add variant (20-bit imm); its $rt/$rs operands are tied in
          // the.td Constraints, so the (rd, rs, imm) shape is identical.
          Tmp.addOperand(MCOperand::createReg(DstReg));
          Tmp.addOperand(
              MCOperand::createReg(Idx == 0 ? Haydn::R0 : DstReg));
          Tmp.addOperand(MCOperand::createImm(Inst.Imm));
          break;
        case Haydn::SLLI32:
          // SLLI32: (rd, rs, imm)
          Tmp.addOperand(MCOperand::createReg(DstReg));
          Tmp.addOperand(
              MCOperand::createReg(Idx == 0 ? Haydn::R0 : DstReg));
          Tmp.addOperand(MCOperand::createImm(Inst.Imm));
          break;
        case Haydn::ORI32:
          // ORI32: (rd, rs, imm)
          Tmp.addOperand(MCOperand::createReg(DstReg));
          Tmp.addOperand(MCOperand::createReg(DstReg));
          Tmp.addOperand(MCOperand::createImm(Inst.Imm));
          break;
        }
        emitWrappedInst(Tmp);
      }
    } else if (MO.isMBB()) {
      // materialize a relaxed-branch destination block's address.
      // LUI rd, sym + ADDI32_W rd, rd, sym — identical shape to the
      // isGlobal/isBlockAddress branches in LOAD_ADDR below. The MBB's
      // symbol is a block label; HI12/LO20 fixups are auto-created by the
      // MC emitter via getExprFixupKind.
      const MCSymbol *Sym = MO.getMBB()->getSymbol();
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);

      MCInst LuiInst;
      LuiInst.setOpcode(Haydn::LUI);
      LuiInst.addOperand(MCOperand::createReg(DstReg));
      LuiInst.addOperand(MCOperand::createReg(Haydn::R0));
      LuiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(LuiInst);

      MCInst AddiInst;
      AddiInst.setOpcode(Haydn::ADDI32_W);
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(AddiInst);
    }
    return;
  }
  case Haydn::LOAD_ADDR: {
    // Expand LOAD_ADDR pseudo for global addresses
    // Emits: LUI rd, symbol + ADDI32 rd, rd, symbol
    // The MC layer will automatically create HI20 fixup for LUI and LO16 fixup for ADDI32
    Register DstReg = MI->getOperand(0).getReg();
    const MachineOperand &MO = MI->getOperand(1);

    if (MO.isGlobal()) {
      const GlobalValue *GV = MO.getGlobal();
      MCSymbol *Sym = getSymbol(GV);
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);

      // Emit LUI with HI20 fixup (auto-created by MC layer based on opcode)
      MCInst LuiInst;
      LuiInst.setOpcode(Haydn::LUI);
      LuiInst.addOperand(MCOperand::createReg(DstReg));
      LuiInst.addOperand(MCOperand::createReg(Haydn::R0));
      LuiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(LuiInst);

      // Emit ADDI32 with LO16 fixup (auto-created by MC layer based on opcode)
      MCInst AddiInst;
      AddiInst.setOpcode(Haydn::ADDI32_W);
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(AddiInst);
    } else if (MO.isBlockAddress()) {
      // Block address (computed goto / label pointer)
      const BlockAddress *BA = MO.getBlockAddress();
      MCSymbol *Sym = GetBlockAddressSymbol(BA);
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);

      // Emit LUI with HI20 fixup
      MCInst LuiInst;
      LuiInst.setOpcode(Haydn::LUI);
      LuiInst.addOperand(MCOperand::createReg(DstReg));
      LuiInst.addOperand(MCOperand::createReg(Haydn::R0));
      LuiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(LuiInst);

      // Emit ADDI32 with LO16 fixup
      MCInst AddiInst;
      AddiInst.setOpcode(Haydn::ADDI32_W);
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(AddiInst);
    } else if (MO.isJTI()) {
      // Jump-table base (.LJTI label, lives in.rodata). Same HI20/LO16 pair
      // as globals — a bare ADDI32 only carries LO16 and cannot reach the
      // rodata address. Normally ExpandPseudos lowers this;
      // this branch keeps an in-bundle LOAD_ADDR/JTI from being silently
      // dropped if it ever reaches the printer.
      MCSymbol *Sym = GetJTISymbol(MO.getIndex());
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);

      MCInst LuiInst;
      LuiInst.setOpcode(Haydn::LUI);
      LuiInst.addOperand(MCOperand::createReg(DstReg));
      LuiInst.addOperand(MCOperand::createReg(Haydn::R0));
      LuiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(LuiInst);

      MCInst AddiInst;
      AddiInst.setOpcode(Haydn::ADDI32_W);
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createReg(DstReg));
      AddiInst.addOperand(MCOperand::createExpr(Expr));
      emitWrappedInst(AddiInst);
    }
    return;
  }
  case Haydn::ADJCALLSTACKDOWN:
  case Haydn::ADJCALLSTACKUP:
    // These are eliminated by HaydnFrameLowering::eliminateCallFramePseudoInstr
    // before AsmPrinter runs. If one reaches here it has a zero size and is a
    // no-op.
    return;
  case Haydn::VAEND:
    // Expanded (no-op erase) in HaydnExpandPseudos; residual = skip.
    return;
  case Haydn::VASTART:
  case Haydn::VACOPY:
    // W1.2: expanded pre-pack in HaydnExpandPseudos (withPostRAScratch).
    // Residual here means ExpandPseudos was disabled — fail closed rather
    // than re-introduce late layout growth.
    report_fatal_error(
        "HaydnAsmPrinter: VASTART/VACOPY must be expanded by "
        "HaydnExpandPseudos before pack (enable -haydn-enable-expand-pseudos)");
    return;
  case Haydn::MOV_GPR_TO_DR64:
  case Haydn::MOV_DR64_TO_GPR:
  case Haydn::LIBCALL_MUL64:
  case Haydn::LIBCALL_SDIV:
  case Haydn::LIBCALL_UDIV:
  case Haydn::LIBCALL_SREM:
  case Haydn::LIBCALL_UREM: {
    // These pseudo instructions should have been expanded by the legalizer/selector.
    // For MVB, they're no-ops since DR64/GPR32 transfers are handled by G_MERGE_VALUES
    // and libcalls are emitted as normal function calls.
    // Skip them to avoid encoding issues.
    return;
  }
  case Haydn::SETCBR_BEGIN:
  case Haydn::SETCBR_END: {
    // Circular-buffer setup: expand the SETCBR_* pseudo to a `csrw_w <addr>
    // rs`. There is no dedicated SETCBR opcode — the CBR boundaries ARE CSRs
    // (Rev 2 manual: 2 CBR sets, no CBR_SIZE register):
    // cbr_sel=0 -> CBR_BEGIN=0x2C, CBR_END=0x2D
    // cbr_sel=1 -> CBR_BEGIN=0x2E, CBR_END=0x2F
    // The pseudo carries (cbr_sel imm, value GPR32). Mirrors Hexagon's
    // `m0=rN; cs0=rN` boundary setup, but via the existing CSR space.
    //
    // Phase 1c: route to the 48-bit WIDE CSRW_W (§5.10, opcode 0x81)
    // instead of the legacy 32-bit Haydn32 CSRW (FmtCSR). The WIDE form is the
    // spec-aligned CSR write encoding; the legacy FmtCSR def remains in the
    // td for Phase 3 deletion but is no longer selected by CodeGen.
    assert(MI->getOperand(0).isImm() && "SETCBR: cbr_sel must be immediate");
    unsigned CbrSel = MI->getOperand(0).getImm();
    assert((CbrSel == 0 || CbrSel == 1) && "SETCBR: cbr_sel must be 0 or 1");
    unsigned ValReg = MI->getOperand(1).getReg();

    // CSR address: BEGIN base 0x2C, END base 0x2D; set 1 adds +2.
    unsigned CsrBase =
        (MI->getOpcode() == Haydn::SETCBR_BEGIN) ? 0x2C : 0x2D;
    unsigned CsrAddr = CsrBase + (CbrSel << 1);

    // CSRW_W's MCInst operand order (Fmt48_WideCSR, §5.10) is [uimm8, rt] with
    // no defs. uimm8 is the CSR address (bits[47:40]); rt is the source GPR
    // (bits[15:12]). The legacy FmtCSR decoder's dead $rd def is gone in the
    // WIDE form — no R0 placeholder needed.
    MCInst CSRWInst;
    CSRWInst.setOpcode(Haydn::CSRW_W);
    CSRWInst.addOperand(MCOperand::createImm(CsrAddr));  // uimm8 (CSR address)
    CSRWInst.addOperand(MCOperand::createReg(ValReg));   // rt (source GPR)
    emitWrappedInst(CSRWInst);
    return;
  }
  case Haydn::LoopStart: {
    // LoopStart is the ZOL setup pseudo from IR HardwareLoops (via
    // GlobalISel). Operands: $src (trip-count GPR32), $adj (simm6).
    //
    // AIE-aligned geometry: HWLR_BEGIN = first real of *header*, HWLR_END =
    // last real of *latch* (inclusive). Single-BB ZOL has header == latch.
    // Multi-BB: PseudoLoopEnd lives on the latch (not necessarily a direct
    // preheader successor) — BFS from the header to find it (previously
    // only scanned direct successors, which forced TTI multi-BB reject).
    assert(MI->getOperand(0).isReg() && "LoopStart: src must be register");
    assert(MI->getOperand(1).isImm() && "LoopStart: adj must be immediate");

    unsigned Rs = MI->getOperand(0).getReg();

    MachineBasicBlock *Preheader =
        const_cast<MachineBasicBlock *>(MI->getParent());
    MachineBasicBlock *Header = nullptr;
    MachineBasicBlock *Latch = nullptr;

    // Header: preferred unique successor of preheader; else layout next.
    if (Preheader->succ_size() == 1)
      Header = *Preheader->succ_begin();
    else {
      for (MachineBasicBlock *Succ : Preheader->successors()) {
        Header = Succ;
        break;
      }
    }
    if (!Header)
      Header = Preheader->getNextNode();

    // Latch: BFS from header for PseudoLoopEnd (bounded).
    if (Header) {
      SmallVector<MachineBasicBlock *, 8> Work;
      SmallPtrSet<MachineBasicBlock *, 16> Seen;
      Work.push_back(Header);
      Seen.insert(Header);
      while (!Work.empty() && !Latch) {
        MachineBasicBlock *BB = Work.pop_back_val();
        for (const MachineInstr &TermMI : BB->terminators()) {
          if (TermMI.getOpcode() == Haydn::PseudoLoopEnd) {
            Latch = BB;
            break;
          }
        }
        if (Latch)
          break;
        for (MachineBasicBlock *Succ : BB->successors()) {
          if (Seen.insert(Succ).second && Seen.size() < 64)
            Work.push_back(Succ);
        }
      }
    }
    if (!Latch)
      Latch = Header; // single-BB / degenerate fallback

    // fully peeled ZOL body — skip emit (no END label site).
    if (!Header || !getLastRealInstr(Header) || !getLastRealInstr(Latch))
      return;

    MCSymbol *StartSym = getOrCreateHwloopStartSym(Header);
    // HWLR_END inclusive = last real instruction of the latch.
    MCSymbol *EndSym = getOrCreateHwloopEndSym(Latch);

    // Sel=1 for innermost (ZOL). Trip count already in GPR (SET_HWLOOP_F2_W).
    emitHWLoopWideInst(/*Sel=*/1, StartSym, EndSym, /*CntImm=*/std::nullopt,
                       Rs);
    return;
  }
  case Haydn::PseudoLoopEnd:
    // PseudoLoopEnd is a meta instruction (isMeta=1). It carries the
    // loop-body MBB for analyzeBranch round-trip but emits NO bytes — the
    // SET_HWLOOP_REG (emitted from LoopStart above) already encodes the
    // start/end offsets. Just drop it.
    return;
  case Haydn::LoopDec:
    // JNZD LoopDec is lowered to a plain SUBI32 (counter -= 1).
    // The LoopJNZ that follows checks the result. This is the software-managed
    // loop model for outer nested loops.
    assert(MI->getOperand(0).isReg() && MI->getOperand(1).isReg());
    EmitToStreamer(*OutStreamer,
                   MCInstBuilder(Haydn::SUBI32)
                       .addReg(MI->getOperand(0).getReg())
                       .addReg(MI->getOperand(1).getReg())
                       .addImm(1));
    return;
  case Haydn::LoopJNZ: {
    // JNZD LoopJNZ is lowered to BNEZ_W (branch if counter != 0).
    // Phase 1b : the WIDE 48-bit form per encoding_manual.md §5.5
    // (opcode 0x2D) replaces the legacy Haydn32 BNEZ.
    assert(MI->getOperand(0).isReg() && MI->getOperand(1).isMBB());
    const MCExpr *BranchTarget =
        MCSymbolRefExpr::create(MI->getOperand(1).getMBB()->getSymbol(),
                                OutContext);
    MCInst Tmp;
    Tmp.setOpcode(Haydn::BNEZ_W);
    Tmp.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
    Tmp.addOperand(MCOperand::createExpr(BranchTarget));
    EmitToStreamer(*OutStreamer, Tmp);
    return;
  }
  case Haydn::SET_HWLOOP: {
    // Hardware loop setup with immediate trip count.
    // The SET_HWLOOP pseudo carries: sel, loop_start MBB, loop_end MBB, cnt.
    // The hardware semantics:
    // HWLR_BEGIN[sel] = PC + (offset1 << 2) / loop start address
    // HWLR_END[sel] = PC + (offset2 << 2) / loop end address
    // HWLR_COUNT[sel] = cnt / iteration count
    //
    // The ISA only has a register-based HWLOOP instruction (SET_HWLOOP_REG
    // in HWLRRRR format). For immediate counts, emit:
    // ADDI32 scratch, R0, cnt (load immediate into scratch register)
    // SET_HWLOOP_REG sel, loop_start, loop_end, scratch
    assert(MI->getOperand(0).isImm() && "SET_HWLOOP: sel must be immediate");
    assert(MI->getOperand(1).isMBB() && "SET_HWLOOP: loop_start must be MBB");
    assert(MI->getOperand(2).isMBB() && "SET_HWLOOP: loop_end must be MBB");
    assert(MI->getOperand(3).isImm() && "SET_HWLOOP: cnt must be immediate");

    unsigned Sel = MI->getOperand(0).getImm();
    MachineBasicBlock *StartMBB = MI->getOperand(1).getMBB();
    MachineBasicBlock *EndMBB = MI->getOperand(2).getMBB();
    int64_t Cnt = MI->getOperand(3).getImm();

    // Fix for G13 (.LBB_-1): the HWLoop pass captures START=Header, END=Latch
    // from the loop structure, but a later pass (block-placement or the post-RA
    // scheduler) can merge the preheader+header+latch+exit into one block or
    // renumber the referenced MBBs out of the function's MBB list
    // (getNumber < 0). In that case the symbol renders as ".LBB<fn>_-1"
    // (invalid). : always use a per-instance temp START symbol
    // (getOrCreateHwloopStartSym) referenced via PC-relative fixup, mirroring
    // the inclusive-END mechanism. emitInstruction emits it — preceded by a
    // 4-byte align pad — at the body's first real instruction, so HWLR_BEGIN
    // is ÷4-representable regardless of intervening 2/6-byte parcels (the
    // c8013cb34c4d SET-only pad was insufficient; a hoisted 6-byte WIDE
    // parcel between SET and body still left the stream at 2-mod-4).
    //
    // For END (inclusive), if the captured Latch MBB was renumbered out
    // fall back to the current block (the merged block IS the latch).
    MachineBasicBlock *StartBody =
        (StartMBB->getNumber() < 0 || StartMBB == MI->getParent())
            ? const_cast<MachineBasicBlock *>(MI->getParent())
            : StartMBB;
    MachineBasicBlock *LatchMBB =
        (EndMBB->getNumber() < 0) ? const_cast<MachineBasicBlock *>(MI->getParent())
                                  : EndMBB;
    // Closed rule: never create START/END temp symbols unless the body will
    // emit real instructions that flush them. Otherwise MC aborts with
    // "Undefined temporary symbol". FixupHwLoops must demote range-bad ZOLs;
    // this is the emit-side half of the same contract.
    if (!getLastRealInstr(StartBody) || !getLastRealInstr(LatchMBB))
      return;
    MCSymbol *StartSym = getOrCreateHwloopStartSym(StartBody);
    MCSymbol *EndSym = getOrCreateHwloopEndSym(LatchMBB);

    // Emit a single 48-bit WIDE SET_HWLOOP_W (§5.11 all-immediate: uimm16_cnt
    // + uimm6_off1 + uimm12_off2 + hwlr_sel). The count must fit uimm16
    // (0..65535); the IR-level HardwareLoops pass (HaydnHardwareLoops.cpp
    // range-overflow check, NumHWLoopRangeOverflow statistic) declines
    // conversion for pathological loops whose body exceeds the uimm12 END
    // field, and a uimm16 count overflow is the same class of guard. If a
    // count outside 0..65535 somehow reaches here, there is no encoding for
    // it — fail loudly (the OLD R12-spill + SET_HWLOOP_REG placeholder
    // fallback was removed along with the OLD path).
    if (Cnt >= 0 && Cnt <= 0xFFFF) {
      emitHWLoopWideInst(Sel, StartSym, EndSym,
                         /*CntImm=*/std::optional<int64_t>(Cnt));
      return;
    }
    report_fatal_error("SET_HWLOOP: trip count out of uimm16 range "
                       "(no WIDE encoding; OLD placeholder path removed)");
  }
  case Haydn::SET_HWLOOP_REG: {
    // Hardware loop setup with register trip count.
    // The SET_HWLOOP_REG pseudo carries: sel, loop_start MBB, loop_end MBB, rs.
    // Same as SET_HWLOOP but trip count comes from a register.
    assert(MI->getOperand(0).isImm() && "SET_HWLOOP_REG: sel must be immediate");
    assert(MI->getOperand(1).isMBB() && "SET_HWLOOP_REG: loop_start must be MBB");
    assert(MI->getOperand(2).isMBB() && "SET_HWLOOP_REG: loop_end must be MBB");
    assert(MI->getOperand(3).isReg() && "SET_HWLOOP_REG: rs must be register");

    unsigned Sel = MI->getOperand(0).getImm();
    MachineBasicBlock *StartMBB = MI->getOperand(1).getMBB();
    MachineBasicBlock *EndMBB = MI->getOperand(2).getMBB();
    unsigned Rs = MI->getOperand(3).getReg();

    // Fix for G13 (.LBB_-1): see SET_HWLOOP case above for rationale. :
    // route START through getOrCreateHwloopStartSym (per-instance temp symbol
    // emitted aligned at the body's first real instr) so HWLR_BEGIN is ÷4
    // representable. When the captured Header/Latch MBBs were renumbered/merged
    // out (getNumber < 0) or the Header is the same block as this
    // instruction, fall back to the current block.
    MachineBasicBlock *StartBody =
        (StartMBB->getNumber() < 0 || StartMBB == MI->getParent())
            ? const_cast<MachineBasicBlock *>(MI->getParent())
            : StartMBB;
    // HWLR_END is INCLUSIVE (address of the latch's last real body
    // instruction). The post-RA pass passes the Latch MBB as the END operand.
    // When the Latch was merged/renumbered out, use the current block.
    MachineBasicBlock *LatchMBB =
        (EndMBB->getNumber() < 0) ? const_cast<MachineBasicBlock *>(MI->getParent())
                                  : EndMBB;
    // Same closed rule as SET_HWLOOP: no symbols without a real body.
    if (!getLastRealInstr(StartBody) || !getLastRealInstr(LatchMBB))
      return;
    MCSymbol *StartSym = getOrCreateHwloopStartSym(StartBody);
    MCSymbol *EndSym = getOrCreateHwloopEndSym(LatchMBB);

    // Emit a single 48-bit WIDE SET_HWLOOP_F2_W (§5.12: rs(count) +
    // uimm6_off1 + uimm12_off2 + hwlr_sel). The offsets remain symbolic
    // PC-relative (loop_begin/end are forward refs); the count comes from Rs.
    emitHWLoopWideInst(Sel, StartSym, EndSym, /*CntImm=*/std::nullopt, Rs);
    return;
  }
  }

  // Skip remaining pseudo instructions that don't have expansions
  if (MI->isPseudo()) {
    return;
  }

  // Regular instruction emission
  MCInstLowering.Lower(MI, TmpInst);

  // ST32 with DR64 data is a selector bug — do not silently rewrite
  // in the printer (sizes/sched already fixed on the wrong opcode). Fail closed.
  if (TmpInst.getOpcode() == Haydn::ST32 && TmpInst.getNumOperands() >= 1 &&
      TmpInst.getOperand(0).isReg()) {
    const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
    unsigned Reg = TmpInst.getOperand(0).getReg();
    if (TRI->getRegClass(Haydn::DR64RegClassID)->contains(Reg)) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnAsmPrinter: ST32 with DR64 source (selector must emit ST64). "
            "MI:\n";
      MI->print(OS);
      report_fatal_error(Twine(OS.str()));
    }
  }

  emitWrappedInst(TmpInst);
  // Soft-zero after JAL/JAL_W is MIR (ExpandPseudos); no post-call inject.
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnAsmPrinter() { // NOLINT
  RegisterAsmPrinter<HaydnAsmPrinter> X(getTheHaydnTarget());
}

// Pseudo instruction expansion (auto-generated, needs class definition)
#include "HaydnGenMCPseudoLowering.inc"
