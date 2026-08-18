//===-- HaydnRegisterBankInfo.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file implements the targeting of the RegisterBankInfo class for Haydn.
//===----------------------------------------------------------------------===//

#include "HaydnRegisterBankInfo.h"
#include "HaydnRegisterInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "HaydnGenRegisterInfo.inc"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/TypeSize.h"

#define DEBUG_TYPE "haydn-regbankinfo"

#define GET_TARGET_REGBANK_IMPL
#include "HaydnGenRegisterBank.inc"
#undef GET_TARGET_REGBANK_IMPL

using namespace llvm;

// Partial mappings for Haydn register banks
const RegisterBankInfo::PartialMapping HaydnGenRegisterBankInfo::PartMappings[] = {
    // GPR32 holding s1
    {0, 1, Haydn::GPR32RegBank},
    // GPR32 holding s8
    {0, 8, Haydn::GPR32RegBank},
    // GPR32 holding s16
    {0, 16, Haydn::GPR32RegBank},
    // GPR32 (full 32-bit)
    {0, 32, Haydn::GPR32RegBank},
    // DR64 (64-bit)
    {0, 64, Haydn::DR64RegBank},
    // AR (64-bit aligned residual)
    {0, 64, Haydn::ARRegBank},
    // 128-bit residual: lo DR64 [0:64), hi DR64 [64:128)
    {0, 64, Haydn::DR64RegBank},
    {64, 64, Haydn::DR64RegBank},
};

// Value mappings for Haydn
const RegisterBankInfo::ValueMapping HaydnGenRegisterBankInfo::ValMappings[] = {
    // 1-bit value held in GPR32
    {&PartMappings[0], 1},
    // 8-bit value held in GPR32
    {&PartMappings[1], 1},
    // 16-bit value held in GPR32
    {&PartMappings[2], 1},
    // 32-bit GPR operations
    {&PartMappings[3], 1},
    // 64-bit DR operations
    {&PartMappings[4], 1},
    // 64-bit AR operations
    {&PartMappings[5], 1},
    // 65..128-bit residual (packed bitfields / pr79737 s72): two DR64 halves.
    {&PartMappings[6], 2},
};

// Index into ValMappings for a scalar (or residual vector) bit width.
// PartialMapping.Length must be >= the value width or ValueMapping::verify
// fires (pr52979 packed i31+i6 is s37; the old default 32-bit slot is 32).
// 1/8/16 keep the tight GPR32 slots; 17..32 use the full 32-bit slot;
// 33..64 use DR64; 65..128 use the two-part 128-bit slot.
static unsigned gprMappingIdxForSize(unsigned Size) {
  if (Size <= 1)
    return 0;
  if (Size <= 8)
    return 1;
  if (Size <= 16)
    return 2;
  if (Size <= 32)
    return 3;
  if (Size <= 64)
    return 4;
  return 6; // 128-bit (two DR64 halves)
}

// True when Reg is constrained to the AR file — by register class
// (ARRegClass), by register bank (ARRegBank, bank-only vreg), or by being
// an AR physreg. ONE classification rule for every 64-bit mapping decision
// (GOALS W45): the AR constraint wins over the size-derived DR64 default,
// because ARRegBank does not cover DR64RegClass — a blanket 64-bit→DR64
// mapping silently drops the AR constraint (the W36 residual in the
// COPY-like early path).
static bool isARConstrained(Register Reg, const MachineRegisterInfo &MRI) {
  if (!Reg)
    return false;
  if (!Reg.isVirtual())
    return Haydn::ARRegClass.contains(Reg.asMCReg());
  if (const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg))
    return RC == &Haydn::ARRegClass;
  const RegisterBank *Bank = MRI.getRegBankOrNull(Reg);
  return Bank && Bank->getID() == Haydn::ARRegBankID;
}

HaydnRegisterBankInfo::HaydnRegisterBankInfo(unsigned HwMode)
    : HaydnGenRegisterBankInfo(HwMode) {
  // Product banks have different widths, so a legal same-size GPR↔DR
  // COPY cannot appear in MIR. Dump the table so the owning lit pin can
  // lock GPR↔DR = 8 without an illegal size-mismatched COPY.
  LLVM_DEBUG(dbgs() << "HaydnRBI copyCost table: same-GPR="
                    << copyCost(Haydn::GPR32RegBank, Haydn::GPR32RegBank,
                                TypeSize::getFixed(32))
                    << " same-DR="
                    << copyCost(Haydn::DR64RegBank, Haydn::DR64RegBank,
                                TypeSize::getFixed(64))
                    << " GPR-DR="
                    << copyCost(Haydn::GPR32RegBank, Haydn::DR64RegBank,
                                TypeSize::getFixed(32))
                    << " DR-GPR="
                    << copyCost(Haydn::DR64RegBank, Haydn::GPR32RegBank,
                                TypeSize::getFixed(32))
                    << '\n');
}

