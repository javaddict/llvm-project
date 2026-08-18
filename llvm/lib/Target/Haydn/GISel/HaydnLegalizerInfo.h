//===-- HaydnLegalizerInfo.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file declares the targeting of the MachineLegalizer class for Haydn.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNLEGALIZERINFO_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNLEGALIZERINFO_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {

class HaydnSubtarget;

class HaydnLegalizerInfo : public LegalizerInfo {
public:
  HaydnLegalizerInfo(const HaydnSubtarget &ST);

  bool legalizeCustom(LegalizerHelper &Helper, MachineInstr &MI,
                      LostDebugLocObserver &LocObserver) const override;

  /// Lower llvm.vacopy / llvm.vaend. Peer: AArch64LegalizerInfo.cpp:1706
  /// and RISCVLegalizerInfo.cpp:776.
  bool legalizeIntrinsic(LegalizerHelper &Helper,
                         MachineInstr &MI) const override;

private:
  /// Initialize the 5×i32 structured va_list from save-area frame indices.
  /// Peer: RISCVLegalizerInfo.cpp:812 legalizeVAStart.
  bool legalizeVAStart(LegalizerHelper &Helper, MachineInstr &MI) const;

  /// Two-bank va_arg with stack overflow. Peer: AArch64LegalizerInfo.cpp:2158
  /// (straight-line list walk). Haydn selects GPR vs DR cursor and overflows
  /// onto __stack with G_SELECT (no post-RA CFG). Pointer dest is the
  /// aggregate Indirect path (GPR cursor, 4-byte step).
  bool legalizeVAArg(LegalizerHelper &Helper, MachineInstr &MI) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNLEGALIZERINFO_H
