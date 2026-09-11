//===-- HaydnFixupHwLoops.cpp - Post-stamp HWLoop layout closer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Library closer, not a pass. DEBUG_TYPE stays haydn-fixup-hwloops so
// existing debug-only FileChecks migrate last. Hexagon FixupHwLoops.cpp:75-81
// matches architectural LOOP then :136-148 converts or leaves by range;
// Haydn overlay is FitPatch Off1/Off2 plus EncodedBytes NOP pads on a
// committed root, after padInternalMBBAlignment (AIE padRegions:370-424).
// Consults LayoutSite canFitPatch/Dest/Dest2/Rank. Never CFG-demotes,
// peels SET from a mixed packet, or sinks the setup cycle (D1.112).
// AIE pads via scheduler NoOps (AIEMachineScheduler.cpp:843-859) and
// never reorders user code.
//
//===----------------------------------------------------------------------===//

#include "HaydnFixupHwLoops.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnHardwareLoops.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnLayoutSite.h"
#include "HaydnMachineAlignment.h"
#include "HaydnMachineFunctionInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <string>

using namespace llvm;

using haydn::hwloop::buildExactLate;
using haydn::hwloop::finalizeExactLateSingleton;
using haydn::hwloop::isLiveMBB;

#define DEBUG_TYPE "haydn-fixup-hwloops"

namespace {

static constexpr unsigned InterveningCycles = haydn::hwloop::InterveningCycles;
static constexpr int64_t MaxOff1Bytes = haydn::hwloop::MaxStartOffsetBytes;
static constexpr int64_t MaxOff2Bytes = haydn::hwloop::MaxEndOffsetBytes;
static constexpr int64_t MaxOff1BytesSafe =
    haydn::hwloop::MaxStartOffsetBytesSafe;

static MachineBasicBlock::iterator nextBundleBoundary(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  assert(MBB && "MI must be inserted");
  MachineBasicBlock::instr_iterator II = std::next(MI.getIterator());
  while (II != MBB->instr_end() && II->isBundledWithPred())
    ++II;
  if (II == MBB->instr_end())
    return MBB->end();
  return MachineBasicBlock::iterator(II);
}

class RetainedHwLoopCloser {
public:
  bool run(MachineFunction &MF, const HaydnInstrInfo &TII);

private:
  unsigned countFollowingBundles(MachineInstr &SetMI,
                                 const HaydnInstrInfo &TII) const;
  int64_t estimateMBBDistance(const MachineFunction &MF,
                              const MachineBasicBlock *FromMBB,
                              MachineBasicBlock::const_iterator FromIt,
                              const MachineBasicBlock *ToMBB,
                              const HaydnInstrInfo &TII) const;
  bool computeOffsets(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                      int64_t &StartOff, int64_t &EndOff,
                      MachineBasicBlock *&StartMBB,
                      MachineBasicBlock *&EndMBB) const;
  bool measureEncodedOffsets(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                             MachineBasicBlock *StartMBB,
                             MachineBasicBlock *EndMBB, int64_t &StartOff,
                             int64_t &EndOff) const;
  bool fixupOne(MachineInstr &SetMI, const HaydnInstrInfo &TII,
                haydn::LayoutSiteTable &Table);
  void collectRetainedSetups(const haydn::LayoutSiteTable &Table,
                             SmallVectorImpl<MachineInstr *> &Sets) const;
  bool setupWindowContains(const MachineInstr &Outer,
                           const MachineInstr &Inner,
                           const HaydnInstrInfo &TII) const;
  void sortInnermostFirst(SmallVectorImpl<MachineInstr *> &Sets,
                          const HaydnInstrInfo &TII) const;
  bool revalidateRetainedSetups(MachineFunction &MF,
                                const HaydnInstrInfo &TII);
};

unsigned RetainedHwLoopCloser::countFollowingBundles(
    MachineInstr &SetMI, const HaydnInstrInfo &TII) const {
  unsigned Bundles = 0;
  MachineBasicBlock *MBB = SetMI.getParent();
  for (MachineBasicBlock::iterator I = nextBundleBoundary(SetMI),
                                   E = MBB->end();
       I != E; ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef() ||
        I->isKill())
      continue;
    unsigned Bytes = TII.getInstSizeInBytes(*I);
    if (Bytes == 0)
      continue;
    Bundles += haydn::bundle::ceilProductParcels(Bytes);
    if (I->isTerminator())
      break;
  }
  return Bundles;
}

