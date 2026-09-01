//===-- HaydnPostSelectOptimize.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select MI peeps for Haydn (O1).
// Live peep via elideCrossBankRoundTrips (product ON O1): identity pack
// recombine (MOVE32_DR_L/H or MOV_DR64_TO_GPR -> MOV_GPR_TO_DR64 -> OR64).
// Lane-store is HaydnCombine.td form_lane_store (AIECombine.td:80 shape).
// Const-pack CSE is generic MachineCSE once MOV_GPR_TO_DR64 is selected.
// Prefer end-to-end DR64 over pack/unpack ping-pong; do not invent
// GPR-pair aliasing for DR64. Slot commit is post-RA only (HR tryAdd /
// AltDesc / leaveRegion setDesc); encode is Desc-as-is.
//===----------------------------------------------------------------------===//

#include "HaydnPostSelectOptimize.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "haydn-postselect-opt"

using namespace llvm;

STATISTIC(NumLaneStoreFolds,
          "Number of MOVE32_DR + ST32 folded to D_SW_L/H_WITH_IMM"); // NOLINT
STATISTIC(NumIdentityPackElides,
          "Number of identity MOVE32/MOV_DR64_TO_GPR + MOV_GPR_TO_DR64 "
          "round-trips elided to OR64"); // NOLINT

char HaydnPostSelectOptimize::ID = 0;

INITIALIZE_PASS(HaydnPostSelectOptimize, DEBUG_TYPE,
                "Haydn Post-Select Peepholes (cross-bank elide)", false, false)

HaydnPostSelectOptimize::HaydnPostSelectOptimize()
    : MachineFunctionPass(ID) {}

bool HaydnPostSelectOptimize::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  // Product: elideCrossBankRoundTrips ON for O1+ (pass only registered then).
  Changed |= elideCrossBankRoundTrips(MF);
  // Slot commitment is post-RA only (HaydnHazardRecognizer tryAdd / AltDesc +
  // leaveRegion setDesc). Encode is Desc-as-is; no pre-RA slot materialize.
  return Changed;
}

//===----------------------------------------------------------------------===//
// Cross-bank peep: identity pack elide. Combiner owns G_STORE-of-lane;
// tryFoldMove32DrToSw is the selected MOVE32+ST32 leftover (align-4 s64
// store). Const-pack CSE is generic MachineCSE.
//===----------------------------------------------------------------------===//

bool haydn::postselect::laneStoreImmForWordScaledOffset(int64_t WordOffset,
                                                        int64_t &ScaledImm) {
  // ST32 and D_SW_L/H_WITH_IMM share one golden word-scaled EA law
  // (S_SW_WITH_IMM / D_SW_L_WITH_IMM: EA = rs + (imm6 << 2)). The selected
  // ST32 offset operand is ALREADY that word-scaled imm, so it passes
  // through unscaled. (Rescaling here halved negative-offset stores and
  // silently moved the EA: CB-160, first DR-pair store of each loop
  // iteration stored to y+12 instead of y+0.)
  if (!isInt<6>(WordOffset))
    return false;
  ScaledImm = WordOffset;
  return true;
}

bool HaydnPostSelectOptimize::tryFoldMove32DrToSw(MachineInstr &MovInst,
                                                   MachineRegisterInfo &MRI,
                                                   const HaydnInstrInfo &TII) {
  unsigned MovOpc = MovInst.getOpcode();
  if (MovOpc != Haydn::MOVE32_DR_L && MovOpc != Haydn::MOVE32_DR_H)
    return false;

  Register GprDst = MovInst.getOperand(0).getReg();
  if (!GprDst.isVirtual() || !MRI.hasOneNonDBGUse(GprDst))
    return false;

  MachineInstr *St32 = nullptr;
  for (MachineInstr &UseMI : MRI.use_nodbg_instructions(GprDst)) {
    St32 = &UseMI;
    break;
  }
  if (!St32 || St32->getOpcode() != Haydn::ST32)
    return false;
  if (St32->getParent() != MovInst.getParent())
    return false;

  Register DrSrc = MovInst.getOperand(1).getReg();
  Register Base = St32->getOperand(1).getReg();
  int64_t ScaledImm;
  if (!haydn::postselect::laneStoreImmForWordScaledOffset(
          St32->getOperand(2).getImm(), ScaledImm))
    return false;

  unsigned SwOpc = (MovOpc == Haydn::MOVE32_DR_L) ? Haydn::D_SW_L_WITH_IMM
                                                   : Haydn::D_SW_H_WITH_IMM;
  for (MachineMemOperand *MMO : St32->memoperands()) {
    if (MMO->isVolatile() || MMO->isAtomic())
      return false;
    if (MMO->getAlign() < Align(4))
      return false;
    if (!MMO->getSize().hasValue() || MMO->getSize() != 4)
      return false;
  }
  if (St32->memoperands_empty())
    return false;

  bool DrKill = MovInst.getOperand(1).isKill();
  MachineInstrBuilder MIB =
      BuildMI(*St32->getParent(), St32, St32->getDebugLoc(), TII.get(SwOpc))
          .addReg(DrSrc, getKillRegState(DrKill))
          .addReg(Base)
          .addImm(ScaledImm);
  MIB.cloneMemRefs(*St32);
  ++NumLaneStoreFolds;
  St32->eraseFromParent();
  MovInst.eraseFromParent();
  return true;
}

