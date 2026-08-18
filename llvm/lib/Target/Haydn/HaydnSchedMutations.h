//===-- HaydnSchedMutations.h - Pre/Post-RA DAG mutations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE dual-sched mutation factories.
//
// Pre-RA: PropagateIncomingLatencies, EnforceCopyEdges, FuncArgCopyEdges
// ( CopyConstrain in createHaydnPreRAScheduler)
// Post-RA: RegionEndEdges (MaxLatencyFinder + successorsAreScheduled;
// default off; -haydn-postra-interblock drops stage latency when
// successors are scheduled — AIE IncludeStages brick, no PerSuccEdges
// remaining-latency invent), MemoryEdges (ExactLatencies),
// MachineSchedWAWEdges (SFR/CBR; default off).
//
// Parked (no Haydn peer): LockDelays, BiasDepth, EmitFixedSUnits
// WAWStickyRegistersEdges. PerSuccEdges replay is residual.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H

#include "llvm/CodeGen/MachineScheduler.h"
#include <memory>
#include <vector>

namespace llvm {

std::vector<std::unique_ptr<ScheduleDAGMutation>> getHaydnPreRAMutations();
std::vector<std::unique_ptr<ScheduleDAGMutation>> getHaydnPostRAMutations();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNSCHEDMUTATIONS_H
