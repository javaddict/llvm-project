//===- HaydnAlternateDescriptors.h - Alternate opcode descriptors -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AIE-shaped opcode-alt map for post-RA multi-slot placement
// (peer AIEAlternateDescriptors.h:27-75). Suffix name discovery is not
// a product alternate source.
//
//   AlternateDescs  — MI → selected format-member MCInstrDesc*.
//                     HR commitPlacementForEmit writes setAlternateDescriptor
//                     (MemberOpcode from exactTryAddProduct preferred collapse;
//                     AIEHazardRecognizer.cpp:389).
//                     leaveRegion materializeMultiOpcodeInstrs reads
//                     getSelectedOpcode and MI.setDesc
//                     (AIEMachineScheduler.cpp:1121-1139), then clear()
//                     (AIEMachineScheduler.cpp:1081-1082;
//                     AIEAlternateDescriptors.h:74) BEFORE exact no-split
//                     multi-MI MIR commit (HaydnBundleMaterialize
//                     instrsFormOneLegalCycle + commitExactMultiMIProductCycle;
// sole surface — no PostRA dual). Transient only —
//                     never a durable side-map.
//
// Pre-existing multi-member shells are not in the scheduled-region alt map.
// Product leaveMBB free multi-MI uses commitExactMultiMIProductCycle; residual
// unstamped multi-member shells use the same ordinary multi-MI commit or
// sequentialize. AltDescs remains region-only.
//
// Transient only — never a durable side-map and never a format identity
// that crosses RA. No slot side-map (AIE has none). Post-commit placement
// is opcode identity via getSlotKind (AIEBaseMCFormats.cpp:66-75) + Bundle
// SlotMap (AIEBundle.h:92-104). Product: Format E composites only (FE8). Selected
// MemberOpcode must be a real placement member (declarative MultiSlot /
// LogicalMaterialize alternate) — never re-stamp a bare logical. Multi-MI
// commit re-solves setDesc as a fail-closed second line so residual logical
// packs cannot reach encode.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include <optional>
#include <unordered_map>

namespace llvm {

// AIE peer: AIEAlternateDescriptors.h:25 — MI → selected format-member Desc.
using MIAltDescsMap = std::unordered_map<MachineInstr *, const MCInstrDesc *>;

class HaydnAlternateDescriptors {
  MIAltDescsMap AlternateDescs;

public:
  HaydnAlternateDescriptors() = default;

  // AIE peer AIEAlternateDescriptors.h:39-44: record selected format-member
  // opcode for multi-slot / format-member logicals. \p TII resolves opcode →
  // MCInstrDesc (AIE uses Subtarget TII; Haydn takes TII from the HR/caller
  // so unit tests can inject without a MachineFunction).
  // Keys may be synthetic addresses in unit tests — do not dereference MI
  // here. Production: HR never records INLINEASM; leaveRegion setDesc also
  // skips isInlineAsm before applying SelectedOpcode.
  void setAlternateDescriptor(MachineInstr *MI, unsigned AltInstOpcode,
                              const MCInstrInfo &TII) {
    AlternateDescs[MI] = &TII.get(AltInstOpcode);
  }

  // Direct Desc inject (tests / callers that already hold MCInstrDesc).
  void setAlternateDescriptor(MachineInstr *MI, const MCInstrDesc *Desc) {
    AlternateDescs[MI] = Desc;
  }

  // AIE peer AIEAlternateDescriptors.h:47-52.
  std::optional<const MCInstrDesc *>
  getSelectedDescriptor(MachineInstr *MI) const {
    if (auto It = AlternateDescs.find(MI); It != AlternateDescs.end())
      return It->second;
    return std::nullopt;
  }

  // AIE peer AIEAlternateDescriptors.h:54-61.
  const MCInstrDesc *getDesc(MachineInstr *MI) const {
    return getSelectedDescriptor(MI).value_or(&MI->getDesc());
  }

  const MCInstrDesc *getDesc(const MachineInstr *MI) const {
    return getSelectedDescriptor(const_cast<MachineInstr *>(MI))
        .value_or(&MI->getDesc());
  }

  // AIE peer AIEAlternateDescriptors.h:64-68 — selected member opcode for
  // materializeMultiOpcodeInstrs setDesc.
  std::optional<unsigned> getSelectedOpcode(MachineInstr *MI) const {
    if (auto It = AlternateDescs.find(MI); It != AlternateDescs.end())
      return It->second->getOpcode();
    return std::nullopt;
  }

  // AIE peer AIEAlternateDescriptors.h:70-72.
  unsigned getOpcode(MachineInstr *MI) const {
    return getSelectedOpcode(MI).value_or(MI->getDesc().getOpcode());
  }

  // AIE peer AIEAlternateDescriptors.h:74 — leaveRegion end-state after
  // materializeMultiOpcodeInstrs setDesc (AIEMachineScheduler.cpp:1081-1082).
  void clear() { AlternateDescs.clear(); }

  /// True when no transient alternate survives (leaveRegion / leaveMBB pin).
  bool empty() const { return AlternateDescs.empty(); }

  unsigned size() const {
    return static_cast<unsigned>(AlternateDescs.size());
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNALTERNATEDESCRIPTORS_H
