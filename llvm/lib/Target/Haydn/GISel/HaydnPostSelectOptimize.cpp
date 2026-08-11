//===-- HaydnPostSelectOptimize.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select MI peeps for Haydn (O1+).
// Live peeps via elideCrossBankRoundTrips: lane-store
// (MOVE32_DR + ST32 → D_SW_*) and DR64 constant CSE.
// Slot commit is post-RA only (HR tryAdd / AltDesc / leaveRegion setDesc);
// encode is Desc-as-is.
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

STATISTIC(NumSextMovFolds,
          "Number of MOV_GPR_TO_DR64 sext-shape folded to SEXT_GPR32_TO_DR64");
STATISTIC(NumLaneStoreFolds,
          "Number of MOVE32_DR + ST32 folded to D_SW_L/H_WITH_IMM"); // NOLINT
STATISTIC(NumDR64ConstCSE,
          "Number of duplicate MOV_GPR_TO_DR64 constants CSE'd"); // NOLINT

char HaydnPostSelectOptimize::ID = 0;

INITIALIZE_PASS(HaydnPostSelectOptimize, DEBUG_TYPE,
                "Haydn Post-Select Peepholes (cross-bank elide)", false, false)

HaydnPostSelectOptimize::HaydnPostSelectOptimize()
    : MachineFunctionPass(ID) {}

bool HaydnPostSelectOptimize::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  // Live: lane-store + DR const CSE only.
  Changed |= elideCrossBankRoundTrips(MF);
  // Slot commitment is post-RA only (HaydnHazardRecognizer tryAdd / AltDesc +
  // leaveRegion setDesc). Encode is Desc-as-is; no pre-RA slot materialize.
  return Changed;
}

//===----------------------------------------------------------------------===//
// Cross-bank peeps (lane-store + DR const CSE).
//===----------------------------------------------------------------------===//

bool HaydnPostSelectOptimize::tryFoldSextMovToDirect(MachineInstr &MovInst,
                                                     MachineRegisterInfo &MRI,
                                                     const HaydnInstrInfo &TII) {
  // Historically this folded `MOV_GPR_TO_DR64 %dr, %x, %x` → SEXT_GPR32_TO_DR64.
  // That is wrong as a general rewrite:
  // * MOV_GPR_TO_DR64 lo,hi means bit-pack: rd = (hi<<32)|lo (dual-lane
  // G_BUILD_VECTOR splat uses x,x to put the same word in both lanes).
  // * SEXT_GPR32_TO_DR64 means sign-extend: hi = all-ones if x<0.
  // For x = INT_MIN (0x80000000), pack is 0x8000000080000000 but sext is
  // 0xFFFFFFFF80000000 — x2abs32s then fails the INT_MIN sat check
  // (BundleSim intrin_x2simd exit 6).
  // G_SEXT i32→i64 already selects SEXT_GPR32_TO_DR64 directly. Leave
  // MOV_GPR_TO_DR64(x,x) for true dual-lane replicate / pack.
  (void)MovInst;
  (void)MRI;
  (void)TII;
  return false;
}

