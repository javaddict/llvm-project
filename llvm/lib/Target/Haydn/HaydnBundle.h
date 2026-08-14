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
// AIE canAdd (AIEBundle.h:62-105) is empty-escape + slot ConflictBits +
// isFormatAvailable. Haydn overlays Format E unit injectivity on the same
// gate: seven execution units are injective and are not encoded entry
// identity (stores exist only at LOADSTORE0 e0). canAdd / exactTryAddProduct
// share opcodesHaveFormatEUnitCover.
//
// A bundle holds instructions in three partially redundant representations:
// the instructions in original issue order
// a map indexed by slot number
// a bitset of occupied slots
// A bundle is valid iff `isFormatAvailable(OccupiedSlots)`.
//
// Bundle is a **solver adapter**. For logicals that have PlacementAlternatives,
// canAdd/add/reserveByOpcode route through pure exactTryAddProduct on a private
// nondominated CycleCandidateSet (HaydnBundleFormatSolver.h ) — the AIE
// alt-try shape (AIEHazardRecognizer.cpp:174-214 getAlternateInstsOpcode +
// Bundle.canAdd AltOpcode) strengthened so first-fit freeze cannot dead-end a
// legal pack. OccupiedSlots / SlotMap report the preferred candidate
// (S2→S1→S0 materialize order); empty standalone escape is retained (SMS ResMII).
//
// Alts-only pickSlot: no getLegalSlots no-alt fallback. No-alt opcodes fail
// pickSlot (nullopt) unless standalone empty-escape accepts them.
//
// getFeasibleFormatMask exposes the pre-commit FormatID frontier from
// generated PacketFormats coverage of OccupiedSlots ( productFeasible
// FormatMask). AIE peer getFormatOrNull (AIEBundle.h:150-156) returns one
// format; Haydn keeps a mask until post-RA freeze. Logical ops only — no
// setDesc / no FormatID commit here.
//
// Haydn adaptation vs AIE: AIE ops have a SINGLE slot (`getSlotKind`); Haydn
// multi-slot logicals enumerate PlacementAlternative FieldSlots with exact
// nondominated rematching (plan §3.2).
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
 // Alts-bearing logicals: exact product candidate expand.
  // Else: no getLegalSlots fallback — pickSlot nullopt outside empty escape.
  // A truly empty bundle (no instructions AND no reserved slots) always
  // accepts (standalone escape, mirrors AIE AIEBundle.h:71-73); a standalone
  // bundle (one unsupported op) accepts nothing more.
  // Contract : canAdd and reserveByOpcode/add MUST agree on slot
  // availability. The empty-bundle early-out is the ONE exception
  // (AIE-faithful standalone escape): on a bundle with neither Instrs nor
  // OccupiedSlots any op is accepted as a future standalone parcel, and
  // reserveByOpcode/add treat "pickSlot found no slot" as a graceful no-op
  // (AIE AIEBundle.h:134-139) — NOT an assert.
  // reserveByOpcode updates OccupiedSlots WITHOUT pushing Instrs
  // (SMS ResourceCycle path). empty alone is therefore insufficient
  // after the first reserveResources the bundle still has Instrs.empty
  // but OccupiedSlots may be non-zero. Gating the standalone escape on
  // OccupiedSlots == 0 forces a second same-slot op through the exact packer
  // so ResMII can grow above 1.
  bool canAdd(unsigned Opcode) const {
    // Truly empty (no instructions AND no reserved slots): always accept
    // (any op can emit standalone; the format check becomes meaningful once
    // a companion is added). After reserveByOpcode, OccupiedSlots may be
    // non-zero while Instrs is empty — fall through to exact packer/format
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
    // Overlay on AIEBundle.h:62-105 canAdd: Format E unit injectivity
    // (units ≠ encoded entries). Residual FieldSlots S0 vs S2 are not
    // permission for two LOADSTORE0-only stores in one issue cycle.
    {
      SmallVector<unsigned, 4> Ops;
      if (!Instrs.empty()) {
        Ops.reserve(Instrs.size() + 1);
        for (I *Inst : Instrs)
          Ops.push_back(Inst->getOpcode());
      } else if (!PackingCandidates.empty()) {
        const haydn::bundle::CycleState &Pref =
            haydn::bundle::selectPreferredCandidate(PackingCandidates);
        Ops.reserve(Pref.Members.size() + 1);
        for (const haydn::bundle::CycleMember &Mem : Pref.Members)
          Ops.push_back(Mem.LogicalOpcode);
      }
      Ops.push_back(Opcode);
      if (!haydn::bundle::opcodesHaveFormatEUnitCover(Ops))
        return false;
    }
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
    reserveSlot(Instr, *Slot, /*ForceSlot=*/false);
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
    // Residual S* kinds carry SlotSet bits 32/64/128; PackingCandidates
    // occupancy uses FieldSlots SLOT0/1/2 (1/2/4). Compare FieldSlots for free.
    std::optional<MCSlotKind> Chosen;
    const MCSlotInfo *HintSI = FormatInterface->getSlotInfo(HintSlot);
    SlotBits HintOcc = residualSlotKindToFieldSlots(HintSlot);
    if (!HintOcc && HintSI)
      HintOcc = HintSI->getSlotSet();
    if (HintSI && HintOcc && !(OccupiedSlots & HintOcc) &&
        isHintSlotLegal(Opcode, HintSlot) &&
        haydn::bundle::productCovers(FormatInterface->getPacketFormats(),
                                     OccupiedSlots | HintOcc)) {
      Chosen = HintSlot;
    }
    // Hint did not fit: fall back to solver pickSlot (alts tryAdd).
    if (!Chosen)
      Chosen = pickSlot(Opcode);
    if (!Chosen) {
      assert(Instrs.size() == 1 && "no-slot op added to a non-empty bundle");
      return;
    }
    reserveSlot(Instr, *Chosen, /*ForceSlot=*/Chosen == HintSlot &&
                                    HintSI != nullptr);
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
 // Exact expand PackingCandidates + preferred OccupiedSlots.
    commitOpcodePlacement(Opcode);
  }

  // Whether a product format admits the currently occupied slots.
  // Uses haydn::bundle::productCovers so transitional legacy SLOT0/1/2
  // occupancy remains legal while PacketFormats tables use E2/E3 entry bits.
  bool hasValidFormat() const {
    assert(!isStandalone());
    return haydn::bundle::productCovers(FormatInterface->getPacketFormats(),
                                        OccupiedSlots);
  }

  // Return a covering VLIWFormat for OccupiedSlots, if any.
  // AIE peer: AIEBundle.h:150-156 getFormatOrNull via PacketFormats::getFormat.
  // Exact PacketFormats entry-slot cover wins. Transitional FieldSlots /
  // residual S* occupancy is not entry-bit identity: resolve the Format E
  // composite from the surviving FeasibleFormatMask (preferred packing
  // candidate when present) via selectProductRow + productVLIWFormatForRow.
  // Never stamp the empty-cover product representative after a miss.
  const VLIWFormat *getFormatOrNull(unsigned Size = 0) const {
    assert(!isStandalone());
    const PacketFormats &PF = FormatInterface->getPacketFormats();
    if (Size) {
      if (const VLIWFormat *F = PF.getFormatBySize(OccupiedSlots, Size))
        return F;
      // Size filter is exact: only resolve transitional cover when EncodedBytes
      // match the product parcel.
      if (Size != haydn::bundle::productParcelBytes().Value)
        return nullptr;
    } else if (const VLIWFormat *F = PF.getFormat(OccupiedSlots)) {
      return F;
    }
    if (!haydn::bundle::productCovers(PF, OccupiedSlots))
      return nullptr;
    uint64_t Mask =
        haydn::bundle::productFeasibleFormatMask(PF, OccupiedSlots);
    if (!PackingCandidates.empty())
      Mask = haydn::bundle::selectPreferredCandidate(PackingCandidates)
                 .FeasibleFormatMask;
    const haydn::bundle::BundleFormatRowID Row =
        haydn::bundle::selectProductRow(Mask, size());
    return haydn::bundle::productVLIWFormatForRow(PF, Row);
  }

  // Feasible FormatID frontier for current OccupiedSlots (logical only).
  // AIE peer: getFormatOrNull returns one covering VLIWFormat*
  // (AIEBundle.h:150-156; AIEFormat.cpp:18-27 first-covering). Haydn keeps a
  // FormatID *mask* so Pre-RA/SMS can reason about multi-format readiness
 // without freezing FormatID or setDesc (plan §7.1). : generated
  // PacketFormats coverage → ProductFormatMask (E2|E3) when composite
  // still covers OccupiedSlots (not hand productFormatTable).
  uint64_t getFeasibleFormatMask() const {
    return haydn::bundle::productFeasibleFormatMask(
        FormatInterface->getPacketFormats(), OccupiedSlots);
  }

  void clear() {
    OccupiedSlots = 0;
    Instrs.clear();
    MetaInstrs.clear();
    SlotMap.clear();
    BundleRoot = nullptr;
    PackingCandidates = haydn::bundle::makeProductCandidateSet(
        FormatInterface->getPacketFormats());
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

  // AIE twin (AIEBundle.h:206-213). HR/ResourceCycle public gate: true
  // zero-resource meta only. MultiSlot_Pseudo is isPseudo=1 but is NOT listed
  // here — it must book issue/stages/ports / tryAdd slots.
  static bool isNoHazardMetaInstruction(unsigned Opcode) {
    switch (Opcode) {
    case TargetOpcode::IMPLICIT_DEF:
    case TargetOpcode::KILL:
      return true;
    default:
      return false;
    }
  }

  // Post-RA cycle reconstruction skip (opcode + isPseudo + formats). Never
  // blanket MCInstrDesc::isPseudo: MultiSlot_Pseudo is isPseudo=1 but has
  // PlacementAlternatives and books slots in HR, so it must remain a cycle
  // member for reconstruction/splice. True zero-resource meta is always
  // skippable. No-alt expand residuals stay skippable until residual
  // expansion is guaranteed before pack.
  static bool isBundlePackSkippableOpcode(unsigned Opcode, bool IsPseudo,
                                          const HaydnBaseMCFormats &Fmts) {
    if (isNoHazardMetaInstruction(Opcode))
      return true;
    if (hasPlacementAlternatives(Fmts, Opcode))
      return false;
    return IsPseudo;
  }

private:
  // True iff \p HintSlot is a legal field for \p Opcode under placement
  // alternatives (alts-only; no getLegalSlots fallback).
  // Committed format-members accept only their fixed getSlotKind.
  bool isHintSlotLegal(unsigned Opcode, MCSlotKind HintSlot) const {
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind())
      return Fixed == HintSlot;
    // Map residual S0/S1/S2 kinds to FieldSlots (SLOT0/1/2). Do not use
    // 1<<Kind — residual S* enum indices sit after E2/E3 entry kinds.
    SlotBits HintBit = residualSlotKindToFieldSlots(HintSlot);
    if (!HintBit)
      return false;
    const HaydnMCFormats &SolverFmts =
        static_cast<const HaydnMCFormats &>(*FormatInterface);
    SmallVector<PlacementAlternative, 4> Alts;
    if (!enumeratePlacementAlternatives(SolverFmts, Opcode, Alts))
      return false;
    for (const PlacementAlternative &A : Alts)
      if (A.FieldSlots == HintBit)
        return true;
    return false;
  }

  // Probe a legal field for \p Opcode under the live candidate set (or fixed
  // getSlotKind for post-setDesc members). Does not commit PackingCandidates.
 // Alts-bearing: canExactTryAddProduct. Else nullopt (no getLegalSlots).
  std::optional<MCSlotKind> pickSlot(unsigned Opcode) const {
    // Already-materialized format-member opcodes (MI.setDesc after
    // leaveRegion) have a single fixed slot — AIE AIEBundle.h:92-104
    // getSlotKind + conflict/format check against preferred occupancy.
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind()) {
      const MCSlotInfo *SI = FormatInterface->getSlotInfo(Fixed);
      if (!SI)
        return std::nullopt;
      // Slot already taken. Prefer SlotSet occupancy over the full conflict
      // mask: residual S0/S1/S2 conflict sets currently include Format E
      // entry-slot bits after E2/E3 kinds were inserted, which would false-
      // reject legal multi-issue of committed members.
      if (OccupiedSlots & SI->getSlotSet())
        return std::nullopt;
      const SlotBits NewSlots = OccupiedSlots | SI->getSlotSet();
      if (!haydn::bundle::productCovers(FormatInterface->getPacketFormats(),
                                        NewSlots))
        return std::nullopt;
      return Fixed;
    }

 // PlacementAlternative + exact product expand (; AIE alt try
    // strengthened). Alts-only — no getLegalSlots no-alt fallback.
    const HaydnMCFormats &SolverFmts =
        static_cast<const HaydnMCFormats &>(*FormatInterface);
    if (!hasPlacementAlternatives(SolverFmts, Opcode))
      return std::nullopt;
    if (!haydn::bundle::canExactTryAddProduct(PackingCandidates, SolverFmts,
                                              Opcode))
      return std::nullopt;
    // Preferred successor field for SlotMap / tests (does not freeze alts).
    haydn::bundle::CycleCandidateSet Probe = PackingCandidates;
    bool Ok = haydn::bundle::exactTryAddProduct(Probe, SolverFmts, Opcode);
    assert(Ok && !Probe.empty());
    (void)Ok;
    const haydn::bundle::CycleState &Pref =
        haydn::bundle::selectPreferredCandidate(Probe);
    assert(!Pref.Members.empty());
    return haydnSlotMaskToKind(Pref.Members.back().FieldSlots);
  }

  // Commit \p Instr into SlotMap and expand PackingCandidates when alts-bearing.
  // \p Slot is the provisional preferred field (hint or pickSlot); after exact
  // expand, SlotMap is re-synced from selectPreferredCandidate so rematching
 // earlier members stays encode-consistent.
  // When \p ForceSlot, only successors that placed the new member on \p Slot
  // are retained (explicit `.sN` / encode hint).
  void reserveSlot(I *Instr, MCSlotKind Slot, bool ForceSlot) {
    const MCSlotInfo *SI = FormatInterface->getSlotInfo(Slot);
    assert(SI && "no SlotInfo for picked slot");
    SlotMap.push_back({Slot, Instr});

    const unsigned Opcode = Instr->getOpcode();
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind()) {
      // Post-setDesc member: occupancy is the fixed slot; rebuild a singleton
      // candidate from the new occupancy (member history not required for
      // further fixed-slot checks).
      OccupiedSlots |= SI->getSlotSet();
      PackingCandidates.clear();
      PackingCandidates.push_back(
          haydn::bundle::makeProductCycleStateFromOccupied(
              FormatInterface->getPacketFormats(), OccupiedSlots));
      return;
    }

    const HaydnMCFormats &SolverFmts =
        static_cast<const HaydnMCFormats &>(*FormatInterface);
    if (hasPlacementAlternatives(SolverFmts, Opcode)) {
 // Exact commit: expand all nondominated successors.
      bool Ok =
          haydn::bundle::exactTryAddProduct(PackingCandidates, SolverFmts, Opcode);
      assert(Ok && "reserveSlot after canAdd/pickSlot without exact expand");
      (void)Ok;
      if (ForceSlot) {
        // FieldSlots are Haydn::SLOT*; residual S* kinds map via helper.
        // SI->getSlotSet() is the residual kind's own SlotSet bit (32/64/128)
        // and must not be compared to FieldSlots.
        SlotBits Want = residualSlotKindToFieldSlots(Slot);
        if (!Want)
          Want = SI->getSlotSet();
        haydn::bundle::CycleCandidateSet HintKept;
        for (const haydn::bundle::CycleState &C : PackingCandidates) {
          if (!C.Members.empty() && C.Members.back().FieldSlots == Want)
            HintKept.push_back(C);
        }
        assert(!HintKept.empty() && "forced slot not among exact successors");
        PackingCandidates = std::move(HintKept);
      }
      syncSlotMapFromPreferred();
      return;
    }

    // No-alt: should not reach here when pickSlot returned a slot.
    OccupiedSlots |= SI->getSlotSet();
  }

  /// Rewrite SlotMap + OccupiedSlots from the preferred surviving matching so
 /// encode SlotMap tracks rematches of earlier members.
  /// SlotMap only records `add` placements; `reserveByOpcode` may have added
  /// earlier CycleMembers without SlotMap rows — sync the SlotMap suffix.
  void syncSlotMapFromPreferred() {
    assert(!PackingCandidates.empty());
    const haydn::bundle::CycleState &Pref =
        haydn::bundle::selectPreferredCandidate(PackingCandidates);
    OccupiedSlots = Pref.OccupiedSlots;
    assert(Pref.Members.size() >= SlotMap.size() &&
           "SlotMap longer than CycleMember history");
    const unsigned Base = Pref.Members.size() - SlotMap.size();
    for (unsigned Idx = 0, End = SlotMap.size(); Idx != End; ++Idx) {
      MCSlotKind Kind =
          haydnSlotMaskToKind(Pref.Members[Base + Idx].FieldSlots);
      assert(Kind != MCSlotKind() && "member FieldSlots not a single slot");
      SlotMap[Idx].first = Kind;
    }
  }

  // Opcode-only reserve (SMS / reserveByOpcode): exact expand without Instrs.
  void commitOpcodePlacement(unsigned Opcode) {
    MCSlotKind Fixed = FormatInterface->getSlotKind(Opcode);
    if (Fixed != MCSlotKind()) {
      const MCSlotInfo *SI = FormatInterface->getSlotInfo(Fixed);
      assert(SI);
      OccupiedSlots |= SI->getSlotSet();
      PackingCandidates.clear();
      PackingCandidates.push_back(
          haydn::bundle::makeProductCycleStateFromOccupied(
              FormatInterface->getPacketFormats(), OccupiedSlots));
      return;
    }
    const HaydnMCFormats &SolverFmts =
        static_cast<const HaydnMCFormats &>(*FormatInterface);
    if (!hasPlacementAlternatives(SolverFmts, Opcode))
      return;
    bool Ok =
        haydn::bundle::exactTryAddProduct(PackingCandidates, SolverFmts, Opcode);
    assert(Ok && "commitOpcodePlacement without canAdd");
    (void)Ok;
    OccupiedSlots =
        haydn::bundle::selectPreferredCandidate(PackingCandidates).OccupiedSlots;
  }

  const HaydnBaseMCFormats *FormatInterface;
  /// Preferred occupancy (selectPreferredCandidate) for getOccupiedSlots /
  /// format coverage. Live rematching state is PackingCandidates.
  SlotBits OccupiedSlots = 0;
 /// : private nondominated CycleState set (plan §3.2). Not MIR-durable.
  haydn::bundle::CycleCandidateSet PackingCandidates =
      haydn::bundle::makeProductCandidateSet();
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
