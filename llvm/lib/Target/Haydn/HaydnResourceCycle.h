//===- HaydnResourceCycle.h - SMS resource model --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn SMS-facing resource model. A `ResourceCycle` subclass backed by a
// *live* `haydn::bundle::CycleState` — the software pipeliner
// (MachinePipeliner) queries `canReserveResources`/`reserveResources` to decide
// whether an instruction can issue in the current cycle, so SMS ResMII uses the
// same pure tryAddProduct depth as post-RA HR `CurrentCycleState` /
// `commitPlacementForEmit` (not a weaker OccupiedSlots-only Bundle rebuild).
//
// AIE peers (port structure; do not invent a parallel packing theory):
//
//   * AIEHazardRecognizer.h:315-328  AIEResourceCycle Bundle-backed
//   * AIEHazardRecognizer.cpp:173-214 canReserve/reserve —
//       getAlternateInstsOpcode + any_of / first canAdd AltOpcode
//   * Haydn maps that alt-try to canTryAddProduct / tryAddProduct on live
//     CycleState (B2.4 solver; B4.2 SMS same depth as post-RA).
//
// Head-LLVM's SMS ResourceManager calls the MCInstrDesc overload, so the MID
// overload is PRIMARY (opcode-keyed) and the MI overload delegates to it
// (AIE inverted: MI primary; Haydn matches head-LLVM ResourceManager).
//
// B4.1: getFeasibleFormatMask exposes the product FormatID frontier.
// B4.2: mask is the *live* CycleState.FeasibleFormatMask (member Compatible
// intersections accumulate); productFeasibleFormatMask(Occupied) remains the
// occupancy-only Pre-RA rebuild. Logical ops only — no FormatID freeze, no
// setDesc (plan §7.1). Product size-1 Full keeps ProductFormatMask while slots
// remain Full-coverable. N-format tables ready via CycleState.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H

#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/ResourceCycle.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include <cstdint>

namespace llvm {

class HaydnResourceCycle : public ResourceCycle {
  HaydnMCFormats Fmts;
  /// Live cycle packing state — peer of HaydnHazardRecognizer::CurrentCycleState.
  haydn::bundle::CycleState State;

  static bool isNoHazardMetaOpcode(unsigned Opcode) {
    switch (Opcode) {
    case TargetOpcode::IMPLICIT_DEF:
    case TargetOpcode::KILL:
    case TargetOpcode::BUNDLE:
      return true;
    default:
      return false;
    }
  }

public:
  HaydnResourceCycle() : State(haydn::bundle::makeProductCycleState()) {}

  void clearResources() override {
    // Reset ProductFormatMask + empty members (B4.1/B4.2).
    State = haydn::bundle::makeProductCycleState();
  }

  // head-LLVM's SMS ResourceManager calls the MCInstrDesc overload
  // (MachinePipeliner.cpp `canReserveResources(&SU.getInstr->getDesc)`).
  // B4.2: live CycleState tryAddProduct (AIE AIEHazardRecognizer.cpp:173-214
  // Bundle canAdd/add alt try → Haydn pure solver depth = post-RA HR).
  bool canReserveResources(const MCInstrDesc *MID) override {
    return canReserveByOpcode(MID->getOpcode());
  }
  void reserveResources(const MCInstrDesc *MID) override {
    reserveByOpcode(MID->getOpcode());
  }

  // MachineInstr overload: delegate to the MID overload (mirror DFAPacketizer
  // DFAPacketizer.cpp:72-82). AIE's AIEResourceCycle made the MI overload
  // primary because AIE's LLVM fork calls it directly; head-LLVM calls the MID
  // overload, so we invert AIE's stub choice for portability.
  bool canReserveResources(MachineInstr &MI) override {
    return canReserveResources(&MI.getDesc());
  }
  void reserveResources(MachineInstr &MI) override {
    reserveResources(&MI.getDesc());
  }

  // For debug/inspection: slots occupied in the current cycle (live State).
  SlotBits getOccupiedSlots() const { return State.OccupiedSlots; }

  // B4.1/B4.2: live FormatID frontier for SMS ResMII / cycle occupancy.
  // AIE ResourceCycle is Bundle-backed without an explicit mask; Haydn exposes
  // CycleState.FeasibleFormatMask so SMS matches post-RA HR (not
  // productFeasibleFormatMask(Occupied) rebuild alone — member Compatible
  // intersections can shrink the live mask under N-format alts).
  uint64_t getFeasibleFormatMask() const { return State.FeasibleFormatMask; }

  /// Live CycleState (unit tests / ResMII probes). No setDesc.
  const haydn::bundle::CycleState &getCycleState() const { return State; }

  unsigned getMemberCount() const { return State.memberCount(); }

  // Opcode-keyed reserve without an MCInstrDesc (unit tests / local probes).
  // Same contract as reserveResources(MID) for alts-bearing logicals.
  bool canReserveByOpcode(unsigned Opcode) {
    if (isNoHazardMetaOpcode(Opcode))
      return true;
    // PlacementAlternative-bearing logicals: pure canTryAddProduct
    // (AIEHazardRecognizer.cpp:183-194 any_of Bundle.canAdd AltOpcode).
    if (hasPlacementAlternatives(Fmts, Opcode))
      return haydn::bundle::canTryAddProduct(State, Fmts, Opcode);
    // No-alt opcodes: Bundle empty standalone escape peer (AIEBundle.h:71-73)
    // — accept only on a truly empty cycle; do not consume slots.
    return State.empty() && State.OccupiedSlots == 0;
  }

  void reserveByOpcode(unsigned Opcode) {
    assert(canReserveByOpcode(Opcode) && "reserve without canReserve");
    if (isNoHazardMetaOpcode(Opcode))
      return;
    if (hasPlacementAlternatives(Fmts, Opcode)) {
      // AIEHazardRecognizer.cpp:208-211 first canAdd AltOpcode → Bundle.add.
      bool Ok = haydn::bundle::tryAddProduct(State, Fmts, Opcode);
      assert(Ok && "canReserve true but tryAddProduct failed");
      (void)Ok;
      return;
    }
    // No-alt standalone escape: no OccupiedSlots / FeasibleFormatMask change.
    assert(State.empty() && State.OccupiedSlots == 0);
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
