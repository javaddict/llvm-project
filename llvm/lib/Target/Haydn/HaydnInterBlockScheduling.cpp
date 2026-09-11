//===-- HaydnInterBlockScheduling.cpp - inter-block DDG substrate -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIE AIEDataDependenceHelper.cpp (InterBlockEdges + its
// DataDependenceHelper base). The DDG substrate only — no AIE
// InterBlockScheduling SWP/fixpoint state machine (forbidden second SMS
// owner; Haydn's SMS is the generic pre-RA MachinePipeliner).
//
//===----------------------------------------------------------------------===//

#include "HaydnInterBlockScheduling.h"
#include "HaydnInstrInfo.h"
#include "HaydnResourceRestrictionClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/MC/MCInstrItineraries.h"
#include <functional>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "haydn-interblock"

HaydnInterBlockEdges::HaydnInterBlockEdges(const MachineSchedContext &Context,
                                           MachineBasicBlock *Pred,
                                           MachineBasicBlock *Succ)
    : ScheduleDAGInstrs(*Context.MF, Context.MLI), Pred(Pred), Succ(Succ) {
}

void HaydnInterBlockEdges::reserveForBlocks(const MachineBasicBlock &Pred,
                                             const MachineBasicBlock &Succ) {
  // newSUnit's reallocation assert forbids growth; reserve the exact upper
  // bound (both blocks' instruction counts) once, up front.
  size_t N = Pred.size() + Succ.size();
  SUnits.reserve(N);
  MISUnitMap.reserve(N);
}

void HaydnInterBlockEdges::addNode(MachineInstr *MI) {
  // AIE exposes ScheduleDAGInstrs::initSUnit for this (a common-mechanism
  // edit Haydn does not carry); the equivalent creation via the protected
  // newSUnit + MISunitMap registration used by buildSchedGraph.
  if (MI->isDebugOrPseudoInstr())
    return;
  SUnit *SU = newSUnit(MI);
  MISUnitMap[MI] = SU;
  const unsigned Index = SU->NodeNum;
  IndexMap &TheMap = Boundary ? SuccMap : PredMap;
  TheMap.emplace(MI, Index);
}

void HaydnInterBlockEdges::markBoundary() {
  assert(!Boundary.has_value());
  Boundary = SUnits.size();
}


