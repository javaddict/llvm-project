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
#include "HaydnBundle.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-asm-printer"

// HiFi-like SMS bounds in product -S (observe-only; default ON).
static cl::opt<bool> HaydnAsmSWPS(
    "haydn-asm-swps", cl::Hidden, cl::init(true),
    cl::desc("Emit #<swps> SMS ResMII/RecMII/II comments on pipelined loop "
             "kernels in assembly (default ON)"));

// B4.5: spill/reload KPI observe on hot kernels (fail-open; default ON).
// Greppable in release -S via #<spill-kpi>; no schedule change.
static cl::opt<bool> HaydnAsmSpillKPI(
    "haydn-asm-spill-kpi", cl::Hidden, cl::init(true),
    cl::desc("Emit #<spill-kpi> spill/reload byte counts per function in "
             "assembly (default ON; observe-only, fail-open for BF4 close)"));

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

/// emitComments - Pretty-print spill/reload comments for bundled instructions.
/// Port of AIEBaseAsmPrinter.cpp:97-126 (N-byte Spill/Reload via
/// getSpillSize/getRestoreSize/getFolded*).
static void emitSpillReloadComments(const MachineInstr *MI,
                                    raw_ostream &CommentOS) {
  auto *MF = MI->getMF();
  auto *TII = MF->getSubtarget().getInstrInfo();

  // We assume a single instruction only has a spill or reload, not both.
  std::optional<LocationSize> Size;
  if ((Size = MI->getRestoreSize(TII))) {
    CommentOS << Size->getValue() << "-byte Reload";
  } else if ((Size = MI->getFoldedRestoreSize(TII))) {
    if (!Size->hasValue())
      CommentOS << "Unknown-size Folded Reload";
    else if (Size->getValue())
      CommentOS << Size->getValue() << "-byte Folded Reload";
  } else if ((Size = MI->getSpillSize(TII))) {
    CommentOS << Size->getValue() << "-byte Spill";
  } else if ((Size = MI->getFoldedSpillSize(TII))) {
    if (!Size->hasValue())
      CommentOS << "Unknown-size Folded Spill";
    else if (Size->getValue())
      CommentOS << Size->getValue() << "-byte Folded Spill";
  }

  // Check for spill-induced copies (AIEBaseAsmPrinter.cpp:123-125).
  if (MI->getAsmPrinterFlag(MachineInstr::ReloadReuse))
    CommentOS << " Reload Reuse";
}

