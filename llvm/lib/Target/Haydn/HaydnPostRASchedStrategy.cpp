//===-- HaydnPostRASchedStrategy.cpp - Haydn post-RA bundle-forming sched -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the post-RA scheduler strategy that forms VLIW bundles in
// leaveRegion/leaveMBB (Stream B Phase B2, ). Port of AIE's
// computeAndFinalizeBundles (AIEMachineScheduler.cpp:158-234) + commitBlock
// Schedule (AIEMachineScheduler.cpp:806-863) + applyBundles
// (AIEHazardRecognizer.cpp:317-351), stripped to the Top-zone, per-region
// core (no inter-block fixpoint, no Bot zone). Stage-0 PostPipeliner is gated
// by -haydn-enable-post-pipeliner (default OFF): multi-stage materialize on
// single-BB ZOL; leaveRegion skips HR-auction bundling for those regions.
// Stage-0 inter-block (-haydn-enable-interblock, default OFF) runs after
// bundle materialize in leaveMBB — see HaydnInterBlockScheduling.h.
//
//===----------------------------------------------------------------------===//

#include "HaydnPostRASchedStrategy.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnInstrInfo.h"

#include "HaydnMachineFunctionInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-post-ra-sched"

// Safety net only: verify AltDescs/HR-assigned slots form a legal Bundle128.
// Does **not** treat opcode `_S*` as placement (: slot = AltDescs).
// Greedy over FlexMap legal slots when AltDescs is empty (late unscheduled).
static bool cycleCanFormLegalBundle(ArrayRef<MachineInstr *> Instrs,
                                    HaydnAlternateDescriptors &AltDescs,
                                    const MCInstrInfo &MCII) {
  if (Instrs.empty() || Instrs.size() > 3)
    return false;
  bool Used[3] = {false, false, false};
  SmallVector<MachineInstr *, 3> Flexible;
  for (MachineInstr *MI : Instrs) {
    if (std::optional<unsigned> Slot = AltDescs.getSelectedSlot(MI)) {
      if (*Slot >= 3 || Used[*Slot])
        return false;
      if (!getHaydnFlexVariantForSlot(MI->getOpcode(), *Slot, MCII))
        return false;
      Used[*Slot] = true;
    } else {
      Flexible.push_back(MI);
    }
  }
  for (MachineInstr *MI : Flexible) {
    bool Placed = false;
    for (unsigned K = 0; K < 3; ++K) {
      if (Used[K])
        continue;
      if (!getHaydnFlexVariantForSlot(MI->getOpcode(), K, MCII))
        continue;
      Used[K] = true;
      Placed = true;
      break;
    }
    if (!Placed)
      return false;
  }
  return true;
}

HaydnPostRASchedStrategy::HaydnPostRASchedStrategy(const MachineSchedContext *C)
    : PostGenericScheduler(C) {
  // Cache the Haydn TII from the MachineFunction's subtarget. enterMBB runs
  // BEFORE the base scheduler calls initialize(DAG), so the DAG member is
  // still null when enterMBB first fires — get TII from the context instead.
  // (ScheduleDAGMI::startBlock -> SchedImpl->enterMBB happens before any
  // region's initialize; see MachineScheduler.cpp:824 vs :858/.)
  HII = static_cast<const HaydnInstrInfo *>(C->MF->getSubtarget().getInstrInfo());
}

bool HaydnPostRASchedStrategy::tryCandidate(SchedCandidate &Cand,
                                            SchedCandidate &TryCand) {
  // ISA-34 Gap D + dual-load+MAC pack:
  // Prefer a load over a pure ALU/MAC when choosing the cycle's FIRST op so
  // multi-cycle load latency overlaps later compute. Once the current best
  // candidate is already a load, do NOT force-reject a non-load Try — fall
  // through to the base critical-path/resource heuristics so a ready MAC can
  // co-issue with the load (S2) instead of always hoisting another load that
  // only dual-issues at best and often serializes (bkfir CB-load streams).
  if (Cand.isValid()) {
    bool CandIsLoad = Cand.SU->getInstr()->mayLoad();
    bool TryIsLoad = TryCand.SU->getInstr()->mayLoad();
    if (TryIsLoad && !CandIsLoad) {
      TryCand.Reason = ResourceDemand;
      return true;
    }
    // CandIsLoad && !TryIsLoad: fall through — do not return false.
  }
  return PostGenericScheduler::tryCandidate(Cand, TryCand);
}

