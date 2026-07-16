//===- HaydnAlternateDescriptors.h - Slot placement side-map ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (G-MC-10 retirement): only the placement-slot map remains.
// AlternateSlots — HR auction / commitSlotFlexVariant write setSlot;
// MCInstLower reads getSelectedSlot → HaydnMCFlags. MachineInstr opcodes
// stay logical (no setDesc(*_S*)).
//
// The old MIAltDescsMap / setAlternateDescriptor / getSelectedDescriptor
// opcode-alt surface (AIE multi-opcode port) had zero writers after
// and is deleted. clearDescriptors is a no-op retained for call-site
// compatibility until leaveRegion is cleaned.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H

#include "llvm/CodeGen/MachineInstr.h"
#include <optional>
#include <unordered_map>

namespace llvm {

// Per-MI chosen VLIW placement slot (0/1/2).
using MIAltSlotsMap = std::unordered_map<MachineInstr *, unsigned>;

class HaydnAlternateDescriptors {
  MIAltSlotsMap AlternateSlots;

public:
  HaydnAlternateDescriptors() = default;

  // Record the chosen VLIW placement slot (0/1/2) for \p MI.
  void setSlot(MachineInstr *MI, unsigned Slot) { AlternateSlots[MI] = Slot; }
  std::optional<unsigned> getSelectedSlot(MachineInstr *MI) const {
    if (auto It = AlternateSlots.find(MI); It != AlternateSlots.end())
      return It->second;
    return std::nullopt;
  }

  // Historical name: previously cleared the opcode-alt map only. The opcode
  // map is gone (G-MC-10); this is now a no-op. Prefer not calling it.
  void clearDescriptors() {}

  // Drop all placement slots. Not used after materialize — wiping slots
  // before AsmPrinter breaks HaydnMCFlags. Available for full MF teardown.
  void clear() { AlternateSlots.clear(); }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H