void HaydnAsmPrinter::emitSpillKPIComments() {
  if (!HaydnAsmSpillKPI || !MF)
    return;

  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  unsigned SpillCount = 0;
  unsigned ReloadCount = 0;
  uint64_t SpillBytes = 0;
  uint64_t ReloadBytes = 0;

  auto BestEffortMemBytes = [](const MachineInstr &MI) -> uint64_t {
    if (!MI.memoperands_empty()) {
      LocationSize S = (*MI.memoperands_begin())->getSize();
      if (S.hasValue())
        return S.getValue().getFixedValue();
    }
    // Haydn PEI CSR path often omits MMO; ST32/LD32 = 4, ST64/LD64 = 8.
    // Opcode may be setDesc member (*_S0/_S1/_S2); use mayLoad width via
    // operand 0 register class when possible.
    if (MI.getNumOperands() >= 1 && MI.getOperand(0).isReg()) {
      Register R = MI.getOperand(0).getReg();
      if (R.isPhysical()) {
        const TargetRegisterInfo *TRI =
            MI.getMF()->getSubtarget().getRegisterInfo();
        if (const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(R)) {
          if (RC == &Haydn::DR64RegClass)
            return 8;
        }
      }
    }
    return 4;
  };

  auto CountMI = [&](const MachineInstr &MI) {
    bool CountedSpill = false;
    bool CountedReload = false;
    if (std::optional<LocationSize> Size = MI.getSpillSize(TII)) {
      ++SpillCount;
      CountedSpill = true;
      if (Size->hasValue())
        SpillBytes += Size->getValue().getFixedValue();
    } else if (std::optional<LocationSize> Size = MI.getFoldedSpillSize(TII)) {
      if (Size->hasValue() && Size->getValue().getKnownMinValue()) {
        ++SpillCount;
        CountedSpill = true;
        SpillBytes += Size->getValue().getFixedValue();
      }
    }
    if (std::optional<LocationSize> Size = MI.getRestoreSize(TII)) {
      ++ReloadCount;
      CountedReload = true;
      if (Size->hasValue())
        ReloadBytes += Size->getValue().getFixedValue();
    } else if (std::optional<LocationSize> Size =
                   MI.getFoldedRestoreSize(TII)) {
      if (Size->hasValue() && Size->getValue().getKnownMinValue()) {
        ++ReloadCount;
        CountedReload = true;
        ReloadBytes += Size->getValue().getFixedValue();
      }
    }

    // HaydnFrameLowering PEI CSR save/restore emits FrameSetup ST* /
    // FrameDestroy LD* with SP/scratch base and often without FixedStack
    // MMO (unlike storeRegToStackSlot). getSpillSize/getRestoreSize miss
    // those; soft-count them so hot-kernel KPI observes CSR traffic
    // (fail-open observe only — not a packing authority).
    if (!CountedSpill && MI.getFlag(MachineInstr::FrameSetup) &&
        MI.mayStore() && !MI.mayLoad()) {
      ++SpillCount;
      SpillBytes += BestEffortMemBytes(MI);
    }
    if (!CountedReload && MI.getFlag(MachineInstr::FrameDestroy) &&
        MI.mayLoad() && !MI.mayStore()) {
      ++ReloadCount;
      ReloadBytes += BestEffortMemBytes(MI);
    }
  };

  for (const MachineBasicBlock &MBB : *MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isBundle()) {
        // Count children (BUNDLE header itself is not a memory op).
        // AIEBaseAsmPrinter.cpp:149-156 walks isInsideBundle the same way.
        MachineBasicBlock::const_instr_iterator I = ++MI.getIterator();
        for (MachineBasicBlock::const_instr_iterator E = MBB.instr_end();
             I != E && I->isInsideBundle(); ++I) {
          if (I->isDebugInstr() || I->isMetaInstruction())
            continue;
          CountMI(*I);
        }
      } else if (!MI.isMetaInstruction() && !MI.isDebugInstr()) {
        CountMI(MI);
      }
    }
  }

  // Haydn comment string is "//"; tag includes #<spill-kpi> for grepping.
  // Patterned after #<swps> (HaydnAsmPrinter.cpp emitSMSSWPSComments).
  OutStreamer->emitRawComment(
      " #<spill-kpi> @" + MF->getName() + " spills=" + Twine(SpillCount) +
          " spill-bytes=" + Twine(SpillBytes) +
          " reloads=" + Twine(ReloadCount) +
          " reload-bytes=" + Twine(ReloadBytes),
      /*TabPrefix=*/false);
}

void HaydnAsmPrinter::emitFunctionBodyStart() {
  // Function-level spill/reload KPI before the first BB (release -S greppable).
  emitSpillKPIComments();
}

