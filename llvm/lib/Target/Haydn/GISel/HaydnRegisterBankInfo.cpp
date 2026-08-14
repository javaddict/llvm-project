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
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

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
};

// Index into ValMappings for a given scalar bit width (1/8/16/32/64).
// Vector types and pointer types pick the 64-bit or 32-bit slot respectively.
static unsigned gprMappingIdxForSize(unsigned Size) {
  switch (Size) {
  case 1:  return 0;
  case 8:  return 1;
  case 16: return 2;
  default: return 3;  // 32-bit GPR
  }
}

HaydnRegisterBankInfo::HaydnRegisterBankInfo(unsigned HwMode)
    : HaydnGenRegisterBankInfo(HwMode) {
  // Constructor handled by base class
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
        if (OpTy.getSizeInBits() == 64)
          OpMapping = &ValMappings[4]; // DR64
        else
          OpMapping = &ValMappings[gprMappingIdxForSize(OpTy.getSizeInBits())];
      }
    }
    if (!OpMapping && Src.isReg() && Src.getReg().isPhysical()) {
      if (Haydn::DR64RegClass.contains(Src.getReg()))
        OpMapping = &ValMappings[4]; // DR64
      else
        OpMapping = &ValMappings[3]; // GPR32 (default 32-bit)
    }
    if (!OpMapping)
      return getInstrMappingImpl(MI);

    // COPY-like: NumOperands must be 1 per upstream verify rule. The source
    // is implicitly the same bank as the destination.
    return getInstructionMapping(/*ID*/ DefaultMappingID, /*Cost*/ 1,
                                  /*OperandsMapping*/ getOperandsMapping({OpMapping}),
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
        const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg);
        assert((!RC || RC != &Haydn::ARRegClass) &&
               "64-bit AR operand must not map to DR64 by size");
        if (RC == &Haydn::ARRegClass)
          OpMappings[Idx] = &ValMappings[5]; // AR
        else
          OpMappings[Idx] = &ValMappings[4]; // DR64
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
