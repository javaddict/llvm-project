//===-- HaydnMCTargetDesc.cpp - Haydn Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file provides Haydn specific target descriptions.
//===----------------------------------------------------------------------===//

#include "HaydnMCTargetDesc.h"
#include "HaydnBaseInfo.h"
#include "HaydnInstPrinter.h"
#include "HaydnMCAsmInfo.h"
#include "HaydnMCELFStreamer.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include <mutex>

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "HaydnGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "HaydnGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "HaydnGenSubtargetInfo.inc"

using namespace llvm;

static MCAsmInfo *createHaydnMCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new HaydnMCAsmInfo(TT);

  // Set the initial CFA for the CIE: CFA = SP + 0 at function entry.
  unsigned SP = MRI.getDwarfRegNum(Haydn::R13, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstrInfo *createHaydnMCInstrInfo() {
  MCInstrInfo *Info = new MCInstrInfo();
  InitHaydnMCInstrInfo(Info);
  return Info;
}

const MCInstrInfo &llvm::getHaydnSharedMCInstrInfo() {
  static MCInstrInfo Info;
  static std::once_flag Once;
  std::call_once(Once, [] { InitHaydnMCInstrInfo(&Info); });
  return Info;
}

const MCRegisterInfo &llvm::getHaydnSharedMCRegisterInfo() {
  static MCRegisterInfo Info;
  static std::once_flag Once;
  std::call_once(Once, [] { InitHaydnMCRegisterInfo(&Info, Haydn::R15); });
  return Info;
}

static MCInstPrinter *createHaydnMCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  return new HaydnInstPrinter(MAI, MII, MRI);
}

static MCRegisterInfo *createHaydnMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *Info = new MCRegisterInfo();
  InitHaydnMCRegisterInfo(Info, Haydn::R15);
  return Info;
}

static MCSubtargetInfo *createHaydnMCSubtargetInfo(const Triple &TT,
                                                    StringRef CPU, StringRef FS) {
  std::string CPUName = std::string(CPU);
  if (CPUName.empty())
    CPUName = Haydn::kDefaultCPUName;
  return createHaydnMCSubtargetInfoImpl(TT, CPUName,
                                        /*TuneCPU=*/Haydn::kDefaultTuneCPUName,
                                        FS);
}

namespace {

/// Product objdump sees Format E parcels as BUNDLE_E96_* (or generic BUNDLE)
/// with isInst children. Control-flow flags and PC-relative targets live on
/// the members, not the composite root. Peer: Hexagon/RISCV MCInstrAnalysis.
bool isHaydnCompositeBundle(unsigned Opcode) {
  return Opcode == Haydn::BUNDLE || Opcode == Haydn::BUNDLE_E96_TWO_ENTRY ||
         Opcode == Haydn::BUNDLE_E96_THREE_ENTRY;
}

class HaydnMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit HaydnMCInstrAnalysis(const MCInstrInfo *MCII)
      : MCInstrAnalysis(MCII) {}

  bool isBranch(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(Inst, [this](const MCInst &C) { return isBranch(C); });
    return MCInstrAnalysis::isBranch(Inst);
  }

  bool isConditionalBranch(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(
          Inst, [this](const MCInst &C) { return isConditionalBranch(C); });
    return MCInstrAnalysis::isConditionalBranch(Inst);
  }

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(
          Inst, [this](const MCInst &C) { return isUnconditionalBranch(C); });
    return MCInstrAnalysis::isUnconditionalBranch(Inst);
  }

  bool isIndirectBranch(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(
          Inst, [this](const MCInst &C) { return isIndirectBranch(C); });
    return MCInstrAnalysis::isIndirectBranch(Inst);
  }

  bool isCall(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(Inst, [this](const MCInst &C) { return isCall(C); });
    return MCInstrAnalysis::isCall(Inst);
  }

  bool isReturn(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(Inst, [this](const MCInst &C) { return isReturn(C); });
    return MCInstrAnalysis::isReturn(Inst);
  }

  bool isTerminator(const MCInst &Inst) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode()))
      return anyChild(Inst,
                      [this](const MCInst &C) { return isTerminator(C); });
    return MCInstrAnalysis::isTerminator(Inst);
  }

  /// PC-relative call/branch target as a byte address. Members already carry
  /// dump-byte displacements (cond-branch / JAL / HWLR Off1/Off2 after
  /// RelocLayout ValueShift). Do not apply a second Imm/2 or Imm/4 scale.
  /// JALR is rs-relative (isIndirectBranch) and is not evaluated here.
  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (isHaydnCompositeBundle(Inst.getOpcode())) {
      for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
        const MCOperand &Op = Inst.getOperand(I);
        if (!Op.isInst() || !Op.getInst())
          continue;
        if (evaluateBranch(*Op.getInst(), Addr, Size, Target))
          return true;
      }
      return false;
    }

    const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
    if (Desc.isIndirectBranch())
      return false;
    if (!Desc.isBranch() && !Desc.isCall())
      return false;
    if (Inst.getNumOperands() == 0)
      return false;
    const MCOperand &ImmOp = Inst.getOperand(Inst.getNumOperands() - 1);
    if (!ImmOp.isImm())
      return false;
    // Unpatched call relocs leave a 0 displacement in the .o; treating that
    // as PC+0 would symbolize the call site itself. Linked images write a
    // non-zero byte offset. Branch-to-self (disp 0) stays evaluable.
    if (Desc.isCall() && ImmOp.getImm() == 0)
      return false;
    Target = Addr + ImmOp.getImm();
    return true;
  }

private:
  template <typename Pred>
  static bool anyChild(const MCInst &Inst, Pred P) {
    for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
      const MCOperand &Op = Inst.getOperand(I);
      if (Op.isInst() && Op.getInst() && P(*Op.getInst()))
        return true;
    }
    return false;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createHaydnMCInstrAnalysis(const MCInstrInfo *Info) {
  return new HaydnMCInstrAnalysis(Info);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnTargetMC() {
  auto &HaydnTarget = getTheHaydnTarget();
  TargetRegistry::RegisterMCAsmBackend(HaydnTarget, createHaydnAsmBackend);
  TargetRegistry::RegisterMCAsmInfo(HaydnTarget, createHaydnMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(HaydnTarget, createHaydnMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(HaydnTarget, createHaydnMCRegisterInfo);
  TargetRegistry::RegisterMCCodeEmitter(HaydnTarget, createHaydnMCCodeEmitter);
  TargetRegistry::RegisterMCInstPrinter(HaydnTarget, createHaydnMCInstPrinter);
  TargetRegistry::RegisterMCSubtargetInfo(HaydnTarget,
                                          createHaydnMCSubtargetInfo);
  TargetRegistry::RegisterMCInstrAnalysis(HaydnTarget,
                                          createHaydnMCInstrAnalysis);
  // custom ELF streamer registers symbols nested in Haydn::BUNDLE
  // children (MCOperand::isInst). Base MCStreamer only visits top-level exprs.
  TargetRegistry::RegisterELFStreamer(HaydnTarget, createHaydnELFStreamer);
}
