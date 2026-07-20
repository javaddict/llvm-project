//===-- HaydnRegisterInfo.cpp - Haydn Register Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Haydn implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "HaydnRegisterInfo.h"

#include "Haydn.h"
#include "HaydnFrameLowering.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"

#define GET_REGINFO_TARGET_DESC
#include "HaydnGenRegisterInfo.inc"

using namespace llvm;

HaydnRegisterInfo::HaydnRegisterInfo(unsigned HwMode)
    : HaydnGenRegisterInfo(Haydn::R15, 0, 0, 0, HwMode) {}

const uint32_t *
HaydnRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID Id) const {
  return CSR_Haydn_RegMask;
}

Register HaydnRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const HaydnFrameLowering *TFI = MF.getSubtarget<HaydnSubtarget>().getFrameLowering();
  return TFI->hasFP(MF) ? Haydn::R14 : Haydn::R13;
}

BitVector HaydnRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFI = ST.getFrameLowering();
  BitVector Reserved(getNumRegs());

  // Reserve R0 as soft-zero register (initialized to 0 in prologue, never
  // allocated). The codegen uses R0 as a constant-zero source for ADDI32
  // copyPhysReg, and constant materialization.
  markSuperRegs(Reserved, Haydn::R0);

  // R12 is a normal allocatable caller-saved GPR (AIE model: no free AT).
  // MatInt / VASTART use HaydnPostRAScratch (free GPR first; PostRAScratchFI spill home).

  // Reserve R13 (SP) and R15 (LR) always
  markSuperRegs(Reserved, Haydn::R13); // SP
  markSuperRegs(Reserved, Haydn::R15); // LR

  // Reserve R14 (FP) only when the function has a dedicated frame pointer
  // (-mattr=+frame-pointer, -fno-omit-frame-pointer, or ABI-required).
  if (TFI->hasFP(MF))
    markSuperRegs(Reserved, Haydn::R14); // FP

  // SFR is a 4-bit status-flag register, never allocatable. Mark reserved so
  // MachineVerifier allows Uses=[SFR] on cmovs even when the preceding compare
  // is outside the function under test (simd-masked-compare.ll isolated
  // movt/movf cases). Compare ops still Defs=[SFR] for true RAW edges.
  markSuperRegs(Reserved, Haydn::SFR);

  return Reserved;
}

const MCPhysReg *
HaydnRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_Haydn_SaveList;
}

const uint32_t *HaydnRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

bool HaydnRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const HaydnFrameLowering *TFI = MF.getSubtarget<HaydnSubtarget>().getFrameLowering();
  const HaydnInstrInfo *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  // Get the frame index operand
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Get the frame index reference
  Register FrameReg;
  StackOffset Offset =
      TFI->getFrameIndexReference(MF, FrameIndex, FrameReg);

  // Adjust offset by SPAdj (stack pointer adjustment due to calls)
  if (SPAdj)
    Offset += StackOffset::getFixed(SPAdj);

  int64_t OffsetVal = Offset.getFixed();

  // Fold in any existing immediate operand (e.g. LD32 $rd, $fi, +off).
  if (MI.getNumOperands() > FIOperandNum + 1 &&
      MI.getOperand(FIOperandNum + 1).isImm()) {
    OffsetVal += MI.getOperand(FIOperandNum + 1).getImm();
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(OffsetVal);
  } else {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    MI.addOperand(MachineOperand::CreateImm(OffsetVal));
  }

  // F33: short-form load/store immediates are golden scaled simm6
  // (EA = base + (simm6 << log2(access_width))). Anything outside that range
  // must rebase (base+offset in a reg, imm 0) or use a WITH_REG form. Never
  // truncate. simm16 overflow is handled by the same rebase path below.
  unsigned Opc = MI.getOpcode();
  unsigned Scale = 0;
  unsigned RegOpc = 0;
  switch (Opc) {
  case Haydn::LD32:
    Scale = 4;
    RegOpc = Haydn::LD32_REG_M0S0LS; // logical; MC FlexMaps → *_S0
    break;
  case Haydn::ST32:
    Scale = 4;
    RegOpc = Haydn::ST32_REG_M0S0LS;
    break;
  case Haydn::LD64:
    Scale = 8;
    RegOpc = Haydn::LD64_REG_M0S0LS;
    break;
  case Haydn::ST64:
    Scale = 8;
    RegOpc = Haydn::ST64_REG_M0S0LS;
    break;
  case Haydn::ST8:
  case Haydn::LD8:
  case Haydn::LDU8:
    Scale = 1;
    break;
  case Haydn::ST16:
  case Haydn::LD16:
  case Haydn::LDU16:
    Scale = 2;
    break;
  default:
    break;
  }

  auto isLegalScaledSimm6 = [](int64_t ByteOff, unsigned Sc) {
    return Sc != 0 && (ByteOff % static_cast<int64_t>(Sc)) == 0 &&
           isInt<6>(ByteOff / static_cast<int64_t>(Sc));
  };

  // If this is not an LS we know how to range-check, keep the legacy simm16
  // guard for other FI users (e.g. ADDI-like shapes that still take simm16).
  bool IsTrackedLS = Scale != 0;
  bool OffsetLegal =
      IsTrackedLS ? isLegalScaledSimm6(OffsetVal, Scale) : isInt<16>(OffsetVal);

  // Scratch for large FI offsets: always a vreg; scavengeFrameVirtualRegs
  // assigns a phys reg (may spill once to an emergency FI). R12 is a normal
  // allocatable GPR (AIE model: no free AT). requiresFrameIndexScavenging
  // means PEI does not pass RS here — RISC-V model, no nested phys scavenge
  // inside eliminateFrameIndex.
  //
  // Post-PEI callers (e.g. branch-relax manual spill of the emergency FI)
  // only hit this path when the FI offset is illegal; emergency FIs are
  // placed near FP/SP so they stay in scaled simm6 and skip this path.
  auto getScratch = [&]() -> Register {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    return MRI.createVirtualRegister(&Haydn::GPR32RegClass);
  };

  if (!OffsetLegal && RegOpc) {
    // §6.5 register-offset (WITH_REG) LS path for LD32/ST32/LD64/ST64.
    // Offset in a scratch GPR vreg; base stays FrameReg.
    Register OffReg = getScratch();
    BuildMI(MBB, II, DL, TII->get(Haydn::LOADI32), OffReg).addImm(OffsetVal);
    MI.setDesc(TII->get(RegOpc));
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    MI.getOperand(FIOperandNum + 1).ChangeToRegister(OffReg, false);
    return false;
  }

  if (!OffsetLegal) {
    // Rebase: materialize FrameReg + Offset into a scratch, imm 0 on the MI.
    Register NewBase = getScratch();

    if (isInt<20>(OffsetVal)) {
      BuildMI(MBB, II, DL, TII->get(Haydn::ADDI32_W), NewBase)
          .addReg(FrameReg)
          .addImm(OffsetVal);
    } else {
      BuildMI(MBB, II, DL, TII->get(Haydn::LOADI32), NewBase).addImm(OffsetVal);
      BuildMI(MBB, II, DL, TII->get(Haydn::ADD32), NewBase)
          .addReg(FrameReg)
          .addReg(NewBase);
    }

    MI.getOperand(FIOperandNum).ChangeToRegister(NewBase, false);
    if (MI.getNumOperands() > FIOperandNum + 1 &&
        MI.getOperand(FIOperandNum + 1).isImm())
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    return false;
  }

  return false;
}

