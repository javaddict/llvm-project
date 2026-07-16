//===- HaydnResourceCycle.h - SMS resource model --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase 2 step 1: the Haydn SMS-facing resource model. A `ResourceCycle`
// subclass backed by `Haydn::Bundle<MachineInstr>` — the software pipeliner
// (MachinePipeliner) queries `canReserveResources`/`reserveResources` to decide
// whether an instruction can issue in the current cycle, so SMS reasons about
// REAL slot pressure (the Bundle's slot-bitset + format-coverage check) instead
// of the blind DFA. This is the SWPS wiring entry point — the M7 goal (real
// loops pipelined) needs this alternative-aware model.
//
// Mirrors AIE's `AIEResourceCycle` (AIEHazardRecognizer.h). difference:
// head-LLVM's SMS ResourceManager calls the MCInstrDesc overload
// (MachinePipeliner.cpp `canReserveResources(&SU.getInstr->getDesc)`)
// whereas AIE's fork calls the MachineInstr overload. So here the MID overload
// is PRIMARY (opcode-keyed via Bundle::canAdd/reserveByOpcode) and the MI
// overload delegates to it — the inverse of AIE's stub choice. This is the
// supported public interface (no upstream MachinePipeliner edit, per #0).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H

#include "HaydnBundle.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/ResourceCycle.h"
#include "llvm/CodeGen/MachineInstr.h"

namespace llvm {

class HaydnResourceCycle : public ResourceCycle {
  HaydnMCFormats Fmts;
  Haydn::Bundle<MachineInstr> Bundle;

public:
  HaydnResourceCycle() : Bundle(&Fmts) {}

  void clearResources() override { Bundle.clear(); }

  // head-LLVM's SMS ResourceManager calls the MCInstrDesc overload
  // (MachinePipeliner.cpp `canReserveResources(&SU.getInstr->getDesc)`)
  // NOT the MachineInstr overload the AIE fork calls. So the MID overload is
  // the PRIMARY path here and must be implemented (not stubbed). It is
  // opcode-keyed: the Bundle slot model (getAltSlotSet → getLegalSlots) and
  // format check (isFormatAvailable) are both keyed on opcode, which the
  // MCInstrDesc provides via getOpcode. This mirrors DFAPacketizer, where
  // the MID overload is primary and the MI overload delegates to it.
  bool canReserveResources(const MCInstrDesc *MID) override {
    return Bundle.canAdd(MID->getOpcode());
  }
  void reserveResources(const MCInstrDesc *MID) override {
    assert(Bundle.canAdd(MID->getOpcode()) && "reserve without canReserve");
    Bundle.reserveByOpcode(MID->getOpcode());
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

  // For debug/inspection: the slots occupied in the current cycle.
  SlotBits getOccupiedSlots() const { return Bundle.getOccupiedSlots(); }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
