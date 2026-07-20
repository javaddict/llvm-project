//===-- HaydnPostRAScratch.cpp - Post-RA MatInt GPR scratch ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRAScratch.h"
#include "HaydnFrameLowering.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

// Call-clobbered first (R1–R7), then callee-saved temps that may still be
// dead at I (R11…R8). R12 is last and only considered when PreferNotR12 is
// false — never "acquire" fixed R12 as free AT.
static constexpr MCPhysReg PostRAScratchPriority[] = {
    Haydn::R1, Haydn::R2,  Haydn::R3,  Haydn::R4, Haydn::R5, Haydn::R6,
    Haydn::R7, Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R12,
};

static bool isExcluded(MCPhysReg Cand, ArrayRef<Register> Exclude) {
  for (Register E : Exclude) {
    if (E.isPhysical() && E.id() == Cand)
      return true;
  }
  return false;
}

Register llvm::findPostRAScratchGPR(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    bool PreferNotR12, bool &NeedsSpill,
                                    ArrayRef<Register> Exclude) {
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(MBB);
  for (MachineBasicBlock::iterator II = MBB.end(); II != I;) {
    --II;
    LPR.stepBackward(*II);
  }

  Register FirstPreferred;
  for (MCPhysReg Cand : PostRAScratchPriority) {
    if (PreferNotR12 && Cand == Haydn::R12)
      continue;
    if (MRI.isReserved(Cand))
      continue;
    if (isExcluded(Cand, Exclude))
      continue;
    if (!FirstPreferred)
      FirstPreferred = Cand;
    if (LPR.available(MRI, Cand)) {
      NeedsSpill = false;
      return Cand;
    }
  }

  // Nothing free: spill the first preferred non-excluded candidate.
  NeedsSpill = true;
  if (FirstPreferred)
    return FirstPreferred;
  // Extreme fallback if every preferred reg is reserved/excluded.
  for (MCPhysReg Cand : PostRAScratchPriority) {
    if (PreferNotR12 && Cand == Haydn::R12)
      continue;
    if (isExcluded(Cand, Exclude) || MRI.isReserved(Cand))
      continue;
    return Cand;
  }
  report_fatal_error(
      "Haydn: no post-RA scratch GPR (all candidates reserved/excluded)");
}

static void emitScratchMemOp(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             const TargetInstrInfo &TII, Register Scr,
                             Register FrameReg, int64_t Off, bool IsStore,
                             unsigned StoreFlags) {
  if (isInt<16>(Off)) {
    if (IsStore)
      BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
          .addReg(Scr, StoreFlags)
          .addReg(FrameReg)
          .addImm(Off);
    else
      BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
          .addReg(FrameReg)
          .addImm(Off);
    return;
  }

  // Far slot: EA in R0 (soft-zero), access at [R0+0], re-zero. Scratch holds
  // the value being moved so it cannot be the address temp.
  const Register Tmp = Haydn::R0;
  if (!isInt<20>(Off))
    report_fatal_error(
        "Haydn: post-RA scratch spill FI offset exceeds simm20");
  BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Tmp)
      .addReg(FrameReg)
      .addImm(Off);
  if (IsStore)
    BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
        .addReg(Scr, StoreFlags)
        .addReg(Tmp)
        .addImm(0);
  else
    BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr).addReg(Tmp).addImm(0);
  BuildMI(MBB, I, DL, TII.get(Haydn::XOR32), Tmp).addReg(Tmp).addReg(Tmp);
}

void llvm::withPostRAScratch(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             const TargetInstrInfo &TII,
                             const HaydnSubtarget &ST, bool PreferNotR12,
                             function_ref<void(Register Scr)> Fn,
                             ArrayRef<Register> Exclude) {
  bool NeedsSpill = false;
  const Register Scr =
      findPostRAScratchGPR(MBB, I, PreferNotR12, NeedsSpill, Exclude);

  if (!NeedsSpill) {
    Fn(Scr);
    return;
  }

  // Spill/restore only when no free GPR. Prefer PEI emergency /
  // PostRAScratchFI (spill *home* for any scavenged GPR). Last resort (MIR
  // without PEI): balanced 8-byte SP bracket — no RegScavenger here.
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int SpillFI = FuncInfo->getBranchRelaxationScratchFI();
  if (SpillFI < 0)
    SpillFI = FuncInfo->getPostRAScratchFI();

  if (SpillFI >= 0) {
    const HaydnFrameLowering *TFL = ST.getFrameLowering();
    Register FrameReg;
    const int64_t Off =
        TFL->getFrameIndexReference(MF, SpillFI, FrameReg).getFixed();
    emitScratchMemOp(MBB, I, DL, TII, Scr, FrameReg, Off, /*IsStore=*/true,
                     /*StoreFlags=*/0);
    Fn(Scr);
    emitScratchMemOp(MBB, I, DL, TII, Scr, FrameReg, Off, /*IsStore=*/false,
                     /*StoreFlags=*/0);
    return;
  }

  // SP bracket (8-byte align for callers that also use LD64 on SP).
  BuildMI(MBB, I, DL, TII.get(Haydn::SUBI32), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
  BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
      .addReg(Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  Fn(Scr);
  BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
}
