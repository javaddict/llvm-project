//===-- HaydnPipelinerLoopInfo.h - SMS PipelinerLoopInfo ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn PipelinerLoopInfo. Peer: AIEBasePipelinerLoopInfo.h / .cpp
// (AIEBasePipelinerLoopInfo.h:24 class; AIEBasePipelinerLoopInfo.cpp:1-14
// file split from InstrInfo; AIE2InstrInfo.cpp:59-64 FormatInterface).
// Product pre-RA SMS / hwloop stay default-gated in shouldUseSchedule.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DebugLoc.h"
#include <optional>

namespace llvm {

class HaydnInstrInfo;
class SwingSchedulerDAG;
class SMSchedule;

// Target-specific loop info for the MachinePipeliner (Swing Modulo Scheduling).
// Mirrors the ARM structure (see ARMBaseInstrInfo.cpp): identify the loop's
// conditional terminator (EndLoop) and the comparison instruction that sets
// its condition register (CmpMI), plus a runtime trip-count register
// (TripCountReg). Trip-count handling is delegated to the target-independent
// ModuloScheduleExpander via createTripCountGreaterCondition
// adjustTripCount, both of which ALWAYS emit a runtime comparison and return
// nullopt -- there is no static-trip-count shortcut. The hand-rolled
// `(limit - init) / step` derivation was a silent wrong-code bug (Blocker-1
// lesson): it returned a compile-time bool that drove
// `PeelingModuloScheduleExpander::fixupBranches` into the static-false
// (`KernelDisposed`) branch, collapsing countable loops to ~1 iteration with
// `-verify-machineinstrs` still green. See and lesson.
// Trip-count source : for a DECREMENTING IV (the shape LSR
// produces for real DSP loops -- `iv -= step` until `iv == 0`), TripCountReg
// is the IV's preheader *init* value (the IV counts N, N-1,..., 0, so its
// initial value IS the trip count). For an INCREMENTING IV (`iv < limit`)
// TripCountReg is the compare's non-IV operand (the upper bound). Taking the
// compare's non-IV operand unconditionally was the matrix_test crash root
// cause: for decrementing loops that operand is a function-wide materialized
// zero (`ADDI32 $r0, 0`), and adjustTripCount's replaceRegWith on it
// corrupted every compare/PHI-init in the function. See AIE's DownCountLoop
// (sms-packetizer-deep-dive.md §2c) for the same init-based derivation.
class HaydnPipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  MachineFunction *MF;
  const HaydnInstrInfo *HII;
  MachineInstr *EndLoop;
  MachineInstr *CmpMI;
  // Optional XORI invert on the latch condition path (see HaydnCountableLoop).
  MachineInstr *InvertMI = nullptr;
  Register TripCountReg;
  // Cached loop basic block — set in the constructor because setPreheader
  // may erase EndLoop (when the expander clones the kernel)
  // making EndLoop->getParent unsafe in later adjustTripCount calls.
  MachineBasicBlock *LoopBB;
  DebugLoc DL;
  // ZOL (Zero-Overhead Loop) mode. When true, the loop is already in
  // hardware-loop form (LoopStart in preheader + PseudoLoopEnd in latch).
  // SMS pipelines it directly, editing LoopStart's adj operand for trip-count
  // adjustment. Mirrors AIE's ZeroOverheadLoop
  // (AIEBasePipelinerLoopInfo.cpp:644-835).
  bool IsZOL = false;
  MachineInstr *LoopStart = nullptr;
  // AIE MinTripCount peer (AIEBasePipelinerLoopInfo). 0 = unknown/unbounded.
  // ZOL SMS is only safe when MinTripCount is known and large enough to cover
  // prologue stages without a dynamic guard (ZOL cannot reverse its exit).
  int64_t MinTripCount = 0;

public:
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *CmpMI,
                         Register TripCountReg,
                         MachineInstr *InvertMI = nullptr)
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(CmpMI), InvertMI(InvertMI),
        TripCountReg(TripCountReg), LoopBB(EndLoop->getParent()),
        DL(EndLoop->getDebugLoc()) {}

  // ZOL constructor — for loops already in hardware-loop form.
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *LoopStart,
                         int64_t MinTripCount)
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(nullptr), InvertMI(nullptr),
        TripCountReg(), LoopBB(EndLoop->getParent()),
        DL(EndLoop->getDebugLoc()), IsZOL(true), LoopStart(LoopStart),
        MinTripCount(MinTripCount) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override;

  // Pre-RA SMS containment: reject every StageCount > 1 (no issue-cycle
  // identity crosses RA; ZOL and soft counted alike — closes inverted ZOL
  // multi-stage gate). Also reject ZOL StageCount <= 1 (no overlap), PPS-3
  // stage-count / reg-pressure gates, and AIE ZeroOverheadLoop
  // MaxStageCount >= MinTripCount. Accepted StageCount==1 schedules stay
  // bare logical MIs (proven counted residual only). Metrics live on the
  // MachinePipeliner success remark — no generic post-expand virtual.
  // Costs hwloop geometry on final parcels (kernel II + MinBodyBundles pad);
  // SetupIssueDistance is preheader→BEGIN, never an II>=Setup floor.
  bool shouldUseSchedule(SwingSchedulerDAG &SSD, SMSchedule &SMS) override;

  std::optional<bool>
  createTripCountGreaterCondition(int TC, MachineBasicBlock &MBB,
                                  SmallVectorImpl<MachineOperand> &Cond) override;

  void adjustTripCount(int TripCountAdjust) override;

  void setPreheader(MachineBasicBlock *NewPreheader) override;
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H