bool HaydnPostSelectOptimize::tryFoldMove32DrToSw(MachineInstr &MovInst,
                                                   MachineRegisterInfo &MRI,
                                                   const HaydnInstrInfo &TII) {
  // MovInst: %gpr = MOVE32_DR_L/H %dr
  unsigned MovOpc = MovInst.getOpcode();
  if (MovOpc != Haydn::MOVE32_DR_L && MovOpc != Haydn::MOVE32_DR_H)
    return false;

  Register GprDst = MovInst.getOperand(0).getReg();
  if (!GprDst.isVirtual())
    return false;

  // The GPR32 result must have exactly one non-debug use, and that use must
  // be an ST32 (the extracted lane goes straight to memory).
  if (!MRI.hasOneNonDBGUse(GprDst))
    return false;

  MachineInstr *St32 = nullptr;
  for (MachineInstr &UseMI : MRI.use_nodbg_instructions(GprDst)) {
    St32 = &UseMI;
    break;
  }
  if (!St32 || St32->getOpcode() != Haydn::S_SW_WITH_IMM)
    return false;

  // Both must be in the same block.
  if (St32->getParent() != MovInst.getParent())
    return false;

  // ST32 operands: $rt(GPR32 data), $rs(GPR32 base), $offset(simm16).
  Register DrSrc = MovInst.getOperand(1).getReg();
  Register Base = St32->getOperand(1).getReg();
  int64_t Off = St32->getOperand(2).getImm();

  // D_SW_L/H_WITH_IMM offset is imm6<<2 (word-scaled, signed). The byte
  // offset must be a multiple of 4 and the word-index must fit in signed
  // 6-bit range (-32..+31, byte range -128..+124).
  if ((Off % 4) != 0)
    return false;
  int64_t ScaledImm = Off >> 2;
  if (!isInt<6>(ScaledImm))
    return false;

  unsigned SwOpc = (MovOpc == Haydn::MOVE32_DR_L) ? Haydn::D_SW_L_WITH_IMM
                                                   : Haydn::D_SW_H_WITH_IMM;

  // reject volatile/atomic; preserve ST32 MMOs on the fused lane-store.
  for (MachineMemOperand *MMO : St32->memoperands()) {
    if (MMO->isVolatile() || MMO->isAtomic())
      return false;
  }

  // Build the lane-store in ST32's position: the DR64 data goes directly to
  // memory, skipping the GPR32 extract entirely.
  // D_SW_L/H_WITH_IMM $rtd(DR64), $rs(GPR32 base), $scaled_imm(imm6)
  bool DrKill = MovInst.getOperand(1).isKill();
  MachineInstrBuilder MIB =
      BuildMI(*St32->getParent(), St32, St32->getDebugLoc(), TII.get(SwOpc))
          .addReg(DrSrc, getKillRegState(DrKill))
          .addReg(Base)
          .addImm(ScaledImm);
  MIB.cloneMemRefs(*St32);

  LLVM_DEBUG(dbgs() << "Folded MOVE32_DR + ST32 into D_SW_L/H_WITH_IMM: "
                    << MovInst << "\n");

  ++NumLaneStoreFolds;
  St32->eraseFromParent();
  MovInst.eraseFromParent();
  return true;
}


