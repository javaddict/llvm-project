//===-- HaydnPreRASchedStrategy.h - AIE-style pre-RA MI sched ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Peer: AIEPreRASchedStrategy (AIEMachineScheduler.h/cpp).
// Mechanism: MachineSchedStrategy::isAvailableNode (ported from AIE into stock
// MachineScheduler so Haydn can delay pressure-worsening SUnits exactly as AIE
// does — not inventing a dual path).
//
// B4.1: Pre-RA reasons about a feasible FormatID *frontier* without freezing
// FormatID or setDesc (plan §7.1). Product table size 1 = BUNDLE128_FULL →
// productFeasibleFormatMask is always ProductFormatMask for Full-coverable
// occupancy. Same vocabulary as Bundle / HaydnResourceCycle (SMS). Logical
// opcodes only through RA.
//
// Plan: /ssd2/mhyang/haydn-plans/topics/scheduling/TOPIC.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPRERASCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPRERASCHEDSTRATEGY_H

#include "HaydnBundleFormatSolver.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include <cstdint>
#include <vector>

namespace llvm {

// Pre-RA: GenericScheduler + AIE isAvailableNode pressure delayer +
// pressure-aware tryCandidate + B4.1 product FormatID frontier (logical only).
class HaydnPreRASchedStrategy : public GenericScheduler {
public:
  HaydnPreRASchedStrategy(const MachineSchedContext *C)
      : GenericScheduler(C) {}

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  void initialize(ScheduleDAGMI *DAG) override;

  void enterRegion(MachineBasicBlock *BB, MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End, unsigned NumRegionInstrs);
  void leaveRegion(const SUnit &ExitSU);

  // AIE peer: delay nodes that would exceed pressure if a pending reducer
  // exists (AIEMachineScheduler.cpp isAvailableNode).
  bool isAvailableNode(SUnit &SU, SchedBoundary &Zone,
                       bool VerifyReadyCycle) override;

  // B4.1: product FormatID frontier for Pre-RA (logical only; no freeze).
  // AIE has no explicit pre-RA FormatID mask; Haydn names the same occupancy →
  // covering-mask path SMS Bundle uses (AIEBundle.h:150-156 getFormatOrNull /
  // AIEFormat.cpp:18-27 first-covering strengthened to a bitset).
  // Product size-1 Full → ProductFormatMask for empty and Full-covering occ.
  static uint64_t productFeasibleFormatMask(SlotBits Occupied = 0) {
    return haydn::bundle::productFeasibleFormatMask(Occupied);
  }

protected:
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                    SchedBoundary *Zone) const override;

  // Whether delaying DelayedSU for Delayer would form a delay cycle.
  bool canBeDelayed(const SUnit &DelayedSU, const SUnit &Delayer) const;

private:
  static constexpr unsigned UnknownSUNum = ~0u;

  MachineBasicBlock *CurMBB = nullptr;
  std::vector<unsigned> PSetThresholds;
  // SUDelayerMap[SU] = NodeNum of SU we are waiting for (AIE).
  std::vector<unsigned> SUDelayerMap;
};

class HaydnScheduleDAGMILive final : public ScheduleDAGMILive {
public:
  using ScheduleDAGMILive::ScheduleDAGMILive;

  void enterRegion(MachineBasicBlock *BB, MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End,
                   unsigned RegionInstrs) override;
  void exitRegion() override;

  HaydnPreRASchedStrategy *getSchedImpl() const {
    return static_cast<HaydnPreRASchedStrategy *>(SchedImpl.get());
  }
};

ScheduleDAGInstrs *createHaydnPreRAScheduler(MachineSchedContext *C);

} // namespace llvm

#endif
