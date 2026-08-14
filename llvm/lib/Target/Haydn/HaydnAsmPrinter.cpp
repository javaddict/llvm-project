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
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnInstPrinter.h"
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
  // HasFunctionAlignment is false (HaydnMCAsmInfo): emit alignment here.
  // User alignment (aligned(N)) is a LANGUAGE guarantee — the pointer's low
  // bits are observable — so it is honored, not capped to the parcel
  // power-of-two. The capping rationale (non-parcel pads at LLD boundaries)
  // is obsolete: BundleSim's coverage walk accepts zero-byte gaps by looking
  // at the bytes (CB-146), the product link runs -ffunction-sections so the
  // pad is inter-section zero fill, and inside a single .text a non-parcel
  // align still fail-closes in writeNopData rather than silently
  // misaligning (cb146_overaligned_function exercises the honored path).
  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  Align A = std::max(MF->getAlignment(), TLI->getMinFunctionAlignment());
  // HasFunctionAlignment=false bypasses the generic header's getGVAlignment,
  // which is where F.getAlign() normally promotes MF alignment — consult it
  // here or aligned(N) never reaches the object.
  if (MaybeAlign FnAlign = MF->getFunction().getAlign())
    A = std::max(A, *FnAlign);
  emitAlignment(A);
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

  // Achieved II ≈ number of product parcels (issue cycles) in the kernel.
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

/// Wrap LUI/ADDI32/ORI32/ANDI32/XORI32 symbolic immediates as %hi12/%lo20
/// so llc .s re-assembles with the same fixups as the opcode default.
static void applyHiLoSpecifiers(MCInst &Inst, MCContext &Ctx,
                                const MCInstrInfo &MII) {
  for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
    MCOperand &MO = Inst.getOperand(I);
    if (MO.isInst() && MO.getInst()) {
      applyHiLoSpecifiers(*const_cast<MCInst *>(MO.getInst()), Ctx, MII);
      continue;
    }
    if (!MO.isExpr() || isa<MCSpecifierExpr>(MO.getExpr()))
      continue;
    std::string Log =
        haydn::format_e::peelLogicalOpcodeName(MII.getName(Inst.getOpcode()));
    uint16_t Spec = 0;
    if (StringRef(Log).equals_insensitive("LUI"))
      Spec = ELF::R_HAYDN_HI12;
    else if (StringRef(Log).equals_insensitive("ADDI32") ||
             StringRef(Log).equals_insensitive("ORI32") ||
             StringRef(Log).equals_insensitive("ANDI32") ||
             StringRef(Log).equals_insensitive("XORI32"))
      Spec = ELF::R_HAYDN_LO20;
    if (!Spec)
      continue;
    MO = MCOperand::createExpr(
        MCSpecifierExpr::create(MO.getExpr(), Spec, Ctx));
  }
}