int64_t RetainedHwLoopCloser::estimateMBBDistance(
    const MachineFunction &MF, const MachineBasicBlock *FromMBB,
    MachineBasicBlock::const_iterator FromIt, const MachineBasicBlock *ToMBB,
    const HaydnInstrInfo &TII) const {
  return haydn::hwloop::estimateLayoutMBBDistance(MF, FromMBB, FromIt, ToMBB,
                                                  TII);
}

bool RetainedHwLoopCloser::computeOffsets(MachineInstr &SetMI,
                                          const HaydnInstrInfo &TII,
                                          int64_t &StartOff, int64_t &EndOff,
                                          MachineBasicBlock *&StartMBB,
                                          MachineBasicBlock *&EndMBB) const {
  StartOff = EndOff = -1;
  StartMBB = EndMBB = nullptr;

  const MachineFunction *MF =
      SetMI.getParent() ? SetMI.getParent()->getParent() : nullptr;
  if (!MF)
    return false;

  if (SetMI.getOpcode() == Haydn::LoopStart) {
    StartMBB = haydn::hwloop::resolveBodyMBBFixup(SetMI);
    if (!StartMBB)
      return false;
    EndMBB = haydn::hwloop::resolveLoopStartLatch(StartMBB, SetMI.getParent());
    if (!EndMBB)
      EndMBB = StartMBB;
  } else if (SetMI.getNumOperands() >= 3 && SetMI.getOperand(1).isMBB() &&
             SetMI.getOperand(2).isMBB()) {
    StartMBB = SetMI.getOperand(1).getMBB();
    EndMBB = SetMI.getOperand(2).getMBB();
    if (!isLiveMBB(*MF, StartMBB) || !isLiveMBB(*MF, EndMBB)) {
      StartMBB = EndMBB = nullptr;
      return false;
    }
  } else {
    return false;
  }

  return measureEncodedOffsets(SetMI, TII, StartMBB, EndMBB, StartOff, EndOff);
}

bool RetainedHwLoopCloser::measureEncodedOffsets(
    MachineInstr &SetMI, const HaydnInstrInfo &TII,
    MachineBasicBlock *StartMBB, MachineBasicBlock *EndMBB, int64_t &StartOff,
    int64_t &EndOff) const {
  StartOff = EndOff = -1;
  const MachineFunction *MF =
      SetMI.getParent() ? SetMI.getParent()->getParent() : nullptr;
  if (!MF || !StartMBB || !EndMBB)
    return false;
  if (!isLiveMBB(*MF, StartMBB) || !isLiveMBB(*MF, EndMBB))
    return false;

  MachineBasicBlock *Pre = SetMI.getParent();
  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  StartOff = estimateMBBDistance(*MF, Pre, AfterSet, StartMBB, TII);
  MachineInstr &SetCycle = haydn::hwloop::topLevelForLayout(SetMI);
  const int64_t SetParcelBytes = static_cast<int64_t>(
      haydn::bundle::committedEncodedBytes(SetCycle).Value);
  StartOff = haydn::hwloop::anchoredFromAfterSet(StartOff, SetParcelBytes);
  EndOff = haydn::hwloop::estimateLastBodyCycleOffset(
      *MF, StartMBB, EndMBB, Pre, StartOff, TII);
  if (EndOff < 0)
    return false;
  return true;
}

