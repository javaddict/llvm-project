//===-- HaydnATScratch.cpp - R12 VASTART/late address scratch -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fixed-R12 spill bracket for VASTART/VACOPY address math (and residual
// withMaterializeScratch callers). Pure MatInt (LOADI64) uses
// HaydnPostRAScratch instead — never hardcode R12 as MatInt dest.
//
//===----------------------------------------------------------------------===//

#include "HaydnATScratch.h"
#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

// Spill/restore R12 around VASTART/emit-time address math (AIE model:
// no free AT). Pure MatInt uses HaydnPostRAScratch, not this helper.
// Preferred path : permanent in-frame R12ScratchFI reserved by
// determineCalleeSaves — same contract as AsmPrinter emitATScratchSave*.
// Never adjusts SP for the AT save itself: a temporary `subi sp` collides
// with PEI SP-relative FI addressing and with VLIW packing of SUBI+ST (same
// bundle RAW on SP can store to the pre-adjust address).
// Fallback: SP-relative 8-byte bracket when R12ScratchFI is not yet reserved
// (MIR unit tests that run only postrapseudos without PEI).
void HaydnATScratch::withMaterializeScratch(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, const DebugLoc &DL,
    const TargetInstrInfo &TII, const HaydnSubtarget &ST,
    function_ref<void(Register Scr)> Fn) {
  const Register Scr = phys();
  // ALWAYS spill/restore around MatInt into R12. LivePhysRegs after post-RA
  // expand can under-report liveness (stale kills / incomplete live-outs), so
  // a live-only save leaves greedy values in R12 clobbered. If R12 is truly
  // dead, mark the store source Undef so the verifier does not see a use of
  // an undefined physreg.
  bool R12Live = false;
  {
    const TargetRegisterInfo *TRI = ST.getRegisterInfo();
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    LivePhysRegs LPR(*TRI);
    LPR.addLiveOuts(MBB);
    for (MachineBasicBlock::iterator II = MBB.end(); II != I;) {
      --II;
      LPR.stepBackward(*II);
    }
    R12Live = !LPR.available(MRI, Scr.asMCReg());
  }
  const unsigned ScrStoreFlags = R12Live ? 0 : RegState::Undef;

  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  const int ScratchFI = FuncInfo->getR12ScratchFI();
  // Prefer PEI R12ScratchFI when reserved. Available after prologepilog;
  // MIR-only tests without PEI fall back to SP.
  const bool UseFrameSlot = ScratchFI >= 0;

  if (UseFrameSlot) {
    // spill into PEI-reserved in-frame slot. Frame layout is fixed by
    // the time expandPostRAPseudo runs (after prologepilog).
    const HaydnFrameLowering *TFL = ST.getFrameLowering();
    Register FrameReg;
    const int64_t Off =
        TFL->getFrameIndexReference(MF, ScratchFI, FrameReg).getFixed();

    auto emitMemOp = [&](bool IsStore) {
      if (isInt<16>(Off)) {
        if (IsStore)
          BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
              .addReg(Scr, ScrStoreFlags)
              .addReg(FrameReg)
              .addImm(Off);
        else
          BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
              .addReg(FrameReg)
              .addImm(Off);
        return;
      }

      // Far slot: materialize EA in R0 (soft-zero), access at [R0+0], re-zero.
      // R12 cannot be the address temp (holds the value being moved). Mirrors
      // HaydnAsmPrinter::emitATScratchMemOp.
      const Register Tmp = Haydn::R0;
      if (isInt<20>(Off)) {
        BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Tmp)
            .addReg(FrameReg)
            .addImm(Off);
      } else {
        report_fatal_error(
            "Haydn: R12ScratchFI offset exceeds simm20; frame too large for "
            "AT scratch far path");
      }
      if (IsStore)
        BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
            .addReg(Scr, ScrStoreFlags)
            .addReg(Tmp)
            .addImm(0);
      else
        BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr).addReg(Tmp).addImm(0);
      // Restore soft-zero invariant.
      BuildMI(MBB, I, DL, TII.get(Haydn::XOR32), Tmp).addReg(Tmp).addReg(Tmp);
    };

    emitMemOp(/*IsStore=*/true);
    Fn(Scr);
    emitMemOp(/*IsStore=*/false);
    return;
  }

  // Fallback: temporary SP bracket (pre-PEI / no R12ScratchFI). Keep SP
  // 8-byte aligned: LOADI64 uses LD64 which needs align 8.
  BuildMI(MBB, I, DL, TII.get(Haydn::SUBI32), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
  BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
      .addReg(Scr, ScrStoreFlags)
      .addReg(Haydn::R13)
      .addImm(0);

  Fn(Scr);

  BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr).addReg(Haydn::R13).addImm(0);
  BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
}