void HaydnAsmPrinter::emitWrappedInst(const MCInst &Inst) {
  // Product encode path is Format E (12 B parcels). Standalone MCInst is
  // streamed for residual representation expands; CodeGen bundles already
  // emit Format E composite opcodes (BUNDLE_E96_*). registerSymbolicOperands
  // for Expr. No force-BUNDLE NOP-pad and no multi-width bare emit.
  MCInst Out = Inst;
  applyHiLoSpecifiers(Out, OutContext, *MF->getSubtarget().getInstrInfo());
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
    // BUNDLE header is the VLIW unit for inclusive END. Representation
    // expands emit real bytes — count them. Residual executable pseudos fatal.
    if (!MI.isBundle() && MI.isPseudo()) {
      if (haydn::bundle::isResidualCycleFormingPseudo(MI.getOpcode()) ||
          haydn::bundle::isResidualExecutablePseudo(MI)) {
        fatalResidualCyclePseudo(
            &MI, "getLastRealInstr: residual executable pseudo (must be "
                 "exact-committed before layout; no silent skip for END)");
      }
      if (haydn::bundle::isRepresentationExpandPseudo(MI.getOpcode()))
        return &MI;
      continue;
    }
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
  // Handle VLIW bundles. AIE: children are already composite operand order;
  // the bundle format TD picks the row. Haydn analogue: membership order is
  // e0, e1, (e2). E2 vs E3 is the BUNDLE-root row stamp, else child count
  // (3 reals → E96ThreeEntry, else E96TwoEntry). Do not re-run canAdd /
  // PacketFormats on an already-formed BUNDLE and do not peel _S*.
  // one-to-one: only representation expands (B/RET/BR_JT/PseudoCALLIndirect).
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
      // Skip debug / pure meta. wraps real MIs as BUNDLE children.
      // One-to-one representation expands only: B→BEQZ_W, RET/BR_JT/
      // PseudoCALLIndirect→JALR_W. Skipping Haydn::B as isPseudo() used to
      // turn singleton BUNDLEs into all-NOP parcels (MEMORY_FAULT).
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

    for (const MachineInstr *I : RawKids) {
      if (HasNonNop && isPadNop(I->getOpcode()))
        continue;

      if (VerboseSpillComments)
        emitSpillReloadComments(&*I, CommentOS);

      // Allocate via MCContext so the child MCInst has stable lifetime.
      MCInst *ChildInst = OutContext.createMCInst();
      unsigned ChildOpc = I->getOpcode();
      if (ChildOpc == Haydn::B) {
        // Unconditional branch pseudo → BEQZ_W R0, target (same as
        // emitInstruction case Haydn::B). Silent skip left all-NOP parcels
 // (MEMORY_FAULT / wrong control flow vs pre- codegen).
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
        // Residual cycle-forming already failed above. Presentation expands
        // (B/RET/BR_JT/PseudoCALLIndirect) are handled in the cases above.
        // Any other bundled isPseudo is an unexpanded executable residual —
        // never silently drop it from a committed cycle.
        fatalResidualCyclePseudo(
            &*I, "bundled residual executable pseudo (must be exact-committed "
                 "real member or typed representation expand before "
                 "AsmPrinter; no silent skip)");
      } else {
        // Desc-only lower (AIE serialize-only). Placement is post-setDesc
        // member identity / Bundle SlotMap (AIEBaseMCFormats.cpp:66-75).
        MCInstLowering.Lower(&*I, *ChildInst);
      }

      // Membership order is the composite operand order. E2/E3 is a
      // bundle-level fact (stamp or child count), not a canAdd replan.
      TypedKids.push_back(ChildInst);
    }

    // Product composite: 3 reals are E3 at the bundle root. Otherwise
    // keep the stamped row or default E2.
    unsigned CompositeOpc = 0;
    unsigned NumEntries = 2;
    std::optional<haydn::bundle::BundleFormatRowID> Row =
        haydn::bundle::getBundleRowID(*MI);
    // Three reals cannot be E2. Child count at the bundle root wins over a
    // stale E2 stamp; otherwise keep the stamp or default E2.
    if (TypedKids.size() >= 3)
      Row = haydn::bundle::BundleFormatRowID::E96ThreeEntry;
    else if (!Row)
      Row = haydn::bundle::BundleFormatRowID::E96TwoEntry;
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
      const unsigned RealMembers = static_cast<unsigned>(TypedKids.size());
      if (RealMembers > 0 && haydn::bundle::isStubCompletion(*Comp))
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
      if (*Comp != haydn::bundle::selectCompletionForMembersAndPads(
                       *Row, HasNonNop ? RealMembers : 0, !HasNonNop))
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
    // with architectural NOP. MC serializes the stamped/count-selected row.
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
    applyHiLoSpecifiers(MCB, OutContext, *MF->getSubtarget().getInstrInfo());
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
    // Expand B pseudo to logical BEQZ_W R0, target (R0 is always zero, so
    // this always branches). Format E encode.
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
    // Logical JALR_W; Format E encode.
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
    // register operand directly.
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

  // Shared residual law must not silently no-op if a printer case drifts
  // from isResidualCycleFormingPseudo (VerifyBundles peer). Representation
  // expands (B/RET/BR_JT/PseudoCALLIndirect) stay out of that set.
  if (haydn::bundle::isResidualCycleFormingPseudo(MI->getOpcode()))
    fatalResidualCyclePseudo(
        MI, "must be exact-committed real MIs before AsmPrinter "
            "(shared residual law; missing specific printer case)");

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
