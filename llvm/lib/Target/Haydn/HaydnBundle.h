//===- HaydnBundle.h - VLIW bundle w/ format checking -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn port of AIE's `AIE::Bundle` (AIEBundle.h). The Bundle is the resource
// model the post-RA hazard recognizer AND the software pipeliner (SMS) reason
// about: each instruction has an associated slot, particular combinations of
// slots (formats) are valid, and a slot can be occupied exactly once. This is
// the format-first tablegen foundation.
//
// A bundle holds instructions in three partially redundant representations:
// the instructions in original issue order
// a map indexed by slot number
// a bitset of occupied slots
// A bundle is valid iff `isFormatAvailable(OccupiedSlots)`.
//
// Bundle is a **solver adapter**. For logicals that have PlacementAlternatives,
// canAdd/add/reserveByOpcode route through pure CycleState tryAddProduct
// (HaydnBundleFormatSolver.h) — the AIE alt-try shape
// (AIEHazardRecognizer.cpp:174-214 getAlternateInstsOpcode + first
// Bundle.canAdd AltOpcode; AIEBundle.h:62-105/110-145 canAdd/add occupancy).
// OccupiedSlots / SlotMap / empty standalone escape are retained (SMS ResMII).
//
// Alts-only pickSlot: no getLegalSlots no-alt fallback. No-alt opcodes fail
// pickSlot (nullopt) unless standalone empty-escape accepts them. Prefer
// S2 → S1 → S0 via tryAdd (loads keep S0).
//
// getFeasibleFormatMask exposes the pre-commit FormatID frontier from
// OccupiedSlots (productFeasibleFormatMask). AIE peer getFormatOrNull
// (AIEBundle.h:150-156) returns one format; Haydn keeps a mask until post-RA
// freeze. Logical ops only — no setDesc / no FormatID commit here.
//
// Haydn adaptation vs AIE: AIE ops have a SINGLE slot (`getSlotKind`); Haydn
// multi-slot logicals enumerate PlacementAlternative FieldSlots (S2→S1→S0).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H

#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include <vector>