// True if MI should be skipped during bundle grouping (not a bundle member):
// pseudos, meta instructions, debug, bundles themselves. Mirrors the retired
// packetizer's ignorePseudoInstruction skip list.
static bool isBundleSkippable(const MachineInstr &MI) {
  return MI.isPseudo() || MI.isImplicitDef() || MI.isKill() ||
         MI.isDebugInstr() || MI.isCopy() || MI.isInlineAsm() ||
         MI.isPosition() || MI.isBundle();
}

void HaydnPostRASchedStrategy::bumpCycleForBundles(
    unsigned ToCycle, SmallVectorImpl<CycleBundle> &Bundles,
    CycleBundle &CurrBundle) {
  // Push the in-progress bundle as the current cycle, then pad with empty
  // bundles until reaching ToCycle. Mirrors AIE's bumpCycleForBundles
  // (AIEMachineScheduler.cpp:133-154). Invariant: Bundles.size == current
  // cycle index.
  unsigned CurrCycle = Bundles.size();
  assert(ToCycle > CurrCycle && "bumpCycleForBundles must move forward");
  Bundles.push_back(CurrBundle);
  ++CurrCycle;
  CurrBundle.Instrs.clear();
  while (ToCycle != CurrCycle) {
    Bundles.push_back({});
    ++CurrCycle;
  }
}

void HaydnPostRASchedStrategy::enterMBB(MachineBasicBlock *MBB) {
  CurrentMBB = MBB;
  // Dual-load placement hint : record AltDescs slot=1 on a second
  // LD32/LD64 following a slot-0 LS op. Opcodes stay logical (Slot01_LD);
  // encoder materializes the S1 Flex window from Flags. No setDesc(LD*_S1).
  HII->promoteLoadsToSlot1(MBB->instr_begin(), MBB->instr_end());

  PostGenericScheduler::enterMBB(MBB);
}

void HaydnPostRASchedStrategy::leaveMBB() {
  // Materialize the bundles accumulated across all regions of this MBB into
  // actual BUNDLE MIs (+ standalone NOPs for idle cycles), in MBB order.
  // Mirrors AIE's commitBlockSchedule (AIEMachineScheduler.cpp:825-863), but
  // without the inter-block fixpoint gate (we always commit).
  //
  // t−3: PostRA DAG mutation ZOLSetupExitLatency raises ExitSU latency from
  // SET_HWLOOP (AIE LoopSetupDistance peer). Fixup still deficit-pads if
  // residual layout is short after relax.
  if (CurrentMBB && !MBBBundles.empty()) {
    materializeBundles(*CurrentMBB, MBBBundles);
    MBBBundles.clear();
  }
  // Stage-0 InterBlock densify deleted (YOLO). Pack ownership = leaveRegion only.
  PostGenericScheduler::leaveMBB();
}

