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
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "haydn-reginfo"

#define GET_REGINFO_TARGET_DESC
#include "HaydnGenRegisterInfo.inc"

using namespace llvm;

// Soft compact-subset physreg order only. Full-only product has no compact
// byte win yet; default OFF so dual-run -stats (InlineSpiller spills/reloads,
// these Hit/Miss counters, post-RA multi-MI finalize; split/hard-root silent)
// — not the hook — gate any future enable. Never demotes RC; never overrides
// base hints. Corpus pins: regalloc-compact-hints.ll + hard-bundle interop in
// format-bundle-through-ra.mir under compact-hints=true.
static cl::opt<bool> EnableHaydnRACompactHints(
    "haydn-ra-compact-hints", cl::init(false), cl::Hidden,
    cl::desc("Prefer GPR32Lo / low-DR physregs in Haydn getRegAllocationHints "
             "(metrics-gated soft order; Full-only product has no compact "
             "byte win until compact format activation)"));

STATISTIC(NumHaydnCompactRAHintsHit,
          "Haydn compact-subset RA hints applied (at least one physreg added)");
STATISTIC(NumHaydnCompactRAHintsMiss,
          "Haydn compact-subset RA hints enabled but no eligible physreg added");

bool llvm::isHaydnCompactSubsetPhysReg(const TargetRegisterInfo &TRI,
                                       MCPhysReg PhysReg) {
  // GPR32Lo: R0–R7 (3-bit compact form encodings). R0 is reserved at use time.
  if (Haydn::GPR32LoRegClass.contains(PhysReg))
    return true;
  // Low-DR encodings D0–D7: peer soft affinity of GPR32Lo for DR64 bank.
  if (Haydn::DR64RegClass.contains(PhysReg) &&
      TRI.getEncodingValue(PhysReg) <= 7)
    return true;
  return false;
}

HaydnRegisterInfo::HaydnRegisterInfo(unsigned HwMode)
    : HaydnGenRegisterInfo(Haydn::R15, 0, 0, 0, HwMode) {}

const uint32_t *
HaydnRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID Id) const {
  (void)Id;
  (void)MF;
  // Single CSR mask: R8–R11, R14, R15, D8–D15. R14 always callee-saved
  // (s0-style); hasFP only reserves + force-saves it as frame base.
  return CSR_Haydn_RegMask;
}

Register HaydnRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const HaydnFrameLowering *TFI = MF.getSubtarget<HaydnSubtarget>().getFrameLowering();
  // R14 is a GPR; when hasFP it is the frame base (BP folded into FP).
  return TFI->hasFP(MF) ? Haydn::R14 : Haydn::R13;
}

bool HaydnRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  // No separate BP — all R* are GPRs; BP role is on R14 when hasFP.
  (void)MF;
  return false;
}

Register HaydnRegisterInfo::getBaseRegister() const { return Haydn::R14; }

BitVector HaydnRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFI = ST.getFrameLowering();
  BitVector Reserved(getNumRegs());

  // Reserve R0 as soft-zero. Short-lived expand/frame/remat may borrow R0
  // when no soft-zero user, then XOR32 R0,R0,R0 to restore.
  markSuperRegs(Reserved, Haydn::R0);

  // R12 is a normal allocatable caller-saved GPR — do not reserve as free AT.

  // Reserve R13 (SP) and R15 (LR) always
  markSuperRegs(Reserved, Haydn::R13); // SP
  markSuperRegs(Reserved, Haydn::R15); // LR

  // R14 is a normal GPR; reserve only when it is the live frame base.
  if (TFI->hasFP(MF))
    markSuperRegs(Reserved, Haydn::R14);

  // SFR is a 4-bit status-flag register, never allocatable.
  markSuperRegs(Reserved, Haydn::SFR);

  // CBR sets are sticky CSR state (programmed via CSRW / SETCBR), never
  // allocatable GPRs. Modeled as physregs for SETCBR→CB scheduling edges.
  markSuperRegs(Reserved, Haydn::CBR0);
  markSuperRegs(Reserved, Haydn::CBR1);

  return Reserved;
}