namespace llvm {
// AIEBundle.h:35 — MachineInstr lives in llvm, used by MachineBundle alias.
class MachineInstr;
namespace Haydn {

// \a I must provide getOpcode.
template <class I> class Bundle {
public:
  Bundle(const HaydnBaseMCFormats *FormatInterface)
      : FormatInterface(FormatInterface) {}

  // Whether adding \p Instr (by opcode) leaves the bundle valid.
  // Committed format-members (post-setDesc): fixed getSlotKind (AIE shape).
  // Alts-bearing logicals: product CycleState tryAdd.
  // Else: no getLegalSlots fallback — pickSlot nullopt outside empty escape.
  // A truly empty bundle (no instructions AND no reserved slots) always
  // accepts (standalone escape, mirrors AIE AIEBundle.h:71-73); a standalone
  // bundle (one unsupported op) accepts nothing more.
  // Contract : canAdd and reserveByOpcode/add MUST agree on slot
  // availability. The verdict is derived from the SAME pickSlot call in every
  // branch, so the two can never diverge. The empty-bundle early-out is the
  // ONE exception (AIE-faithful standalone escape): on a bundle with neither
  // Instrs nor OccupiedSlots any op is accepted as a future standalone
  // parcel, and reserveByOpcode/add treat "pickSlot found no slot" as a
  // graceful no-op (AIE AIEBundle.h:134-139) — NOT an assert.
  // reserveByOpcode updates OccupiedSlots WITHOUT pushing Instrs
  // (SMS ResourceCycle path). empty alone is therefore insufficient
  // after the first reserveResources the bundle still has Instrs.empty
  // but OccupiedSlots may be non-zero. Gating the standalone escape on
  // OccupiedSlots == 0 forces a second same-slot op through pickSlot so
  // ResMII can grow above 1.
  bool canAdd(unsigned Opcode) const {
    // Truly empty (no instructions AND no reserved slots): always accept
    // (any op can emit standalone; the format check becomes meaningful once
    // a companion is added). After reserveByOpcode, OccupiedSlots may be
    // non-zero while Instrs is empty — fall through to pickSlot/format
    // checks so a second MAC/ALU that needs the same saturated slot returns
    // false (ResMII fix).
    if (empty() && OccupiedSlots == 0)
      return true;
    if (isNoHazardMetaInstruction(Opcode))
      return true;
    if (Opcode == TargetOpcode::BUNDLE)
      return !BundleRoot;
    if (isStandalone())
      return false;
    if (!FormatInterface->isSupportedInstruction(Opcode))
      return false;
    return pickSlot(Opcode).has_value();
  }
  bool canAdd(const I *Instr) const { return canAdd(Instr->getOpcode()); }

  // Add \p Instr, picking its slot. \pre canAdd(Instr->getOpcode).
  void add(I *Instr) {
    unsigned Opcode = Instr->getOpcode();
    if (isNoHazardMetaInstruction(Opcode)) {
      MetaInstrs.push_back(Instr);
      return;
    }
    if (Opcode == TargetOpcode::BUNDLE) {
      assert(!BundleRoot && "Haydn::Bundle already has a root BUNDLE");
      BundleRoot = Instr;
      return;
    }
    assert(canAdd(Opcode) && "pre-condition canAdd violated");
    Instrs.push_back(Instr);
    // Unsupported op in an empty bundle → standalone (no slot assigned).
    if (!FormatInterface->isSupportedInstruction(Opcode)) {
      assert(Instrs.size() == 1 && "unsupported op added to a non-empty bundle");
      return;
    }
    // pickSlot may return nullopt for a supported opcode with no alts /
    // no free field (alts-only; no getLegalSlots fallback). On a truly empty
    // bundle canAdd returned true (standalone escape), so this op becomes a
    // standalone parcel with no OccupiedSlots update — mirroring AIE's add
    // (AIEBundle.h:134-139). Reserve the slot only when pickSlot finds one.
    auto Slot = pickSlot(Opcode);
    if (!Slot) {
      assert(Instrs.size() == 1 && "no-slot op added to a non-empty bundle");
      return;
    }
    reserveSlot(Instr, *Slot);
  }

  // add \p Instr with a HINT slot. If \p HintSlot is free, legal under
  // PlacementAlternative FieldSlots (alts-only),
  // and the resulting occupancy has a valid format, place \p Instr there.
  // Otherwise fall back to solver `pickSlot`. Used by the MC encoder when
  // an explicit `.sN` suffix records the intended slot.
  // \pre canAdd(Instr->getOpcode).
  void add(I *Instr, MCSlotKind HintSlot) {
    unsigned Opcode = Instr->getOpcode();
    if (isNoHazardMetaInstruction(Opcode)) {
      MetaInstrs.push_back(Instr);
      return;
    }
    if (Opcode == TargetOpcode::BUNDLE) {
      assert(!BundleRoot && "Haydn::Bundle already has a root BUNDLE");
      BundleRoot = Instr;
      return;
    }
    assert(canAdd(Opcode) && "pre-condition canAdd violated");
    Instrs.push_back(Instr);
    if (!FormatInterface->isSupportedInstruction(Opcode)) {
      assert(Instrs.size() == 1 && "unsupported op added to a non-empty bundle");
      return;
    }
    // Try the hint first: free + legal under alts/FieldSlots + format-valid.
    std::optional<MCSlotKind> Chosen;
    const MCSlotInfo *HintSI = FormatInterface->getSlotInfo(HintSlot);
    if (HintSI && !(OccupiedSlots & HintSI->getSlotSet()) &&
        isHintSlotLegal(Opcode, HintSlot) &&
        FormatInterface->isFormatAvailable(OccupiedSlots |
                                           HintSI->getSlotSet())) {
      Chosen = HintSlot;
    }
    // Hint did not fit: fall back to solver pickSlot (alts tryAdd).
    if (!Chosen)
      Chosen = pickSlot(Opcode);
    if (!Chosen) {
      assert(Instrs.size() == 1 && "no-slot op added to a non-empty bundle");
      return;
    }
    reserveSlot(Instr, *Chosen);
  }

  // reserve resources for \p Opcode WITHOUT storing an instruction
  // pointer. This is the opcode-keyed resource update used by the SMS
  // `ResourceCycle` MCInstrDesc overload path (head-LLVM's ResourceManager
  // calls `canReserveResources(const MCInstrDesc*)` which has no MachineInstr).
  // It performs the SAME slot pick + OccupiedSlots update as `add`, but does
  // not touch Instrs/SlotMap (those track real instructions for the post-RA
  // packetizer; the SMS resource model only needs the slot-occupancy effect).
  // \pre canAdd(Opcode). Meta/BUNDLE opcodes have no slot effect (no-op).
  // Contract : if pickSlot returns nullopt this is a graceful
  // no-op, NOT an assert — canAdd returns true on a truly empty bundle
  // (Instrs.empty && OccupiedSlots == 0, standalone escape) without
  // consulting pickSlot, so reserveByOpcode must tolerate the same "no slot
  // found" outcome AIE's add tolerates.
  void reserveByOpcode(unsigned Opcode) {
    if (isNoHazardMetaInstruction(Opcode) || Opcode == TargetOpcode::BUNDLE)
      return;
    assert(canAdd(Opcode) && "pre-condition canAdd violated");
    // A truly empty bundle accepts via the canAdd early-out; a standalone
    // unsupported op has no slot to reserve. Either way, no OccupiedSlots
    // change — the next supported op will start the slot accounting.
    if (!FormatInterface->isSupportedInstruction(Opcode))
      return;
    auto Slot = pickSlot(Opcode);
    if (!Slot)
      return;
    const MCSlotInfo *SI = FormatInterface->getSlotInfo(*Slot);
    assert(SI && "no SlotInfo for picked slot");
    OccupiedSlots |= SI->getSlotSet();
  }

  // Whether a packet format covers the currently occupied slots. Haydn has no
  // VLIWFormat object zoo (Mode-0/1/3 only); this is the boolean coverage
  // verdict from HaydnMCFormats::isFormatAvailable.
  bool hasValidFormat() const {
    assert(!isStandalone());
    return FormatInterface->isFormatAvailable(OccupiedSlots);
  }

  // Return the minimum-size valid packet format for OccupiedSlots, if any.
  // AIE peer: AIEBundle.h:150-156 getFormatOrNull via PacketFormats::getFormat.
  // Product: sole live row is BUNDLE128_FULL (N-format-ready table scan).
  const VLIWFormat *getFormatOrNull(unsigned Size = 0) const {
    assert(!isStandalone());
    if (Size)
      return FormatInterface->getPacketFormats().getFormatBySize(OccupiedSlots,
                                                                 Size);
    return FormatInterface->getPacketFormats().getFormat(OccupiedSlots);
  }

  // Feasible FormatID frontier for current OccupiedSlots (logical only).
  // AIE peer: getFormatOrNull returns one covering VLIWFormat*
  // (AIEBundle.h:150-156; AIEFormat.cpp:18-27 first-covering). Haydn keeps a
  // FormatID *mask* so Pre-RA/SMS can reason about multi-format readiness
  // without freezing FormatID or setDesc (plan §7.1). Product size-1 Full →
  // ProductFormatMask whenever Full still covers OccupiedSlots.
  uint64_t getFeasibleFormatMask() const {
    return haydn::bundle::productFeasibleFormatMask(OccupiedSlots);
  }

  void clear() {
    OccupiedSlots = 0;
    Instrs.clear();
    MetaInstrs.clear();
    SlotMap.clear();
    BundleRoot = nullptr;
  }

  bool empty() const { return Instrs.empty(); }
  unsigned size() const { return Instrs.size(); }
  SlotBits getOccupiedSlots() const { return OccupiedSlots; }

  // Instruction occupying \p Slot, or nullptr.
  I *at(MCSlotKind Slot) const {
    for (const auto &KV : SlotMap)
      if (KV.first == Slot)
        return KV.second;
    return nullptr;
  }

  const std::vector<I *> &getInstrs() const { return Instrs; }
  const std::vector<I *> &getMetaInstrs() const { return MetaInstrs; }
  const SmallVector<std::pair<MCSlotKind, I *>, 3> &
  getSlotMap() const {
    return SlotMap;
  }

  // A bundle holding one op with no format/slot entry (must emit standalone).
  bool isStandalone() const {
    return Instrs.size() == 1 &&
           !FormatInterface->isSupportedInstruction(Instrs.front()->getOpcode());
  }

  // For testing: the slot committed for the LAST reserved instruction.
  // (Production callers iterate getSlotMap.) Reads SlotMap — does not re-probe
  // tryAdd (OccupiedSlots already includes the last field).
  MCSlotKind lastPickedSlot() const {
    return SlotMap.empty() ? MCSlotKind() : SlotMap.back().first;
  }

private:
  // True iff \p HintSlot is a legal field for \p Opcode under placement
  // alternatives (alts-only; no getLegalSlots fallback).
  // Committed format-members accept only their fixed getSlotKind.
  bool isHintSlotLegal(unsigned Opcode, MCSlotKind HintSlot) const {
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind())
      return Fixed == HintSlot;
    const SlotBits HintBit =
        SlotBits(1) << static_cast<unsigned>(HintSlot);
    HaydnMCFormats SolverFmts;
    SmallVector<PlacementAlternative, 4> Alts;
    if (!enumeratePlacementAlternatives(SolverFmts, Opcode, Alts))
      return false;
    for (const PlacementAlternative &A : Alts)
      if (A.FieldSlots == HintBit)
        return true;
    return false;
  }