SmallVector<HaydnPostRASchedStrategy::CycleBundle>
HaydnPostRASchedStrategy::computeRegionBundles() {
  // Walk the scheduled region's MIs and group by SU->TopReadyCycle (the cycle
  // the base list scheduler assigned). The MIs are in the MBB in scheduled
  // order (the base scheduler reorders in place via moveInstruction), so
  // same-cycle MIs are already adjacent — this is what makes bundleWithPred
  // safe in materializeBundles. Port of AIE computeAndFinalizeBundles
  // (AIEMachineScheduler.cpp:158-234), Top zone only.
  //
  // Only called for regions that were actually scheduled (RegionWasScheduled
  // gate in leaveRegion), so the MBB iterators are valid.
  SmallVector<CycleBundle> Bundles;
  CycleBundle CurrBundle;

  MachineBasicBlock::iterator Begin = DAG->begin();
  MachineBasicBlock::iterator End = DAG->end();
  if (Begin == End)
    return Bundles;

  for (MachineBasicBlock::iterator I = Begin; I != End; ++I) {
    MachineInstr &MI = *I;
    if (isBundleSkippable(MI))
      continue;

    SUnit *SU = DAG->getSUnit(&MI);
    if (!SU || !SU->isScheduled)
      continue;
    unsigned EmitCycle = SU->TopReadyCycle;

    if (EmitCycle != Bundles.size()) {
      assert(EmitCycle >= Bundles.size() && "emit cycle went backward");
      bumpCycleForBundles(EmitCycle, Bundles, CurrBundle);
    }
    CurrBundle.Instrs.push_back(&MI);
  }
  if (!CurrBundle.Instrs.empty())
    bumpCycleForBundles(Bundles.size() + 1, Bundles, CurrBundle);

  return Bundles;
}