void HaydnInterBlockEdges::buildCrossBoundaryEdges(AAResults *AA,
                                                   const TargetInstrInfo *TII,
                                                   const TargetRegisterInfo *TRI,
                                                   const TargetSchedModel *TSM) {
  // Target-owned conservative construction (HC#0 NOT minted): stock
  // buildSchedGraph cannot walk two blocks, and a subclass re-implementation
  // of its full memory walk would drift.
  //
  // INVARIANT (dependence superset over ADMITTED nodes): for every admitted
  // (Pre, Post) node pair, a cross-boundary SDep exists whenever one of these
  // classes holds:
  //   (1) register truth: an exact-register operand match, OR a pre-boundary
  //       regmask that clobbers a post operand's register. Exact-register
  //       equality IS regunit-exact on Haydn: the register file is flat
  //       (HaydnRegisterInfo.td has no SubRegs / SubRegIndices), so a
  //       register equals its only regunit.
  //   (2) memory truth: pre memory op OR pre call, against post memory op OR
  //       post call, priced through mayAlias (returns true whenever either
  //       side isCall — MachineInstr.cpp mayAlias).
  //   (3) unmodeled truth: pre (isCall || hasUnmodeledSideEffects) against
  //       post (isCall || hasUnmodeledSideEffects || mayLoad || mayStore)
  //       gets a latency-0 Order edge (CSRW->CSRR, WFI pairs, call->call).
  // Adding an edge can only RAISE Eff in the effective-latency cut
  // (Remaining = EdgeLat - Depth(Succ)); a MISSING edge shrinks Eff and cuts
  // latency MORE — under-padding on a no-interlock machine. So these classes
  // are mandatory before any W69 interblock flip, and conservatism (extra
  // edges) is the safe direction, not merely a QoR knob.
  //
  // NON-SUPERSET boundaries (what this DDG deliberately does NOT cover —
  // do not read the invariant above as blanket over-approximation):
  //   (a) Pred and Succ terminators are not nodes on either side (gather
  //       excludes them): a pre-terminator branch read vs a post-boundary
  //       writer is a WAR-only, latency-0, cut-inert pair — safe to omit.
  //   (b) Bundle-internal reads (IsInternalRead) are not cross edges.
  // Bot scoreboard replay does not read this edge set at all (it replays
  // recorded successor occupancy), so these classes only feed the cut
  // consumer.
  auto MIFor = [&](const SUnit &SU) -> MachineInstr & { return *SU.getInstr(); };

  // Dest writeback Data_Latency for a cross-boundary register Data edge.
  // getInstrLatency is STAGE latency (every product InstrStage is
  // single-cycle). Dest writeback lives in OperandCycles (loads [2], ALU
  // [1], Slot0_LS_WbLat writeback [1]). Memory-edge spacing is
  // EdgeMemoryLatency / getMemoryLatency — not this number. Golden store
  // Data_Latency is 0 (no dest); using max OperandCycles as a store→store
  // price under-pads when that vector is [1] or empty. Guard on the
  // itinerary itself, NOT hasInstrSchedModel: Haydn publishes
  // ProcessorItineraries with CompleteModel=0. Clamped so the conservative
  // SIN_COS/ARCTAN OperandCycles-17 scaffold cannot size the edge
  // (clampPublishedDataLatency; those windows are HR-reserved).
  auto EdgeDataLatency = [&](const MachineInstr &MI) -> unsigned {
    const InstrItineraryData *Itin =
        TSM ? TSM->getInstrItineraries() : nullptr;
    if (!Itin || Itin->isEmpty())
      return MI.mayLoad() ? 2 : 1;
    const unsigned SchedClass = MI.getDesc().getSchedClass();
    unsigned Lat = 0;
    for (unsigned I = 0;; ++I) {
      std::optional<unsigned> OpLat = Itin->getOperandCycle(SchedClass, I);
      if (!OpLat)
        break;
      Lat = std::max(Lat, *OpLat);
    }
    return std::max(
        1u, haydn::restriction::clampPublishedDataLatency(std::max(Lat, 1u)));
  };

  // Memory-edge latency is the published MemoryCycle pair
  // (getMemoryLatency = LastSrc - FirstDst + 1). AIE peer:
  // MemInstrItinData First/Last (AIETarget.td:39-47),
  // getMemoryLatency (AIEBaseInstrInfo.cpp:1038-1049), MemoryEdges
  // mutation (AIEBaseSubtarget.cpp:805-844). Intra-block Haydn
  // MemoryEdges already uses this API (HaydnSchedMutations.cpp).
  // Slot0_LS Last=1, First=0 → last-first+1=2. Do not copy AIE II_ST
  // MemoryCycles[5]; Haydn last is the LoadLatency=2 scaffold.
  auto EdgeMemoryLatency = [&](const MachineInstr &Src,
                               const MachineInstr &Dst) -> unsigned {
    const auto *HII = static_cast<const HaydnInstrInfo *>(TII);
    if (HII) {
      const unsigned SrcSC = Src.getDesc().getSchedClass();
      const unsigned DstSC = Dst.getDesc().getSchedClass();
      if (auto MemLat = HII->getMemoryLatency(SrcSC, DstSC))
        return std::max(1u, static_cast<unsigned>(*MemLat));
      // Dest class is not table-driven (calls). Keep the writer's
      // conservative window: AIE getConservativeMemoryLatency
      // (AIEBaseInstrInfo.cpp:1026-1034).
      if (auto Last = HII->getLastMemoryCycle(SrcSC)) {
        int WorstDst = HII->getMinFirstMemoryCycle();
        return std::max(1u, static_cast<unsigned>(*Last - WorstDst + 1));
      }
    }
    return Src.mayLoad() || Src.mayStore() ? 2 : 1;
  };

  // Register dependences, pre -> post: for each post node's operands,
  // connect against every pre node touching the same register (exact
  // equality == regunit equality on the flat file) or clobbering it via a
  // regmask. Every matching Pre operand contributes its edge class; a
  // single Pre MI that both reads and defines the same register must not
  // deliver only the Anti edge when a Data edge is true, so there is no
  // first-match break. SDep::overlaps (kind+reg) inside SUnit::addPred
  // (ScheduleDAG.cpp) is the sole collapse, extending latency when the
  // re-added edge is the costlier one.
  for (const SUnit &PostSU : SUnits) {
    if (!isPostBoundaryNode(&PostSU))
      continue;
    MachineInstr &Post = MIFor(PostSU);
    for (const MachineOperand &MO : Post.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      bool PostReads = MO.readsReg();
      bool PostWrites = MO.isDef();
      for (const SUnit &PreSU : SUnits) {
        if (!isPreBoundaryNode(&PreSU) || PreSU.isBoundaryNode())
          continue;
        MachineInstr &Pre = MIFor(PreSU);
        // (1a) Exact-register operands (covers compiled JAL_W calls, whose
        // caller-saved clobbers are materialized as implicit-def operands).
        for (const MachineOperand &PMO : Pre.operands()) {
          if (!PMO.isReg() || !PMO.getReg() || PMO.getReg() != Reg)
            continue;
          bool PreWrites = PMO.isDef();
          bool PreReads = PMO.readsReg();
          SDep::Kind Kind;
          if (PreWrites && PostReads)
            Kind = SDep::Data; // RAW across the boundary
          else if (PreReads && PostWrites)
            Kind = SDep::Anti; // WAR across the boundary
          else if (PreWrites && PostWrites)
            Kind = SDep::Output; // WAW across the boundary
          else
            continue;
          // addPred on the POST side: Post depends on Pre. The register SDep
          // constructor hardcodes latency (Data=1, Anti/Output=0); stamp the
          // producer's published Data_Latency on Data edges so the
          // effective-latency cut prices Remaining = EdgeLat - Depth(Succ)
          // with the true edge cost.
          SDep Cross(const_cast<SUnit *>(&PreSU), Kind, Reg);
          if (Kind == SDep::Data)
            Cross.setLatency(EdgeDataLatency(Pre));
          const_cast<SUnit &>(PostSU).addPred(Cross);
        }
        // (1b) Regmask clobber: a pre-boundary regmask that clobbers a post
        // operand's register is a def of that register. Today's compiled
        // JAL_W materializes its clobber set as implicit-defs, so this arm
        // dedups away there; it exists for the regmask-only shape (hand
        // MIR, future calling-convention edits) the explicit scan cannot
        // see. Exact equality == regunit equality (flat register file).
        // isPhysical guards the clobbersPhysReg assert (this builder is
        // post-RA; a vreg-shaped operand has no mask truth).
        bool PreHasRegMaskClobber = false;
        if (Reg.isPhysical()) {
          for (const MachineOperand &PMO : Pre.operands()) {
            if (PMO.isRegMask() && PMO.clobbersPhysReg(Reg)) {
              PreHasRegMaskClobber = true;
              break;
            }
          }
        }
        if (PreHasRegMaskClobber) {
          if (PostReads) {
            SDep Cross(const_cast<SUnit *>(&PreSU), SDep::Data, Reg);
            Cross.setLatency(EdgeDataLatency(Pre));
            const_cast<SUnit &>(PostSU).addPred(Cross);
          } else if (PostWrites) {
            SDep Cross(const_cast<SUnit *>(&PreSU), SDep::Output, Reg);
            const_cast<SUnit &>(PostSU).addPred(Cross);
          }
        }
      }
    }
  }

  // Memory dependences, pre -> post: any pre memory writer (or pre CALL —
  // generated JAL_W/JAL_E members carry no MayLoad/MayStore flags but a call
  // reads and writes arbitrary memory) to any post memory reader/writer or
  // post call (store->load, store->store, load->store, call-vs-memory both
  // directions) unless AA proves no-alias. Loads before the boundary reading
  // after a post store is impossible in this direction (post runs later) —
  // only pre->post edges. mayAlias returns true whenever either side isCall,
  // so calls need no separate alias logic.
  SmallVector<std::pair<const SUnit *, bool>> PreMem; // (SU, isWrite)
  for (const SUnit &SU : SUnits)
    if (isPreBoundaryNode(&SU) && !SU.isBoundaryNode()) {
      const MachineInstr &MI = MIFor(SU);
      if (MI.isCall()) {
        // A call is both a memory writer and a memory reader.
        PreMem.emplace_back(&SU, true);
        PreMem.emplace_back(&SU, false);
      } else if (MI.mayStore())
        PreMem.emplace_back(&SU, true);
      else if (MI.mayLoad())
        PreMem.emplace_back(&SU, false);
    }
  for (const SUnit &PostSU : SUnits) {
    if (!isPostBoundaryNode(&PostSU))
      continue;
    MachineInstr &Post = MIFor(PostSU);
    bool PostWrites = Post.mayStore();
    bool PostReads = Post.mayLoad();
    // Post-boundary calls read arbitrary memory (the callee may read what
    // the pre-boundary side wrote); unmodeled-side-effect post ops stay out
    // of the memory arm (no known address) and get Order edges below.
    bool PostIsMemOrCall = PostWrites || PostReads || Post.isCall();
    if (!PostIsMemOrCall)
      continue;
    for (const auto &[PreSU, PreWrites] : PreMem) {
      // load -> load is not a dependence.
      if (!PreWrites && !PostWrites)
        continue;
      // MachineInstr::mayAlias handles calls, volatile, and PSV-vs-value
      // classes (MachineInstr.cpp:1531-1577); null AA ⇒ conservative true.
      if (AA && !MIFor(*PreSU).mayAlias(AA, Post, /*UseTBAA=*/false))
        continue;
      // Memory edges carry getMemoryLatency, not dest OperandCycles.
      // The 3-arg SDep constructor binds a REGISTER, not a latency
      // (Data hardcodes Latency=1); stamp the MemoryCycle price.
      SDep MemEdge(const_cast<SUnit *>(PreSU), SDep::Data, /*Reg=*/0);
      MemEdge.setLatency(EdgeMemoryLatency(MIFor(*PreSU), Post));
      const_cast<SUnit &>(PostSU).addPred(MemEdge);
    }
  }

  // (3) Unmodeled-side-effect Order edges, pre -> post: real (non-pseudo)
  // carriers of MCID::UnmodeledSideEffects — MOVESFR2GPR/MOVEGPR2SFR,
  // FLAR/WBARWUA, and generated Format E members like JAL_E* and the SFR
  // member family — model their effect on SFR/AR state, not on the GPR/DR
  // operands or memory addresses the arms above price. These pairs must not
  // reorder across the boundary. Latency 0 by construction (the 2-arg Order
  // SDep constructor), hence cut-inert today (Remaining <= 0 skips); the
  // edge exists so any future dependence-superset consumer (W69) cannot
  // drop the ordering. This is NOT a latency guarantee.
  for (const SUnit &PostSU : SUnits) {
    if (!isPostBoundaryNode(&PostSU) || PostSU.isBoundaryNode())
      continue;
    MachineInstr &Post = MIFor(PostSU);
    bool PostUnmodeled = Post.hasUnmodeledSideEffects() || Post.isCall() ||
                         Post.mayLoad() || Post.mayStore();
    if (!PostUnmodeled)
      continue;
    for (const SUnit &PreSU : SUnits) {
      if (!isPreBoundaryNode(&PreSU) || PreSU.isBoundaryNode())
        continue;
      MachineInstr &Pre = MIFor(PreSU);
      if (!Pre.hasUnmodeledSideEffects() && !Pre.isCall())
        continue;
      SDep Ord(const_cast<SUnit *>(&PreSU), SDep::Barrier);
      const_cast<SUnit &>(PostSU).addPred(Ord);
    }
  }

  // Depth of post-boundary nodes (static topological lower bound; AIE uses
  // the same for unscheduled successors) — computed on the built graph.
  recomputePostDepthsFromEdges(TSM, TII);
}