bool HaydnRegisterInfo::getRegAllocationHints(
    Register VirtReg, ArrayRef<MCPhysReg> Order,
    SmallVectorImpl<MCPhysReg> &Hints, const MachineFunction &MF,
    const VirtRegMap *VRM, const LiveRegMatrix *Matrix) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // First, collect target-independent copy-based hints (from argument/return
  // lowering and coalescing). These are the highest priority.
  bool BaseRetVal = TargetRegisterInfo::getRegAllocationHints(
      VirtReg, Order, Hints, MF, VRM, Matrix);

  // If we already have a strong hint from the base (e.g. argument register
  // from a COPY), don't dilute it with priority hints.
  if (!Hints.empty())
    return BaseRetVal;

  // For temporaries without a specific hint, add priority hints to prefer
  // caller-saved registers over callee-saved ones. This reduces callee-save
  // spill/restore overhead.
  //
  // Haydn register convention (AIE model: no free AT):
  // R0 = soft-zero (reserved)
  // R1-R7 = arguments/return/caller-saved
  // R8-R11 = callee-saved
  // R12 = normal allocatable caller-saved GPR (not free AT; MatInt
  // scavenges any dead GPR via HaydnPostRAScratch)
  // R13 = SP (reserved)
  // R14 = FP (conditionally reserved; caller-saved when allocatable)
  // R15 = LR (reserved)
  //
  // D0-D7 = caller-saved DR64
  // D8-D15 = callee-saved DR64
  //
  // By hinting caller-saved registers first, the allocator avoids using
  // callee-saved registers for short-lived temporaries, which would require
  // unnecessary save/restore in prologue/epilogue.
  //
  // R12 must be treated as caller-saved so greedy prefers it for
  // short-lived temps like other call-clobbered GPRs, instead of treating it
  // as an unhinted "last resort" that absorbs long-lived values while R8–R11
  // pay CSR cost.

  SmallSet<MCPhysReg, 16> HintedRegs;
  for (MCPhysReg PhysReg : Hints)
    HintedRegs.insert(PhysReg);

  // Check if this virtual register is in GPR32 or DR64.
  const TargetRegisterClass *RC = MRI.getRegClass(VirtReg);

  // Caller-saved ranges include R12. R14 is call-clobbered when allocatable
  // but left unhinted here — PEI/FP interactions make a soft preference for
  // R14 more subtle; CSR avoidance for R1–R7+R12 is enough for.
  auto isCallerSaved = [&](MCPhysReg Reg) -> bool {
    // GPR32 caller-saved: R1-R7 and R12.
    if (Haydn::GPR32RegClass.contains(Reg)) {
      unsigned Enc = getEncodingValue(Reg);
      return (Enc >= 1 && Enc <= 7) || Enc == 12;
    }
    // DR64 caller-saved: D0-D7
    if (Haydn::DR64RegClass.contains(Reg)) {
      unsigned Enc = getEncodingValue(Reg);
      return Enc <= 7;
    }
    return false;
  };

  // Add caller-saved registers as hints first (higher priority).
  for (MCPhysReg PhysReg : Order) {
    if (HintedRegs.count(PhysReg))
      continue;
    if (MRI.isReserved(PhysReg))
      continue;
    if (RC->contains(PhysReg) && isCallerSaved(PhysReg)) {
      Hints.push_back(PhysReg);
      HintedRegs.insert(PhysReg);
    }
  }

  return BaseRetVal;
}
