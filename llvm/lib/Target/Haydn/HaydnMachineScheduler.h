//===-- HaydnMachineScheduler.h - Haydn dual MI scheduler ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE2 dual-scheduler contract (topics/scheduling/TOPIC.md):
//
// Pre-RA: HaydnScheduleDAGMILive + HaydnPreRASchedStrategy
// (pressure / live-range order for RA; CopyConstrain + mutations).
// createMachineScheduler MUST never return nullptr.
//
// Post-RA: HaydnScheduleDAGMI + HaydnPostRASchedStrategy + HazardRecognizer
// (sole owner of final VLIW pack in leaveRegion/leaveMBB).
// Multi-stage SMS (HaydnPostRAMultiStage) optional after ordinary convergence; default OFF.
//
// Do NOT revive VLIWMachineScheduler / ConvergingVLIWScheduler (UAF).
// Do NOT use bare GenericScheduler via nullptr factory fallback.
//
// Plan: /ssd2/mhyang/haydn-plans/topics/scheduling/TOPIC.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMACHINESCHEDULER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMACHINESCHEDULER_H

#include "HaydnPreRASchedStrategy.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include <memory>

namespace llvm {

// Post-RA ScheduleDAGMI subclass. Overrides exitRegion to form bundles.
class HaydnScheduleDAGMI : public ScheduleDAGMI {
public:
  HaydnScheduleDAGMI(MachineSchedContext *C,
                     std::unique_ptr<MachineSchedStrategy> S, bool IsPreRA)
      : ScheduleDAGMI(C, std::move(S), IsPreRA) {}

  /// AIE `AIEPostRASchedStrategy::buildGraph` passes `Context->AA` into
  /// `buildEdges` (`AIEMachineScheduler.cpp:1792`). This is that same
  /// `ScheduleDAGMI::AA` without calling protected `getAAForDep`.
  AAResults *getAliasAnalysis() const { return AA; }

  void exitRegion() override;
  void schedule() override;
};

// Create the post-RA scheduler DAG for Haydn.
ScheduleDAGInstrs *createHaydnPostRAScheduler(MachineSchedContext *C);

// createHaydnPreRAScheduler declared in HaydnPreRASchedStrategy.h

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMACHINESCHEDULER_H