// AIE and RISCV do not override copyCost (default: 0 same-bank, 1 otherwise).
// AArch64 is the port: GPR↔FPR is FMOV at cost 4/5. Haydn GPR32↔DR64 is
// heavier — MOV_DR64_TO_GPR is two MOVE32 extracts; MOV_GPR_TO_DR64 of two
// live halves is ST32+ST32+LD64 through the pack slot. 8 ≫ same-bank 0 and
// ≫ the default cross-bank 1. Size is unused: the bank pair is the cost.
// Alternative mappings stay empty until a measured miss.
static constexpr unsigned CrossBankGPRDRCopyCost = 8;

unsigned HaydnRegisterBankInfo::copyCost(const RegisterBank &A,
                                         const RegisterBank &B,
                                         TypeSize Size) const {
  if ((&A == &Haydn::GPR32RegBank && &B == &Haydn::DR64RegBank) ||
      (&A == &Haydn::DR64RegBank && &B == &Haydn::GPR32RegBank))
    return CrossBankGPRDRCopyCost;
  return RegisterBankInfo::copyCost(A, B, Size);
}

RegisterBankInfo::InstructionMappings
HaydnRegisterBankInfo::getInstrAlternativeMappings(
    const MachineInstr &MI) const {
  // AIEBaseRegisterBankInfo.cpp:108-180 exposes PTR vs GPR alternatives.
  // Haydn has no PTR bank — do not invent one. Identity G_OR (x | x) is a
  // same-bank copy (TII isCopyInstrImpl on OR32/OR64 rs,rs). Publish Cost=0
  // so RegBankSelect prefers it over the generic Cost=1 mapping. applyMapping
  // keeps default operand banks; the opcode stays G_OR.
  if (MI.getOpcode() == TargetOpcode::G_OR && MI.getNumOperands() >= 3) {
    const MachineFunction &MF = *MI.getMF();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    Register Dst = MI.getOperand(0).getReg();
    Register Src0 = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    if (Dst.isVirtual() && Src0.isVirtual() && Src1 == Src0) {
      LLT Ty = MRI.getType(Dst);
      if (Ty.isValid() && Ty == MRI.getType(Src0)) {
        const ValueMapping *VM =
            Ty.getSizeInBits() == 64
                ? (isARConstrained(Dst, MRI) ? &ValMappings[5]
                                             : &ValMappings[4])
                : &ValMappings[gprMappingIdxForSize(Ty.getSizeInBits())];
        InstructionMappings Alts;
        Alts.push_back(&getInstructionMapping(
            /*ID*/ 1, /*Cost*/ 0, getOperandsMapping({VM, VM, VM}),
            /*NumOperands*/ 3));
        return Alts;
      }
    }
  }
  return RegisterBankInfo::getInstrAlternativeMappings(MI);
}

void HaydnRegisterBankInfo::applyMappingImpl(
    MachineIRBuilder &Builder, const OperandsMapper &OpdMapper) const {
  (void)Builder;
  // ID 1 is the identity-OR copy-cost mapping; same banks as the default.
  applyDefaultMapping(OpdMapper);
}