bool RetainedHwLoopCloser::fixupOne(MachineInstr &SetMI,
                                    const HaydnInstrInfo &TII,
                                    haydn::LayoutSiteTable &Table) {
  bool Changed = false;
  DebugLoc DL = SetMI.getDebugLoc();
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  haydn::LayoutSite *Site = Table.requireMember(&SetMI, "HaydnFixupHwLoops");
  const MachineFunction &MF = *Pre->getParent();

  // Rank consult: already-neutralized sites are not re-patched.
  if (Site->Rank == haydn::LayoutSiteRank::NeutralizedNop)
    return false;

  unsigned Opc = SetMI.getOpcode();
  if (Opc == Haydn::LoopStart) {
    report_fatal_error(
        "HaydnFixupHwLoops: residual LoopStart after stamp; Role A must "
        "expand or demote before PostMachineScheduler",
        /*gen_crash_diag=*/false);
  }
  if (TII.isHardwareLoopSetupOpcode(Opc)) {
    if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm()) {
      report_fatal_error(
          "HaydnFixupHwLoops: malformed SET_HWLOOP; refusing erase-only "
          "once-through",
          /*gen_crash_diag=*/false);
    }
    const int64_t Sel = SetMI.getOperand(0).getImm();
    if (!haydn::hwloop::isProductSelector(Sel))
      report_fatal_error(
          "HaydnFixupHwLoops: unsupported SET_HWLOOP selector cannot "
          "demote to software loop; refusing erase-only once-through",
          /*gen_crash_diag=*/false);
    // Dest/Dest2 consult — do not rediscover Header/Latch from operands.
    MachineBasicBlock *H = Site->Dest;
    MachineBasicBlock *L = Site->Dest2;
    if (!H || !L) {
      report_fatal_error(
          "HaydnFixupHwLoops: malformed SET_HWLOOP; refusing erase-only "
          "once-through",
          /*gen_crash_diag=*/false);
    }
    if (!isLiveMBB(MF, H) || !isLiveMBB(MF, L)) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stale Header/Latch "
                           "(%bb.-1 or foreign) — erase SET only\n"
                        << "HaydnFixupHwLoops: site rank NeutralizedNop\n");
      Site->raiseRank(haydn::LayoutSiteRank::NeutralizedNop);
      Table.dropMember(&SetMI);
      return eraseHardwareLoopSetup(SetMI, "HaydnFixupHwLoops",
                                    haydn::hwloop::resolveBodyMBBFixup);
    }
    haydn::hwloop::LoopBlockSet Blocks;
    haydn::hwloop::collectLoopBlocks(H, L, Pre, Blocks);
    if (haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(Blocks))
      report_fatal_error(
          "HaydnFixupHwLoops: unpublished HWLR CSR write in "
          "hardware-loop body; product programs HWLR only through "
          "SET_HWLOOP",
          /*gen_crash_diag=*/false);
  }

  unsigned Following = countFollowingBundles(SetMI, TII);
  if (Following < InterveningCycles) {
    unsigned Deficit = InterveningCycles - Following;
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: setup gap Following=" << Following
                      << " < InterveningCycles=" << InterveningCycles
                      << " (SetupIssueDistance="
                      << haydn::hwloop::SetupIssueDistance
                      << ") — insert deficit " << Deficit
                      << " exact-commit NOP bundle(s) after " << SetMI);
    MachineBasicBlock *MBB = SetMI.getParent();
    MachineBasicBlock::iterator InsertPt = nextBundleBoundary(SetMI);
    for (unsigned I = 0; I < Deficit; ++I) {
      MachineInstr *Pad = buildExactLate(*MBB, InsertPt, DL, TII, Haydn::NOP);
      finalizeExactLateSingleton(*Pad);
    }
    Changed = true;
  }
#ifndef NDEBUG
  assert(countFollowingBundles(SetMI, TII) >= InterveningCycles &&
         "Following >= InterveningCycles after deficit-only pad");
