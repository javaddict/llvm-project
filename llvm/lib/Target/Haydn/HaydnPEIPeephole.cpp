//===-- HaydnPEIPeephole.cpp - Haydn Prologue/Epilogue Peephole -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-RA MachineFunctionPass that optimizes prologue
// and epilogue instruction sequences for the Haydn VLIW DSP target. It runs
// after Prologue/Epilogue Insertion (PEI) and before PostRA pack (addPreSched2).
//
// Product role (residual): belt-and-suspenders cleanup when FrameLowering
// still emits a dead FP setup. Prefer fixing emission upstream in
// HaydnFrameLowering; keep this pass as the last safety net (default ON).
// PushPopOpt was deleted — do not reintroduce SP fuse here.
//
// Optimization performed:
//
// Dead frame-pointer setup elimination: When the function does not use
// a frame pointer (hasFP == false), any ADDI32 R14, R13, <offset>
// instruction with a FrameSetup flag that was emitted by the prologue
// to set up FP is dead code. This can happen when the prologue emits
// the FP setup speculatively or when earlier passes fail to clean up.
// Additionally, COPY-like patterns (OR32 R14, R13, R13 or ADDI32 R14
// R13, 0) are also eliminated when FP is not needed.
//
//===----------------------------------------------------------------------===//

#include "HaydnPEIPeephole.h"
#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-pei-peephole"

using namespace llvm;

STATISTIC(NumDeadFPSetupEliminated,
          "Number of dead frame-pointer setup instructions eliminated");

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnPEIPeephole::ID = 0;

INITIALIZE_PASS(HaydnPEIPeephole, "haydn-pei-peephole",
                "Haydn PEI Peephole Optimizer", false, false)

FunctionPass *llvm::createHaydnPEIPeepholePass() {
  return new HaydnPEIPeephole();
}

HaydnPEIPeephole::HaydnPEIPeephole()
    : MachineFunctionPass(ID) {}

void HaydnPEIPeephole::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnPEIPeephole::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn PEI Peephole: " << MF.getName()
                     << " =====\n");

  bool Changed = false;
  Changed |= eliminateDeadFPSetup(MF);
  return Changed;
}

//===----------------------------------------------------------------------===//
// Optimization: Dead frame-pointer setup elimination
//===----------------------------------------------------------------------===//

bool HaydnPEIPeephole::isCalleeSaveReg(const MachineFunction &MF, unsigned Reg) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo()) {
    if (CSI.getReg() == Reg)
      return true;
  }
  return false;
}

bool HaydnPEIPeephole::tryRemoveDeadFPSetup(
    MachineFunction &MF, MachineInstr &MI, StringRef OpcodeName,
    SmallVectorImpl<MachineInstr *> &ToRemove) {
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  if (DstReg != Haydn::R14 || SrcReg != Haydn::R13)
    return false;

  // For 3-reg patterns (OR32, ADD32), also verify the second source is R13.
  unsigned Opcode = MI.getOpcode();
  if ((Opcode == Haydn::OR32 || Opcode == Haydn::ADD32) &&
      (!MI.getOperand(2).isReg() ||
       MI.getOperand(2).getReg() != Haydn::R13))
    return false;

  // R14 could be a callee-saved register that the epilogue restores.
  // Only remove the FP setup if R14 is not callee-saved.
  if (isCalleeSaveReg(MF, Haydn::R14))
    return false;

  // PEI may use FrameSetup ADDI/OR R14,R13 as a *temporary* base for CSR
  // stride stores (call-clobbered scratch), not as an FP. If any later
  // instruction reads this def of R14 before R14 is redefined, the setup is
  // live — do not remove it. (Deleting a live temp left ST64 d*, R14, * with
  // garbage base → MEMORY_FAULT; seed3148 / large DR CSR prologues.)
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  MachineBasicBlock &MBB = *MI.getParent();
  for (MachineBasicBlock::iterator I = std::next(MI.getIterator()),
                                   E = MBB.end();
       I != E; ++I) {
    if (I->readsRegister(Haydn::R14, TRI))
      return false;
    // Next full def of R14 without a read ends the live range of this setup.
    if (I->modifiesRegister(Haydn::R14, TRI))
      break;
  }

  LLVM_DEBUG(dbgs() << "  Removing dead FP setup (" << OpcodeName
                    << "): " << MI);
  ++NumDeadFPSetupEliminated;
  ToRemove.push_back(&MI);
  return true;
}

bool HaydnPEIPeephole::eliminateDeadFPSetup(MachineFunction &MF) {
  // When hasFP is false, the frame pointer (R14) is not used for frame
  // access — all addressing goes through SP (R13). However, the prologue
  // may still emit instructions that write to R14:
  //
  // ADDI32 R14, R13, <stack_size> (FP = SP + frame size)
  // OR32 R14, R13, R13 (FP = SP, when stack size is 0)
  //
  // When FP is not needed, these instructions produce a value in R14 that
  // is never consumed (R14 is not reserved and not used for frame access).
  // Remove them.
  //
  // Safety: We only remove FP-setup instructions that have the FrameSetup
  // flag, ensuring we only touch prologue-emitted instructions and never
  // user code that happens to write to R14.

  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFL = STI.getFrameLowering();

  // Only optimize when FP is not used.
  if (TFL->hasFP(MF))
    return false;

  SmallVector<MachineInstr *, 4> ToRemove;

  // Scan the entry block for FP-setup instructions with FrameSetup flag.
  // The prologue always emits into the entry block.
  MachineBasicBlock &EntryMBB = MF.front();
  for (MachineInstr &MI : EntryMBB) {
    // Skip non-prologue instructions.
    if (!MI.getFlag(MachineInstr::FrameSetup))
      continue;

    unsigned Opcode = MI.getOpcode();

    // Pattern 1: ADDI32 R14, R13, <imm> — FP = SP + offset.
    // Pattern 2: OR32 R14, R13, R13 — FP = SP (copy via identity-OR).
    // Pattern 3: ADD32 R14, R13, <reg> — FP = SP + reg (large frame setup).
    // Pattern 4: MOVE32 R14, R13 — FP = SP (register move).
    if (Opcode == Haydn::ADDI32 || Opcode == Haydn::ADDI32_W) {
      if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
        tryRemoveDeadFPSetup(MF, MI, "ADDI32", ToRemove);
      }
    } else if (Opcode == Haydn::OR32 || Opcode == Haydn::ADD32) {
      if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg() && MI.getOperand(2).isReg()) {
        tryRemoveDeadFPSetup(MF, MI,
                             Opcode == Haydn::OR32 ? "OR32" : "ADD32",
                             ToRemove);
      }
    } else if (Opcode == Haydn::MOVE32) {
      if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg()) {
        tryRemoveDeadFPSetup(MF, MI, "MOVE32", ToRemove);
      }
    }
  }

  for (MachineInstr *MI : reverse(ToRemove))
    MI->eraseFromParent();

  return !ToRemove.empty();
}
