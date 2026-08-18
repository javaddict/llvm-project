//===-- HaydnRegisterBankInfo.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file declares the targeting of the RegisterBankInfo class for Haydn.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNREGISTERBANKINFO_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNREGISTERBANKINFO_H

#include "llvm/CodeGen/RegisterBankInfo.h"

namespace llvm {
class MachineIRBuilder;
}

#define GET_REGBANK_DECLARATIONS
#include "HaydnGenRegisterBank.inc"

namespace llvm {

class TargetRegisterInfo;

class HaydnGenRegisterBankInfo : public RegisterBankInfo {
protected:
  enum PartialMappingIdx {
    PMI_None = -1,
    PMI_GPR32 = 1,
    PMI_DR64 = 2,
    PMI_AR = 3,
  };

  static const RegisterBankInfo::PartialMapping PartMappings[];
  static const RegisterBankInfo::ValueMapping ValMappings[];

#define GET_TARGET_REGBANK_CLASS
#include "HaydnGenRegisterBank.inc"
};

// This class provides the information for the target register banks.
class HaydnRegisterBankInfo final : public HaydnGenRegisterBankInfo {
public:
  HaydnRegisterBankInfo(unsigned HwMode = 0);

  /// Cost of A = COPY B. Same-bank is 0 (coalesced). GPR32↔DR64 is a
  /// pack/extract or stack round-trip, not a coalescable copy.
  unsigned copyCost(const RegisterBank &A, const RegisterBank &B,
                    TypeSize Size) const override;

  const InstructionMapping &
  getInstrMapping(const MachineInstr &MI) const override;

  /// Identity G_OR is a Cost=0 same-bank copy. AIE alts are PTR-vs-GPR;
  /// Haydn has no PTR bank and does not invent one.
  InstructionMappings
  getInstrAlternativeMappings(const MachineInstr &MI) const override;

  /// AIE applyMappingImpl (AIEBaseRegisterBankInfo.cpp:183) applies the
  /// default mapping for every alternative ID it publishes. ID 1 is the
  /// identity-OR copy-cost mapping.
  void applyMappingImpl(MachineIRBuilder &Builder,
                        const OperandsMapper &OpdMapper) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNREGISTERBANKINFO_H