#endif

  auto fatalUnencodable = [&](const char *Why) {
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                      << " — hard-unencodable after stamp\n");
    report_fatal_error(
        "HaydnFixupHwLoops: out-of-range/invalid SET_HWLOOP cannot demote to "
        "software loop (no free counter GPR or usable exit); refusing "
        "erase-only once-through",
        /*gen_crash_diag=*/false);
    return false;
  };

  int64_t StartOff = -1, EndOff = -1;
  MachineBasicBlock *StartMBB = Site->Dest;
  MachineBasicBlock *EndMBB = Site->Dest2;
  if (!measureEncodedOffsets(SetMI, TII, StartMBB, EndMBB, StartOff, EndOff))
    return fatalUnencodable("Dest/Dest2 EncodedBytes measure failed");

  auto padBodyToMinLaw = [&]() -> bool {
    if (!EndMBB || StartOff < 0 || EndOff < 0)
      return false;
    unsigned Parcels = 0;
    if (EndOff > StartOff)
      Parcels = haydn::hwloop::bodyParcelsFromOffsets(StartOff, EndOff);
    else if (EndOff == StartOff)
      Parcels = 1;
    else
      return false;
    if (Parcels >= haydn::hwloop::MinBodyBundles)
      return false;
    unsigned Deficit = haydn::hwloop::MinBodyBundles - Parcels;
    MachineBasicBlock::iterator InsertPt = EndMBB->getFirstTerminator();
    if (InsertPt == EndMBB->end()) {
      for (MachineInstr &MI : *EndMBB) {
        if (MI.isBundledWithPred())
          continue;
        unsigned BodyOpc = MI.getOpcode();
        if (BodyOpc == Haydn::PseudoLoopEnd || BodyOpc == Haydn::LoopJNZ ||
            BodyOpc == Haydn::LoopDec) {
          InsertPt = MI.getIterator();
          break;
        }
      }
    }
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: body parcels=" << Parcels
                      << " < MinBodyBundles=" << haydn::hwloop::MinBodyBundles
                      << " — insert deficit " << Deficit
                      << " exact-commit NOP bundle(s) in body\n");
    for (unsigned I = 0; I < Deficit; ++I) {
      MachineInstr *Pad =
          buildExactLate(*EndMBB, InsertPt, DL, TII, Haydn::NOP);
      finalizeExactLateSingleton(*Pad);
    }
    return true;
  };

  if (padBodyToMinLaw()) {
    Changed = true;
    if (!measureEncodedOffsets(SetMI, TII, StartMBB, EndMBB, StartOff, EndOff))
      return fatalUnencodable("Dest/Dest2 measure failed after body pad");
  }

  auto rangeBad = [&](bool HardOff1Only) {
    if (StartOff < 0 || EndOff < 0)
      return true;
    int64_t Off1Lim = HardOff1Only ? MaxOff1Bytes : MaxOff1BytesSafe;
    if (StartOff > Off1Lim)
      return true;
    if (EndOff > MaxOff2Bytes)
      return true;
    if (!haydn::hwloop::offsetsMeetImmRelocLaw(StartOff, EndOff))
      return true;
    if (!haydn::hwloop::bodyMeetsMinLaw(StartOff, EndOff))
      return true;
    if (StartOff < haydn::hwloop::MinSetupIssueBytes)
      return true;
    if (TII.isHardwareLoopImmTripOpcode(SetMI.getOpcode()) &&
        SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isImm() &&
        !haydn::hwloop::countMeetsFieldLaw(SetMI.getOperand(3).getImm()))
      return true;
    return false;
  };

  if (rangeBad(/*HardOff1Only=*/false)) {
    if (padBodyToMinLaw()) {
      Changed = true;
      if (!measureEncodedOffsets(SetMI, TII, StartMBB, EndMBB, StartOff,
                                 EndOff))
        return fatalUnencodable("Dest/Dest2 measure failed after body pad");
    }
    if (rangeBad(/*HardOff1Only=*/true)) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: fatal startOff=" << StartOff
                        << " endOff=" << EndOff
                        << " hardLim=" << MaxOff1Bytes
                        << " safeLim=" << MaxOff1BytesSafe << "\n");
      return fatalUnencodable("hard Off1/Off2 still unencodable after NOP pad");
    }
  }

  {
    using haydn::hwloop::MaxSingleBranchGrowthBytes;
    using haydn::hwloop::isStillRelaxableShortBranch;

    auto countBranchGrowthIn = [&](const MachineInstr &Probe) -> int64_t {
      int64_t G = 0;
      if (Probe.isBundle()) {
        for (const MachineInstr *C : haydn::bundle::members(Probe)) {
          if (isStillRelaxableShortBranch(*C))
            G += MaxSingleBranchGrowthBytes;
        }
      } else if (isStillRelaxableShortBranch(Probe)) {
        G += MaxSingleBranchGrowthBytes;
      }
      return G;
    };
    auto sumStillRelaxableGrowth =
        [&](MachineBasicBlock::iterator FromIt, const MachineBasicBlock *ToMBB,
            bool InclusiveTo) -> int64_t {
      if (!ToMBB || !isLiveMBB(MF, ToMBB))
        return 0;
      int64_t Growth = 0;
      bool Started = false;
      for (const MachineBasicBlock &MBB : MF) {
        if (&MBB == Pre)
          Started = true;
        if (!Started)
          continue;
        auto Begin = (&MBB == Pre) ? FromIt : MBB.begin();
        for (auto I = Begin, E = MBB.end(); I != E; ++I) {
          if (&MBB == ToMBB && !InclusiveTo && I == ToMBB->begin())
            return Growth;
          if (&MBB == ToMBB && InclusiveTo &&
              I->getOpcode() == Haydn::PseudoLoopEnd)
            return Growth;
          Growth += countBranchGrowthIn(*I);
        }
        if (&MBB == ToMBB)
          return Growth;
      }
      return Growth;
    };

    MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
    int64_t BeginGrowth =
        sumStillRelaxableGrowth(AfterSet, StartMBB, /*InclusiveTo=*/false);
    int64_t EndGrowth =
        sumStillRelaxableGrowth(AfterSet, EndMBB, /*InclusiveTo=*/true);
    int64_t BeginMargin = MaxOff1Bytes - StartOff;
    int64_t EndMargin = MaxOff2Bytes - EndOff;
    if (BeginGrowth > BeginMargin || EndGrowth > EndMargin) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: post-Fixup BR growth budget "
                           "exceeds Off margin (beginGrowth="
                        << BeginGrowth << " beginMargin=" << BeginMargin
                        << " endGrowth=" << EndGrowth
                        << " endMargin=" << EndMargin << ")\n");
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stamped — keep despite BR "
                           "margin (cannot CFG-demote)\n");
    }
  }

  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: startOff=" << StartOff
                    << " endOff=" << EndOff
                    << " sameMBB=" << (StartMBB == EndMBB)
                    << " followingBundles="
                    << countFollowingBundles(SetMI, TII) << "\n");
  if (!Site->canFitPatch())
    return fatalUnencodable("Rank/Template cannot FitPatch Off1/Off2");
  Site->raiseRank(haydn::LayoutSiteRank::FitPatch);
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: canFitPatch Dest="
                    << (StartMBB ? StartMBB->getNumber() : -1)
                    << " Dest2=" << (EndMBB ? EndMBB->getNumber() : -1)
                    << "\n");
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: range-fit HWLOOP kept\n");
  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: site rank FitPatch Off1/Off2\n");
  return Changed;
}