  // Pick the first legal field for \p Opcode given CURRENT occupied slots.
  // Committed format-member opcodes (post-setDesc) use fixed getSlotKind
  // (AIEBundle.h:92-104). Alts-bearing logicals: CycleState tryAddProduct.
  // Else nullopt (no getLegalSlots first-fit).
  std::optional<MCSlotKind> pickSlot(unsigned Opcode) const {
    return pickSlotForOccupied(Opcode, OccupiedSlots);
  }

  // Commit \p Slot for \p Instr: append to SlotMap and mark OccupiedSlots.
  // Shared tail of both `add` overloads.
  void reserveSlot(I *Instr, MCSlotKind Slot) {
    const MCSlotInfo *SI = FormatInterface->getSlotInfo(Slot);
    assert(SI && "no SlotInfo for picked slot");
    SlotMap.push_back({Slot, Instr});
    OccupiedSlots |= SI->getSlotSet();
  }

  std::optional<MCSlotKind>
  pickSlotForOccupied(unsigned Opcode, SlotBits Occ) const {
    // Already-materialized format-member opcodes (MI.setDesc after
    // leaveRegion) have a single fixed slot — AIE AIEBundle.h:92-104
    // getSlotKind + conflict/format check.
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind()) {
      const MCSlotInfo *SI = FormatInterface->getSlotInfo(Fixed);
      if (!SI)
        return std::nullopt;
      if (Occ & SI->getConflictSet())
        return std::nullopt;
      const SlotBits NewSlots = Occ | SI->getSlotSet();
      if (!FormatInterface->isFormatAvailable(NewSlots))
        return std::nullopt;
      return Fixed;
    }

