//===-- HaydnFrameLowering.h - Define frame lowering for Haydn -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements Haydn-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNFRAMELOWERING_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class HaydnSubtarget;
class BitVector;
class CalleeSavedInfo;
class RegScavenger;

class HaydnFrameLowering : public TargetFrameLowering {
  const HaydnSubtarget &STI;

public:
  // Stack ABI alignment is 8 bytes: DR64 st64/ld64 require 8-byte addresses
  // (BundleSim D_SDW_* MEMORY_FAULT otherwise). Product text parcel is 12
  // bytes — do not confuse code alignment with SP ABI. Call-frame
  // adjustments always round up to this StackAlign (see
  // eliminateCallFramePseudoInstr + CallLowering).
  //
  // Static MaxAlign > StackAlign(8): after FP = incoming SP, realign SP
  // with AND32 of a MatInt(-MaxAlign) mask (RISCVFrameLowering.cpp:1142-1153
  // ANDI overlay; ANDI32 is uimm20 ZEXT so the mask is a scavenged GPR).
  // VLAs + realign stay fail-closed: no BP (folded into FP).
  explicit HaydnFrameLowering(const HaydnSubtarget &STI)
      : TargetFrameLowering(StackGrowsDown,
                            /*StackAlignment=*/Align(8),
                            /*LocalAreaOffset=*/0),
        STI(STI) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  bool hasFPImpl(const MachineFunction &MF) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                           RegScavenger *RS) const override;

  // Reserve emergency spill slots for scavengeFrameVirtualRegs / branch
  // relaxation. AIE model (no free AT): keep one FI so scavenger may spill
  // under pressure. Never always-N>1.
  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  // Match scavenger FI base to the FI base used for locals (Hexagon-style
  // useFPForScavengingIndex). hasFP && !realign → near FP (incoming-SP
  // side). Realign uses post-AND SP for non-fixed locals
  // (RISCVFrameLowering.cpp:1428-1467), so scavenger FIs sit late near
  // final SP (TargetFrameLoweringImpl.cpp:150-157). !hasFP → late SP.
  bool allocateScavengingFrameIndexesNearIncomingSP(
      const MachineFunction &MF) const override;

  // Enable shrink-wrapping so the prologue/epilogue are placed at the optimal
  // points (first use of callee-saved registers) rather than always at entry/exit.
  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  // Reject a shrink-wrap save point when PEI would need a scratch GPR
  // (large SP adjust) and no call-clobbered register is free at block
  // start. Peer: AArch64FrameLowering.cpp:932-970 / PPCFrameLowering.cpp:551-555.
  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;

  // Same at the epilogue insertion point (before first terminator / return),
  // with RetCC R1/R2 excluded. Peer: PPCFrameLowering.cpp:558-561.
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  // Override to prevent PEI from inserting default individual spill stores.
  // Our emitPrologue handles optimized spill sequences (ST32_POST_INC).
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  // Override to prevent PEI from inserting default individual restore loads.
  // Our emitEpilogue handles optimized restore sequences (LD32_POST_INC).
  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   MutableArrayRef<CalleeSavedInfo> CSI,
                                   const TargetRegisterInfo *TRI) const override;

  // Expand ADJCALLSTACKDOWN/UP pseudos to real SP adjustments. Because
  // hasReservedCallFrame is false, outgoing stack args are not pre-reserved
  // in the prologue, so each call must adjust SP around itself. Operand 0 is
  // the byte amount; it is rounded up to StackAlign (8) so SP never lands at
  // ≡4 mod 8 across a call (B1). Operand 1 is the requested align (optional).
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  void determineFrameLayout(MachineFunction &MF) const;

  // Return initial CFA offset value (0 — CFA = SP at function entry).
  int getInitialCFAOffset(const MachineFunction &MF) const override;

  // Return initial CFA register (SP / R13 at function entry).
  Register getInitialCFARegister(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNFRAMELOWERING_H
