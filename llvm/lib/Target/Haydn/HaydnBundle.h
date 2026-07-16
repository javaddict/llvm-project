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
// the format-first tablegen foundation — Phase 1 dependency infrastructure
// (inert: no pass wires it yet).
//
// A bundle holds instructions in three partially redundant representations:
// the instructions in original issue order
// a map indexed by slot number
// a bitset of occupied slots
// A bundle is valid iff `isFormatAvailable(OccupiedSlots)`.
//
// Haydn adaptation vs AIE: AIE ops have a SINGLE slot (`getSlotKind`); Haydn
// ops are multi-slot (`getLegalSlots` returns a SET — e.g. funct-ALU64 is
// S1|S2, ALU32 is S0|S1|S2). So `canAdd`/`add` PICK the first legal slot that
// keeps the bundle's format valid (the `findFittingSlot` first-fit logic
// unified here). The pick is deterministic given the bundle's current
// occupancy, so SMS sees stable slot pressure.
//
// Built on `HaydnMCFormats` (the AIE-parity format interface: getLegalSlots
// getSlotInfo, isFormatAvailable, isSupportedInstruction —). No
// upstream patch required for this data model.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H

#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include <vector>

namespace llvm {
namespace Haydn {

// \a I must provide getOpcode.
template <class I> class Bundle {
public:
  Bundle(const HaydnBaseMCFormats *FormatInterface)
      : FormatInterface(FormatInterface) {}

  // Whether adding \p Instr (by opcode) leaves the bundle valid. Picks the
  // first legal slot from getLegalSlots whose addition keeps a format
  // available (isFormatAvailable) and that doesn't conflict with an already
  // occupied slot. A truly empty bundle (no instructions AND no reserved
  // slots) always accepts (standalone escape, mirrors AIE AIEBundle.h:71-73);
  // a standalone bundle (one unsupported op) accepts nothing more.
  // Contract : canAdd and reserveByOpcode/add MUST agree on slot
  // availability. The verdict is derived from the SAME pickSlot call in every
  // branch, so the two can never diverge. The empty-bundle early-out is the
  // ONE exception (AIE-faithful standalone escape): on a bundle with neither
  // Instrs nor OccupiedSlots any op is accepted as a future standalone
  // parcel, and reserveByOpcode/add treat "pickSlot found no slot" as a
  // graceful no-op (AIE AIEBundle.h:134-139) — NOT an assert. This is the
  // only way canAdd(true on empty) can hold without pickSlot having to
  // succeed for every supported opcode (some supported opcodes have
  // getLegalSlots==0, e.g. a Flex-table opcode with no legacy enc mapping
  // and no funct routing — pickSlot returns nullopt for them).
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
    // pickSlot may return nullopt for a supported opcode whose getLegalSlots is
    // empty (no legal slot). On a truly empty bundle canAdd returned true
    // (the AIE-faithful standalone escape), so this op becomes a standalone
    // parcel with no OccupiedSlots update — mirroring AIE's add
    // (AIEBundle.h:134-139, which returns without setting OccupiedSlots when
    // getSlotKind is unknown). Reserve the slot only when pickSlot finds one.
    auto Slot = pickSlot(Opcode);
    if (!Slot) {
      assert(Instrs.size() == 1 && "no-slot op added to a non-empty bundle");
      return;
    }
    reserveSlot(Instr, *Slot);
  }

