//===-- HaydnMachineScheduler.cpp - Dual MI Scheduler for Haydn -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA ScheduleDAGMI (HaydnScheduleDAGMI) + factory. Pre-RA lives in
// HaydnPreRASchedStrategy.cpp (AIE2 dual-sched contract).
// VLIWMachineScheduler / ConvergingVLIWScheduler remain retired ( UAF).
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineScheduler.h"
#include "HaydnPostRAMultiStage.h"
#include "HaydnTargetMachine.h"
#include "HaydnPostRASchedStrategy.h"
#include "HaydnSchedMutations.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

using namespace llvm;

static_assert(!HaydnMultiStageSMS::productDefaultEnabled(),
              "multi-stage product default stays off");
static_assert(!HaydnMultiStageSMS::productHwloopCombinedEnabled(),
              "combined hwloop+SMS is not product");
static_assert(!HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(),
              "hardware-loop product default stays OFF; not flipped here");

/// Live tblgen SchedMachineModel pin. AIE1 is in-order
/// (`aie1/AIE1Schedule.td:258` MicroOpBufferSize=0, CompleteModel=0).
/// AIE2/AIE2P/AIE2PS overlay buffer=1000 is a KPI heuristic, not this
/// model. Haydn overlay: LoopMicroOpBufferSize stays 0 and IssueWidth
/// is E3 (`Haydn::ISSUE_SLOT_COUNT`), not AIE1's 9. Per-op admission
/// stays closed (`CompleteModel=0`) until a generated import lands.
static void pinHaydnInOrderIncompleteSchedModel(const MachineFunction &MF) {
  const MCSchedModel &SM = MF.getSubtarget().getSchedModel();
  if (SM.MicroOpBufferSize != 0 || SM.LoopMicroOpBufferSize != 0 ||
      SM.isOutOfOrder() || SM.isComplete() ||
      SM.IssueWidth != Haydn::ISSUE_SLOT_COUNT)
    report_fatal_error(
        "Haydn SchedMachineModel drifted from in-order incomplete pin",
        /*GenCrashDiag=*/false);
}

#define DEBUG_TYPE "haydn-machine-scheduler"

void HaydnScheduleDAGMI::schedule() {
  // Shared post-RA host: ordinary list schedule first (rollback baseline),
  // then optional transactional multi-stage mode (product default OFF).
  ScheduleDAGMI::schedule();
  if (EnableHaydnMultiStageSMS) {
    HaydnMultiStageSMS Host;
    (void)Host.tryAfterOrdinarySchedule(*this);
  }
}

bool HaydnScheduleDAGMI::successorsAreScheduled(
    const MachineBasicBlock *MBB) const {
  // AIE AIEMachineScheduler.cpp:251-258. Empty / unknown succs stay
  // conservative so MaxLatencyFinder keeps stage latency.
  if (!MBB || MBB->succ_empty())
    return false;
  return llvm::all_of(MBB->successors(), [&](const MachineBasicBlock *S) {
    return ScheduledMBBs.contains(S);
  });
}

void HaydnScheduleDAGMI::finishBlock() {
  if (BB)
    ScheduledMBBs.insert(BB);
  ScheduleDAGMI::finishBlock();
}

void HaydnScheduleDAGMI::exitRegion() {
  // Forward to the strategy's leaveRegion BEFORE the base class closes the
  // region, so the strategy can read the scheduled SUs' TopReadyCycle values
  // (still valid here) and compute the in-memory bundle list. The base
  // ScheduleDAGInstrs::exitRegion is a no-op aside from region bookkeeping
  // so the order is safe. Mirrors AIE's AIEScheduleDAGMI::exitRegion
  // (AIEMachineScheduler.cpp:1600-1604).
  auto *S = static_cast<HaydnPostRASchedStrategy *>(SchedImpl.get());
  S->leaveRegion(ExitSU);
  ScheduleDAGMI::exitRegion();
}

// Create the post-RA scheduler DAG: HaydnScheduleDAGMI driven by
// HaydnPostRASchedStrategy (which carries HaydnHazardRecognizer for slot +
// GPR 4R2W + DR64 7R3W resource checks, and forms bundles in leaveRegion).
// This replaces the B1 path (createSchedPostRA<HaydnPostRASchedStrategy>
// which used the base ScheduleDAGMI with no leaveRegion hook and left bundle
// formation to the now-retired HaydnVLIWPacketizer). It also retires the
// older createHaydnPostRAVLIWScheduler factory, which constructed a
// VLIWMachineScheduler — the / UAF trap (ConvergingVLIWScheduler::
// SchedulingCost dereferences a dangling SUnit); HaydnScheduleDAGMI never
// instantiates VLIWMachineScheduler, so that UAF is structurally
// inapplicable.
ScheduleDAGInstrs *llvm::createHaydnPostRAScheduler(MachineSchedContext *C) {
  if (!C || !C->MF)
    report_fatal_error("Haydn post-RA scheduler factory missing MF",
                       /*GenCrashDiag=*/false);
  pinHaydnInOrderIncompleteSchedModel(*C->MF);
  auto *DAG = new HaydnScheduleDAGMI(
      C, std::make_unique<HaydnPostRASchedStrategy>(C), /*IsPreRA=*/false);
  // Soft post-RA mutations (AIE getPostRAMutationsImpl subset). No
  // CopyConstrain post-RA.
  for (auto &M : getHaydnPostRAMutations())
    DAG->addMutation(std::move(M));
  return DAG;
}