const RegisterBankInfo::InstructionMapping &
HaydnRegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const unsigned NumOperands = MI.getNumOperands();

  // Handle PHI with the base class (1-operand mapping required)
  if (MI.getOpcode() == TargetOpcode::G_PHI)
    return getInstrMappingImpl(MI);

  // Handle COPY-like instructions specially. Use the same value mapping
  // (sized by the destination type) for both operands so verify is happy.
  if (MI.isCopyLike()) {
    const MachineOperand &Dst = MI.getOperand(0);
    const MachineOperand &Src = MI.getOperand(1);

    const ValueMapping *OpMapping = nullptr;
    if (Dst.isReg() && Dst.getReg().isVirtual()) {
      LLT OpTy = MRI.getType(Dst.getReg());
      if (OpTy.isValid()) {
        // 64-bit SIMD (v2i32/v4i16/v8i8) and s64 → DR64. Residual 32-bit
        // SLP vectors (v4i8/v2i16) share a GPR32 with s32.
        // An AR-constrained 64-bit dst maps to the AR bank (isARConstrained)
        // — W36 residual: the old size-only test blanket-mapped 64-bit to
        // DR64 even when the operand is AR-constrained, and RegBankSelect
        // applies this mapping as a bank reassign, so the AR constraint was
        // silently dropped.
        if (OpTy.getSizeInBits() == 64)
          OpMapping = isARConstrained(Dst.getReg(), MRI)
                          ? &ValMappings[5]   // AR
                          : &ValMappings[4];  // DR64
        else
          OpMapping = &ValMappings[gprMappingIdxForSize(OpTy.getSizeInBits())];
      }
    }
    if (!OpMapping && Src.isReg() && Src.getReg().isPhysical()) {
      if (isARConstrained(Src.getReg(), MRI))
        OpMapping = &ValMappings[5]; // AR
      else if (Haydn::DR64RegClass.contains(Src.getReg()))
        OpMapping = &ValMappings[4]; // DR64
      else
        OpMapping = &ValMappings[3]; // GPR32 (default 32-bit)
    }
    if (!OpMapping && Dst.isReg() && Dst.getReg().isPhysical()) {
      if (isARConstrained(Dst.getReg(), MRI))
        OpMapping = &ValMappings[5]; // AR
      else if (Haydn::DR64RegClass.contains(Dst.getReg()))
        OpMapping = &ValMappings[4]; // DR64
      else
        OpMapping = &ValMappings[3]; // GPR32 (default 32-bit)
    }
    if (!OpMapping)
      return getInstrMappingImpl(MI);

    // COPY-like: NumOperands must be 1 per upstream verify rule. Mapping
    // Cost is the AArch64-shaped copyCost of the two banks (0 same-bank,
    // CrossBankGPRDRCopyCost for GPR32↔DR64).
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    const RegisterBank *DstRB =
        Dst.isReg() && Dst.getReg() ? getRegBank(Dst.getReg(), MRI, TRI)
                                    : nullptr;
    const RegisterBank *SrcRB =
        Src.isReg() && Src.getReg() ? getRegBank(Src.getReg(), MRI, TRI)
                                    : nullptr;
    if (!DstRB)
      DstRB = OpMapping->BreakDown->RegBank;
    if (!SrcRB)
      SrcRB = DstRB;
    TypeSize Size = TypeSize::getFixed(32);
    if (Dst.isReg() && Dst.getReg())
      Size = getSizeInBits(Dst.getReg(), MRI, TRI);
    return getInstructionMapping(/*ID*/ DefaultMappingID,
                                 copyCost(*DstRB, *SrcRB, Size),
                                 getOperandsMapping({OpMapping}),
                                 /*NumOperands*/ 1);
  }

  // For non-COPY instructions, map each register operand to the appropriate bank based on its type.
  // Non-register operands (immediates, MBB refs) get nullptr.
  SmallVector<const ValueMapping *, 8> OpMappings(NumOperands);
  for (unsigned Idx = 0; Idx < NumOperands; ++Idx) {
    if (!MI.getOperand(Idx).isReg()) {
      OpMappings[Idx] = nullptr;
      continue;
    }
    Register Reg = MI.getOperand(Idx).getReg();
    LLT OpTy = MRI.getType(Reg);

    // 64-bit SIMD (v2i32/v4i16/v8i8) and s64 → DR64. Residual 32-bit SLP
    // vectors (v4i8/v2i16) and smaller scalars use GPR32 width slots.
    if (OpTy.isValid()) {
      if (OpTy.getSizeInBits() == 64) {
        // An AR-constrained 64-bit operand must map to the AR bank, not the
        // size-derived DR64 default — that is exactly what the branch below
        // does (class, bank-only, or physreg AR constraint; W45 closed the
        // bank-only gap). The post-branch assert pins the invariant: after
        // mapping, an AR-constrained operand must never carry the DR64
        // mapping.
        if (isARConstrained(Reg, MRI))
          OpMappings[Idx] = &ValMappings[5]; // AR
        else
          OpMappings[Idx] = &ValMappings[4]; // DR64
        assert((!isARConstrained(Reg, MRI) ||
                OpMappings[Idx] == &ValMappings[5]) &&
               "AR-constrained 64-bit operand must map to the AR bank, not "
               "the size-derived DR64 default");
      } else
        OpMappings[Idx] = &ValMappings[gprMappingIdxForSize(OpTy.getSizeInBits())];
    } else {
      OpMappings[Idx] = &ValMappings[3]; // GPR32 default
    }
  }

  return getInstructionMapping(/*ID*/ DefaultMappingID, /*Cost*/ 1,
                                /*OperandsMapping*/ getOperandsMapping(OpMappings),
                                NumOperands);
}
