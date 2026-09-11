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
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/bit.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnInstPrinter.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-asm-printer"

// : spill/reload KPI observe on hot kernels (fail-open; default ON).
// Greppable in release -S via #<spill-kpi>; no schedule change.
static cl::opt<bool> HaydnAsmSpillKPI(
    "haydn-asm-spill-kpi", cl::Hidden, cl::init(true),
    cl::desc("Emit #<spill-kpi> spill/reload byte counts per function in "
             "assembly (default ON; observe-only, fail-open observe-only)"));

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

void HaydnAsmPrinter::emitFunctionEntryLabel() {
  // W70.2r: function-entry alignment is pre-label MC fill
  // (HasFunctionAlignment=true → AsmPrinter::emitAlignment →
  // HaydnMCELFStreamer::emitCodeAlignment). Internal MBB alignment is
  // padInternalMBBAlignment in stamped addPreEmit LBN
  // (ClearMetadata=true after the closer). addPostBBSections is empty;
  // the MachineAlignment pass class is gone. This override does not
  // grow the committed stream: no emitAlignment, no MC fill in front
  // of the entry symbol.
  //
  // Serialize-only residue (zero bytes): promote sh_addralign so LLD
  // honors the requirement where the section start already satisfies it —
  // under the product -ffunction-sections link every entry sits at section
  // offset 0, so the promoted section alignment IS the guarantee. User
  // alignment (aligned(N)) stays a language guarantee, honored not capped.
  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  Align A = std::max(MF->getAlignment(), TLI->getMinFunctionAlignment());
  if (MaybeAlign FnAlign = MF->getFunction().getAlign())
    A = std::max(A, *FnAlign);
  if (A > Align(1) && TM.getTargetTriple().isOSBinFormatELF()) {
    // MCSectionELF has no classof (TargetLoweringObjectFileImpl.cpp:1001
    // static_cast peer); the triple check is the guard.
    auto *Sec = static_cast<MCSectionELF *>(MF->getSection());
    Sec->ensureMinAlignment(A);
  }
  AsmPrinter::emitFunctionEntryLabel();
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
    // Opcode may be a generated setDesc member; use mayLoad width via
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
  // Observe-only KPI comment (HiFi-like grepping).
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

void HaydnAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  AsmPrinter::emitBasicBlockStart(MBB);
}

void HaydnAsmPrinter::emitBasicBlockEnd(const MachineBasicBlock &MBB) {
  // Terminator-only / empty latch: emitInstruction never saw LastReal.
  // AIE unreachables "LoopEnd without last bundle" (AIEBaseAsmPrinter.cpp:64-70);
  // Haydn overlay: flush the MCInstLower temps at the current address so the
  // object does not fail with an undefined temporary symbol.
  auto Flush = [&](DenseMap<const MachineBasicBlock *, SmallVector<MCSymbol *, 2>>
                       &Pending) {
    auto It = Pending.find(&MBB);
    if (It == Pending.end() || It->second.empty())
      return;
    for (MCSymbol *Sym : It->second)
      OutStreamer->emitLabel(Sym);
    It->second.clear();
  };
  Flush(PendingHwloopEndLabels);
  Flush(PendingHwloopStartLabels);
  AsmPrinter::emitBasicBlockEnd(MBB);
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
  // Product encode path is Format E (12 B parcels). CodeGen bundles emit
  // Format E composite opcodes (BUNDLE_E96_*). Standalone MCInst is
  // already-concrete members / NOP; representation expansion is fatal
  // before this point. registerSymbolicOperands for Expr. No force-BUNDLE
  // NOP-pad and no multi-width bare emit. HI12/LO20 fixups bind from the
  // generated (TypeName,opcode)-keyed table in the emitter
  // (findFixupFromFixupFields).
  MCInst Out = Inst;
  registerSymbolicOperands(Out);
  EmitToStreamer(*OutStreamer, Out);
}