  // (GAP-MC2): add \p Instr with a HINT slot. If \p HintSlot is free
  // legal for \p Instr, and the resulting occupancy has a valid format, place
  // \p Instr there. Otherwise fall back to the first-fit `pickSlot` (bare
  // auto-assign). Used by the MC encoder when an explicit `.sN` suffix or
  // source-order position records the intended slot (AsmParser
  // `setHaydnSlot`). \pre canAdd(Instr->getOpcode).
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
    // Try the hint first: is it free, legal, and format-valid?
    std::optional<MCSlotKind> Chosen;
    const MCSlotInfo *HintSI = FormatInterface->getSlotInfo(HintSlot);
    if (HintSI && !(OccupiedSlots & HintSI->getSlotSet()) &&
        (FormatInterface->getLegalSlots(Opcode) &
         (SlotBits(1) << static_cast<unsigned>(HintSlot))) &&
        FormatInterface->isFormatAvailable(OccupiedSlots | HintSI->getSlotSet())) {
      Chosen = HintSlot;
    }
    // Hint did not fit: fall back to first-fit pickSlot (bare auto-assign).
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
  // found" outcome AIE's add tolerates. The SMS ResourceManager
  // (MachinePipeliner.cpp calculateResMIIDFA:4155-4156) creates a fresh empty
  // HaydnResourceCycle, asserts canReserveResources(MI)=true (empty→true), then
  // calls reserveResources(MI) — a hard assert here crashes SMS on any op whose
  // getLegalSlots is empty (regression). After the first successful
  // reserve, OccupiedSlots is non-zero while Instrs stays empty; canAdd then
  // consults pickSlot so a second same-slot op is rejected and ResMII grows
  void reserveByOpcode(unsigned Opcode) {
    if (isNoHazardMetaInstruction(Opcode) || Opcode == TargetOpcode::BUNDLE)
      return;
    assert(canAdd(Opcode) && "pre-condition canAdd violated");
    // A truly empty bundle accepts via the canAdd early-out; a standalone
    // unsupported op has no slot to reserve. Either way, no OccupiedSlots
    // change — the next supported op will start the slot accounting.
    if (!FormatInterface->isSupportedInstruction(Opcode))
      return;
    // pickSlot may return nullopt for a supported opcode whose getLegalSlots is
    // empty (no legal slot). No OccupiedSlots change — mirrors AIE's add
    // standalone behavior and keeps canAdd/reserveByOpcode consistent.
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

  // For testing: the slot picked for the LAST added supported instruction.
  // (Production callers iterate getSlotMap.)
  MCSlotKind lastPickedSlot() const {
    return Instrs.empty() ? MCSlotKind()
                          : pickSlotForOccupied(Instrs.back()->getOpcode());
  }

private:
  // Pick the first legal slot for \p Opcode given the CURRENT occupied slots.
  // Iterates the FlexMap legal-slot set (HaydnMCFormats::getLegalSlots — the
  // single authority); for each, checks the slot is free and the
  // resulting occupancy still has a valid format.
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
    // getLegalSlots (FlexMap-derived) replaces the former getAltSlotSet
    // (legal ∩ FU-acceptance). The FU-acceptance gate is redundant subsequent :
    // the FlexMap is already FU-aware (slot k is legal iff a _S<k>
    // variant exists, and the.td FU/slot assignment produces that variant).
    // The isFormatAvailable check below still rejects invalid slot combos.
    SlotBits Alts = FormatInterface->getLegalSlots(Opcode);
    if (Alts == 0)
      return std::nullopt;
    // Prefer higher slots first (S2 → S1 → S0). First-fit S0-first lets
    // flexible ALU (ADD32/ADDI32, legal on any slot) steal S0/S1 before a
    // load is scheduled; LD64 is only legal on S0|S1, so the load then
    // fails to pack into the only cycle its es/ls window allows and SMS
    // reports Schedule Found=0. Preferring S2 for multi-slot ALU leaves
    // S0 free for loads (matches the Slot0 LS unit's primary home).
    for (int B = 2; B >= 0; --B) {
      SlotBits Bit = SlotBits(1) << B;
      if (!(Alts & Bit))
        continue;
      // Bit is a Haydn::SLOT* bitmask (1/2/4); convert to the MCSlotKind index
      // (Haydn_SLOT_S0/S1/S2) that getSlotInfo expects.
      MCSlotKind Slot = haydnSlotMaskToKind(Bit);
      const MCSlotInfo *SI = FormatInterface->getSlotInfo(Slot);
      if (!SI)
        continue;
      SlotBits NewSlots = Occ | SI->getSlotSet();
      // Slot must be free, and (if conflict info is available) the slot's
      // conflict-closure must not overlap the occupied slots. AIE derives
      // ConflictBits from the defined packet formats (self | slots that no
      // packet format combines with this slot). wires Haydn's generated
      // PacketFormats table (GET_FORMATS_PACKETS_TABLE now consumed): with
      // BUNDLE128_FULL covering {S0,S1,S2}, the backend's computeSlotSets
      // derives ConflictBits = self-only for every slot (1/2/4 — each slot
      // co-emits with the other two in Bundle128, so none are excluded). The
      // SLOT_ALL sentinel branch below therefore never triggers for the real
      // table; it remains as a defensive fallback for a future slot with no
      // packet-format membership (ConflictBits would degenerate to AllSlots).
      // With self-only ConflictBits, `(Occ & ConflictSet & ~SlotSet)` is always
      // 0 (self is masked out), so this clause is a permissive no-op for valid
      // combos — correct, since Bundle128 permits any subset of {S0,S1,S2}.
      if (Occ & SI->getSlotSet())
        continue;
      if (SI->getConflictSet() != SLOT_ALL &&
          (Occ & SI->getConflictSet() & ~SI->getSlotSet()))
        continue;
      if (!FormatInterface->isFormatAvailable(NewSlots))
        continue;
      return Slot;
    }
    return std::nullopt;
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

} // namespace Haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNBUNDLE_H