    // PlacementAlternative + product tryAdd for multi-slot logicals
    // (AIEHazardRecognizer.cpp:174-214 alt try; AIEBundle.h:62-105 canAdd).
    // Alts-only — no getLegalSlots no-alt fallback.
    HaydnMCFormats SolverFmts;
    if (!hasPlacementAlternatives(SolverFmts, Opcode))
      return std::nullopt;
    haydn::bundle::CycleState Probe =
        haydn::bundle::makeProductCycleStateFromOccupied(Occ);
    if (Probe.FeasibleFormatMask == 0)
      return std::nullopt;
    if (!haydn::bundle::tryAddProduct(Probe, SolverFmts, Opcode))
      return std::nullopt;
    assert(!Probe.Members.empty());
    return haydnSlotMaskToKind(Probe.Members.back().FieldSlots);
  }

  static bool isNoHazardMetaInstruction(unsigned Opcode) {
    switch (Opcode) {
    case TargetOpcode::IMPLICIT_DEF:
    case TargetOpcode::KILL:
      return true;
    default:
      return false;
    }
  }

  const HaydnBaseMCFormats *FormatInterface;
  SlotBits OccupiedSlots = 0;
  std::vector<I *> Instrs;
  SmallVector<std::pair<MCSlotKind, I *>, 3> SlotMap;
  std::vector<I *> MetaInstrs;
  I *BundleRoot = nullptr;
};

using MCBundle = Bundle<MCInst>;
// AIE peer: AIEBundle.h:271-273 MachineBundle / ConstMachineBundle.
using MachineBundle = Bundle<MachineInstr>;
using ConstMachineBundle = Bundle<const MachineInstr>;

} // namespace Haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H