// : print-time fixed-R12 AT helpers removed. VASTART/VACOPY expand via
// withPostRAScratch (free GPR first; PostRAScratchFI only if spill needed).
// Final SET_HWLOOP_* forms are Desc-only via HaydnMCInstLower (no printer
// wide-inst dual path).

// late-MC: residual cycle-forming / multi-cycle / loop-control pseudos
// must not reach AsmPrinter. Shared law with VerifyBundles via
// haydn::bundle::isResidualCycleFormingPseudo. Printer is one-to-one
// serialization only (durable-rules §30; ).
[[noreturn]] static void
fatalResidualCyclePseudo(const MachineInstr *MI, const char *Why) {
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "HaydnAsmPrinter: residual cycle-forming pseudo (one-to-one MC "
        "ban) — "
     << Why << ". MI:\n";
  if (MI)
    MI->print(OS);
  // gen_crash_diag=false → non-zero exit for lit `not`, not abort.
  report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
}

// Find the last size-bearing instruction of \p MBB whose start address is
// the inclusive HWLR_END target (HWLR_END = address of the LAST instruction
// of the loop body). Prefer a non-terminator real / BUNDLE header; a
// terminator-only latch falls back to the last size-bearing terminator
// (including a terminator-flagged BUNDLE) so the MCInstLower temp is
// flushed. Empty/meta-only returns nullptr and emitBasicBlockEnd emits
// the leftover temps. Uses MachineBasicBlock::iterator (NOT instr_iterator)
// so a VLIW BUNDLE is one unit (the header AsmPrinter receives).
static MachineInstr *getLastRealInstr(MachineBasicBlock *MBB) {
  MachineInstr *LastTerm = nullptr;
  for (auto I = MBB->rbegin(), E = MBB->rend(); I != E; ++I) {
    MachineInstr &MI = *I;
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isPosition())
      continue;
    // Residual executable / unexpanded representation is fatal — printer
    // does not emit bytes for them. Encode-peel overlays (B / JALR_CALL /
    // JAL_TCO / JALR_TCO) emit catalog bits and are terminator fallbacks.
    if (!MI.isBundle() && MI.isPseudo()) {
      const unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::B || Opc == Haydn::JALR_CALL || Opc == Haydn::JAL_TCO ||
          Opc == Haydn::JALR_TCO) {
        if (!LastTerm)
          LastTerm = &MI;
        continue;
      }
      if (haydn::bundle::isResidualCycleFormingPseudo(Opc) ||
          haydn::bundle::isResidualExecutablePseudo(MI) ||
          haydn::bundle::isRepresentationExpandPseudo(Opc)) {
        fatalResidualCyclePseudo(
            &MI, "getLastRealInstr: residual executable or unexpanded "
                 "representation (must be exact-committed before layout; "
                 "no silent skip for END)");
      }
      continue;
    }
    // Skip terminator packets (including BUNDLE-of-B: isTerminator is
    // AnyInBundle) so inclusive END stays on the last body cycle, not the
    // software backedge. Terminator-only latches fall through to LastTerm.
    if (MI.isTerminator()) {
      if (!LastTerm)
        LastTerm = &MI;
      continue;
    }
    return &MI;
  }
  return LastTerm;
}

static const MachineInstr *getLastRealInstr(const MachineBasicBlock *MBB) {
  return getLastRealInstr(const_cast<MachineBasicBlock *>(MBB));
}

// Create a fresh inclusive-END MCSymbol for ONE hardware-loop instance whose
// latch is \p Latch. /AIE semantics: END address = last real body MI.
// Haydn implements that via PendingHwloopEndLabels flushed in emitInstruction
// immediately before that last real MI (: labels only — no streamer
// executable alignment pad; Fixup/exact-commit owns MIR pads). Not via
// setPreInstrSymbol. Per-instance symbols (vector) for shared-latch nested loops.
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
  // HWLR_BEGIN labels: emit pending START symbols at the first real body MI.
 // : no streamer executable alignment pad — product parcels are
  // already 16 B (÷4 PC for HWLoopOff); Fixup/exact-commit owns MIR pads.
  // Do not use setPreInstrSymbol for these symbols — parent AsmPrinter would
  // order PreInstr relative to other streamer ops.
  if (!PendingHwloopStartLabels.empty()) {
    const MachineBasicBlock *Parent = MI->getParent();
    auto It = PendingHwloopStartLabels.find(Parent);
    if (It != PendingHwloopStartLabels.end() && !It->second.empty() &&
        !MI->isMetaInstruction() && !MI->isDebugInstr()) {
      for (MCSymbol *Sym : It->second)
        OutStreamer->emitLabel(Sym);
      It->second.clear();
    }
  }

  // Inclusive-END: emit END labels at the last real body MI start address.
  // getLastRealInstr treats a VLIW BUNDLE header as the unit (matches
 // emitInstruction). : labels only — no streamer NOP injection.
  if (!PendingHwloopEndLabels.empty()) {
    const MachineBasicBlock *Parent = MI->getParent();
    auto It = PendingHwloopEndLabels.find(Parent);
    if (It != PendingHwloopEndLabels.end()) {
      const MachineInstr *LastReal = getLastRealInstr(Parent);
      if (LastReal == MI) {
        for (MCSymbol *Sym : It->second)
          OutStreamer->emitLabel(Sym);
        It->second.clear();
      }
    }
  }
  // Handle VLIW bundles. AIEBaseAsmPrinter.cpp:155-166 lowers each child
  // as-is into the stamped format opcode; unused slots get the slot NOP.
  // Haydn overlay: membership order is e0, e1, (e2). E2 vs E3 is the
  // stamped BUNDLE-root row only — never a child-count override or
  // missing-row E2 default. Do not re-run canAdd / PacketFormats, peel
  // _S*, or bind public logicals to members. Encoder peels B / JALR_CALL
  // / JAL_TCO / JALR_TCO (same Format E member pick as the D1.74 table).
  // Residual LOADI32/LOAD_ADDR/SETCBR/Loop* fail closed — no multi-cycle repair.
  if (MI->isBundle()) {
    // Fail closed on residual cycle-forming children (was multi-parcel expand).
    for (MachineBasicBlock::const_instr_iterator I = ++MI->getIterator(),
                                                 E = MI->getParent()->instr_end();
         I != E && I->isInsideBundle(); ++I) {
      if (I->isDebugInstr() || I->isImplicitDef() || I->isKill() ||
          I->isCFIInstruction())
        continue;
      if (haydn::bundle::isResidualCycleFormingPseudo(I->getOpcode()))
        fatalResidualCyclePseudo(
            &*I, "must be exact-committed real MIs before AsmPrinter "
                 "(no multi-cycle LOADI32/LOAD_ADDR; SETCBR→CSRW_W; "
                 "LoopDec/JNZ→SUBI32/BNEZ_W; LoopStart→SET_HWLOOP_*)");
    }

    SmallVector<MCInst *, 4> TypedKids;
    SmallVector<const MachineInstr *, 4> RawKids;

    // AIEBaseAsmPrinter.cpp:143-154: verbose per-slot Spill/Reload comments
    // on BUNDLE children (base AsmPrinter only comments the BUNDLE header).
    raw_ostream &CommentOS = OutStreamer->getCommentOS();
    const bool VerboseSpillComments = isVerbose();

    MachineBasicBlock::const_instr_iterator I = ++MI->getIterator();
    for (MachineBasicBlock::const_instr_iterator E =
             MI->getParent()->instr_end();
         I != E && I->isInsideBundle(); ++I) {
      // Skip debug / pure meta. Real children must already be generated
      // members or architectural NOP.
      if (I->isDebugInstr() || I->isImplicitDef() || I->isKill() ||
          I->isCFIInstruction())
        continue;
      RawKids.push_back(&*I);
    }
    // Pad NOP beside real work is CompletionState (Finalize already erases
    // it). A NOP-only BUNDLE is the architectural idle/stall parcel and
    // must encode — skipping it leaves AllEntriesReal vs 0 typed kids.
    auto isPadNop = [](unsigned Opc) {
      return haydn::format_e::logicalOpcodeOrSelf(Opc) == Haydn::NOP;
    };
    bool HasNonNop = false;
    for (const MachineInstr *Kid : RawKids) {
      if (!isPadNop(Kid->getOpcode())) {
        HasNonNop = true;
        break;
      }
    }

    // Compiler BUNDLE roots carry a product row. Children serialize
    // Desc-as-is (AIEBaseAsmPrinter.cpp:155-166). Residual public logicals
    // and unexpanded representation are fatal — no inverse-at-entry fill
    // and no keep-map.
    std::optional<haydn::bundle::BundleFormatRowID> Row =
        haydn::bundle::getBundleRowID(*MI);
    if (!Row)
      report_fatal_error(
          "HaydnAsmPrinter: BUNDLE missing BundleFormatRowID — refuse E2 "
          "default",
          /*GenCrashDiag=*/false);
    uint32_t UsedUnits = 0;
    const MCInstrInfo &MII = *MF->getSubtarget().getInstrInfo();

    for (const MachineInstr *I : RawKids) {
      if (HasNonNop && isPadNop(I->getOpcode()))
        continue;

      if (VerboseSpillComments)
        emitSpillReloadComments(&*I, CommentOS);

      // Allocate via MCContext so the child MCInst has stable lifetime.
      MCInst *ChildInst = OutContext.createMCInst();
      unsigned ChildOpc = I->getOpcode();
      const bool EncodePeel = ChildOpc == Haydn::B ||
                              ChildOpc == Haydn::JALR_CALL ||
                              ChildOpc == Haydn::JAL_TCO ||
                              ChildOpc == Haydn::JALR_TCO;
      if (EncodePeel) {
        // Wrap-only Finalize keeps B as a bundle child. Encoder peels
        // B → BEQZ rs=R0, JALR_CALL / JALR_TCO → JALR, JAL_TCO → JAL.
        MCInstLowering.Lower(&*I, *ChildInst);
      } else if (haydn::bundle::isRepresentationExpandPseudo(ChildOpc)) {
        fatalResidualCyclePseudo(
            &*I, "bundled residual unexpanded representation "
                 "(RET/BR_JT/PseudoCALLIndirect)");
      } else if (isPadNop(ChildOpc)) {
        // Architectural NOP (logical / generated member) is the
        // idle/pad opcode. TableGen may mark the logical as isPseudo; still
        // lower it. Unused windows and pad-only idle encode as zero-entry NOP.
        MCInstLowering.Lower(&*I, *ChildInst);
      } else if (I->isPseudo()) {
        fatalResidualCyclePseudo(
            &*I, "bundled residual executable pseudo (must be exact-committed "
                 "real member before AsmPrinter; no silent skip)");
      } else {
        // Desc-only lower (AIE serialize-only). Placement is post-setDesc
        // member identity / Bundle SlotMap (AIEBaseMCFormats.cpp:66-75).
        MCInstLowering.Lower(&*I, *ChildInst);
      }

      // Membership order is the composite operand order. E2/E3 is the
      // stamped bundle-root row. Already-private members serialize as-is.
      // Public logicals, leftover FieldSlot names, and extra-op never
      // reconstruct here.
      if (!isPadNop(ChildInst->getOpcode()) &&
          !haydnFindFormatEMemberByOpcode(ChildInst->getOpcode())) {
        const StringRef ChildName = MII.getName(ChildInst->getOpcode());
        // Encoder peels B / JALR_CALL / JAL_TCO / JALR_TCO onto generated members
        // before this check. Residual non-member children are fatal.
        if (haydnIsResidualFieldSlotName(ChildName) ||
            haydnIsGeneratedMemberName(ChildName))
          report_fatal_error(
              Twine("HaydnAsmPrinter: compiler BUNDLE child '") + ChildName +
                  "' is not a public logical — refuse FieldSlot / member-name "
                  "peel",
              /*GenCrashDiag=*/false);
        report_fatal_error(
            Twine("HaydnAsmPrinter: compiler BUNDLE child '") + ChildName +
                "' is not a generated Format E member — refuse skip-Finalize "
                "DFS / bag-sort",
            /*GenCrashDiag=*/false);
      }
      if (const haydn::format_e::FormatEMemberRec *Bound =
              haydnFindFormatEMemberByOpcode(ChildInst->getOpcode())) {
        if (Bound->Unit < 32) {
          if (UsedUnits & (1u << Bound->Unit))
            report_fatal_error(
                "HaydnAsmPrinter: compiler BUNDLE unit injectivity — refuse "
                "skip-Finalize DFS / bag-sort",
                /*GenCrashDiag=*/false);
          UsedUnits |= (1u << Bound->Unit);
        }
      }
      TypedKids.push_back(ChildInst);
    }

    // Product composite: E2 vs E3 is the stamped bundle-root row only.
    // NumEntries stays 0 until the row stamp selects a product composite —
    // never an E2 default for a missing row.
    unsigned CompositeOpc = 0;
    unsigned NumEntries = 0;
    if (TypedKids.size() >= 3 &&
        *Row != haydn::bundle::BundleFormatRowID::E96ThreeEntry)
      report_fatal_error(
          "HaydnAsmPrinter: BUNDLE child count exceeds stamped row — refuse "
          "E3 override",
          /*GenCrashDiag=*/false);
    if (TypedKids.size() > 3)
      report_fatal_error(
          "HaydnAsmPrinter: BUNDLE has more than 3 real children",
          /*GenCrashDiag=*/false);
    if (*Row == haydn::format::BundleFormatRowID::E96ThreeEntry) {
      CompositeOpc = Haydn::BUNDLE_E96_THREE_ENTRY;
      NumEntries = 3;
    } else if (*Row == haydn::format::BundleFormatRowID::E96TwoEntry) {
      CompositeOpc = Haydn::BUNDLE_E96_TWO_ENTRY;
      NumEntries = 2;
    } else {
      report_fatal_error(
          "HaydnAsmPrinter: non-product BundleFormatRowID on BUNDLE root",
          /*GenCrashDiag=*/false);
    }
    // Product emission gate: non-empty cycles authorize architectural NOP pad
    // into MC only under full-slot product-legal completion (AllEntriesReal).
    // Unqualified singleton/underfill stub IDs must never reach executable
    // NOP fill as a substitute for open underfill/top-pad answers. Missing
    // completion remains allowed on residual row-only MIR fixtures; product
    // Finalize always stamps full-slot completion before this point.
    if (auto Comp = haydn::bundle::getBundleCompletionID(*MI)) {
      // One census with Finalize/Verify: pad NOP is CompletionState, not a
      // membership entry. TypedKids keeps pad-only idle children so MC can
      // encode the architectural NOP parcel; do not use that vector as the
      // real-member count.
      const unsigned RealMembers = static_cast<unsigned>(
          haydn::bundle::collectBundleMemberOpcodes(*MI).size());
      const bool HasPadNop = haydn::bundle::bundleHasPadNop(*MI);
      // Logical children are still real encode work (hand MIR / pre-cutover).
      // collectBundleMemberOpcodes only sees generated members — do not
      // let a stub completion ride a non-empty TypedKids pack.
      if ((RealMembers > 0 || HasNonNop) &&
          haydn::bundle::isStubCompletion(*Comp))
        report_fatal_error(
            "HaydnAsmPrinter: unqualified stub CompletionStateID on "
            "non-empty BUNDLE — CompletionStateID does not match row and "
            "real member count (refuse filler reselection; refuse "
            "executable singleton-stub fill; product uses full-slot "
            "architectural NOP pad)",
            /*GenCrashDiag=*/false);
      if (!haydn::bundle::isStubCompletion(*Comp) &&
          !haydn::bundle::isProductLegalCompletion(*Comp))
        report_fatal_error(
            "HaydnAsmPrinter: unknown CompletionStateID on BUNDLE root",
            /*GenCrashDiag=*/false);
      // Golden-row fill, not the stamper helper: unused windows and
      // pad-only idle are architectural NOP (AllEntriesReal). Empty
      // membership with no pad is residual idle stub.
      const haydn::bundle::CompletionStateID Expected =
          haydn::bundle::expectedGoldenRowCompletion(RealMembers, HasPadNop);
      if (*Comp != Expected)
        report_fatal_error(
            "HaydnAsmPrinter: BUNDLE CompletionStateID does not match "
            "row and real member count — refuse filler reselection",
            /*GenCrashDiag=*/false);
    }
    assert((CompositeOpc == Haydn::BUNDLE_E96_TWO_ENTRY ||
            CompositeOpc == Haydn::BUNDLE_E96_THREE_ENTRY) &&
           "product live format must be Format E E2/E3 composite");

    MCInst MCB;
    MCB.setOpcode(CompositeOpc);

    // Format E entry dag e0..eN is BUNDLE child order. Pad unused entries
    // with architectural NOP. MC serializes the stamped row only.
    for (unsigned K = 0; K < NumEntries; ++K) {
      MCInst *Instr = nullptr;
      if (K < TypedKids.size())
        Instr = TypedKids[K];
      if (!Instr) {
        Instr = OutContext.createMCInst();
        Instr->setOpcode(Haydn::NOP);
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
  case Haydn::PseudoCALLIndirect:
    // The fnptr call expands to JALR_CALL in HaydnExpandPseudos
    // (pre-S1, pre-layout) so every byte-counting pass sees real parcels.
    // Reaching the printer unexpanded is an escape — fail closed.
    fatalResidualCyclePseudo(
        MI, "residual unexpanded representation "
            "(RET/BR_JT/PseudoCALLIndirect)");
    return;
  case Haydn::RET:
  case Haydn::BR_JT:
    // MIR CFG API (insertBranch / expandPostRAPseudo). Printer is
    // serialize-only — residual unexpanded representation is fatal.
    // B peels at MCInstLower (BEQZ rs=R0); it is not this wall.
    fatalResidualCyclePseudo(
        MI, "residual unexpanded representation "
            "(RET/BR_JT/PseudoCALLIndirect)");
    return;
  case Haydn::LOADI32:
 // : multi-cycle materializer. Imm form expands in expandPostRAPseudo;
    // MBB form must be exact-committed LUI+ADDI32_W by BranchRelaxation hooks
    // (insertIndirectBranch). Printer never splits one MIR root into multiple
    // issue cycles.
    fatalResidualCyclePseudo(
        MI, "LOADI32 residual — expandPostRAPseudo (imm) or exact-commit "
            "LUI+ADDI32_W (MBB) before layout");
  case Haydn::LOAD_ADDR:
 // : ExpandPseudos owns LOAD_ADDR → LUI+ADDI32_W before pack.
    fatalResidualCyclePseudo(
        MI, "LOAD_ADDR residual — ExpandPseudos must expand before PostRA pack");
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
 // : expanded pre-pack in HaydnExpandPseudos (withPostRAScratch).
    // Residual here means ExpandPseudos was disabled — fail closed rather
    // than re-introduce late layout growth.
    report_fatal_error(
        "HaydnAsmPrinter: VASTART/VACOPY must be expanded by "
        "HaydnExpandPseudos before pack (ExpandPseudos is unconditional)");
    return;
  case Haydn::MOV_GPR_TO_DR64:
  case Haydn::MOV_DR64_TO_GPR:
  case Haydn::LIBCALL_MUL64:
  case Haydn::LIBCALL_SDIV:
  case Haydn::LIBCALL_UDIV:
  case Haydn::LIBCALL_SREM:
  case Haydn::LIBCALL_UREM:
    // Semantic multi-MI residuals: ExpandPseudos / selector must own them
    // before pack. Silent skip turned missing moves/libcalls into idle
    // (CG-06 / AR0). Fail closed at the late boundary.
    report_fatal_error(
        "HaydnAsmPrinter: residual cross-bank or LIBCALL_* pseudo — expand "
        "before pack (no silent drop)",
        /*GenCrashDiag=*/false);
  case Haydn::SETCBR_BEGIN:
  case Haydn::SETCBR_END:
 // : ExpandPseudos lowers SETCBR_* → CSRW_W before post-RA pack so the
    // DAG sees real CSR issue/hazard. Residual at printer is fatal.
    fatalResidualCyclePseudo(
        MI, "SETCBR residual — ExpandPseudos must emit CSRW_W before pack");
  case Haydn::LoopStart:
 // : HardwareLoops / ExpandPseudos must install final SET_HWLOOP_*.
    fatalResidualCyclePseudo(
        MI, "LoopStart residual — must be SET_HWLOOP_{W,F2_W} before layout");
  case Haydn::PseudoLoopEnd:
    // PseudoLoopEnd is a meta instruction (isMeta=1). It carries the
    // loop-body MBB for analyzeBranch round-trip but emits NO bytes — the
    // SET already encodes start/end offsets. Just drop it.
    return;
  case Haydn::LoopDec:
 // : Fixup demotion must exact-commit final SUBI32 (not residual LoopDec).
    fatalResidualCyclePseudo(
        MI, "LoopDec residual — Fixup demotion must commit SUBI32 before layout");
  case Haydn::LoopJNZ:
 // : Fixup demotion must exact-commit final BNEZ_W.
    fatalResidualCyclePseudo(
        MI, "LoopJNZ residual — Fixup demotion must commit BNEZ_W before layout");
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
    // Must be SET_HWLOOP_{W,F2_W} before pack (HaydnExpandPseudos /
    // HaydnHardwareLoops). Printer is Desc-only for those forms.
    fatalResidualCyclePseudo(
        MI, "SET_HWLOOP{,_REG} residual — expand to SET_HWLOOP_{W,F2_W} "
            "in ExpandPseudos before PostRA pack");
  }

  // Encoder peels B / JALR_CALL / JAL_TCO / JALR_TCO to catalog members.
  // Wrap-only Finalize keeps B in MIR; JALR_CALL is the standalone
  // returning call. JALR_TCO wraps like JAL_TCO.
  switch (MI->getOpcode()) {
  case Haydn::B:
  case Haydn::JALR_CALL:
  case Haydn::JAL_TCO:
  case Haydn::JALR_TCO:
    MCInstLowering.Lower(MI, TmpInst);
    emitWrappedInst(TmpInst);
    return;
  default:
    break;
  }

  // Shared residual law must not silently no-op if a printer case drifts
  // from isResidualCycleFormingPseudo (VerifyBundles peer). Unexpanded
  // representation (RET/BR_JT/PseudoCALLIndirect) is fatal above.
  if (haydn::bundle::isResidualCycleFormingPseudo(MI->getOpcode()))
    fatalResidualCyclePseudo(
        MI, "must be exact-committed real MIs before AsmPrinter "
            "(shared residual law; missing specific printer case)");
  if (haydn::bundle::isRepresentationExpandPseudo(MI->getOpcode()))
    fatalResidualCyclePseudo(
        MI, "residual unexpanded representation "
            "(RET/BR_JT/PseudoCALLIndirect)");

  if (haydn::bundle::isResidualExecutablePseudo(*MI)) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    OS << "HaydnAsmPrinter: residual executable pseudo — expand before "
          "pack (one-to-one ban; only typed representation/meta allowed). "
          "MI:\n";
    MI->print(OS);
    report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
  }
  if (MI->isPseudo()) {
    std::string Msg;
    raw_string_ostream OS(Msg);
    OS << "HaydnAsmPrinter: unhandled pseudo at emit (no silent drop). MI:\n";
    MI->print(OS);
    report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
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