void HaydnInterBlockEdges::clearSuccessorOccupancy() {
  SuccOccupancy.clear();
  OccupancyIsScheduled = false;
  OccupancyIsStale = false;
  OccupancyRecordingActive = false;
}

void HaydnInterBlockEdges::noteSuccessorOccupancy(MachineInstr *MI,
                                                  int Cycle) {
  // BUNDLE roots seed depths from gatherHaydnInterBlockEdges; they are not
  // the occupancy AIE replays (Region::Bundles members at
  // AIEMachineScheduler.cpp:380-384). Extra conservative skip is safe.
  if (!MI || Cycle < 0 || MI->isBundle() || MI->isDebugOrPseudoInstr())
    return;
  if (!OccupancyRecordingActive) {
    SuccOccupancy.clear();
    OccupancyIsStale = false;
    OccupancyRecordingActive = true;
  }
  OccupancyIsScheduled = true;
  if ((unsigned)Cycle >= SuccOccupancy.size())
    SuccOccupancy.resize(Cycle + 1);
  auto &Slot = SuccOccupancy[Cycle];
  if (is_contained(Slot, MI))
    return;
  Slot.push_back(MI);
}

bool HaydnInterBlockEdges::successorOccupancyIsLive() const {
  if (!OccupancyIsScheduled || OccupancyIsStale)
    return false;
  for (const auto &Cycle : SuccOccupancy) {
    for (MachineInstr *MI : Cycle) {
      if (!MI || MI->getParent() != Succ)
        return false;
    }
  }
  return true;
}