void HaydnPostRASchedStrategy::materializeBundles(
    MachineBasicBlock &MBB, SmallVector<CycleBundle> &Bundles) {
  // Port of AIE materializeEmptyBundles (AIEMachineScheduler.cpp:806-823) +
  // applyBundles/applyFormatOrdering (AIEHazardRecognizer.cpp:278-351), Top
  // zone only and with Haydn's NOP insertion. The AsmPrinter still pads each
  // bundle's idle SLOTS to 3 (HaydnAsmPrinter.cpp:163-167); this handles
  // idle CYCLES.
  //
  // After scheduling, the region's MIs are already in the MBB in scheduled
  // order (the base scheduler reorders in place via moveInstruction). We walk
  // the bundle list and, for each non-empty cycle whose MIs are NOT already
  // contiguous (they should be, since the scheduler places same-cycle MIs
  // adjacently), we bundle them in place using the MI pointers stored in
  // CycleBundle.Instrs — NOT by advancing a fragile MBB iterator. This mirrors
  // AIE's approach of using the stored MI pointers rather than position-walking.
  //
  // For each cycle:
  // * empty cycle -> insert one standalone NOP (idle cycle padding);
  // * single MI -> leave standalone (no BUNDLE needed);
  // * 2-3 MIs -> bundleWithPred chain + finalizeBundle into a BUNDLE MI.
  for (CycleBundle &CB : Bundles) {
    if (CB.Instrs.empty()) {
      // Idle cycle: insert a standalone NOP to pad an entirely idle cycle.
      // Insert BEFORE the first terminator so the NOP never lands after a
      // branch (which the verifier rejects as "Non-terminator instruction
      // after the first terminator"). Idle cycles now arise from the MAC
      // 2-cycle result latency: when the last MAC's consumer is 2 cycles
      // away, the scheduler leaves an empty Top-zone cycle that previously
      // (under the old latency-1 model) never appeared. Inserting at
      // MBB.end placed the NOP after BNEZ/JALR terminators (D2XX). Use
      // the first terminator as the anchor; if the block has no terminator
      // yet (e.g. the exit block), MBB.end is correct.
      MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
      HII->insertNoop(MBB, InsertPt);
      continue;
    }
    if (CB.Instrs.size() == 1)
      continue; // Standalone MI — no BUNDLE needed.

    // 2-3 MIs: bundle them. The MIs in CB.Instrs are in MBB order (the
    // scheduler emits Top-zone SUs in MBB order). They SHOULD be contiguous
    // but a pseudo (e.g. LOAD_ADDR) scheduled into the same cycle can end up
    // interleaved between two real same-cycle MIs. computeRegionBundles skips
    // pseudos, so CB.Instrs would then be non-contiguous — and bundleWithPred
    // (which chains to the raw MBB predecessor) would wrongly pull the pseudo
    // into the bundle while finalizeBundle(First) finalized only a prefix
    // leaving a dangling bundle with no BUNDLE header. HaydnExpandPseudos then
    // expands the pseudo-as-bundle-head outside the bundle and DROPS the other
    // members -> their defs are lost -> "Using an undefined physical register"
    // (yarpgen seeds 7/8/9/11/15/18). Newly exposed by the MAC 2-cycle result
    // latency (prior revision), which first interleaved these independent
    // same-cycle MIs.
    //
    // Fix: move any intervening isBundleSkippable MIs to just before the first
    // real MI so the real MIs are contiguous — exactly as the code already
    // assumed (but never enforced). Reorder-safety: the post-RA scheduler
    // emits each cycle's MIs contiguously in MBB order, so a pseudo that lands
    // BETWEEN two same-cycle real MIs is itself assigned to that same cycle
    // i.e. it is dependency-independent of them (the scheduler's cycle
    // assignment is the proof of independence). Moving it earlier within the
    // same cycle's window therefore preserves all RAW/WAW/WAR edges. (.)
    MachineInstr *First = CB.Instrs.front();
    MachineInstr *Last = CB.Instrs.back();
    const TargetRegisterInfo *TRI =
        MBB.getParent()->getSubtarget().getRegisterInfo();
    // if any same-cycle skippable MI cannot be safely spliced before the
    // bundle (it defines a physreg a bundle member reads/defines — the
    // "same cycle ⇒ independent" claim is false), refuse to bundle this cycle
    // at all and leave the MIs standalone. Bundling would require reordering a
    // dependent pseudo, corrupting the data dependency (LOAD_ADDR defs $r3
    // spliced before ST8 $r3 -> store reads the address, not the value).
    bool BundleUnsafe = false;
    for (MachineBasicBlock::instr_iterator It = First->getIterator(),
                                            E = Last->getIterator();
         It != E;) {
      MachineInstr &MI = *It;
      ++It;
      if (!isBundleSkippable(MI) || &MI == First)
        continue;
      bool HasRegConflict = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef() || !MO.getReg())
          continue;
        Register DefReg = MO.getReg();
        for (const MachineInstr *RealMI : CB.Instrs) {
          if (RealMI == &MI)
            continue;
          if (RealMI->readsRegister(DefReg, TRI) ||
              RealMI->definesRegister(DefReg, TRI)) {
            HasRegConflict = true;
            break;
          }
        }
        if (HasRegConflict)
          break;
      }
      if (HasRegConflict) {
        BundleUnsafe = true;
        continue; // leave this MI where the scheduler placed it
      }
      MBB.splice(First->getIterator(), &MBB, MI.getIterator());
    }
    if (BundleUnsafe)
      continue; // leave the whole cycle standalone; do not form an unsafe bundle
    // Defensive: after splicing skippables, CB.Instrs MUST be contiguous. If a
    // future change lets a NON-skippable MI interleave same-cycle reals, the
    // assert fires instead of silently producing a dangling bundle (the original
    // bug); in a release build (assert compiled out) we skip bundling this
    // cycle and leave the MIs standalone rather than corrupt the bundle.
    bool Contiguous = true;
    for (unsigned I = 1; I < CB.Instrs.size(); ++I) {
      MachineBasicBlock::instr_iterator Prev =
          CB.Instrs[I - 1]->getIterator();
      if (std::next(Prev) != CB.Instrs[I]->getIterator()) {
        // Do not fatal: NatureDSP math kernels (e.g. vec_atan_32x32) hit this
        // when a non-skippable MI interleaves same-cycle reals after splice.
        // Leave the cycle unbundled (correct, denser packing missed) rather
        // than abort the whole compile.
        LLVM_DEBUG(dbgs() << "HaydnPostRASched: same-cycle bundle MIs not "
                             "contiguous after splice — skip bundling\n");
        Contiguous = false;
        break;
      }
    }
    if (!Contiguous)
      continue; // leave MIs standalone; do not form a dangling bundle

    // Slot legality before finalize : dual S0-only same-cycle etc.
    // Leave standalone rather than emit an oversubscribed BUNDLE that forces
    // AsmPrinter emergency split / size-model lies.
    HaydnAlternateDescriptors &AltDescs =
        MBB.getParent()->getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
    const MCInstrInfo *MCII =
        MBB.getParent()->getTarget().getMCInstrInfo();
    if (!MCII ||
        !cycleCanFormLegalBundle(CB.Instrs, AltDescs, *MCII)) {
      LLVM_DEBUG(dbgs() << "HaydnPostRASched: same-cycle MIs not Bundle128-"
                           "legal (slot collision) — leave unbundled\n");
      continue;
    }

    // bundleWithPred each subsequent MI to its (now-adjacent) predecessor.
    for (unsigned I = 1; I < CB.Instrs.size(); ++I)
      CB.Instrs[I]->bundleWithPred();
    // finalizeBundle scans forward from First collecting InsideBundle MIs.
    finalizeBundle(MBB, First->getIterator());
  }
}

void HaydnPostRASchedStrategy::materializeMultiOpcodeInstrs() {
  // Phase 1 — placement only, no opcode bake. The HR's per-cycle slot
  // auction recorded each issued MI's chosen slot in AltDescs. This ensures
  // every scheduled MI has a recorded slot (nullopt → FU/legal-slot derive via
  // commitSlotFlexVariant). MachineInstr opcodes stay LOGICAL; AltDescs slots
  // are the placement vessel through AsmPrinter / MCInstLower (HaydnMCFlags).
  //
  // DO NOT clear AlternateSlots here — MCInstLower needs them until AsmPrinter
  // (I2). Encoder materializes Flex variants only at encode time.
  HaydnAlternateDescriptors &AltDescs =
      DAG->MF.getInfo<HaydnMachineFunctionInfo>()->getAltDescs();
  for (MachineInstr &MI : make_range(DAG->begin(), DAG->top()))
    HII->commitSlotFlexVariant(MI, AltDescs.getSelectedSlot(&MI));
  for (MachineInstr &MI : make_range(DAG->bottom(), DAG->end()))
    HII->commitSlotFlexVariant(MI, AltDescs.getSelectedSlot(&MI));
  // Keep AlternateSlots for whole MF. Opcode-map side table is unused subsequent
  // bake removal; drop only that map if anything ever wrote it.
  AltDescs.clearDescriptors();
}

void HaydnPostRASchedStrategy::leaveRegion(const SUnit &ExitSU) {
  // Compute this region's bundle list from the scheduled Top zone and append
  // it to the per-MBB accumulator. The MBB is mutated later in leaveMBB.
  // Mirrors AIEPostRASchedStrategy::leaveRegion (AIEMachineScheduler.cpp:1037
  // 1082) stripped of the inter-block fixpoint gate, Bot zone, and
  // handleRegionConflicts. materializeMultiOpcodeInstrs is PORTED (
  // enabled by the pipeline reorder) — see above.
  //
  // CRITICAL: the base drive loop calls exitRegion (and thus this leaveRegion)
  // even for empty/single-MI regions that were SKIPPED (MachineScheduler.cpp:
  // 862-866) — schedule was never called, so the DAG's SUnits and region
  // iterators are stale from the previous region. The RegionWasScheduled flag
  // (set by schedNode, cleared at the start of each region) gates this: only
  // regions that actually ran the list scheduler produce bundles.
  if (!RegionWasScheduled)
    return;
  RegionWasScheduled = false;

  // record each scheduled MI's HR-auction slot in AltDescs BEFORE bundle
  // formation (placement only — no setDesc Flex bake). Bundle children keep
  // logical opcodes; slot authority is AltDescs → HaydnMCFlags → encoder.
  materializeMultiOpcodeInstrs();

  SmallVector<CycleBundle> RegionBundles = computeRegionBundles();
  for (CycleBundle &CB : RegionBundles)
    MBBBundles.push_back(std::move(CB));

  LLVM_DEBUG({
    dbgs() << "  << leaveRegion: " << RegionBundles.size()
           << " cycle(s) bundled\n";
  });
}