const MCPhysReg *
HaydnRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  (void)MF;
  // R14 is always in the CSR bank; PEI spills it if used or force-saved
  // (hasFP frame base via determineCalleeSaves).
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
    RegOpc = Haydn::LD32_REG_M0S0LS; // logical; post-RA setDesc / member Desc-as-is at encode
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

  // Scratch for large FI offsets. Always a vreg: PEI FrameIndexVirtualScavenging
  // (RS == nullptr on the first replaceFrameIndices walk) and nested scavenger
  // spill of an emergency FI (RS != nullptr, hasNoVRegs already true) both
  // need one. scavengeFrameVirtualRegs assigns the physreg. R12 is a normal
  // allocatable GPR (AIE: no free AT).
  //
  // hasNoVRegs is true for the whole of PEI (asserted before CSR spill), so it
  // is not a post-PEI signal. gcc-c-torture multi-ix.c ICE'd when a fail-closed
  // treated RS != nullptr as "after PEI". Post-PEI leftover vregs fail the
  // verifier; do not abort the nested PEI scavenger.
  auto getScratch = [&]() -> Register {
    return MF.getRegInfo().createVirtualRegister(&Haydn::GPR32RegClass);
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

  // Legal scaled short-form LS: MI immediate is the **element index**
  // (EA = base + (imm << log2(width))), not the byte offset. Convert now so
  // Format E encode and BundleSim dump agree with the golden model.
  if (IsTrackedLS && Scale > 1) {
    assert(OffsetVal % static_cast<int64_t>(Scale) == 0 &&
           "scaled LS offset must be width-aligned");
    int64_t Element = OffsetVal / static_cast<int64_t>(Scale);
    if (MI.getNumOperands() > FIOperandNum + 1 &&
        MI.getOperand(FIOperandNum + 1).isImm())
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Element);
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

  // Soft physreg ordering priority:
  //   1. base copy/coalesce (already returned if non-empty)
  //   2. compact-subset (GPR32Lo / low-DR) when -haydn-ra-compact-hints
  //   3. caller-saved preference (existing)
  // Soft order only — never demotes RC; never freezes format/member choice.
  //
  // All R* are GPRs. Soft roles: R0 soft-zero; R13 SP; R14 CSR (fp when
  // hasFP); R15 LR. CSR bank: R8–R11, R14. R12 caller-saved (not free AT).
  // D0–D7 caller-saved / D8–D15 CSR DR banks.

  SmallSet<MCPhysReg, 16> HintedRegs;
  for (MCPhysReg PhysReg : Hints)
    HintedRegs.insert(PhysReg);

  const TargetRegisterClass *RC = MRI.getRegClass(VirtReg);

  auto tryAddHint = [&](MCPhysReg PhysReg) -> bool {
    if (HintedRegs.count(PhysReg))
      return false;
    if (MRI.isReserved(PhysReg))
      return false;
    if (!RC->contains(PhysReg))
      return false;
    // Hints must be members of AllocationOrder (AllocationOrder.cpp assert).
    if (!is_contained(Order, PhysReg))
      return false;
    Hints.push_back(PhysReg);
    HintedRegs.insert(PhysReg);
    return true;
  };

  // (2) Compact-subset soft order — metrics-gated, default OFF.
  if (EnableHaydnRACompactHints) {
    unsigned Added = 0;
    for (MCPhysReg PhysReg : Order) {
      if (!isHaydnCompactSubsetPhysReg(*this, PhysReg))
        continue;
      if (tryAddHint(PhysReg))
        ++Added;
    }
    if (Added)
      ++NumHaydnCompactRAHintsHit;
    else
      ++NumHaydnCompactRAHintsMiss;
  }

  // (3) Caller-saved preference: R1–R7 + R12; D0–D7. Reduces CSR spill cost
  // for short temps. After compact subset when that path is enabled.
  auto isCallerSaved = [&](MCPhysReg Reg) -> bool {
    if (Haydn::GPR32RegClass.contains(Reg)) {
      unsigned Enc = getEncodingValue(Reg);
      return (Enc >= 1 && Enc <= 7) || Enc == 12;
    }
    if (Haydn::DR64RegClass.contains(Reg)) {
      unsigned Enc = getEncodingValue(Reg);
      return Enc <= 7;
    }
    return false;
  };

  for (MCPhysReg PhysReg : Order) {
    if (!isCallerSaved(PhysReg))
      continue;
    tryAddHint(PhysReg);
  }

  return BaseRetVal;
}