bool HaydnInterBlockEdges::canReplaySuccessorOccupancy() const {
  // Empty scheduled occupancy is AIE's empty successor
  // (AIEMachineScheduler.cpp:346-349): FirstBlockedCycle = 0, full latency.
  return successorOccupancyIsLive() && !SuccOccupancy.empty();
}

void HaydnInterBlockEdges::recomputePostDepthsFromEdges(
    const TargetSchedModel *TSM, const TargetInstrInfo *TII) {
  PostDepths.clear();
  PostRegionMaxDepth = 0;
  DepthsAreScheduled = false;
  clearSuccessorOccupancy();
  // AIE InterBlockScheduling.cpp:1069-1087 unscheduled fill: earliest-cycle
  // depth = max over post-boundary Preds of latency+getPostDepthOr(Pred,0);
  // no such pred => 0. Memoized so visit order is not a hidden topo walk
  // (ScheduleDAG.cpp ComputeDepth walks Preds; ComputeHeight walks Succs).
  std::function<int(const SUnit &)> DepthOf = [&](const SUnit &SU) -> int {
    const int Have = getPostDepth(SU);
    if (Have >= 0)
      return Have;
    PostDepths[SU.NodeNum] = 0;
    int Best = 0;
    for (const SDep &Dep : SU.Preds) {
      const SUnit *Src = Dep.getSUnit();
      if (!Src || Src->isBoundaryNode() || !isPostBoundaryNode(Src))
        continue;
      (void)DepthOf(*Src);
      Best = std::max(Best, (int)Dep.getLatency() + getPostDepthOr(Src, 0));
    }
    PostDepths[SU.NodeNum] = Best;
    return Best;
  };
  for (const SUnit &SU : SUnits) {
    if (!isPostBoundaryNode(&SU) || SU.isBoundaryNode())
      continue;
    int D = DepthOf(SU);
    PostRegionMaxDepth = std::max(PostRegionMaxDepth, D);
  }
}