void RetainedHwLoopCloser::collectRetainedSetups(
    const haydn::LayoutSiteTable &Table,
    SmallVectorImpl<MachineInstr *> &Sets) const {
  Sets.clear();
  for (const haydn::LayoutSite &S : Table.sites())
    if (S.Kind == haydn::LayoutSiteKind::HWLoop && S.Member &&
        S.Member->getParent() && S.canFitPatch())
      Sets.push_back(S.Member);
}

bool RetainedHwLoopCloser::setupWindowContains(
    const MachineInstr &Outer, const MachineInstr &Inner,
    const HaydnInstrInfo &TII) const {
  if (&Outer == &Inner)
    return false;
  const MachineBasicBlock *OuterMBB = Outer.getParent();
  const MachineBasicBlock *InnerMBB = Inner.getParent();
  if (!OuterMBB || !InnerMBB)
    return false;
  const MachineFunction *MF = OuterMBB->getParent();
  if (!MF || InnerMBB->getParent() != MF)
    return false;

  MachineInstr &OuterMut = const_cast<MachineInstr &>(Outer);
  MachineBasicBlock *StartMBB = nullptr;
  MachineBasicBlock *EndMBB = nullptr;
  int64_t StartOff = -1, EndOff = -1;
  (void)computeOffsets(OuterMut, TII, StartOff, EndOff, StartMBB, EndMBB);

  if (InnerMBB == OuterMBB) {
    bool SeenOuter = false;
    for (const MachineInstr &I : OuterMBB->instrs()) {
      if (&I == &Outer) {
        SeenOuter = true;
        continue;
      }
      if (&I == &Inner)
        return SeenOuter;
    }
    return false;
  }

  MachineBasicBlock::iterator AfterOuter = nextBundleBoundary(OuterMut);
  const int64_t ToInner =
      estimateMBBDistance(*MF, OuterMBB, AfterOuter, InnerMBB, TII);
  if (ToInner < 0)
    return false;
  if (!EndMBB)
    return true;
  MachineInstr &InnerMut = const_cast<MachineInstr &>(Inner);
  MachineBasicBlock::iterator AfterInner = nextBundleBoundary(InnerMut);
  const int64_t ToEnd =
      estimateMBBDistance(*MF, InnerMBB, AfterInner, EndMBB, TII);
  return ToEnd >= 0 || InnerMBB == EndMBB;
}