// Identity cross-bank recombine: lanes extracted from DR64 and immediately
// packed back form a pure round-trip. MOV_GPR_TO_DR64 expands post-RA to a
// SP stack chain when both halves are live GPRs — eliding here keeps the
// value in DR64 end-to-end without inventing GPR-pair aliasing.
bool HaydnPostSelectOptimize::tryElideIdentityPack(MachineInstr &MovInst,
                                                    MachineRegisterInfo &MRI,
                                                    const HaydnInstrInfo &TII) {
  // MovInst: %dr = MOV_GPR_TO_DR64 %lo, %hi
  if (MovInst.getOpcode() != Haydn::MOV_GPR_TO_DR64)
    return false;

  Register DrDst = MovInst.getOperand(0).getReg();
  Register LoReg = MovInst.getOperand(1).getReg();
  Register HiReg = MovInst.getOperand(2).getReg();
  if (!DrDst.isVirtual() || !LoReg.isVirtual() || !HiReg.isVirtual())
    return false;

  MachineInstr *LoDef = MRI.getVRegDef(LoReg);
  MachineInstr *HiDef = MRI.getVRegDef(HiReg);
  if (!LoDef || !HiDef)
    return false;

  Register SrcDr;
  // Pattern A: separate lane extracts of the same DR64.
  //   lo = MOVE32_DR_L src; hi = MOVE32_DR_H src
  if (LoDef->getOpcode() == Haydn::MOVE32_DR_L &&
      HiDef->getOpcode() == Haydn::MOVE32_DR_H) {
    Register LoSrc = LoDef->getOperand(1).getReg();
    Register HiSrc = HiDef->getOperand(1).getReg();
    if (LoSrc != HiSrc || !LoSrc.isVirtual())
      return false;
    SrcDr = LoSrc;
  } else if (LoDef == HiDef && LoDef->getOpcode() == Haydn::MOV_DR64_TO_GPR) {
    // Pattern B: single unpack feeding both pack inputs in order.
    //   lo, hi = MOV_DR64_TO_GPR src
    // Require LoReg/HiReg are the lo/hi defs (ops 0/1), not swapped.
    if (LoDef->getOperand(0).getReg() != LoReg ||
        LoDef->getOperand(1).getReg() != HiReg)
      return false;
    SrcDr = LoDef->getOperand(2).getReg();
    if (!SrcDr.isVirtual())
      return false;
  } else {
    return false;
  }

  // Avoid trivial no-op only when dst already is src (RA/copy later).
  if (SrcDr == DrDst)
    return false;

  // Same-block only: keep dominance/layout simple (matches CSE policy).
  if (LoDef->getParent() != MovInst.getParent() ||
      HiDef->getParent() != MovInst.getParent())
    return false;

  LLVM_DEBUG(dbgs() << "Elide identity pack round-trip: " << MovInst << "\n");

  // Product DR64 copy is OR64 d,s,s (HaydnInstrInfo::isCopyInstrImpl +
  // copyPhysReg). Generic MachineCSE already CSEs selected MOV_GPR_TO_DR64;
  // this peep only removes the extract+repack ping-pong. Do not kill SrcDr:
  // MOVE32 / MOV_DR64_TO_GPR still use it until DCE.
  BuildMI(*MovInst.getParent(), MovInst, MovInst.getDebugLoc(),
          TII.get(Haydn::OR64), DrDst)
      .addReg(SrcDr)
      .addReg(SrcDr);
  ++NumIdentityPackElides;
  MovInst.eraseFromParent();
  return true;
}

bool HaydnPostSelectOptimize::elideCrossBankRoundTrips(MachineFunction &MF) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  // Iterate to a fixed point: each fold removes instructions and may
  // expose a new fold opportunity (rare, but cheap to handle).
  bool LocalChanged = true;
  while (LocalChanged) {
    LocalChanged = false;
    for (MachineBasicBlock &MBB : MF) {
      SmallVector<MachineInstr *, 16> MovCandidates;
      for (MachineInstr &MI : MBB) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64 || Opc == Haydn::MOVE32_DR_L ||
            Opc == Haydn::MOVE32_DR_H)
          MovCandidates.push_back(&MI);
      }
      for (MachineInstr *MI : MovCandidates) {
        if (!MI->getParent())
          continue;
        unsigned Opc = MI->getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64) {
          if (tryElideIdentityPack(*MI, MRI, TII)) {
            Changed = true;
            LocalChanged = true;
          }
        } else if (tryFoldMove32DrToSw(*MI, MRI, TII)) {
          Changed = true;
          LocalChanged = true;
        }
      }
    }
  }

  return Changed;
}

FunctionPass *llvm::createHaydnPostSelectOptimizePass() {
  return new HaydnPostSelectOptimize();
}