const SUnit *HaydnInterBlockEdges::getPreBoundaryNode(MachineInstr *MI) const {
  const auto Found = PredMap.find(MI);
  if (Found == PredMap.end())
    return nullptr;
  return &SUnits.at(Found->second);
}

const SUnit *HaydnInterBlockEdges::getPostBoundaryNode(MachineInstr *MI) const {
  const auto Found = SuccMap.find(MI);
  if (Found == SuccMap.end())
    return nullptr;
  return &SUnits.at(Found->second);
}

int HaydnInterBlockEdges::getPostDepth(const SUnit &SU) const {
  const auto It = PostDepths.find(SU.NodeNum);
  return It != PostDepths.end() ? It->second : -1;
}

int HaydnInterBlockEdges::getPostDepthOr(const SUnit *SU, int Default) const {
  if (!SU)
    return Default;
  const int D = getPostDepth(*SU);
  return D < 0 ? Default : D;
}

void HaydnInterBlockEdges::recomputePostDepths() {
  // Skipped-region / unscheduled successor: same Pred-based fill as the
  // DDG constructor. Do not stamp generic SU.getDepth() (that is a whole-
  // graph value, not a post-boundary earliest cycle).
  recomputePostDepthsFromEdges(&getSchedModelRef(), nullptr);
}

