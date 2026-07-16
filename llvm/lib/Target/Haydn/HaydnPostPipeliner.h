//===-- HaydnPostPipeliner.h - Post-RA software pipeliner -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA software pipeliner for Haydn (AIE-aligned, Stage-0 subset).
//
// Reference:
// AIE::PostPipeliner — /ssd2/mhyang/llvm-aie/.../AIEPostPipeliner.{h,cpp}
// Port map P0 — haydn-plans/research/aie-postpipeliner-port-map-.md
//
// Stage-0 scope (this file):
// * Single-BB ZOL in either form:
// LoopStart + PseudoLoopEnd (IR HardwareLoops)
// SET_HWLOOP{,_REG} fallthrough body (post-RA HaydnHardwareLoops)
// * ASAP first-iteration placement + modulo resource packing via
// HaydnHazardRecognizer footprints (no multi-heuristic matrix, no Z3)
// * 1-copy DAG (no InterBlockScheduling / NCopies=2 —)
// * Materialize: peel stage-stripped prologue into preheader, rewrite
// kernel by modulo cycle (+ same-cycle BUNDLEs), epilogue clones into
// unique exit, tripcount -= (NStages-1) via LoopStart $adj or ADDI on
// SET_HWLOOP_REG count reg
//
// Gate: -haydn-enable-post-pipeliner (default false).
//
// Still deferred vs full AIE PostPipeliner:
// 2-copy LCD validation, multi-strategy tryApproaches, peelSideEffectFree
// solver path, preferPostPipeliner PLI partition, inter-block scoreboard.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPOSTPIPELINER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPOSTPIPELINER_H

#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/Support/CommandLine.h"
#include <vector>

namespace llvm {

class HaydnHazardRecognizer;
class MachineInstr;
class SUnit;

// Default OFF. When false, HaydnScheduleDAGMI::schedule is identical to
// ScheduleDAGMI::schedule. When true, Stage-0 PostPipeliner is attempted
// on single-BB ZOL candidates before falling back to list schedule.
extern cl::opt<bool> EnableHaydnPostPipeliner;

// Per-instruction placement for the modulo schedule (AIE NodeInfo subset).
struct HaydnPPNodeInfo {
  bool Scheduled = false;
  int Cycle = 0;
  int ModuloCycle = 0;
  int Stage = 0;
  int Earliest = 0;

  void update(int InitiationInterval) {
    assert(InitiationInterval > 0);
    ModuloCycle = Cycle % InitiationInterval;
    Stage = Cycle / InitiationInterval;
  }
};

// Stage-0 post-RA modulo scheduler.
// Public surface mirrors AIE::PostPipeliner at the P0 level:
// isPostPipelineCandidate, getResMII, schedule(DAG, II)
// schedule returns true only after a valid multi-stage schedule has been
// materialized into the MF.
class HaydnPostPipeliner {
public:
  HaydnPostPipeliner() = default;

  // ZOL single-BB candidate: dedicated fallthrough preheader with LoopStart
  // or SET_HWLOOP{,_REG}, PseudoLoopEnd (form A) or fallthrough body (form B)
  // unique exit, body size in range.
  bool isPostPipelineCandidate(MachineBasicBlock &LoopBlock);

  // Lower bound on II from issue width (ceil(NBody / 3)).
  int getResMII(MachineBasicBlock &LoopBlock) const;

  // Attempt a post-RA modulo schedule.
  // \p IIHint 0 = search from ResMII upward; otherwise try that II first.
  // \return true if a multi-stage pipeline was found AND materialized
  // (caller skips list schedule for the region). SUnits are marked
  // scheduled with TopReadyCycle = ModuloCycle so leaveRegion can form
  // bundles.
  bool schedule(ScheduleDAGMI &DAG, unsigned IIHint);

  int getStageCount() const { return NStages; }
  int getII() const { return II; }

private:
  ScheduleDAGMI *DAG = nullptr;
  MachineBasicBlock *LoopBB = nullptr;
  MachineBasicBlock *Preheader = nullptr;
  MachineBasicBlock *ExitBB = nullptr;
  MachineInstr *TripCountDef = nullptr; // LoopStart or SET_HWLOOP{,_REG}

  // Body SUnits in original region order (excludes boundary + PseudoLoopEnd).
  std::vector<SUnit *> Body;
  std::vector<HaydnPPNodeInfo> Info;

  int II = 1;
  int NStages = 0;
  int LinearLength = 0;

  bool computeASAPEarliest();
  bool tryII(int TryII, const HaydnHazardRecognizer &HR);
  bool materialize();
  void adjustTripCount(int Delta) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPOSTPIPELINER_H