void HaydnAsmPrinter::emitSMSSWPSComments(const MachineBasicBlock &MBB) {
  if (!HaydnAsmSWPS || !MF)
    return;
  const auto *HMFI = MF->getInfo<HaydnMachineFunctionInfo>();
  if (!HMFI)
    return;
  const auto *Info = HMFI->getSMSLoop(&MBB);
  if (!Info)
    return;

  // Achieved II ≈ number of Bundle128 parcels (issue cycles) in the kernel.
  unsigned AchievedII = 0;
  for (const MachineInstr &MI : MBB) {
    if (MI.isBundle())
      ++AchievedII;
    else if (!MI.isMetaInstruction() && !MI.isDebugInstr() &&
             !MI.isCFIInstruction() && !MI.isImplicitDef() && !MI.isKill() &&
             !MI.isInlineAsm())
      // Unbundled real MI still issues as one parcel on Haydn.
      ++AchievedII;
  }
  if (AchievedII == 0)
    AchievedII = Info->ScheduledII;

  unsigned Res = Info->ResMII;
  unsigned Rec = Info->RecMII;
  unsigned Bound = Res > Rec ? Res : Rec;
  const char *Verdict = "dual-limited";
  if (AchievedII > Bound + 0)
    Verdict = "schedule-limited";
  else if (Res > Rec)
    Verdict = "resource-limited";
  else if (Rec > Res)
    Verdict = "recurrence-limited";

  // Haydn comment string is "//"; tag includes #<swps> for HiFi-like grepping.
  // emitRawComment → "// #<swps> …"
  OutStreamer->emitRawComment(
      " #<swps> loop bb." + Twine(MBB.getNumber()) + " @" + MF->getName(),
      /*TabPrefix=*/false);
  OutStreamer->emitRawComment(
      " #<swps> II=" + Twine(Info->ScheduledII) +
          " cycles per pipeline stage (SMS schedule)",
      /*TabPrefix=*/false);
  OutStreamer->emitRawComment(
      " #<swps> stages=" + Twine(Info->StageCount), /*TabPrefix=*/false);
  OutStreamer->emitRawComment(
      " #<swps> ops=" + Twine(Info->NumOps) + " (non-meta at SMS)",
      /*TabPrefix=*/false);
  OutStreamer->emitRawComment(" #<swps> ResMII=" + Twine(Res),
                              /*TabPrefix=*/false);
  OutStreamer->emitRawComment(" #<swps> RecMII=" + Twine(Rec),
                              /*TabPrefix=*/false);
  OutStreamer->emitRawComment(
      " #<swps> MII=max(res,rec)=" + Twine(Info->MII), /*TabPrefix=*/false);
  OutStreamer->emitRawComment(
      " #<swps> AchievedII=" + Twine(AchievedII) + " (kernel parcels)",
      /*TabPrefix=*/false);
  OutStreamer->emitRawComment(" #<swps> verdict=" + Twine(Verdict),
                              /*TabPrefix=*/false);
}

void HaydnAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  AsmPrinter::emitBasicBlockStart(MBB);
  emitSMSSWPSComments(MBB);
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
  // Product encode path is Bundle128 only (16 B parcels). Standalone MCInst
  // is streamed; HaydnMCCodeEmitter serialize-only path builds a transient
  // BUNDLE128_FULL for residual logical/hand-asm (B3.5). CodeGen bundles
  // already emit Format->Opcode composite. registerSymbolicOperands for Expr.
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
  // Align the SET parcel to Bundle128 (16 B) before emit. HWLoop offsets are
  // PC-relative to the SET address (FIXUP_HAYDN_HWLoopOff1/2). A.6: never
  // request sub-parcel Align(4) pads — writeNopData only accepts 16 B multiples.
  OutStreamer->emitCodeAlignment(Align(16), &getSubtargetInfo());
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
      // A.6: Bundle128 pad only (16 B); implies 4-byte PC for ÷4 HWLoop fixups.
      OutStreamer->emitCodeAlignment(Align(16), &getSubtargetInfo());
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
        // A.6: 16 B Bundle128 parcels only (covers ÷4 HWLoop END alignment).
        OutStreamer->emitCodeAlignment(Align(16), &getSubtargetInfo());
        for (MCSymbol *Sym : It->second)
          OutStreamer->emitLabel(Sym);
        It->second.clear();
      }
    }
  }
  // Handle VLIW bundles: AIEBaseAsmPrinter.cpp:128-184 peer (B3.4).
  //
  //   Bundle.add(children) → getFormatOrNull → for each slot:
  //     Bundle.at(Slot) or NOP → lower → MC operand of composite
  //
  // Placement is Haydn::Bundle SlotMap from member Desc getSlotKind
  // (AIEBaseMCFormats.cpp:66-75; AIEBundle.h:92-145) after B3.1 setDesc /
  // B3.3 AltDesc clear. Residual multi-slot logicals still use Bundle
  // pickSlot tryAdd (same as canAdd authority) — not a third printer
  // re-slot path.
  //
  // Deleted vs pre-B3.4 (plan §1.2 / §3.1 third-authority ban):
  //   * MCFlags get/set slot re-slot in this path
  //   * Flex name-suffix auction / flex-variant-for-slot re-place
  //   * emergency multi-parcel overflow split + split STATISTIC
  //   * strict-bundles soft-off escape (always fail-closed)
  //
  // B3.5: composite MC opcode is Format->Opcode (AIEBaseAsmPrinter.cpp:161-164).
  // Product sole live row is BUNDLE128_FULL; N-format-ready via Format table
  // (no second product row). Encode operand order is S0-S1-S2 (BUNDLE128_FULL
  // dag); MIR field order is Format.getSlots() S2→S1→S0 (B3.2). Keep
  // BUNDLE-child pseudo expands (B/RET/SETCBR/BR_JT/CALL/LOADI32/LOAD_ADDR).
  if (MI->isBundle()) {
    // Multi-parcel address materializers (LOADI32 MBB from BranchRelaxation,
    // LOAD_ADDR) expand to LUI+ADDI32_W (2+ Bundle128 parcels). FinalizeBundle
    // wraps them as singleton BUNDLEs. The generic isPseudo→continue path used
    // to drop them silently so the following JALR_W jumped with a stale
    // scratch (BAD_PC into .data / garbage — cf_branches, yarpgen seeds).
    // Expand those singleton BUNDLEs via the standalone switch and return.
    {
      unsigned Expandable = 0, OtherReal = 0;
      const MachineInstr *OnlyExpand = nullptr;
      for (MachineBasicBlock::const_instr_iterator I = ++MI->getIterator(),
                                                   E = MI->getParent()->instr_end();
           I != E && I->isInsideBundle(); ++I) {
        if (I->isDebugInstr() || I->isImplicitDef() || I->isKill() ||
            I->isCFIInstruction())
          continue;
        unsigned Opc = I->getOpcode();
        if (Opc == Haydn::LOADI32 || Opc == Haydn::LOAD_ADDR) {
          ++Expandable;
          OnlyExpand = &*I;
          continue;
        }
        // Zero-size CodeGen pseudos that the child loop also skips.
        if (I->isPseudo() && Opc != Haydn::B && Opc != Haydn::RET &&
            Opc != Haydn::BR_JT && Opc != Haydn::PseudoCALLIndirect &&
            Opc != Haydn::SETCBR_BEGIN && Opc != Haydn::SETCBR_END)
          continue;
        ++OtherReal;
      }
      if (Expandable == 1 && OtherReal == 0 && OnlyExpand) {
        emitInstruction(OnlyExpand);
        return;
      }
      if (Expandable > 0 && OtherReal > 0)
        report_fatal_error(
            "HaydnAsmPrinter: LOADI32/LOAD_ADDR packed with other ops in "
            "BUNDLE — multi-parcel expand cannot share a Bundle128 composite");
    }

    HaydnMCFormats Fmts;
    Haydn::MCBundle Bundle(&Fmts);

    // AIEBaseAsmPrinter.cpp:143-154: verbose per-slot Spill/Reload comments
    // on BUNDLE children (base AsmPrinter only comments the BUNDLE header).
    raw_ostream &CommentOS = OutStreamer->getCommentOS();
    const bool VerboseSpillComments = isVerbose();

    MachineBasicBlock::const_instr_iterator I = ++MI->getIterator();
    for (MachineBasicBlock::const_instr_iterator E =
             MI->getParent()->instr_end();
         I != E && I->isInsideBundle(); ++I) {
      // Skip debug / pure meta. B1.2 wraps *all* real and byte-emitting
      // pseudos as BUNDLE children (HaydnFinalizeBundle / AIE FinalizeBundle).
      // AsmPrinter must expand the same pseudos the standalone path expands
      // (B→BEQZ_W R0, RET→JALR_W, SETCBR→CSRW_W). Skipping Haydn::B as a
      // generic isPseudo() turned singleton BUNDLEs into all-NOP parcels —
      // branch to fallthrough deleted in the binary → MEMORY_FAULT.
      if (I->isDebugInstr() || I->isImplicitDef() || I->isKill() ||
          I->isCFIInstruction())
        continue;

      if (VerboseSpillComments)
        emitSpillReloadComments(&*I, CommentOS);

      // Allocate via MCContext so the child MCInst has stable lifetime.
      MCInst *ChildInst = OutContext.createMCInst();
      unsigned ChildOpc = I->getOpcode();
      if (ChildOpc == Haydn::SETCBR_BEGIN || ChildOpc == Haydn::SETCBR_END) {
        assert(I->getOperand(0).isImm() && "SETCBR: cbr_sel must be immediate");
        unsigned CbrSel = I->getOperand(0).getImm();
        assert((CbrSel == 0 || CbrSel == 1) && "SETCBR: cbr_sel must be 0 or 1");
        unsigned ValReg = I->getOperand(1).getReg();
        unsigned CsrBase =
            (ChildOpc == Haydn::SETCBR_BEGIN) ? 0x2Cu : 0x2Du;
        unsigned CsrAddr = CsrBase + (CbrSel << 1);
        ChildInst->setOpcode(Haydn::CSRW_W);
        ChildInst->addOperand(MCOperand::createImm(CsrAddr));
        ChildInst->addOperand(MCOperand::createReg(ValReg));
      } else if (ChildOpc == Haydn::B) {
        // Unconditional branch pseudo → BEQZ_W R0, target (same as
        // emitInstruction case Haydn::B). Silent skip left all-NOP parcels
        // (MEMORY_FAULT / wrong control flow vs pre-B1.2 codegen).
        ChildInst->setOpcode(Haydn::BEQZ_W);
        ChildInst->addOperand(MCOperand::createReg(Haydn::R0));
        bool GotTarget = false;
        for (const MachineOperand &MO : I->operands()) {
          if (!MO.isMBB())
            continue;
          const MCSymbol *Sym = MO.getMBB()->getSymbol();
          const MCExpr *Expr = MCSymbolRefExpr::create(Sym, OutContext);
          ChildInst->addOperand(MCOperand::createExpr(Expr));
          GotTarget = true;
          break;
        }
        if (!GotTarget)
          llvm_unreachable("B pseudo in BUNDLE has no MBB operand");
      } else if (ChildOpc == Haydn::RET) {
        ChildInst->setOpcode(Haydn::JALR_W);
        ChildInst->addOperand(MCOperand::createReg(Haydn::R0));
        ChildInst->addOperand(MCOperand::createReg(Haydn::R15));
        ChildInst->addOperand(MCOperand::createImm(0));
      } else if (ChildOpc == Haydn::BR_JT) {
        // Jump-table branch → JALR_W R0, addr, 0 (emitInstruction peer).
        Register AddrReg = I->getOperand(0).getReg();
        ChildInst->setOpcode(Haydn::JALR_W);
        ChildInst->addOperand(MCOperand::createReg(Haydn::R0));
        ChildInst->addOperand(MCOperand::createReg(AddrReg));
        ChildInst->addOperand(MCOperand::createImm(0));
      } else if (ChildOpc == Haydn::PseudoCALLIndirect) {
        // Fnptr call → JALR_W R15, rs, 0 (emitInstruction peer).
        Register Rs = I->getOperand(1).getReg();
        ChildInst->setOpcode(Haydn::JALR_W);
        ChildInst->addOperand(MCOperand::createReg(Haydn::R15));
        ChildInst->addOperand(MCOperand::createReg(Rs));
        ChildInst->addOperand(MCOperand::createImm(0));
      } else if (I->isPseudo()) {
        // Other CodeGen-only / zero-size pseudos: no Bundle128 child.
        continue;
      } else {
        // Desc-only lower (AIE serialize-only). Placement is post-setDesc
        // member identity / Bundle SlotMap (AIEBaseMCFormats.cpp:66-75).
        MCInstLowering.Lower(&*I, *ChildInst);
      }

      // AIE Bundle.add pre-condition: canAdd or fail closed (no emergency
      // multi-parcel split — AIE assert Format / plan §3.1).
      if (!Bundle.canAdd(ChildInst->getOpcode())) {
        std::string Msg;
        raw_string_ostream OS(Msg);
        OS << "HaydnAsmPrinter: oversubscribed Bundle128 (Bundle.canAdd "
              "failed) after pack — Desc-only placement, fail-closed (B3.4; "
              "AIEBaseAsmPrinter.cpp:162 assert Format peer). Fix PostRA "
              "placement. Bundle MIR:\n";
        MI->print(OS);
        report_fatal_error(Twine(OS.str()));
      }
      Bundle.add(ChildInst);
    }

    // Unsupported single child with no format/slot (standalone escape) must
    // not silently become a 3×NOP parcel. Post-B3.1 children are members or
    // expandable pseudos; residual gaps fail closed.
    if (Bundle.isStandalone()) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnAsmPrinter: standalone unsupported BUNDLE child (no "
            "getSlotKind / format) — refuse silent drop (B3.4). Bundle MIR:\n";
      MI->print(OS);
      report_fatal_error(Twine(OS.str()));
    }

    // AIE: const VLIWFormat *Format = Bundle.getFormatOrNull(); assert(Format);
    // Empty stall (all meta skipped) has OccupiedSlots==0; product
    // BUNDLE128_FULL covers every subset including empty (N-format-ready
    // table scan via getPacketFormats).
    const VLIWFormat *Format = Bundle.getFormatOrNull();
    if (!Format) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnAsmPrinter: no covering packet format for OccupiedSlots="
         << Bundle.getOccupiedSlots()
         << " (getFormatOrNull null; AIEBaseAsmPrinter.cpp:162-163). "
            "Bundle MIR:\n";
      MI->print(OS);
      report_fatal_error(Twine(OS.str()));
    }
    // AIEBaseAsmPrinter.cpp:161-164 — MCBundle.setOpcode(Format->Opcode).
    // Product sole live row is BUNDLE128_FULL (N-format-ready: when more
    // packet rows land, Format table selects Opcode; no hard-coded second
    // product path here).
    assert(Format->Opcode == Haydn::BUNDLE128_FULL &&
           "product live format must be BUNDLE128_FULL (table-ready for N)");

    MCInst MCB;
    MCB.setOpcode(Format->Opcode);

    // Emit in S0-S1-S2 encode order (BUNDLE128_FULL operand dag), not
    // Format.getSlots() S2→S1→S0 MIR field order. Empty slots → NOP
    // (AIE SlotInfo NOP peer; HaydnSlots NopOpc is 0 → Haydn::NOP).
    for (unsigned K = 0; K < llvm::Haydn::ISSUE_SLOT_COUNT; ++K) {
      MCSlotKind Slot = MCSlotKind(MCSlotKind::Haydn_SLOT_S0 +
                                   static_cast<int>(K));
      MCInst *Instr = Bundle.at(Slot);
      if (!Instr) {
        Instr = OutContext.createMCInst();
        // Product HaydnSlots NopOpc is 0; use public Haydn::NOP (encoder
        // materializes slot peer). N-format-ready: prefer SI->getNOPOpcode()
        // when tablegen fills per-slot NOPs (AIE SlotInfo NOP peer).
        unsigned NopOpc = Haydn::NOP;
        if (const MCSlotInfo *SI = Fmts.getSlotInfo(Slot)) {
          unsigned TableNop = SI->getNOPOpcode();
          if (TableNop != 0)
            NopOpc = TableNop;
        }
        Instr->setOpcode(NopOpc);
      }
      MCB.addOperand(MCOperand::createInst(Instr));
    }

    // Register each child's Expr operands (e.g. JAL call targets) so undefined
    // extern symbols make it into.symtab. See registerSymbolicOperands.
    registerSymbolicOperands(MCB);
    EmitToStreamer(*OutStreamer, MCB);
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
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
    // Must be SET_HWLOOP_{W,F2_W} before pack (HaydnExpandPseudos /
    // HaydnHardwareLoops). Printer is Desc-only for those forms.
    report_fatal_error(
        "HaydnAsmPrinter: residual SET_HWLOOP{,_REG} pseudo — expand to "
        "SET_HWLOOP_{W,F2_W} in ExpandPseudos before PostRA pack");
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