SmallVector<const SDep *, 4>
HaydnInterBlockEdges::getCrossBoundaryEdges(const SUnit &SU) const {
  SmallVector<const SDep *, 4> Out;
  if (!isPreBoundaryNode(&SU))
    return Out;
  for (const SDep &Dep : SU.Succs) {
    const SUnit *Dst = Dep.getSUnit();
    if (Dst->isBoundaryNode() || isPostBoundaryNode(Dst))
      Out.push_back(&Dep);
  }
  return Out;
}

void HaydnInterBlockEdges::inheritRecordedPostDepths(
    const HaydnInterBlockEdges &S1) {
  // Occupancy first: S2 gather may already have DepthsAreScheduled from
  // BUNDLE-root seeding, which used to skip this function entirely and drop
  // the successor bundle view Bot replay needs.
  if (!OccupancyIsScheduled && S1.OccupancyIsScheduled) {
    SuccOccupancy.clear();
    OccupancyIsScheduled = true;
    OccupancyRecordingActive = false;
    OccupancyIsStale = S1.OccupancyIsStale;
    for (const auto &Cycle : S1.SuccOccupancy) {
      SuccOccupancy.emplace_back();
      for (MachineInstr *MI : Cycle) {
        if (!MI || MI->getParent() != Succ) {
          OccupancyIsStale = true;
          continue;
        }
        SuccOccupancy.back().push_back(MI);
      }
    }
  }
  // Copy S1 per-MI depths even if S2 already has DepthsAreScheduled from
  // BUNDLE-root seeding (that path does not publish per-MI PostDepths).
  if (!S1.DepthsAreScheduled)
    return;
  for (const SUnit &SU : SUnits) {
    if (!isPostBoundaryNode(&SU) || SU.isBoundaryNode())
      continue;
    MachineInstr *MI = SU.getInstr();
    if (!MI || MI->getParent() != Succ)
      continue;
    if (const SUnit *Old = S1.getPostBoundaryNode(MI)) {
      auto It = S1.PostDepths.find(Old->NodeNum);
      if (It == S1.PostDepths.end())
        continue;
      PostDepths[SU.NodeNum] = It->second;
      PostRegionMaxDepth = std::max(PostRegionMaxDepth, It->second);
      DepthsAreScheduled = true;
    }
  }
}
