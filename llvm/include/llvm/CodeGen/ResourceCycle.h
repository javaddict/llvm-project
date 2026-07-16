//===- llvm/CodeGen/ResourceCycle.h - Resource Tracking -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an abstraction to track the resource use of one cycle. It is the
// SMS-facing resource model: the MachinePipeliner queries
// `CreateTargetScheduleState` (which returns a `ResourceCycle*`) to decide
// whether an instruction (or an alternative opcode for it) can issue in the
// current cycle. `DFAPacketizer` is one concrete `ResourceCycle`; targets may
// provide their own (AIE's `AIEResourceCycle`, Haydn's forthcoming
// `HaydnResourceCycle`) to model alternative-aware slot pressure.
//
// D342: ported from Xilinx/llvm-aie (ResourceCycle.h) as the AIE-faithful SMS
// infrastructure — a bounded exception to hard constraint #0 (recorded in
// D342). The base class is pure-abstract; no .cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RESOURCECYCLE_H
#define LLVM_CODEGEN_RESOURCECYCLE_H

namespace llvm {

class MachineInstr;
class MCInstrDesc;

class ResourceCycle {
public:
  ResourceCycle() = default;
  virtual ~ResourceCycle() = default;

  /// Reset the current state to make all resources available.
  virtual void clearResources() = 0;

  /// Check if the resources occupied by a machine instruction are available
  /// in the current state.
  virtual bool canReserveResources(MachineInstr &MI) = 0;

  /// Reserve the resources occupied by a machine instruction and change the
  /// current state to reflect that change.
  virtual void reserveResources(MachineInstr &MI) = 0;

  /// Check if the resources occupied by a MCInstrDesc are available in
  /// the current state.
  virtual bool canReserveResources(const MCInstrDesc *MID) = 0;

  /// Reserve the resources occupied by a MCInstrDesc and change the current
  /// state to reflect that change.
  virtual void reserveResources(const MCInstrDesc *MID) = 0;

  /// Property that is required for DFAPacketizer. Used for dynamic checks.
  bool CanTrackResources = false;
};

} // namespace llvm

#endif // LLVM_CODEGEN_RESOURCECYCLE_H