void RetainedHwLoopCloser::sortInnermostFirst(
    SmallVectorImpl<MachineInstr *> &Sets, const HaydnInstrInfo &TII) const {
  // AIEBaseHardwareLoops.cpp:304-306 processLoop: inner loops first.
  std::stable_sort(Sets.begin(), Sets.end(),
                   [&](const MachineInstr *A, const MachineInstr *B) {
                     if (!A || !B || A == B)
                       return false;
                     const bool AInB = setupWindowContains(*B, *A, TII);
                     const bool BInA = setupWindowContains(*A, *B, TII);
                     if (AInB != BInA)
                       return AInB;
                     return false;
                   });
}

bool RetainedHwLoopCloser::revalidateRetainedSetups(
    MachineFunction &MF, const HaydnInstrInfo &TII) {
  haydn::LayoutSiteTable Table;
  SmallVector<MachineInstr *, 8> Sets;
  auto rebuild = [&]() {
    std::string Err;
    if (!Table.collect(MF, TII, Err))
      report_fatal_error("HaydnFixupHwLoops: " + Twine(Err),
                         /*gen_crash_diag=*/false);
    collectRetainedSetups(Table, Sets);
  };

  rebuild();
  const unsigned Initial = Sets.size();
  const unsigned Bound = haydn::hwloop::nestedCascadeBound(Initial);
  bool Changed = false;

  for (unsigned Wave = 0; Wave < Bound; ++Wave) {
    // Alignment then inner-first HWLoop (branches/calls last in LBN).
    // ClearMetadata=false: seated MachineAlignment is the final clear
    // (GR2.10). AIEMachineAlignment.cpp:370-424 padRegions overlay.
    Changed |=
        haydn::padInternalMBBAlignment(MF, TII, /*ClearMetadata=*/false);
    rebuild();
    if (!haydn::hwloop::setupsMonotone(Initial, Sets.size()))
      report_fatal_error(
          "HaydnFixupHwLoops: hardware-loop setups increased; demotion is "
          "monotone",
          /*gen_crash_diag=*/false);
    if (Sets.empty())
      break;
    sortInnermostFirst(Sets, TII);
    LLVM_DEBUG({
      dbgs() << "HaydnFixupHwLoops: revalidate wave " << Wave << " of " << Bound
             << " (" << Sets.size() << " retained setup(s), inner-first)\n";
    });

    bool WaveChanged = false;
    for (MachineInstr *MI : Sets) {
      if (!MI || !MI->getParent())
        continue;
      WaveChanged |= fixupOne(*MI, TII, Table);
    }
    Changed |= WaveChanged;
    if (!WaveChanged)
      break;
    if (Wave + 1 == Bound) {
      rebuild();
      if (!Sets.empty() && WaveChanged)
        report_fatal_error(
            "HaydnFixupHwLoops: nested cascade exhausted without a "
            "no-mutation wave",
            /*gen_crash_diag=*/false);
    }
  }
  return Changed;
}

bool RetainedHwLoopCloser::run(MachineFunction &MF,
                               const HaydnInstrInfo &TII) {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB.instrs()) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::SET_HWLOOP || Opc == Haydn::SET_HWLOOP_REG)
        report_fatal_error(
            "HaydnFixupHwLoops: residual SET_HWLOOP{,_REG} — ExpandPseudos "
            "must rewrite to SET_HWLOOP_{W,F2_W} before Fixup",
            /*gen_crash_diag=*/false);
    }
  }
  return revalidateRetainedSetups(MF, TII);
}

} // namespace

bool haydn::hwloop::closeRetainedHwLoops(MachineFunction &MF,
                                         const HaydnInstrInfo &TII) {
  const HaydnMachineFunctionInfo *Info =
      MF.getInfo<HaydnMachineFunctionInfo>();
  if (!Info || !Info->hasPostCommitBlockBudget())
    return false;
  RetainedHwLoopCloser Closer;
  return Closer.run(MF, TII);
}
