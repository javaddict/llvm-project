//===-- HaydnATScratch.h - R12 VASTART/late address scratch ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE model: no free assembler temporary. R12 is a normal allocatable
// caller-saved GPR.
//
// This helper is for VASTART / VACOPY (and residual SET_HWLOOP) address-math
// that still uses a fixed R12 phys for emit-time sequences, with PEI
// R12ScratchFI spill/restore around use.
//
// Pure MatInt (LOADI64 expandPostRAPseudo) should prefer HaydnPostRAScratch
// (scavenged live-checked GPR) rather than hardcoding R12 as MatInt dest.
// Residual withMaterializeScratch callers still get an always-spill bracket.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNATSCRATCH_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNATSCRATCH_H

#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class DebugLoc;
class HaydnSubtarget;
class TargetInstrInfo;

namespace HaydnATScratch {

// Phys reg used as VASTART / VACOPY address-math scratch (not free AT).
inline Register phys() { return Haydn::R12; }

// Bracket a MachineInstr-level sequence that needs fixed R12.
// Always spill/restore R12 (AIE model: no free AT) via permanent PEI
// R12ScratchFI (preferred) or temporary SP bracket (MIR without PEI):
// ST R12, [FI]; Fn(R12); LD R12, [FI]
// All BuildMIs in Fn must insert before the same \p I.
void withMaterializeScratch(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            const TargetInstrInfo &TII,
                            const HaydnSubtarget &ST,
                            function_ref<void(Register Scr)> Fn);

} // namespace HaydnATScratch
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNATSCRATCH_H