// Same-block CSE of MOV_GPR_TO_DR64 with constant GPR32 sources.
// AIE PostSelect only cleans physreg identity COPYs (in-block tracker)
// it does not invent function-wide virt-const CSE. We keep a *local* CSE
// because MOV_GPR_TO_DR64 expands to a costly stack round-trip; reusing an
// earlier same-BB def is dominance-safe (defs earlier in the block dominate
// later uses in SSA layout order within one MBB) and matches AIE's local
// tracker spirit. Cross-block reuse is left to generic MachineCSE once
// materialization is a real selected MOV rather than a pseudo expand.
bool HaydnPostSelectOptimize::tryCSEConstantDR64(MachineInstr &MovInst,
                                                   MachineRegisterInfo &MRI,
                                                   const HaydnInstrInfo &TII) {
  // MovInst: %dr = MOV_GPR_TO_DR64 %lo, %hi
  if (MovInst.getOpcode() != Haydn::MOV_GPR_TO_DR64)
    return false;

  // Opcode-matching says nothing about operand KINDS. Every getReg/getImm
  // below has to be guarded, because an opcode can reach this pass carrying a
  // global address, a frame index or a symbol where the .td declares a plain
  // register or immediate -- and MachineOperand::getReg() asserts rather than
  // returning anything a caller could check.
  if (!MovInst.getOperand(0).isReg() || !MovInst.getOperand(1).isReg() ||
      !MovInst.getOperand(2).isReg()) {
    LLVM_DEBUG(dbgs() << "DR64 CSE: non-register operand on " << MovInst);
    return false;
  }
  Register DrDst = MovInst.getOperand(0).getReg();
  if (!DrDst.isVirtual())
    return false;

  Register LoReg = MovInst.getOperand(1).getReg();
  Register HiReg = MovInst.getOperand(2).getReg();

  // Look through the GPR32 defs to find constant values.
  // Supported constant forms: ADDI32 r0, C and LOADI32 C.
  auto getConstVal = [&](Register R, int64_t &Val) -> bool {
    if (!R.isVirtual())
      return false;
    MachineInstr *Def = MRI.getVRegDef(R);
    if (!Def)
      return false;
    if (Def->getOpcode() == Haydn::ADDI32) {
      // `addi32 rd, r0, C` is the constant form. Anything else wearing this
      // opcode -- an @global in the immediate slot, a frame index in the
      // source -- is not a constant this pass can fold, and asking it for a
      // register or an immediate would abort rather than say so.
      if (!Def->getOperand(1).isReg() || !Def->getOperand(2).isImm()) {
        LLVM_DEBUG(dbgs() << "DR64 CSE: ADDI32 is not the constant form: "
                          << *Def);
        return false;
      }
      if (Def->getOperand(1).getReg() != Haydn::R0)
        return false;
      Val = Def->getOperand(2).getImm();
      return true;
    }
    if (Def->getOpcode() == Haydn::LOADI32) {
      if (!Def->getOperand(1).isImm()) {
        LLVM_DEBUG(dbgs() << "DR64 CSE: LOADI32 without an immediate: "
                          << *Def);
        return false;
      }
      Val = Def->getOperand(1).getImm();
      return true;
    }
    return false;
  };

  int64_t LoVal, HiVal;
  if (!getConstVal(LoReg, LoVal) || !getConstVal(HiReg, HiVal))
    return false;

  // Same basic block only: walk predecessors of MovInst in layout order.
  MachineBasicBlock *MBB = MovInst.getParent();
  for (MachineInstr &MI : *MBB) {
    if (&MI == &MovInst)
      break;
    if (MI.getOpcode() != Haydn::MOV_GPR_TO_DR64)
      continue;
    if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isReg())
      continue;
    Register OtherLo = MI.getOperand(1).getReg();
    Register OtherHi = MI.getOperand(2).getReg();
    int64_t OtherLoVal, OtherHiVal;
    if (!getConstVal(OtherLo, OtherLoVal) || !getConstVal(OtherHi, OtherHiVal))
      continue;
    if (OtherLoVal == LoVal && OtherHiVal == HiVal) {
      Register ExistingDr = MI.getOperand(0).getReg();
      LLVM_DEBUG(dbgs() << "Same-BB CSE MOV_GPR_TO_DR64 constant (" << LoVal
                        << ", " << HiVal << "): " << MovInst << " -> from "
                        << MI << "\n");
      BuildMI(*MBB, MovInst, MovInst.getDebugLoc(), TII.get(Haydn::OR64),
              DrDst)
          .addReg(ExistingDr)
          .addReg(ExistingDr);
      ++NumDR64ConstCSE;
      MovInst.eraseFromParent();
      return true;
    }
  }

  return false;
}

bool HaydnPostSelectOptimize::elideCrossBankRoundTrips(MachineFunction &MF) {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  // Iterate to a fixed point: each fold removes two instructions and may
  // expose a new fold opportunity (rare, but cheap to handle).
  bool LocalChanged = true;
  while (LocalChanged) {
    LocalChanged = false;
    for (MachineBasicBlock &MBB : MF) {
      SmallVector<MachineInstr *, 16> MovCandidates;
      for (MachineInstr &MI : MBB) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64 || Opc == Haydn::MOV_DR64_TO_GPR ||
            Opc == Haydn::MOVE32_DR_L || Opc == Haydn::MOVE32_DR_H)
          MovCandidates.push_back(&MI);
      }
      for (MachineInstr *MI : MovCandidates) {
        if (!MI->getParent())
          continue;
        unsigned Opc = MI->getOpcode();
        if (Opc == Haydn::MOV_GPR_TO_DR64) {
          // CSE + sext stub (pack≠sext; see tryFoldSextMovToDirect).
          if (tryCSEConstantDR64(*MI, MRI, TII) ||
              tryFoldSextMovToDirect(*MI, MRI, TII)) {
            Changed = true;
            LocalChanged = true;
          }
        } else if (Opc == Haydn::MOVE32_DR_L || Opc == Haydn::MOVE32_DR_H) {
          // Lane-store fusion with MMO clone + vol/atomic reject.
          if (tryFoldMove32DrToSw(*MI, MRI, TII)) {
            Changed = true;
            LocalChanged = true;
          }
        }
      }
    }
  }

  return Changed;
}

FunctionPass *llvm::createHaydnPostSelectOptimizePass() {
  return new HaydnPostSelectOptimize();
}
