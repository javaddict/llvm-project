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
#include "HaydnResourceRestrictionClasses.h"
#include "llvm/ADT/DenseMap.h"
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
  // of its full memory walk would drift. This builder only OVER-APPROXIMATES
  // the cross-boundary dependences the effective-latency cut consumes —
  // extra edges only suppress cuts, never invent them, so conservatism is
  // the safety property (AIE peer semantics: AIEMaxLatencyFinder
  // computeEffectiveLatency, Remaining = EdgeLat - Depth(Succ)).
  auto MIFor = [&](const SUnit &SU) -> MachineInstr & { return *SU.getInstr(); };

  // Published Data_Latency for a cross-boundary producer. getInstrLatency
  // answers the STAGE latency (every product InstrStage is single-cycle);
  // the memory Data_Latency=2 scaffold lives in OperandCycles (the same
  // source MaxLatencyFinder::maxOperandCycles reads). Guard on the
  // itinerary itself, NOT hasInstrSchedModel: Haydn publishes
  // ProcessorItineraries with CompleteModel=0, so the MachineScheduler
  // level model is absent and the hasInstrSchedModel gate is always false
  // here. Clamped so the conservative SIN_COS/ARCTAN OperandCycles-17
  // scaffold cannot size the edge either (clampPublishedDataLatency law;
  // those windows are HR-reserved instead).
  auto EdgeDataLatency = [&](const MachineInstr &MI) -> unsigned {
    const InstrItineraryData *Itin =
        TSM ? TSM->getInstrItineraries() : nullptr;
    if (!Itin || Itin->isEmpty())
      return MI.mayLoad() || MI.mayStore() ? 2 : 1;
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

  // Register dependences, pre -> post: for each post node's operands,
  // connect against every pre node touching the same reg units.
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
          break;
        }
      }
    }
  }

  // Memory dependences, pre -> post: any pre memory writer to any post
  // memory reader/writer (store->load, store->store; load->store) unless AA
  // proves no-alias. Loads before the boundary reading after a post store is
  // impossible in this direction (post runs later) — only pre->post edges.
  SmallVector<std::pair<const SUnit *, bool>> PreMem; // (SU, isWrite)
  for (const SUnit &SU : SUnits)
    if (isPreBoundaryNode(&SU) && !SU.isBoundaryNode()) {
      const MachineInstr &MI = MIFor(SU);
      if (MI.mayStore())
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
    if (!PostWrites && !PostReads)
      continue;
    for (const auto &[PreSU, PreWrites] : PreMem) {
      // load -> load is not a dependence.
      if (!PreWrites && !PostWrites)
        continue;
      // MachineInstr::mayAlias handles calls, volatile, and PSV-vs-value
      // classes (MachineInstr.cpp:1531-1577); null AA ⇒ conservative true.
      if (AA && !MIFor(*PreSU).mayAlias(AA, Post, /*UseTBAA=*/false))
        continue;
      // Memory edges carry the writer's published Data_Latency (memory
      // scaffold OperandCycles=2). The 3-arg SDep constructor binds a
      // REGISTER, not a latency (Data hardcodes Latency=1); stamp it.
      SDep MemEdge(const_cast<SUnit *>(PreSU), SDep::Data, /*Reg=*/0);
      MemEdge.setLatency(EdgeDataLatency(MIFor(*PreSU)));
      const_cast<SUnit &>(PostSU).addPred(MemEdge);
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
  // Longest-path depth from each post-boundary node downward through its
  // post-boundary successors (edges into pre-boundary nodes do not extend
  // the in-block path).
  SmallDenseMap<unsigned, int, 16> Depth;
  std::function<int(const SUnit &)> DepthOf = [&](const SUnit &SU) -> int {
    auto It = Depth.find(SU.NodeNum);
    if (It != Depth.end())
      return It->second;
    int Best = 0;
    for (const SDep &Dep : SU.Succs) {
      const SUnit *Dst = Dep.getSUnit();
      if (Dst->isBoundaryNode() || !isPostBoundaryNode(Dst))
        continue;
      Best = std::max(Best, DepthOf(*Dst) + (int)Dep.getLatency());
    }
    Depth[SU.NodeNum] = Best;
    return Best;
  };
  for (const SUnit &SU : SUnits) {
    if (!isPostBoundaryNode(&SU) || SU.isBoundaryNode())
      continue;
    int D = DepthOf(SU);
    PostDepths[SU.NodeNum] = D;
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
  PostDepths.clear();
  PostRegionMaxDepth = 0;
  clearSuccessorOccupancy();
  // Top-down earliest-cycle over the post-boundary sub-DAG (AIE depth
  // convention: SU.getDepth() as maintained by ComputeDepth for the whole
  // graph; post-boundary depths read straight off it).
  for (const SUnit &SU : SUnits) {
    if (!isPostBoundaryNode(&SU))
      continue;
    PostDepths[SU.NodeNum] = static_cast<int>(SU.getDepth());
    PostRegionMaxDepth =
        std::max(PostRegionMaxDepth, static_cast<int>(SU.getDepth()));
  }
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
  if (!S1.DepthsAreScheduled || DepthsAreScheduled)
    return;
  for (const SUnit &SU : SUnits) {
    if (!isPostBoundaryNode(&SU) || SU.isBoundaryNode())
      continue;
    MachineInstr *MI = SU.getInstr();
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
