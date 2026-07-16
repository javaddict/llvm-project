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
#include "HaydnInstPrinter.h"
#include "HaydnMCAsmInfo.h"
#include "HaydnMCELFStreamer.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

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
    CPUName = "generic";
  return createHaydnMCSubtargetInfoImpl(TT, CPUName, /*TuneCPU=*/CPUName, FS);
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
  // custom ELF streamer registers symbols nested in Haydn::BUNDLE
  // children (MCOperand::isInst). Base MCStreamer only visits top-level exprs.
  TargetRegistry::RegisterELFStreamer(HaydnTarget, createHaydnELFStreamer);
}
