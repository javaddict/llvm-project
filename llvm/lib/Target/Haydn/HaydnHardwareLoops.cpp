//===-- HaydnHardwareLoops.cpp - Role-A Hardware Loop Expansion ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Product path is SCEV-proven retained-state expand only. Product default
// -haydn-enable-hwloops is ON (qualified 2026-08-22).
//
// Sole product entry when the flag is on:
//   1. Generic IR HardwareLoops + Haydn TTI prove trip/CFG (innermost
//      single-latch/single-exit, including a measured multi-BB CFG
//      extension; never post-RA physical rediscovery).
//   2. GlobalISel lowers to LoopStart / PseudoLoopEnd logical pseudos.
//   3. This pass (post-RA, before physical scheduling):
//        stripEmptyZeroOverheadLoops — remove empty retained ZOLs only
//        splitLoopEndJump — AIE dedicated fallthrough exit (Wave 4)
//        expandRoleALoopStarts — preflight retained metadata/CFG/body, then
//          LoopStart → SET_HWLOOP_F2_W (product sel domain {0,1}) with
//          intervening setup pads, or reject incomplete seats fail-closed
//          before any mutation (fatal).
//   4. This pass owns software-loop demotion: if the retained body already
//      cannot encode Off1/Off2 (SET-anchored walk + last body cycle +
//      still-relaxable MaxSingleBranchGrowthBytes vs residual Off margins),
//      the soft edge is installed here (before PostMachineScheduler).
//   5. Post-stamp closeRetainedHwLoops (library, not a pass) FitPatches
//      Off1/Off2 and inserts EncodedBytes NOP pads. Isolated unstamped
//      Fixup demote is deleted. Product CFG demote is this pass only.
//
// Post-RA soft-branch convert and its reconstruction helpers are permanently
// deleted from this TU.
//
//===----------------------------------------------------------------------===//

#include "HaydnHardwareLoops.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnFrameLowering.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPortModel.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <string>

#define DEBUG_TYPE "haydn-hwloops"

STATISTIC(NumEmptyZOLStripped,
          "Number of empty IR-form ZOLs stripped (peeled body)");
STATISTIC(NumRoleAExpanded,
          "Number of Role A LoopStart expanded to SET_HWLOOP_F2_W pre-sched");
STATISTIC(NumRoleARejected,
          "Number of incomplete Role A LoopStart rejected before mutation");

using llvm::haydn::hwloop::InnermostProductSelector;
using llvm::haydn::hwloop::InterveningCycles;
using llvm::haydn::hwloop::isProductSelector;
using llvm::haydn::hwloop::isUnpublishedHwlrCsrAddress;
using llvm::haydn::hwloop::MaxEndOffsetBytes;
using llvm::haydn::hwloop::MaxStartOffsetBytes;
using llvm::haydn::hwloop::MaxStartOffsetBytesSafe;
using llvm::haydn::hwloop::MinBodyBundles;
using llvm::haydn::hwloop::MaxSingleBranchGrowthBytes;
using llvm::haydn::hwloop::MinSetupBytes;
using llvm::haydn::hwloop::MinSetupBundles;
using llvm::haydn::hwloop::MinSetupIssueBytes;
using llvm::haydn::hwloop::SetupIssueDistance;
using llvm::haydn::bundle::ceilProductParcels;
using llvm::haydn::bundle::productBundlesToBytes;
using llvm::haydn::bundle::productParcelBytes;

static constexpr int64_t MaxHWLoopStartOffsetBytes = MaxStartOffsetBytes;
static constexpr int64_t MaxHWLoopEndOffsetBytes = MaxEndOffsetBytes;
static constexpr unsigned HWLoopSetupPadBundles = InterveningCycles;
static_assert(HWLoopSetupPadBundles == MinSetupBundles,
              "formation pad floor must equal MinSetupBundles alias");
static_assert(HWLoopSetupPadBundles + 1 == SetupIssueDistance,
              "formation pad + 1 == SetupIssueDistance");
static_assert(MinBodyBundles >= 1, "body floor must be positive");
static_assert(MaxHWLoopStartOffsetBytes > 0 && MaxHWLoopEndOffsetBytes > 0,
              "hwloop offset ceilings must stay positive");
static_assert(productParcelBytes().Value > 0,
              "product parcel EncodedBytes must be positive");
static_assert(llvm::HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(),
              "hardware-loop product default is ON (2026-08-22 "
              "qualification); re-parking requires new failing evidence");
// Inserted only when EnableHaydnHardwareLoops (product default ON).
// AIE inserts HardwareLoops unconditionally at O1+
// (AIE2TargetMachine.cpp:81-82, :234-235). FeatureHWLoop is ISA only.

using namespace llvm;

char HaydnHardwareLoops::ID = 0;

INITIALIZE_PASS(HaydnHardwareLoops, DEBUG_TYPE,
                "Haydn Hardware Loop Expansion", false, false)

HaydnHardwareLoops::HaydnHardwareLoops() : MachineFunctionPass(ID) {
  initializeHaydnHardwareLoopsPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnHardwareLoopsPass() {
  return new HaydnHardwareLoops();
}

enum class ZOLBodyKind {
  NotZOL,
  Empty,
  Real,
};

/// Occupancy-only empty vs real (AIE `isEmptyZeroOverheadLoop`). A store,
/// copy, or compute is a real op; FmtLS stores have `mayLoad=0` so they are
/// not classified via load flags.
static ZOLBodyKind classifyZOLBody(const MachineBasicBlock &Body) {
  bool SawPseudoLoopEnd = false;
  unsigned RealOps = 0;
  for (const MachineInstr &MI : Body) {
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::PseudoLoopEnd) {
      SawPseudoLoopEnd = true;
      continue;
    }
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isPosition())
      continue;
    if (Opc == Haydn::B || Opc == Haydn::NOP)
      continue;
    ++RealOps;
  }
  if (!SawPseudoLoopEnd)
    return ZOLBodyKind::NotZOL;
  if (RealOps == 0)
    return ZOLBodyKind::Empty;
  return ZOLBodyKind::Real;
}

static bool stripEmptyZeroOverheadLoops(MachineFunction &MF) {
  const auto *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  SmallVector<MachineInstr *, 4> LoopStarts;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == Haydn::LoopStart)
        LoopStarts.push_back(&MI);

  bool Changed = false;
  for (MachineInstr *LS : LoopStarts) {
    MachineBasicBlock *Preheader = LS->getParent();
    if (!Preheader)
      continue;

    auto findPLE = [](MachineBasicBlock *BB) -> MachineInstr * {
      if (!BB)
        return nullptr;
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Haydn::PseudoLoopEnd)
          return &MI;
      return nullptr;
    };

    // AIE AIEBaseHardwareLoops.cpp:161-176 / :454-462 strips empty ZOL by
    // MLI LoopEnd-parent occupancy. Haydn overlay is CFG successor/pred
    // only (resolveBodyMBBCore + resolveLoopStartLatch) — same Header/Latch
    // seats as expand, including Header != Latch.
    MachineBasicBlock *Header = haydn::hwloop::resolveBodyMBBCore(*LS);
    MachineBasicBlock *Latch =
        haydn::hwloop::resolveLoopStartLatch(Header, Preheader);
    if (!Latch)
      Latch = Header;
    MachineInstr *PLE = findPLE(Latch);
    if (!PLE)
      PLE = findPLE(Header);
    if (!Header || !PLE)
      continue;

    haydn::hwloop::LoopBlockSet Blocks;
    haydn::hwloop::collectLoopBlocks(Header, Latch, Preheader, Blocks);
    unsigned RealOps = 0;
    for (const MachineBasicBlock *BB : Blocks) {
      if (!BB)
        continue;
      for (const MachineInstr &MI : *BB) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::PseudoLoopEnd)
          continue;
        if (MI.isMetaInstruction() || MI.isDebugInstr() ||
            MI.isImplicitDef() || MI.isKill() || MI.isPosition())
          continue;
        if (Opc == Haydn::B || Opc == Haydn::NOP)
          continue;
        ++RealOps;
      }
    }
    if (RealOps != 0)
      continue;

    MachineBasicBlock *ExitBB = nullptr;
    for (MachineBasicBlock *Succ : Latch->successors()) {
      if (Succ != Header && Succ != Latch) {
        ExitBB = Succ;
        break;
      }
    }
    if (!ExitBB) {
      MachineFunction::iterator NextIt = std::next(Latch->getIterator());
      if (NextIt != MF.end() && &*NextIt != Header)
        ExitBB = &*NextIt;
    }
    if (!ExitBB)
      continue;

    LLVM_DEBUG(dbgs() << "HaydnHWLoops: stripping empty IR ZOL "
                      << printMBBReference(*Header) << " (LoopStart in "
                      << printMBBReference(*Preheader) << ")\n");

    DebugLoc DL = LS->getDebugLoc();
    LS->eraseFromParent();
    PLE->eraseFromParent();

    if (Latch->isSuccessor(Header))
      Latch->removeSuccessor(Header);
    if (Latch->isSuccessor(Latch))
      Latch->removeSuccessor(Latch);
    if (!Latch->isSuccessor(ExitBB))
      Latch->addSuccessor(ExitBB);

    bool HasExitBranch = false;
    for (const MachineInstr &MI : Latch->terminators()) {
      if (MI.getOpcode() == Haydn::B) {
        HasExitBranch = true;
        break;
      }
    }
    if (!HasExitBranch) {
      MachineFunction::iterator NextIt = std::next(Latch->getIterator());
      bool ExitIsFallthrough =
          (NextIt != MF.end()) && (&*NextIt == ExitBB);
      if (!ExitIsFallthrough)
        TII->insertBranch(*Latch, ExitBB, /*FBB=*/nullptr,
                          /*Cond=*/SmallVector<MachineOperand, 0>(), DL);
    }

    ++NumEmptyZOLStripped;
    Changed = true;
  }
  return Changed;
}

static MachineInstr *findPseudoLoopEnd(MachineBasicBlock *BB) {
  if (!BB)
    return nullptr;
  for (MachineInstr &MI : *BB)
    if (MI.getOpcode() == Haydn::PseudoLoopEnd)
      return &MI;
  return nullptr;
}

// Resolve Header/Latch/PLE from the shared CFG helpers only. Never invent
// a body from layout order: a next-MBB that is not a successor is
// incomplete retained state and must reject fail-closed.
static void resolveRoleABody(MachineInstr *LS, MachineBasicBlock *&Header,
                             MachineBasicBlock *&Latch, MachineInstr *&PLE) {
  Header = nullptr;
  Latch = nullptr;
  PLE = nullptr;
  if (!LS || !LS->getParent())
    return;
  // One CFG mechanism (resolveBodyMBBCore + resolveLoopStartLatch).
  // AIE walks MLI findLoopControlBlock (AIEBaseHardwareLoops.cpp:301-318);
  // Haydn stays successor/pred only — never layout order, never MLI.
  MachineBasicBlock *Preheader = LS->getParent();
  Header = haydn::hwloop::resolveBodyMBBCore(*LS);
  Latch = haydn::hwloop::resolveLoopStartLatch(Header, Preheader);
  if (Latch)
    PLE = findPseudoLoopEnd(Latch);
  else if (Header)
    PLE = findPseudoLoopEnd(Header);
}

static bool isNestedRoleASetupOpcode(const MachineInstr &MI) {
  // Tii family membership (W64 QW2). Reject-site, fail-closed: adding the
  // bare golden SET_HWLOOP_F2 logical (previously name-peel-only at TII,
  // absent here) only refuses more bodies — it is a setup form under the MC
  // same-sel law (HaydnMCChecker starts_with("SET_HWLOOP")) and a nested
  // one inside a ZOL body is illegal for the same reason as the rest.
  return haydnClassifyHwloopSetupOpcode(MI.getOpcode()) !=
         HaydnHwloopSetupFamily::None;
}

[[noreturn]] static void rejectIncompleteRoleA(MachineInstr *LS, MachineBasicBlock *Body,
                                  MachineInstr *PLE, StringRef Why) {
  (void)Body;
  (void)PLE;
  ++NumRoleARejected;
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "HaydnHardwareLoops: incomplete Role-A retained state rejected "
        "before mutation — "
     << Why << "; refusing erase-only once-through";
  if (LS && LS->getParent())
    OS << " (LoopStart in " << printMBBReference(*LS->getParent()) << ")";
  report_fatal_error(Twine(Msg), /*GenCrashDiag=*/false);
}

static bool scanRoleABlock(const MachineBasicBlock &BB,
                           const haydn::hwloop::LoopBlockSet *Blocks,
                           bool AllowInternalBranch, unsigned &PLECount,
                           unsigned &RealOps, std::string &Why) {
  // Unpublished HWLR CSR is fail-closed even when packed inside a BUNDLE.
  // AIE AIEBaseHardwareLoops.cpp:348-370 walks every MI; Haydn overlay
  // refuses the unpublished 0x20-0x25 window (SET-only program).
  if (haydn::hwloop::blockContainsUnpublishedHwlrCsr(BB)) {
    Why = "body contains unpublished HWLR CSR write";
    return false;
  }
  bool SeenPLE = false;
  for (const MachineInstr &MI : BB) {
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::PseudoLoopEnd) {
      ++PLECount;
      SeenPLE = true;
      continue;
    }
    if (isNestedRoleASetupOpcode(MI)) {
      Why = "body contains nested LoopStart/SET_HWLOOP";
      return false;
    }
    if (MI.isCall() || MI.isIndirectBranch() || MI.isReturn()) {
      Why = "body contains call/indirect/return";
      return false;
    }
    if (MI.isBranch()) {
      if (SeenPLE)
        continue;
      if (!AllowInternalBranch) {
        Why = "body contains early-exit branch before PseudoLoopEnd";
        return false;
      }
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isMBB() && Blocks && !Blocks->contains(MO.getMBB())) {
          Why = "body contains early-exit branch before PseudoLoopEnd";
          return false;
        }
      }
    }
    if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
        MI.isKill() || MI.isPosition())
      continue;
    if (Opc == Haydn::B || Opc == Haydn::NOP)
      continue;
    ++RealOps;
  }
  return true;
}

static bool preflightRoleABody(MachineBasicBlock *Header,
                               MachineBasicBlock *Latch,
                               MachineBasicBlock *Preheader, MachineInstr *PLE,
                               std::string &Why) {
  if (!Header) {
    Why = "no body MBB";
    return false;
  }
  if (!Latch)
    Latch = Header;
  if (!PLE || PLE->getParent() != Latch) {
    Why = Header == Latch ? "PseudoLoopEnd missing or not on body"
                          : "PseudoLoopEnd missing or not on latch";
    return false;
  }

  if (Header == Latch) {
    if (!Header->isSuccessor(Header)) {
      Why = "body missing self back-edge";
      return false;
    }
    unsigned NonSelfSucc = 0;
    for (const MachineBasicBlock *Succ : Header->successors()) {
      if (Succ != Header)
        ++NonSelfSucc;
    }
    if (NonSelfSucc != 1) {
      Why = "body must have exactly one non-self exit";
      return false;
    }
    ZOLBodyKind Kind = classifyZOLBody(*Header);
    if (Kind == ZOLBodyKind::Empty) {
      Why = "empty body (should have been stripped)";
      return false;
    }
    if (Kind == ZOLBodyKind::NotZOL) {
      Why = "body is not ZOL form";
      return false;
    }
    unsigned PLECount = 0;
    unsigned RealOps = 0;
    if (!scanRoleABlock(*Header, /*Blocks=*/nullptr,
                        /*AllowInternalBranch=*/false, PLECount, RealOps, Why))
      return false;
    if (PLECount != 1) {
      Why = "body must carry exactly one PseudoLoopEnd";
      return false;
    }
    return true;
  }

  if (!Latch->isSuccessor(Header)) {
    Why = "latch missing back-edge to header";
    return false;
  }
  unsigned NonHeaderSucc = 0;
  for (const MachineBasicBlock *Succ : Latch->successors()) {
    if (Succ != Header)
      ++NonHeaderSucc;
  }
  if (NonHeaderSucc != 1) {
    Why = "latch must have exactly one non-header exit";
    return false;
  }

  haydn::hwloop::LoopBlockSet Blocks;
  haydn::hwloop::collectLoopBlocks(Header, Latch, Preheader, Blocks);

  // Reverse-CFG collect misses a Header successor that never reaches
  // Latch (fallthrough/branch early-exit). Those blocks are not in the
  // natural loop and must not stay armed. Same reject string as the
  // interior-branch scan so the preflight seats stay one law.
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB || BB == Latch)
      continue;
    for (const MachineBasicBlock *Succ : BB->successors()) {
      if (!Blocks.contains(Succ)) {
        Why = "body contains early-exit branch before PseudoLoopEnd";
        return false;
      }
    }
  }

  unsigned PLECount = 0;
  unsigned RealOps = 0;
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB)
      continue;
    if (!scanRoleABlock(*BB, &Blocks, /*AllowInternalBranch=*/true, PLECount,
                        RealOps, Why))
      return false;
  }
  if (PLECount != 1) {
    Why = "body must carry exactly one PseudoLoopEnd";
    return false;
  }
  if (RealOps == 0) {
    Why = "empty body (should have been stripped)";
    return false;
  }
  return true;
}

// Golden END follows BEGIN (Off2 > Off1). AIE splitLoopEndJump
// (AIEBaseHardwareLoops.cpp:227-264) forces the ZOL exit to the layout
// successor; Haydn overlay (no MLI) also puts Preheader, Header, interiors,
// Latch in that layout order so Fixup Off1/Off2 stay ordered.
// Interiors keep their existing function order — SmallPtrSet walk would
// scramble then/else fallthrough. MBP may leave Latch before Header; that
// is legal CFG and illegal ZOL. Header==Latch (MBP-rotated single-BB)
// still splices Preheader then Header so StartOff is not -1.
static void placeLoopBlocksForZOL(MachineBasicBlock *Preheader,
                                  MachineBasicBlock *Header,
                                  MachineBasicBlock *Latch,
                                  const haydn::hwloop::LoopBlockSet &Blocks) {
  if (!Preheader || !Header || !Latch)
    return;
  MachineFunction &MF = *Header->getParent();

  auto layoutSucc = [&](MachineBasicBlock *BB) -> MachineBasicBlock * {
    auto N = std::next(BB->getIterator());
    return N == MF.end() ? nullptr : &*N;
  };

  SmallVector<MachineBasicBlock *, 8> Interiors;
  for (MachineBasicBlock &BB : MF) {
    if (!Blocks.contains(&BB) || &BB == Header || &BB == Latch ||
        &BB == Preheader)
      continue;
    Interiors.push_back(&BB);
  }

  bool HeaderBeforeLatch = false;
  for (auto I = Header->getIterator(), E = MF.end(); I != E; ++I) {
    if (&*I == Latch) {
      HeaderBeforeLatch = true;
      break;
    }
  }
  bool AllBetween = HeaderBeforeLatch;
  if (HeaderBeforeLatch) {
    for (MachineBasicBlock *BB : Interiors) {
      bool Seen = false;
      for (auto I = std::next(Header->getIterator());
           I != MF.end() && &*I != Latch; ++I) {
        if (&*I == BB) {
          Seen = true;
          break;
        }
      }
      if (!Seen) {
        AllBetween = false;
        break;
      }
    }
  }
  if (layoutSucc(Preheader) == Header && AllBetween)
    return;

  // Header==Latch with non-loop blocks between Preheader and Header
  // (nested FIR inner body after an inner lr.ph) still has to sit next
  // to the setup so Off1 is the Following floor, not the intervening
  // work. updateTerminator restores CFG for any fallthrough we break.
  SmallVector<MachineBasicBlock *, 8> Order;
  Order.push_back(Header);
  Order.append(Interiors.begin(), Interiors.end());
  if (Latch != Header)
    Order.push_back(Latch);

  SmallDenseMap<MachineBasicBlock *, MachineBasicBlock *, 16> PrevFall;
  for (MachineBasicBlock &BB : MF)
    PrevFall[&BB] = layoutSucc(&BB);

  MachineBasicBlock *After = Preheader;
  for (MachineBasicBlock *BB : Order) {
    if (std::next(After->getIterator()) != BB->getIterator())
      BB->moveAfter(After);
    After = BB;
  }

  // SMS prologue/epilog sit between Preheader and Header but are not loop
  // blocks. Moving Header changes their layout successor; restore explicit
  // branches so CFG stays identical (layout-only overlay).
  for (MachineBasicBlock &BB : MF) {
    auto It = PrevFall.find(&BB);
    if (It != PrevFall.end() && layoutSucc(&BB) != It->second)
      BB.updateTerminator(It->second);
  }
}

// Parcel census used by both the expand pads and the post-expand QUALIFY
// seal. Same walk as Fixup countFollowingBundles / bodyParcelsFromOffsets:
// size-bearing parcels only (AIE ZOLSupport LoopSetupDistance overlay;
// AIEBaseHardwareLoops.cpp:391-428 expandLoopStart does not pad — Haydn
// enforces InterveningCycles + MinBodyBundles here because SET is one
// combined Off1/Off2 form, not AIE's separate LC/LS/LE writes).
static unsigned countFollowingSizeBearing(const MachineInstr &SetMI,
                                          const HaydnInstrInfo &TII) {
  const MachineBasicBlock *Preheader = SetMI.getParent();
  if (!Preheader)
    return 0;
  unsigned FollowingBundles = 0;
  // Walk from the next top-level cycle, not from std::next(member) which
  // would count siblings inside a coissued packet as Following.
  MachineBasicBlock::const_instr_iterator II = std::next(SetMI.getIterator());
  while (II != Preheader->instr_end() && II->isBundledWithPred())
    ++II;
  MachineBasicBlock::const_iterator I =
      II == Preheader->instr_end()
          ? Preheader->end()
          : MachineBasicBlock::const_iterator(II);
  for (MachineBasicBlock::const_iterator E = Preheader->end(); I != E; ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef() ||
        I->isKill())
      continue;
    unsigned Bytes = TII.getInstSizeInBytes(*I);
    if (Bytes == 0)
      continue;
    FollowingBundles += ceilProductParcels(Bytes);
    // W61: count the first size-bearing terminator, then stop — same rule as
    // Fixup countFollowingBundles and the scheduler tail credit
    // (countSizeBearingTailParcels). The B/cond to the header executes on
    // every activation path and the byte-law walk (StartOff) charges it.
    if (I->isTerminator())
      break;
  }
  return FollowingBundles;
}

static unsigned
countLoopBodyParcels(const haydn::hwloop::LoopBlockSet &Blocks,
                     const HaydnInstrInfo &TII) {
  unsigned BodyParcels = 0;
  for (const MachineBasicBlock *BB : Blocks) {
    if (!BB)
      continue;
    for (const MachineInstr &MI : *BB) {
      unsigned Opc = MI.getOpcode();
      if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::B)
        continue;
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
          MI.isKill() || MI.isPosition())
        continue;
      unsigned Bytes = TII.getInstSizeInBytes(MI);
      if (Bytes == 0)
        continue;
      BodyParcels += ceilProductParcels(Bytes);
    }
  }
  return BodyParcels;
}

// Fail-closed QUALIFY after pads/remat: SET-only program, product selector,
// Following floor, body floor, no unpublished HWLR CSR. Incomplete
// geometry rejects before LoopStart is erased so the retained seat
// never becomes a half-expanded SET.
static void qualifyExpandedRoleA(MachineInstr *LS, MachineInstr &SetMI,
                                 MachineBasicBlock *Header, MachineInstr *PLE,
                                 const haydn::hwloop::LoopBlockSet &Blocks,
                                 const HaydnInstrInfo &TII) {
  if (SetMI.getOpcode() != Haydn::SET_HWLOOP_F2_W)
    rejectIncompleteRoleA(LS, Header, PLE,
                          "expanded setup is not SET_HWLOOP_F2_W");
  if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm() ||
      !isProductSelector(SetMI.getOperand(0).getImm()))
    rejectIncompleteRoleA(LS, Header, PLE,
                          "expanded SET selector not in product domain");
  if (countFollowingSizeBearing(SetMI, TII) < InterveningCycles)
    rejectIncompleteRoleA(LS, Header, PLE,
                          "expanded SET misses setup Following floor");
  if (countLoopBodyParcels(Blocks, TII) < MinBodyBundles)
    rejectIncompleteRoleA(LS, Header, PLE,
                          "expanded body misses MinBodyBundles");
  if (haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(Blocks))
    rejectIncompleteRoleA(LS, Header, PLE,
                          "expanded body contains unpublished HWLR CSR");
}

// Next top-level MI after \p MI's issue cycle. Formation SET is bare;
// copy of Fixup nextBundleBoundary so a coissued root still walks AfterSet.
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

// Formation CFG is unstamped: sink SET toward Header so Off1 is the
// setup-to-begin gap, not leftover user work after LoopStart (nested FIR
// / pointer-IV i64). Stop on a Prefer def (SET would consume the new
// value) or a terminator. D1.112 no-sink is the post-stamp closer only.
static bool sinkSetupTowardHeader(MachineInstr &SetMI,
                                  const HaydnInstrInfo &TII) {
  MachineBasicBlock *MBB = SetMI.getParent();
  if (!MBB || SetMI.isBundled())
    return false;
  const TargetRegisterInfo *TRI =
      MBB->getParent()->getSubtarget().getRegisterInfo();
  Register Prefer;
  if (SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isReg())
    Prefer = SetMI.getOperand(3).getReg();
  MachineInstr &Root = haydn::hwloop::topLevelForLayout(SetMI);
  MachineBasicBlock::iterator SrcBegin = Root.getIterator();
  MachineBasicBlock::iterator SrcEnd = nextBundleBoundary(Root);
  MachineBasicBlock::iterator InsertPt = SrcEnd;
  bool PassedSize = false;

  auto cycleIsTerm = [](const MachineInstr &MI) {
    if (MI.isTerminator())
      return true;
    if (!MI.isBundle())
      return false;
    for (MachineInstr *K : haydn::bundle::members(const_cast<MachineInstr &>(MI)))
      if (K && K->isTerminator())
        return true;
    return false;
  };
  auto cycleDefines = [&](const MachineInstr &MI) {
    if (!Prefer.isPhysical())
      return false;
    auto one = [&](const MachineInstr &X) {
      return X.modifiesRegister(Prefer, TRI);
    };
    if (MI.isBundle()) {
      for (MachineInstr *K : haydn::bundle::members(const_cast<MachineInstr &>(MI)))
        if (K && one(*K))
          return true;
      return false;
    }
    return one(MI);
  };
  auto cycleNopFill = [](const MachineInstr &MI) {
    auto one = [](const MachineInstr &X) {
      return X.getOpcode() == Haydn::NOP ||
             haydn::bundle::isPadNopOpcode(X.getOpcode());
    };
    if (MI.isBundle()) {
      bool Any = false;
      for (MachineInstr *K : haydn::bundle::members(const_cast<MachineInstr &>(MI))) {
        if (!K)
          continue;
        if (!one(*K))
          return false;
        Any = true;
      }
      return Any;
    }
    return one(MI);
  };

  for (MachineBasicBlock::iterator Cur = SrcEnd, End = MBB->end(); Cur != End;
       ++Cur) {
    // SET is built immediately before the LoopStart it replaces. Skipping
    // that leftover lets Off1 be the real setup-to-begin gap.
    if (Cur->getOpcode() == Haydn::LoopStart) {
      InsertPt = std::next(Cur);
      continue;
    }
    if (cycleIsTerm(*Cur) || TII.isHardwareLoopSetupInstr(*Cur) ||
        cycleDefines(*Cur))
      break;
    if (cycleNopFill(*Cur)) {
      InsertPt = std::next(Cur);
      continue;
    }
    PassedSize = true;
    InsertPt = std::next(Cur);
  }
  if (!PassedSize || InsertPt == SrcEnd)
    return false;
  MBB->splice(InsertPt, MBB, SrcBegin, SrcEnd);
  LLVM_DEBUG(dbgs() << "HaydnHWLoops: sank SET toward header in "
                    << printMBBReference(*MBB) << "\n");
  return true;
}

static void ensureFollowingFloor(MachineInstr &SetMI,
                                 const HaydnInstrInfo &TII) {
  if (!SetMI.getParent())
    return;
  // Pad AFTER the issue cycle. A bundled SET must not splice NOPs into
  // the packet (gr14-demote-same-row-nop); nextBundleBoundary is the
  // first top-level MI after the packet, same insert the closer uses.
  unsigned FollowingBundles = countFollowingSizeBearing(SetMI, TII);
  if (FollowingBundles >= HWLoopSetupPadBundles)
    return;
  unsigned Deficit = HWLoopSetupPadBundles - FollowingBundles;
  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  for (unsigned I = 0; I < Deficit; ++I)
    BuildMI(*SetMI.getParent(), AfterSet, SetMI.getDebugLoc(),
            TII.get(Haydn::NOP));
}

// SET-anchored Off1/Off2 + last-body-cycle walk + still-relaxable second-BR
// budget. Same hard rangeBad laws Fixup uses (MaxStartOffsetBytes /
// MaxEndOffsetBytes, offsetsMeetImmRelocLaw, bodyMeetsMinLaw,
// MinSetupIssueBytes). Do not compose D1.35 PreS1PostStampGrowthBytes.
// PostRA must not undo formation SET-sink (ZOLSetupExitLatency late-pin).
static bool formationHardRangeForcesDemote(MachineInstr &SetMI,
                                           const HaydnInstrInfo &TII) {
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return true;
  MachineFunction &MF = *Pre->getParent();
  if (SetMI.getNumOperands() < 3 || !SetMI.getOperand(1).isMBB() ||
      !SetMI.getOperand(2).isMBB())
    return true;
  MachineBasicBlock *StartMBB = SetMI.getOperand(1).getMBB();
  MachineBasicBlock *EndMBB = SetMI.getOperand(2).getMBB();
  if (!haydn::hwloop::isLiveMBB(MF, StartMBB) ||
      !haydn::hwloop::isLiveMBB(MF, EndMBB))
    return true;

  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  int64_t StartOff = haydn::hwloop::estimateLayoutMBBDistance(
      MF, Pre, AfterSet, StartMBB, TII);
  MachineInstr &SetCycle = haydn::hwloop::topLevelForLayout(SetMI);
  const int64_t SetParcelBytes = static_cast<int64_t>(
      haydn::bundle::committedEncodedBytes(SetCycle).Value);
  StartOff = haydn::hwloop::anchoredFromAfterSet(StartOff, SetParcelBytes);
  // END is the last size-bearing non-term in Header/interiors/Latch, not
  // Latch-only (PLE-only SMS latches have no size-bearing non-term).
  int64_t EndOff = haydn::hwloop::estimateLastBodyCycleOffset(
      MF, StartMBB, EndMBB, Pre, StartOff, TII);
  // GR1.4: no estimate-only SET-sink or packed-gap credit. Unsunk Off1
  // that would encode only after peel/sink/pack demotes here (Hexagon
  // Fixup converts or leaves LOOP by range; Haydn has no extender).

  int64_t EndOffHard = EndOff;
  {
    const int64_t Parcel = static_cast<int64_t>(productParcelBytes().Value);
    haydn::hwloop::LoopBlockSet Blocks;
    haydn::hwloop::collectLoopBlocks(StartMBB, EndMBB, Pre, Blocks);
    for (const MachineBasicBlock *BB : Blocks) {
      if (!BB)
        continue;
      for (const MachineInstr &MI : *BB) {
        if (MI.isTerminator() || MI.isMetaInstruction())
          continue;
        if (MI.mayStore())
          EndOffHard += Parcel;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Off1/Off2 startOff=" << StartOff
                    << " endOff=" << EndOff << " endOffHard=" << EndOffHard
                    << " safeLim=" << MaxStartOffsetBytesSafe << "\n");

  auto rangeBad = [&]() {
    if (StartOff < 0 || EndOff < 0)
      return true;
    if (StartOff > MaxStartOffsetBytes || EndOffHard > MaxEndOffsetBytes)
      return true;
    if (!haydn::hwloop::offsetsMeetImmRelocLaw(StartOff, EndOff))
      return true;
    if (!haydn::hwloop::bodyMeetsMinLaw(StartOff, EndOff))
      return true;
    if (StartOff < MinSetupIssueBytes)
      return true;
    if (TII.isHardwareLoopImmTripOpcode(SetMI.getOpcode()) &&
        SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isImm() &&
        !haydn::hwloop::countMeetsFieldLaw(SetMI.getOperand(3).getImm()))
      return true;
    return false;
  };
  if (rangeBad()) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: hard Off1/Off2 unencodable startOff="
                      << StartOff << " endOff=" << EndOff
                      << " — demote before PostMachineScheduler\n");
    return true;
  }

  using haydn::hwloop::isStillRelaxableShortBranch;
  auto countBranchGrowthIn = [&](const MachineInstr &Probe) -> int64_t {
    // Net extra over the size already in Off (unbundled B/cond is one
    // parcel). Absolute MaxSingleBranchGrowthBytes over-demotes SMS
    // 2-stage forms whose packed Off1 still encodes.
    auto one = [&](const MachineInstr &I) -> int64_t {
      if (!isStillRelaxableShortBranch(I))
        return 0;
      // Reserve the full late-BR expansion (LUI+ADDI+JALR), not extra
      // over the current short encoding. Charging only Extra=Max-Cur
      // under-demotes SET→BEGIN windows whose short B bytes are not in
      // StartOff (growth_budget_demote: 3×48 vs residual BeginMargin).
      return static_cast<int64_t>(MaxSingleBranchGrowthBytes);
    };
    int64_t G = 0;
    if (Probe.isBundle()) {
      const MachineBasicBlock *PBB = Probe.getParent();
      for (MachineBasicBlock::const_instr_iterator I =
               std::next(Probe.getIterator());
           I != PBB->instr_end() && I->isBundledWithPred(); ++I)
        G += one(*I);
    } else {
      G += one(Probe);
    }
    return G;
  };
  auto sumStillRelaxableGrowth =
      [&](MachineBasicBlock::iterator FromIt, const MachineBasicBlock *ToMBB,
          bool InclusiveTo) -> int64_t {
    if (!ToMBB || !haydn::hwloop::isLiveMBB(MF, ToMBB))
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
  const int64_t BeginGrowth =
      sumStillRelaxableGrowth(AfterSet, StartMBB, /*InclusiveTo=*/false);
  const int64_t EndGrowth =
      sumStillRelaxableGrowth(AfterSet, EndMBB, /*InclusiveTo=*/true);
  const int64_t BeginMargin = MaxStartOffsetBytes - StartOff;
  const int64_t EndMargin = MaxEndOffsetBytes - EndOff;
  if (BeginGrowth > BeginMargin || EndGrowth > EndMargin) {
    LLVM_DEBUG(dbgs() << "HaydnHWLoops: still-relaxable BR growth exceeds Off "
                         "margin (beginGrowth="
                      << BeginGrowth << " beginMargin=" << BeginMargin
                      << " endGrowth=" << EndGrowth
                      << " endMargin=" << EndMargin
                      << ") — demote before PostMachineScheduler\n");
    return true;
  }
  return false;
}

static bool expandRoleALoopStarts(MachineFunction &MF) {
  const auto *TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  SmallVector<MachineInstr *, 4> LoopStarts;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.getOpcode() == Haydn::LoopStart)
        LoopStarts.push_back(&MI);

  bool Changed = false;
  for (MachineInstr *LS : LoopStarts) {
    if (!LS->getParent())
      continue;

    MachineBasicBlock *Header = nullptr;
    MachineBasicBlock *Latch = nullptr;
    MachineInstr *PLE = nullptr;
    resolveRoleABody(LS, Header, Latch, PLE);

    if (LS->getNumOperands() < 2 || !LS->getOperand(0).isReg() ||
        !LS->getOperand(1).isImm()) {
      rejectIncompleteRoleA(LS, Header, PLE, "incomplete LoopStart operands");
    }

    MachineBasicBlock *Preheader = LS->getParent();
    Register TripReg = LS->getOperand(0).getReg();
    int64_t Adj = LS->getOperand(1).getImm();
    DebugLoc DL = LS->getDebugLoc();

    if (!TripReg.isPhysical() || TripReg == Haydn::R0) {
      rejectIncompleteRoleA(LS, Header, PLE, "trip reg not a physical GPR");
    }

    std::string Why;
    if (!preflightRoleABody(Header, Latch, Preheader, PLE, Why)) {
      // Shape-preflight failure splits into two classes:
      //  * body-resolvable shape (Header+PLE parsed; early-exit branch,
      //    body call, multi-exit, nested setup, oversize geometry): the
      //    loop is simply not ZOL-able — demote to the software loop,
      //    exactly like the oversize-body path below. While the flag was
      //    product-OFF only explicit opt-in ever reached the fatal, so it
      //    was invisible; default-ON makes demote the default action.
      //  * unresolvable retained state (no Header / no PLE on the body):
      //    demoteHardwareLoopToSoftware cannot parse the body either and
      //    erases LoopStart erase-only, leaving a dangling PseudoLoopEnd
      //    with no counter and no back-edge — silent infinite loop. That
      //    class stays fatal (same refuse-erase-only law as the demoter's
      //    own refuseUnparseable).
      const bool BodyResolvable = Header && PLE;
      if (!BodyResolvable ||
          !demoteHardwareLoopToSoftware(*LS, *TII))
        rejectIncompleteRoleA(LS, Header, PLE, Why);
      Changed = true;
      continue;
    }

    haydn::hwloop::LoopBlockSet LoopBlocks;
    haydn::hwloop::collectLoopBlocks(Header, Latch, Preheader, LoopBlocks);
    placeLoopBlocksForZOL(Preheader, Header, Latch, LoopBlocks);

    {
      int64_t BodyBytes = 0;
      for (const MachineBasicBlock *BB : LoopBlocks) {
        if (!BB)
          continue;
        for (const MachineInstr &MI : *BB) {
          unsigned Opc = MI.getOpcode();
          if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::B)
            continue;
          if (MI.isMetaInstruction() || MI.isDebugInstr() ||
              MI.isImplicitDef() || MI.isKill() || MI.isPosition())
            continue;
          BodyBytes += TII->getInstSizeInBytes(MI);
        }
      }
      if (MinSetupBytes + BodyBytes > MaxHWLoopEndOffsetBytes) {
        if (!demoteHardwareLoopToSoftware(*LS, *TII))
          rejectIncompleteRoleA(
              LS, Header, PLE,
              "unencodable body cannot install software loop");
        Changed = true;
        continue;
      }
    }

    Header->setLabelMustBeEmitted();
    Latch->setLabelMustBeEmitted();

    // Product selector domain is {0,1}. Innermost Role-A expand always arms
    // the inner selector via SET_HWLOOP only — never invent free HWLR CSR
    // addresses (unpublished 0x20-0x25 window) or out-of-domain selectors.
    // Fixup demotes residual out-of-domain seats.
    static_assert(isProductSelector(InnermostProductSelector),
                  "Role-A expand selector must stay in product domain");
    static_assert(isProductSelector(InnermostProductSelector) &&
                      !isProductSelector(2) && !isProductSelector(3),
                  "expand must not invent out-of-domain selectors");
    static_assert(!isUnpublishedHwlrCsrAddress(InnermostProductSelector),
                  "product selector is not an unpublished HWLR CSR address");
    MachineBasicBlock::iterator InsertPt = LS->getIterator();
    // SET-only program: product sel via SET_HWLOOP_F2_W. Descriptor
    // Defs=[SFR] supplies the implicit SFR write; never a free CSRW
    // to the unpublished HWLR window (HaydnPortModel.h haydnHwloopCsrAddr).
    MachineInstr *SetMI =
        BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SET_HWLOOP_F2_W))
            .addImm(InnermostProductSelector)
            .addMBB(Header)
            .addMBB(Latch)
            .addReg(TripReg);

    // Adj remat must see the original trip while it is still live. Sink
    // then moves SET only; the remat def stays.
    if (Adj != 0) {
      Register Count =
          rematerializeAddImmForUse(*SetMI, /*UseOpIdx=*/3, Adj);
      (void)Count;
    }

    sinkSetupTowardHeader(*SetMI, *TII);
    ensureFollowingFloor(*SetMI, *TII);

    {
      unsigned BodyParcels = countLoopBodyParcels(LoopBlocks, *TII);
      if (BodyParcels < MinBodyBundles) {
        unsigned Deficit = MinBodyBundles - BodyParcels;
        MachineBasicBlock::iterator BeforePLE = PLE->getIterator();
        for (unsigned I = 0; I < Deficit; ++I)
          BuildMI(*Latch, BeforePLE, DL, TII->get(Haydn::NOP));
        LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A body pad " << Deficit
                          << " NOP bundle(s) before PLE (MinBodyBundles="
                          << MinBodyBundles << ")\n");
      }
    }

    qualifyExpandedRoleA(LS, *SetMI, Header, PLE, LoopBlocks, *TII);

    // After pads: SET-anchored Off1/Off2 must encode under the same hard
    // laws Fixup will re-check. Unencodable form demotes here (unstamped;
    // CFG rewrite legal) so stamped Fixup never needs a successor rewrite.
    if (formationHardRangeForcesDemote(*SetMI, *TII)) {
      if (!demoteHardwareLoopToSoftware(*SetMI, *TII))
        rejectIncompleteRoleA(
            LS, Header, PLE,
            "unencodable Off1/Off2 cannot install software loop");
      if (LS->getParent())
        LS->eraseFromParent();
      Changed = true;
      continue;
    }

    LS->eraseFromParent();
    ++NumRoleAExpanded;
    Changed = true;
  }
  return Changed;
}

// AIE splitLoopEndJump (AIEBaseHardwareLoops.cpp:232-272): a ZOL exit
// that is not fallthrough is split into a dedicated exit block so the
// loop-end terminator falls through. Haydn PseudoLoopEnd is the END
// opcode (AIE isHardwareLoopEnd). Must run before expand/demote and
// before scheduler inventory (Wave 4). insertBranch already emits
// PseudoLoopEnd + trailing B.
static bool splitLoopEndJump(MachineBasicBlock &MBB,
                             const HaydnInstrInfo *TII) {
  auto Terminator = MBB.getFirstInstrTerminator();
  if (Terminator == MBB.end() ||
      Terminator->getOpcode() != Haydn::PseudoLoopEnd)
    return false;
  SmallVector<MachineOperand, 4> Cond;
  MachineBasicBlock *TBB = nullptr;
  MachineBasicBlock *FBB = nullptr;
  if (TII->analyzeBranch(MBB, TBB, FBB, Cond, /*AllowModify=*/false))
    return false;
  if (!FBB)
    return false;

  MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *NewBB =
      MF->CreateMachineBasicBlock(FBB->getBasicBlock());
  MF->insert(std::next(MBB.getIterator()), NewBB);
  TII->removeBranch(MBB);
  DebugLoc DL;
  TII->insertBranch(MBB, TBB, nullptr, Cond, DL);
  Cond.clear();
  TII->insertBranch(*NewBB, FBB, nullptr, Cond, DL);
  for (MachineBasicBlock *Edge : make_early_inc_range(MBB.successors())) {
    if (Edge == FBB)
      MBB.removeSuccessor(FBB);
  }
  NewBB->addSuccessor(FBB);
  MBB.addSuccessor(NewBB);
  LLVM_DEBUG(dbgs() << "HaydnHWLoops: splitLoopEndJump "
                    << printMBBReference(MBB) << " exit -> "
                    << printMBBReference(*NewBB) << " -> "
                    << printMBBReference(*FBB) << "\n");
  return true;
}

// Pre-commit Latch→Exit split so restore lands in a pred_size==1 block
// (RISC-V insertIndirectBranch RestoreBB RISCVInstrInfo.cpp:1433-1443;
// AIE splitLoopEndJump AIEBaseHardwareLoops.cpp:232-272). Join Exit
// keeps its other predecessors; Dedicated is Latch-only.
static MachineBasicBlock *
splitDedicatedLatchExit(MachineBasicBlock &Latch, MachineBasicBlock &Exit,
                        const HaydnInstrInfo &TII) {
  if (Exit.pred_size() == 1 && *Exit.pred_begin() == &Latch)
    return &Exit;
  MachineFunction &MF = *Latch.getParent();
  MachineBasicBlock *Dedicated =
      MF.CreateMachineBasicBlock(Exit.getBasicBlock());
  MF.insert(std::next(Latch.getIterator()), Dedicated);
  Latch.replaceSuccessor(&Exit, Dedicated);
  Dedicated->addSuccessor(&Exit);
  for (MachineInstr &MI : Latch.instrs()) {
    for (MachineOperand &MO : MI.operands())
      if (MO.isMBB() && MO.getMBB() == &Exit)
        MO.setMBB(Dedicated);
  }
  for (MachineInstr &MI : Exit) {
    if (!MI.isPHI())
      break;
    for (unsigned I = 1, E = MI.getNumOperands(); I < E; I += 2) {
      if (MI.getOperand(I + 1).isMBB() &&
          MI.getOperand(I + 1).getMBB() == &Latch)
        MI.getOperand(I + 1).setMBB(Dedicated);
    }
  }
  for (const auto &LI : Exit.liveins())
    Dedicated->addLiveIn(LI.PhysReg);
  DebugLoc DL;
  SmallVector<MachineOperand, 0> Cond;
  TII.insertBranch(*Dedicated, &Exit, nullptr, Cond, DL);
  LLVM_DEBUG(dbgs() << "HaydnHWLoops: dedicated-exit split "
                    << printMBBReference(Latch) << " -> "
                    << printMBBReference(*Dedicated) << " -> "
                    << printMBBReference(Exit) << "\n");
  return Dedicated;
}

static bool setupContains(const MachineInstr &Outer, const MachineInstr &Inner) {
  if (&Outer == &Inner)
    return false;
  if (Outer.getNumOperands() < 3 || !Outer.getOperand(1).isMBB() ||
      !Outer.getOperand(2).isMBB())
    return false;
  haydn::hwloop::LoopBlockSet Blocks;
  haydn::hwloop::collectLoopBlocks(Outer.getOperand(1).getMBB(),
                                   Outer.getOperand(2).getMBB(),
                                   Outer.getParent(), Blocks);
  if (Blocks.count(Inner.getParent()))
    return true;
  if (Inner.getNumOperands() >= 2 && Inner.getOperand(1).isMBB() &&
      Blocks.count(Inner.getOperand(1).getMBB()))
    return true;
  return false;
}

// Already-expanded SET_HWLOOP_* (isolated tests and leftover Role-A
// forms): demote unencodable setups before scheduler inventory. AIE
// processLoop is inner-first (AIEBaseHardwareLoops.cpp:300-306).
static bool demoteUnencodableExpandedSetups(MachineFunction &MF,
                                            const HaydnInstrInfo &TII) {
  auto collectSets = [&](SmallVectorImpl<MachineInstr *> &Sets) {
    Sets.clear();
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB.instrs()) {
        if (MI.getOpcode() == TargetOpcode::BUNDLE)
          continue;
        if (TII.isHardwareLoopSetupInstr(MI) &&
            MI.getOpcode() != Haydn::LoopStart)
          Sets.push_back(&MI);
      }
  };

  auto shouldDemote = [&](MachineInstr &SetMI) -> bool {
    if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm() ||
        !SetMI.getOperand(1).isMBB() || !SetMI.getOperand(2).isMBB())
      report_fatal_error(
          "HaydnHardwareLoops: malformed SET_HWLOOP; refusing erase-only "
          "once-through",
          /*gen_crash_diag=*/false);
    if (!haydn::hwloop::isProductSelector(SetMI.getOperand(0).getImm()))
      return true;
    MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
    MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
    if (!haydn::hwloop::isLiveMBB(MF, H) || !haydn::hwloop::isLiveMBB(MF, L))
      return true;
    haydn::hwloop::LoopBlockSet Blocks;
    haydn::hwloop::collectLoopBlocks(H, L, SetMI.getParent(), Blocks);
    if (haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(Blocks))
      report_fatal_error(
          "HaydnHardwareLoops: unpublished HWLR CSR write in "
          "hardware-loop body; product programs HWLR only through "
          "SET_HWLOOP",
          /*gen_crash_diag=*/false);
    return formationHardRangeForcesDemote(SetMI, TII);
  };

  bool Changed = false;
  SmallVector<MachineInstr *, 8> Sets;
  for (;;) {
    collectSets(Sets);
    if (Sets.empty())
      break;
    std::stable_sort(Sets.begin(), Sets.end(),
                     [&](const MachineInstr *A, const MachineInstr *B) {
                       if (!A || !B || A == B)
                         return false;
                       const bool AInB = setupContains(*B, *A);
                       const bool BInA = setupContains(*A, *B);
                       if (AInB != BInA)
                         return AInB;
                       return false;
                     });
    bool Progress = false;
    for (MachineInstr *MI : Sets) {
      if (!MI || !MI->getParent())
        continue;
      sinkSetupTowardHeader(*MI, TII);
      ensureFollowingFloor(*MI, TII);
      if (!shouldDemote(*MI))
        continue;
      if (!demoteHardwareLoopToSoftware(*MI, TII))
        report_fatal_error(
            "HaydnHardwareLoops: out-of-range/invalid SET_HWLOOP cannot "
            "demote to software loop (no free counter GPR or usable exit); "
            "refusing erase-only once-through",
            /*gen_crash_diag=*/false);
      Changed = true;
      Progress = true;
      break;
    }
    if (!Progress)
      break;
  }
  return Changed;
}

bool HaydnHardwareLoops::runOnMachineFunction(MachineFunction &MF) {
  // Product insert is EnableHaydnHardwareLoops (default ON since the
  // 2026-08-22 qualification). hasHWLoop() is ISA capability only —
  // +hwloop does not flip product policy.
  // Never skipFunction here: the cl flag is the pipeline insert gate.
  const auto &STI = MF.getSubtarget<HaydnSubtarget>();
  if (!STI.hasHWLoop())
    return false;

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Running on " << MF.getName()
                    << " (Role A expand only; post-RA rediscovery deleted)\n");

  // Frame-deadline law: when this pass runs it is the first Haydn pass
  // after PEI (before HaydnExpandPseudos). Earliest-wins snapshot; the
  // demote below must never grow the frame past it.
  MF.getInfo<HaydnMachineFunctionInfo>()->takeFrameFreezeSnapshot(
      MF.getFrameInfo());

  bool Changed = false;
  Changed |= stripEmptyZeroOverheadLoops(MF);
  const HaydnInstrInfo *TII = STI.getInstrInfo();
  for (MachineBasicBlock &MBB : MF)
    Changed |= splitLoopEndJump(MBB, TII);
  Changed |= expandRoleALoopStarts(MF);
  Changed |= demoteUnencodableExpandedSetups(MF, *TII);
  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expand-only complete\n");
  return Changed;
}


//===----------------------------------------------------------------------===//
// Formation-owned software-loop demotion / setup erase
//
// Encodability is decided here (or the soft edge is installed) before layout
// lock-in. Isolated unstamped Fixup may still call the same helpers. Product
// post-stamp Fixup does not CFG-demote. Residual generic SET_HWLOOP{,_REG}
// is not rewritten here or in Fixup.
//===----------------------------------------------------------------------===//
// MI-level demote/erase helpers shared with HaydnFixupHwLoops live in
// HaydnHWLoopDemote.{h,cpp} (single owner; previously duplicated verbatim).
// Body resolution here is CFG-only via resolveBodyMBBCore: formation must
// never resolve a body from layout order — incomplete retained state rejects
// fail-closed (resolveRoleABody law above).
using haydn::hwloop::LoopBlockSet;
using haydn::hwloop::addComputedSuccessorLiveIns;
using haydn::hwloop::collectLoopBlocks;
using haydn::hwloop::demoteSavePlacement;
using haydn::hwloop::resolveDemoteSaveHome;
using haydn::hwloop::resolveLoopStartLatch;
using haydn::hwloop::emitExactLate;
using haydn::hwloop::emitExactLateDef;
using haydn::hwloop::eraseInstrSafe;
using haydn::hwloop::eraseSetMemberAndRecommitSiblings;
using haydn::hwloop::isLiveMBB;
using haydn::hwloop::isSoundDemoteCounter;
using haydn::hwloop::materializeTripCount;
using haydn::hwloop::pickCounterReg;
using haydn::hwloop::pickDeadLatchScratch;
using haydn::hwloop::regClobberedNonCountdownIn;
using haydn::hwloop::regMentionedInPreheaderTail;
using haydn::hwloop::regUsedFromSetInPreheaderTail;
using haydn::hwloop::regDefdInPreheaderTail;
using haydn::hwloop::regIsPreheaderTailFrameAddress;
using haydn::hwloop::regIsLiveIntoExitTailImm;
using haydn::hwloop::hasIncomingValue;
using haydn::hwloop::regIsUnsoundLatchScratchAtSet;
using haydn::hwloop::regExitPathReadsPhysReg;
using haydn::hwloop::regMentionedInBlocks;
using haydn::hwloop::regUsedNonCountdownIn;
using haydn::hwloop::residualCountdownEquivalentAtLatch;
using haydn::hwloop::stripResidualCountdown;
using haydn::hwloop::HwLoopDemoteSaveKind;
using haydn::hwloop::topLevelForLayout;

bool llvm::eraseHardwareLoopSetup(
    MachineInstr &SetMI, const char *DebugPrefix,
    MachineBasicBlock *(*ResolveBody)(MachineInstr &)) {
  // Body-resolution law: default is the CFG-only core; only pre-emit Fixup
  // passes its final-layout tail resolver (layout order is not a formation
  // body proof — resolveRoleABody rejects incomplete retained state).
  MachineBasicBlock *(*const ResolveBodyFn)(MachineInstr &) =
      ResolveBody ? ResolveBody : haydn::hwloop::resolveBodyMBBCore;
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  MachineFunction &MF = *Pre->getParent();
  const auto &TII = *static_cast<const HaydnInstrInfo *>(
      MF.getSubtarget().getInstrInfo());

  SmallVector<MachineInstr *, 8> PLEs;

  // Collect PseudoLoopEnd from live body (LoopStart) or live Header/Latch.
  auto collectPLE = [&](MachineBasicBlock *BB) {
    if (!isLiveMBB(MF, BB))
      return;
    for (MachineInstr &MI : BB->instrs()) {
      if (MI.isBundledWithPred())
        continue;
      if (MI.getOpcode() == Haydn::PseudoLoopEnd)
        PLEs.push_back(&MI);
    }
  };

  unsigned Opc = SetMI.getOpcode();
  if (Opc == Haydn::LoopStart) {
    MachineBasicBlock *Body = ResolveBodyFn(SetMI);
    collectPLE(Body);
    collectPLE(haydn::hwloop::resolveLoopStartLatch(Body, Pre));
  } else if (SetMI.getNumOperands() >= 3) {
    if (SetMI.getOperand(1).isMBB())
      collectPLE(SetMI.getOperand(1).getMBB());
    if (SetMI.getOperand(2).isMBB()) {
      MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
      if (SetMI.getOperand(1).isMBB() && L != SetMI.getOperand(1).getMBB())
        collectPLE(L);
    }
  }

  // SET/LoopStart first: sibling recommit needs the coissue root intact.
  eraseSetMemberAndRecommitSiblings(SetMI, TII, DebugPrefix);

  SmallPtrSet<MachineInstr *, 8> Seen;
  for (MachineInstr *MI : PLEs) {
    if (!MI || !MI->getParent() || !Seen.insert(MI).second)
      continue;
    eraseInstrSafe(MI);
  }
  return true;
}

// Demote SET_HWLOOP{,_REG} / LoopStart to a countable software loop:
// materialise the trip counter at the former SET site, erase SET (and
// PseudoLoopEnd for ZOL), restore latch Header+Exit edges with final-real
// SUBI32 + BNEZ_W (: no residual LoopDec/LoopJNZ after late commit).
// eraseHardwareLoopSetup is SET-member-only (bundle-preserving): coissued slot
// siblings survive and are exact-recommitted (rebuilt root operands/kills).
// Return value :
// true — handled: soft edge installed, OR L1 erase-only because
// Header/Latch/body is dead (body gone / peeled).
// false — live loop body but soft edge cannot be installed (no free
// counter GPR, no usable exit, or unparseable trip). Caller must
// NOT erase-only: keep SET (pad already applied) or fatal.
// Fail-closed on dead MBB: if Header/Latch are not live (e.g. `%bb.-1`)
// only erase the SET — never walk a dead MBB.
bool llvm::demoteHardwareLoopToSoftware(
    MachineInstr &SetMI, const HaydnInstrInfo &TII,
    const char *DebugPrefix, MachineBasicBlock *(*ResolveBody)(MachineInstr &)) {
  MachineBasicBlock *(*const ResolveBodyFn)(MachineInstr &) =
      ResolveBody ? ResolveBody : haydn::hwloop::resolveBodyMBBCore;
  unsigned Opc = SetMI.getOpcode();
  const bool IsLoopStart = Opc == Haydn::LoopStart;
  if (!TII.isHardwareLoopSetupOpcode(Opc) && !IsLoopStart)
    return false;

  MachineBasicBlock *Preheader = SetMI.getParent();
  if (!Preheader)
    return false;
  MachineFunction &MF = *Preheader->getParent();

  MachineBasicBlock *Header = nullptr;
  MachineBasicBlock *Latch = nullptr;
  Register Prefer;
  int64_t Imm = 0;
  bool HasImm = false;
  // LoopStart op1 is the pipeliner's signed trip adjustment (typically -S).
  // Remaining kernel trip after a peel is Prefer+Adj. SET_* already rematted
  // this addend at Role-A expand; applying it again would double-count.
  int64_t LoopStartAdj = 0;

  // Cannot parse Header/Latch/trip: cannot prove the body is dead and
  // cannot install a soft edge. Hexagon FixupHwLoops.cpp:137-148 converts
  // or leaves LOOP; AIEBaseHardwareLoops.cpp:311-316 early-returns with
  // the setup intact. Haydn overlay: refuse erase-only so the caller
  // fatals. Dead-body L1 erase stays on the path that parsed Header/Latch
  // and proved them not live.
  auto refuseUnparseable = [&]() -> bool {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": unparseable SET/LoopStart — refuse erase-only "
                         "once-through\n");
    return false;
  };

  if (IsLoopStart) {
    if (!SetMI.getOperand(0).isReg())
      return refuseUnparseable();
    Prefer = SetMI.getOperand(0).getReg();
    if (SetMI.getNumOperands() >= 2 && SetMI.getOperand(1).isImm())
      LoopStartAdj = SetMI.getOperand(1).getImm();
    Header = ResolveBodyFn(SetMI);
    Latch = haydn::hwloop::resolveLoopStartLatch(Header, Preheader);
    if (Header && !Latch) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — LoopStart latch unresolved "
                           "(live body)\n");
      return false;
    }
  } else {
    if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(1).isMBB() ||
        !SetMI.getOperand(2).isMBB())
      return refuseUnparseable();
    Header = SetMI.getOperand(1).getMBB();
    Latch = SetMI.getOperand(2).getMBB();
    if (TII.isHardwareLoopRegTripOpcode(Opc)) {
      if (!SetMI.getOperand(3).isReg())
        return refuseUnparseable();
      Prefer = SetMI.getOperand(3).getReg();
    } else {
      if (!SetMI.getOperand(3).isImm())
        return refuseUnparseable();
      Imm = SetMI.getOperand(3).getImm();
      HasImm = true;
    }
  }

  // Contract §1: dead MBB operands → L1 erase only (body gone).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch)) {
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote L1-only — Header/Latch "
                         "not live in MF (stale %bb.-1 or erased body)\n");
    return eraseHardwareLoopSetup(SetMI, DebugPrefix);
  }

  HaydnMachineFunctionInfo *FuncInfo =
      MF.getInfo<HaydnMachineFunctionInfo>();

  // Live body. Debug-only demote OFF: Hexagon FixupHwLoops skips
  // conversion when the loop cannot stay legal
  // (HexagonFixupHwLoops.cpp:97-148). Overlay: refuse the soft-edge
  // install so the caller fatals rather than erase-only once-through.
  if (!haydn::hwloop::isHwLoopDemoteEnabled()) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote disabled on live body — refuse "
                         "erase-only once-through\n");
    return false;
  }

  // Wave 4 H: CFG-changing demote after PostCommitCfgSnapshot is refused.
  // SET/CFG stay untouched (D1.51 atomicity). Formation unstamped demote
  // (this function, before inventory) stays legal. closeRetainedHwLoops
  // no-ops unless stamped and never demotes/peels/sinks; a stamped
  // live-body refuse fatals in recoverRangeOrOrder. Dead-body L1 erase
  // above is not CFG-changing. AIE addPreEmitPass is empty
  // (AIE2TargetMachine.cpp:92 / :238-257).
  if (FuncInfo && FuncInfo->hasPostCommitBlockBudget()) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote refused — post-commit CFG snapshot is "
                         "stamped; CFG rewrite is illegal\n");
    return false;
  }

  DebugLoc DL = SetMI.getDebugLoc();

  // Exit selection (ZOL demote):
  // After BranchRelaxation, a far PseudoLoopEnd back-edge is rewritten as a
  // continue trampoline (empty MBB → LUI+ADDI+JALR Header) that remains a
  // latch successor alongside the true exit. Taking the *first* non-Header
  // successor then installs:
  //   BNEZ Header ; fallthrough/B trampoline→Header
  // so both soft edges return to the header (infinite loop; gcc-c-torture
 // 20021120-1 @ -O1). Prefer:
  //  1) Unconditional branch target on the latch (B after PseudoLoopEnd) —
  //     that is the ZOL fallthrough exit.
  //  2) Non-Header successor that is not a continue-only trampoline chain
  //     that only reaches Header.
  //  3) Layout successor of the latch.
  MachineBasicBlock *Exit = nullptr;

  auto uncondBranchTarget = [&TII](const MachineInstr &TermMI)
      -> MachineBasicBlock * {
    // terminators() yields top-level MIs (BUNDLE roots included). Prefer
    // TII.getBranchDestBlock when analyzable; else shape-match B / JAL*_W R0.
    if (TermMI.isIndirectBranch() || TermMI.isReturn())
      return nullptr;
    unsigned Opc = TermMI.getOpcode();
    if (Opc == TargetOpcode::BUNDLE) {
      // First non-meta child that is a branch.
      for (const MachineInstr &C : make_range(getBundleStart(TermMI.getIterator()),
                                              getBundleEnd(TermMI.getIterator()))) {
        if (&C == &TermMI)
          continue;
        if (C.isMetaInstruction() || C.isCFIInstruction())
          continue;
        if (C.isUnconditionalBranch() && !C.isIndirectBranch()) {
          if (C.getNumOperands() > 0 && C.getOperand(0).isMBB())
            return C.getOperand(0).getMBB();
          if (C.getNumOperands() > 1 && C.getOperand(1).isMBB())
            return C.getOperand(1).getMBB();
        }
      }
      return nullptr;
    }
    if (!TermMI.isUnconditionalBranch() || TermMI.isIndirectBranch())
      return nullptr;
    // Direct uncond: B MBB, or JAL/JAL_W R0, MBB.
    if (TermMI.getNumOperands() > 0 && TermMI.getOperand(0).isMBB())
      return TermMI.getOperand(0).getMBB();
    if (TermMI.getNumOperands() > 1 && TermMI.getOperand(0).isReg() &&
        TermMI.getOperand(0).getReg() == Haydn::R0 &&
        TermMI.getOperand(1).isMBB())
      return TermMI.getOperand(1).getMBB();
    (void)TII;
    return nullptr;
  };

  // (1) Latch unconditional branch target (skip PseudoLoopEnd / cond).
  for (const MachineInstr &Term : Latch->terminators()) {
    if (Term.getOpcode() == Haydn::PseudoLoopEnd)
      continue;
    if (MachineBasicBlock *T = uncondBranchTarget(Term)) {
      if (T != Header && isLiveMBB(MF, T))
        Exit = T;
    }
  }

  // Continue-trampoline: empty / LUI+ADDI+JALR chain whose only reachable
  // latch-side destination is Header (BranchRelaxation artifact).
  auto reachesOnlyHeader = [&](MachineBasicBlock *S) -> bool {
    SmallPtrSet<const MachineBasicBlock *, 8> Visited;
    MachineBasicBlock *Cur = S;
    for (int Depth = 0; Cur && Depth < 8; ++Depth) {
      if (!Visited.insert(Cur).second)
        return Cur == Header;
      if (Cur == Header)
        return true;
      if (Cur->succ_size() != 1)
        return false;
      if (!haydn::hwloop::isContinueTrampolineBlock(Cur))
        return false;
      Cur = *Cur->succ_begin();
    }
    return false;
  };

  // (2) Successor scan, skipping Header and continue-trampolines.
  if (!Exit) {
    if (Latch->succ_size() == 1) {
      MachineBasicBlock *S = *Latch->succ_begin();
      if (S != Header && isLiveMBB(MF, S) && !reachesOnlyHeader(S))
        Exit = S;
    } else {
      for (MachineBasicBlock *S : Latch->successors()) {
        if (S != Header && isLiveMBB(MF, S) && !reachesOnlyHeader(S)) {
          Exit = S;
          break;
        }
      }
    }
  }

  // (3) Layout fallthrough of latch (also skip trampolines).
  if (!Exit) {
    MachineFunction::iterator LatchIt = Latch->getIterator();
    MachineFunction::iterator NextIt = std::next(LatchIt);
    if (NextIt != MF.end() && isLiveMBB(MF, &*NextIt) &&
        &*NextIt != Header && !reachesOnlyHeader(&*NextIt))
      Exit = &*NextIt;
  }
  if (!isLiveMBB(MF, Exit)) {
    // Live body but no usable exit — cannot install soft edge. Leave SET
    // intact for the caller (never erase-only once-through).
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote refused — no live exit "
                         "(body still live)\n");
    return false;
  }

  // Closed demote model (AIE expand-style: total restore, no half state):
  // Decide CountReg *before* any erase. Only then L1 erase + L2 soft edge.
  // Never erase SET when soft edge cannot be installed on a live body.

  LLVM_DEBUG(dbgs() << DebugPrefix << ": demoting hwloop header="
                    << printMBBReference(*Header) << " latch="
                    << printMBBReference(*Latch) << " exit="
                    << printMBBReference(*Exit)
                    << (IsLoopStart ? " (LoopStart)\n" : "\n"));

  // CFG loop blocks (not layout range — Latch may precede Header).
  LoopBlockSet LoopBlocks;
  collectLoopBlocks(Header, Latch, Preheader, LoopBlocks);

  // D1.105: successor rewrite keeps {Header, Exit} plus any extra live
  // exits. Loop-interior successors (a pred of Latch: spacer/back-edge
  // into the latch, e.g. gr27 latch→spacer→latch) are not extra exits.
  // Continue-trampolines that reach only Header are not live exits.
  // True extras (Role-A multi_exit): drop SET, keep Extra, install the
  // software countdown — never fatal, never drop Extra.
  bool KeepExtraSuccs = false;
  {
    unsigned LiveNonHeader = 0;
    for (MachineBasicBlock *S : Latch->successors()) {
      if (!S || S == Header || !isLiveMBB(MF, S))
        continue;
      if (reachesOnlyHeader(S))
        continue;
      if (S->isSuccessor(Latch))
        continue;
      ++LiveNonHeader;
    }
    if (LiveNonHeader > 1) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote keep-extra — latch has "
                        << LiveNonHeader
                        << " live non-Header successors; drop SET, keep Extra, "
                           "install software countdown\n");
      KeepExtraSuccs = true;
    }
  }

  // Actual post-rewrite latch successors: {Header, Exit} plus kept Extra
  // dests. NoSpill / pickDeadLatchScratch / FPL must probe this set —
  // hardcoded {Header, Exit} left Extra-path reads unoccupied (stack
  // window at getFirstTerminator clobbers them). Dropped extras stay
  // out (nsichneu). AIE splitLoopEndJump
  // (AIEBaseHardwareLoops.cpp:232-272) avoids Extra; Haydn D1.105 keeps
  // Extra and passes the kept set into the PostSuccs-restricted probes.
  SmallVector<MachineBasicBlock *, 4> PostRewriteSuccs;
  haydn::hwloop::collectPostRewriteLatchSuccessors(
      *Latch, Header, Exit, KeepExtraSuccs, PostRewriteSuccs);

  // Snapshot soft-loop decision *before* erasing SetMI (operands die with it).
  Register CountReg;
  Register LatchScr; // stack-counter latch scratch (probed spill-free)
  bool InstallSoftLoop = false;
  bool UseStackCounter = false;
  int StackCounterFI = -1;
  // CB-162 value-preserve: set when the live trip value was saved to the
  // demote-save FI and must be reloaded into Prefer at the loop exit.
  bool PendingSaveRestore = false;
  // Occupancy miss (empty 1a/1b LatchScr): last-resort LatchScr=R0 with
  // Header-begin XOR-zero restore. Never Prefer SUBI32/LD32, never pass-2
  // steal, never overlay ST.
  bool RepairSoftZeroR0 = false;
  // Empty LatchScr used to refuse before emission. Product O2 completes a
  // software loop instead of LLVM ERROR. Never SUBI32/LD32 Prefer.
  // CB-165: set when the loop body redefines Prefer (its exit value then
  // originates from that body def, not from the ZOL trip).
  bool PreferRedefinedInBody = false;
  // CB-165: the resolved value-preserve placement (pure decision lives in
  // haydn::hwloop::demoteSavePlacement — gtest seam).
  HwLoopDemoteSaveKind SavePlacement = HwLoopDemoteSaveKind::NoSave;
  int SaveFI = -1;
  MCRegister SaveFrameReg;
  int64_t SaveElem = 0;
  // D1.51 deferred-emission captures (stack-counter arm decision state;
  // the free-counter arm needs none — CountReg/LoopStartAdj suffice).
  Register CounterPreheaderScr;
  Register CounterFrameReg;
  int64_t CounterFIElem = 0;
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFL = ST.getFrameLowering();
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();

  // Dead-after-loop: the D1.61 whole-function fixed point's converged
  // live-in sets of every loop-exit successor. Stored MBB live-ins are
  // stale this late; the one-block walks are superseded by the owner
  // (transitive, guarded-tail transfer, pristines in the seed).
  haydn::hwloop::FunctionPhysLiveness FPL;
  FPL.build(MF);
  auto isLiveAfterLoop = [&](MCPhysReg R) -> bool {
    for (const MachineBasicBlock *B : LoopBlocks) {
      if (!B)
        continue;
      for (const MachineBasicBlock *S : B->successors()) {
        if (LoopBlocks.contains(S))
          continue;
        if (FPL.isLiveIn(*S, R))
          return true;
      }
    }
    return false;
  };

  // CountReg that the preheader tail still owns (va-arg-22 r3=sp+off after
  // SET) is unsound: MOVE32 into it at SET is clobbered before the latch
  // SUBI. isSoundDemoteCounter refuses tail mentions; this also refuses a
  // live-through pred/pre-SET frame-address that the tail walker does not
  // see. Prefer-as-counter and pickCounterReg both consult it so a miss
  // falls through to stack-counter rather than SUBI/BNEZ r3.
  auto countRegUnsoundAtSet =
      [&](Register R, MachineBasicBlock::const_iterator InsPt) -> bool {
    if (!R.isPhysical())
      return false;
    return regMentionedInPreheaderTail(R.asMCReg(), Preheader, InsPt, TRI) ||
           regIsPreheaderTailFrameAddress(R.asMCReg(), Preheader, InsPt, TRI);
  };

  auto canUsePreferAsCounter = [&]() -> bool {
    if (!Prefer.isPhysical() || Prefer == Haydn::R0 || Prefer == Haydn::R13 ||
        Prefer == Haydn::R15)
      return false;
    if (regClobberedNonCountdownIn(Prefer, LoopBlocks))
      return false;
    // D1.64: Prefer is unsound as the countdown if the body still reads it.
    // The clobber check is defs-only (CB-165 save placement); live-after
    // covers exit edges only. Residual ±1 / LoopDec and Latch-block
    // terminator zero-tests of Prefer are not body uses (L2 erases the
    // latter before SUBI32+BNEZ_W).
    if (regUsedNonCountdownIn(Prefer, LoopBlocks, Latch)) {
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote Prefer "
                        << printReg(Prefer, &TRI)
                        << " has non-countdown body use — need "
                           "copy/materialize counter\n");
      return false;
    }
    // Dead-after-loop law (CB-162): the ZOL SET only READS the trip
    // register, but the software demote DESTROYS it (SUBI32 countdown to
    // zero). RA may keep a value live in Prefer across the loop for later
    // users (bkfir16x16 stored descriptor M from the same register the
    // h-fill loop counted down). Prefer is a sound countdown only when it
    // is dead on every exit edge: live-in of each exit-successor block must
    // not contain it. Live-after-exit → the free-counter materialize path
    // below copies the trip into an untouched register instead.
    if (isLiveAfterLoop(Prefer.asMCReg())) {
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote Prefer "
                        << printReg(Prefer, &TRI)
                        << " live after loop exit — need copy/materialize "
                           "counter\n");
      return false;
    }
    // Counter ownership law (calls / callee-saved): see isSoundDemoteCounter.
    // Live range starts at the SET site — RA proved Prefer live up to here;
    // the preheader tail and the loop blocks are demote's responsibility.
    MachineBasicBlock::const_iterator From(
        topLevelForLayout(SetMI).getIterator());
    // Live-through r3=sp+off is not a tail mention; CountReg==Prefer would
    // still SUBI/BNEZ the va_list cursor. Refuse here so pickCounterReg /
    // stack-counter run instead.
    if (countRegUnsoundAtSet(Prefer, From)) {
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote Prefer "
                        << printReg(Prefer, &TRI)
                        << " preheader-tail frame address / mention "
                           "(va-arg-22) — need copy/materialize "
                           "counter\n");
      return false;
    }
    return isSoundDemoteCounter(Prefer.asMCReg(), LoopBlocks, Preheader, From,
                                MF, TRI);
  };

  // D1.88: dedicated counter pool only. Peek until admission so a
  // refused demote does not consume a home. Never fall back to
  // PostRAScratchFI / BranchRelaxationScratchFI (ephemeral alias).
  auto resolveScratchFI = [&]() -> int {
    return FuncInfo->peekHwLoopStackCounterFI();
  };

  if ((IsLoopStart || TII.isHardwareLoopRegTripOpcode(Opc)) &&
      Prefer.isPhysical() && Prefer != Haydn::R0) {
    // Trip reg at LoopStart / SET_HWLOOP_REG.
    // Prefer is correct only if the body neither redefines it as a
    // non-countdown nor still reads it (D1.64). Residual Prefer+=-1 is
    // strippable only when residualCountdownEquivalentAtLatch; a
    // header-side leftover plus a body read is a one-iteration shift.
    // Bundle-preserving: materialize before the SET cycle root, not mid-bundle.
    MachineBasicBlock::iterator Ins =
        topLevelForLayout(SetMI).getIterator();
    // CountReg==Prefer is legal only for Adj==0. In-place ADDI dest==Prefer
    // violates AIE SetLoopCount LC-vs-src and rematerializeAddImmForUse
    // (never Dest==Src); the body may still read the original trip.
    if (canUsePreferAsCounter() && LoopStartAdj == 0) {
      CountReg = Prefer;
      InstallSoftLoop = true;
    } else {
      CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins,
                        DebugPrefix);
      // pickCounterReg tries Prefer first; Adj!=0 needs dest != Prefer.
      if (LoopStartAdj != 0 && CountReg == Prefer)
        CountReg = pickCounterReg(LoopBlocks, Register(), ST, *Preheader, Ins,
                                  DebugPrefix);
      if (LoopStartAdj != 0 && CountReg == Prefer)
        CountReg = Register();
      if (CountReg.isPhysical() && countRegUnsoundAtSet(CountReg, Ins)) {
        LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip CountReg "
                          << printReg(CountReg, &TRI)
                          << " preheader-tail frame address / mention "
                             "(va-arg-22)\n");
        CountReg = Register();
      }
      if (CountReg.isPhysical()) {
        // D1.51 refusal-atomicity: the emit is DEFERRED to the post-
        // preflight emission block; this arm only DECIDES (InstallSoftLoop
        // + the recorded CountReg/Adj shape). Emitting here let every
        // later refusal (Exit==Header / unknown span / no long-latch
        // scratch) leave a materialized trip behind a returned-false.
        InstallSoftLoop = true;
        LLVM_DEBUG(dbgs() << DebugPrefix << ": demote trip "
                          << printReg(Prefer)
                          << " clobbered in body — counter "
                          << printReg(CountReg) << "\n");
      }
    }
  } else if (!IsLoopStart &&
             (TII.isHardwareLoopImmTripOpcode(Opc) || HasImm)) {
    // Imm form: need a free GPR + materialize.
    MachineBasicBlock::iterator Ins =
        topLevelForLayout(SetMI).getIterator();
    CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins,
                        DebugPrefix);
    if (CountReg.isPhysical() && countRegUnsoundAtSet(CountReg, Ins)) {
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip CountReg "
                        << printReg(CountReg, &TRI)
                        << " preheader-tail frame address / mention "
                           "(va-arg-22)\n");
      CountReg = Register();
    }
    if (CountReg.isPhysical()) {
      // D1.51: decision only — the imm materialize is emitted after the
      // preflight (same deferred-emit law as the reg-trip arm above).
      InstallSoftLoop = true;
    }
  }

  // No free body-wide counter: keep trip on a dedicated pre-PEI counter
  // FI and reload each latch with a short-lived scratch (does not steal
  // body physregs; D1.88 — not PostRA/BranchRelaxation scratch).
  if (!InstallSoftLoop) {
    StackCounterFI = resolveScratchFI();
    if (StackCounterFI >= 0 &&
        ((Prefer.isPhysical() && Prefer != Haydn::R0) || HasImm)) {
      MachineBasicBlock::iterator Ins =
          topLevelForLayout(SetMI).getIterator();

      Register FrameReg;
      int64_t Off =
          TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
      // ST32/LD32 take word element indices (imm<<2). A non-simm6 or
      // misaligned FI would need an R0 address temp + XOR-zero; that
      // restore can land after BNEZ_W and the XOR clobbers a live R0.
      // PostRA scratch FI is a small slot — refuse rather than invent
      // an address-temp window.
      if ((Off % 4) != 0 || !isInt<6>(Off / 4)) {
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote refused — stack-counter FI#"
                          << StackCounterFI << " offset " << Off
                          << " is not a word-aligned simm6 element\n");
        return false;
      }
      const int64_t Elem = Off / 4;
      // D1.51: Elem/FrameReg/PreheaderScr are captured for the deferred
      // emission block below the preflight (the arm itself never emits).
      CounterFIElem = Elem;
      CounterFrameReg = FrameReg;

      // Scratch-window soundness: every demote
      // scratch window that touches the counter FI must be spill-free and
      // non-R0, decided BEFORE any mutation.
      //   Latch window: the decremented counter feeds BNEZ_W after the
      //     bracket closes — a NeedsSpill bracket restores the scratch's
      //     original live-through value over it AND beginSpill's spill home
      //     aliases the dedicated counter FI (destroying the stored trip);
      //     an R0 borrow XOR-zeroes it. Both
      //     are silent wrong code (fixed-address / single-pass loop).
      //   Imm-trip preheader window: stores the trip into the counter FI —
      //     a NeedsSpill bracket here aliases the same FI.
      // Probe against the POST-REWRITE latch successors (Header, Exit,
      // kept Extra dests): the pre-rewrite CFG hides the back-edge
      // liveness inside PseudoLoopEnd. No spill-free GPR32NoSPNoLR
      // candidate → occupancy
      // miss last-resorts LatchScr=R0 (soft-zero restore at Header), never
      // a spill bracket, never Prefer SUBI32/LD32.
      const ArrayRef<Register> NoExclude;
      // CB-162 value-preserve: when Prefer is live after the loop (the
      // reason no GPR countdown was sound), Prefer itself is a VALID latch
      // scratch — the demote saves its value to the dedicated save FI
      // before the loop and reloads it at the exit (below). 1a/1b still
      // exclude R0; occupancy miss last-resorts R0 after those miss, with
      // Header-begin XOR-zero restore.
      // PreferLiveAfterLoop is the one-block exit-successor walk (same law
      // as canUsePreferAsCounter), not the Prefer.isPhysical() stub: empty
      // stored live-ins on a BR split-tail must not hide a live-through GPR.
      const bool PreferLiveAfterLoop =
          Prefer.isPhysical() && isLiveAfterLoop(Prefer.asMCReg());
      // CB-165: whether the loop body itself redefines Prefer. When it
      // does, Prefer's exit value originates from that body def (software
      // pipelining routinely assigns the stage-k value to the same physreg
      // the ZOL trip used), and only the demote's own latch-scratch window
      // can destroy it. A preheader save would capture the stale TRIP and
      // the exit restore would overwrite the live body value with it.
      PreferRedefinedInBody =
          Prefer.isPhysical() &&
          regClobberedNonCountdownIn(Prefer, LoopBlocks);
      // D1.64: Prefer is unsound as latch scratch when the body still
      // reads it. CB-162 save/restore returns the EXIT value only; a
      // latch SUBI/LD32 of Prefer would still corrupt in-loop uses.
      const bool PreferHasBodyUse =
          Prefer.isPhysical() &&
          regUsedNonCountdownIn(Prefer, LoopBlocks, Latch);
      // Prefer-only Exclude for pickDeadLatchScratch (pass 1a/1b). FA /
      // tail-imm stay in that picker's skipCommon; feeding the caller's
      // skipLatchScrReason set into 1a/1b over-excludes a genuine dead
      // temp (pjpeg_decode_mcu). NoSpill gets the broader skip set below.
      SmallVector<Register, 8> DeadExcl;
      if (Prefer.isPhysical() &&
          (!PreferLiveAfterLoop || PreferHasBodyUse))
        DeadExcl.push_back(Prefer);
      const MachineRegisterInfo &MRI = MF.getRegInfo();
      LivePhysRegs AtSet(TRI);
      addComputedSuccessorLiveIns(AtSet, *Preheader);
      for (MachineBasicBlock::iterator II = Preheader->end(); II != Ins;) {
        --II;
        AtSet.stepBackward(*II);
      }
      // LivePhysRegs at SET is top-level (bundle interiors after SET are
      // invisible). Seed is FunctionPhysLiveness live-ins of successors
      // (SeedPristines=false; AIE LiveRegs worklist). Stored MBB live-ins
      // are never occupancy authority (D1.71r): FPL already occupies a
      // Header live-in with no Header use, including Header→E second-hop
      // and Header→Latch live-through. Never addLiveOuts pristines.
      // Occupancy is a tail USE whose reaching def is at/before Ins, or
      // LivePhysRegs-live with no tail def: a pure tail def kills SET-site
      // occupancy, including live-out false-positives. Use-then-def stays
      // occupied so PreheaderScr / cell-(d) copy cannot clobber the tail
      // use. Do not restamp occupiedAtSet (D1.71).
      auto occupiedAtSet = [&](Register R) -> bool {
        if (!R.isPhysical())
          return false;
        const bool UseFromSet = regUsedFromSetInPreheaderTail(
            R.asMCReg(), Preheader, Ins, TRI);
        const bool DefInTail =
            regDefdInPreheaderTail(R.asMCReg(), Preheader, Ins, TRI);
        return UseFromSet || (!AtSet.available(MRI, R) && !DefInTail);
      };
      // skipLatchScrReason is NoSpill / last-resort Prefer / NoSpill
      // re-pick only. PreferHasBodyUse ∪ unmentioned FA last-def Exit
      // actually reads ∪ live-into-exit ADDI r0,imm / COPY/MOVE copy-chain
      // (header redef of the ADDI dest must not hide it) ∪ any GPR whose
      // incoming value cfgPathReadsPhysReg cannot prove unread on the Exit
      // path (trampoline Exit empty live-ins + successor ST32). Mentioned
      // FA and PEI dests / SMS-guard ADDI with no Exit-path use stay
      // pass-1a (bqriir). Occupancy stays UseFromSet or (LivePhysRegs-live
      // and not DefInTail) — do not restamp occupiedAtSet (D1.71). Occupancy
      // miss refuses via demoteStackCounterAdmissible before the D1.51
      // barrier (below). AIE SetLoopCount dest is dedicated LC
      // (AIE2InstrInfo.cpp:1338-1346, AIEBaseHardwareLoops.cpp:411-414);
      // Hexagon COPY trip into a new vreg before LOOP_r
      // (HexagonHardwareLoops.cpp:1284-1291). RISC-V insertIndirectBranch
      // scavenges Define|Dead AllowSpill=false (RISCVInstrInfo.cpp:1433-
      // 1471). Haydn innermost CounterInReg=false is the extra Prefer-as-
      // counter hole.
      auto skipLatchScrReason = [&](Register R) -> const char * {
        if (!R.isPhysical())
          return nullptr;
        // PreferHasBodyUse, live-into-exit tail-imm, and unsound FA are
        // independent arms: do not gate tail-imm on unsound (a helper miss
        // must still Exclude $r14 from NoSpill). Header/interior/early-exit
        // BNEZ of Prefer is a body use; latch-terminator zero-tests are
        // skipped inside regUsedNonCountdownIn. Exit-path last so FA /
        // tail-imm keep their named reasons.
        if (PreferHasBodyUse && TRI.regsOverlap(R, Prefer))
          return "non-countdown body use";
        if (regIsLiveIntoExitTailImm(R.asMCReg(), Preheader, Ins, Exit, TRI))
          return "live-into-exit tail-imm ADDI r0,imm / copy-chain (va-arg-22)";
        if (regIsUnsoundLatchScratchAtSet(R.asMCReg(), Preheader, Ins, Exit,
                                           LoopBlocks, TRI))
          return "preheader-tail frame address (va-arg-22)";
        // Unmentioned incoming whose Exit-path read cannot be proven
        // absent (trampoline successor ST32, WAR-in-BUNDLE header
        // ExternUse even when the header also has aggregated Defs).
        // Do not Exclude Prefer here: last-resort LatchScr=Prefer is
        // CB-162 when this predicate is otherwise clear. Mentioned PEI
        // dests / SMS-guard ADDI with no Exit use stay eligible.
        if (R != Haydn::R0 && R != Haydn::R13 && R != Haydn::R15 &&
            !TRI.regsOverlap(R, Prefer) &&
            !regMentionedInBlocks(R, LoopBlocks) &&
            hasIncomingValue(R.asMCReg(), MF)) {
          for (MachineBasicBlock *S : PostRewriteSuccs) {
            if (!S || S == Header || S == Latch)
              continue;
            if (regExitPathReadsPhysReg(R.asMCReg(), S, Preheader, LoopBlocks,
                                        TRI))
              return "unproven-absent Exit-path read";
          }
        }
        return nullptr;
      };
      // NoSpill Exclude is skipLatchScrReason (FA ∪ tail-imm ∪
      // PreferHasBodyUse ∪ Exit-path incoming). findPostRAScratchNoSpill
      // is available-only (LivePhysRegs at latch end vs post-rewrite
      // successors, including kept Extra dests);
      // it is not a live-through steal. Do not collect Prefer/CountReg
      // copy-chain aliases as ExtraExclude — that exhausted NoSpill
      // (hwloop-demote-preheader-tail-scratch). Occupancy is not restamped.
      // pickDeadLatchScratch Exclude stays Prefer-only (DeadExcl): its
      // skipCommon already skips FA/tail-imm/Exit-path. Re-pick applies
      // only to NoSpill hits so a skipLatchScrReason false-positive cannot
      // drop a genuine 1a/1b dead temp.
      SmallVector<Register, 8> NoSpillExcl(DeadExcl.begin(), DeadExcl.end());
      for (MCPhysReg P : Haydn::GPR32NoSPNoLRRegClass) {
        Register Cand(P);
        const char *Why = skipLatchScrReason(Cand);
        if (!Why)
          continue;
        LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip LatchScr "
                          << printReg(P, &TRI) << " " << Why << "\n");
        NoSpillExcl.push_back(Cand);
      }
      auto pickNoSpillLatchScr = [&]() -> Register {
        return findPostRAScratchNoSpill(
            *Latch, Latch->end(), /*PreferNotR12=*/true, PostRewriteSuccs,
            NoSpillExcl);
      };
      LatchScr = pickNoSpillLatchScr();
      while (LatchScr.isPhysical()) {
        if (const char *Why = skipLatchScrReason(LatchScr)) {
          LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip LatchScr "
                            << printReg(LatchScr, &TRI) << " " << Why
                            << "\n");
          NoSpillExcl.push_back(LatchScr);
          LatchScr = pickNoSpillLatchScr();
          continue;
        }
        break;
      }
      const bool LatchScrFromNoSpill = LatchScr.isPhysical();
      if (!LatchScr.isPhysical())
        LatchScr = pickDeadLatchScratch(*Latch, PostRewriteSuccs, LoopBlocks,
                                       DeadExcl, DebugPrefix);
      auto preferLastResortClear = [&]() -> bool {
        return Prefer.isPhysical() && Prefer != Haydn::R0 &&
               Prefer != Haydn::R13 && Prefer != Haydn::R15 &&
               !PreferHasBodyUse && !skipLatchScrReason(Prefer) &&
               !countRegUnsoundAtSet(Prefer, Ins);
      };
      auto skipPreferLastResortDbg = [&]() {
        if (const char *Why = skipLatchScrReason(Prefer))
          LLVM_DEBUG(dbgs() << DebugPrefix
                            << ": demote skip LatchScr=Prefer "
                            << printReg(Prefer, &TRI) << " " << Why << "\n");
        else if (PreferHasBodyUse)
          LLVM_DEBUG(dbgs() << DebugPrefix
                            << ": demote skip LatchScr=Prefer "
                            << printReg(Prefer, &TRI)
                            << " non-countdown body use\n");
      };
      // Adj==0 CB-162 last-resort Prefer is skip-clear. Adj!=0 waits
      // until PreheaderScr is known: Prefer-as-LatchScr without a store
      // dest is occupancy miss, not cell-(e) steal of the full trip.
      if (!LatchScr.isPhysical() && preferLastResortClear() &&
          (LoopStartAdj == 0 || HasImm)) {
        LatchScr = Prefer;
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote latch scratch = Prefer "
                          << printReg(Prefer)
                          << " (value saved to demote-save FI, restored at "
                             "exit)\n");
      } else if (!LatchScr.isPhysical() && Prefer.isPhysical() &&
                 (LoopStartAdj == 0 || HasImm)) {
        skipPreferLastResortDbg();
      }
      if (LatchScr.isPhysical()) {
        // PreferHasBodyUse still refuses Prefer from any seat. NoSpill
        // re-skip is a helper-arm miss net. Do not re-apply
        // skipLatchScrReason to a 1a/1b dead temp — that over-excludes
        // (pjpeg_decode_mcu) after the picker already honored skipCommon.
        if (PreferHasBodyUse && TRI.regsOverlap(LatchScr, Prefer)) {
          LLVM_DEBUG(dbgs() << DebugPrefix
                            << ": demote skip LatchScr=Prefer "
                            << printReg(Prefer, &TRI)
                            << " non-countdown body use\n");
          LatchScr = Register();
        } else if (LatchScrFromNoSpill) {
          if (const char *Why = skipLatchScrReason(LatchScr)) {
            LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip LatchScr "
                              << printReg(LatchScr, &TRI) << " " << Why
                              << "\n");
            LatchScr = Register();
          }
        }
      }
      if (LatchScr.isPhysical())
        LLVM_DEBUG(dbgs() << DebugPrefix << ": demote LatchScr="
                          << printReg(LatchScr, &TRI) << "\n");
      // D1.65: cell-(d) copy and the Adj!=0 extra refuse consult this
      // occupancy, not any-mention (regMentionedInPreheaderTail).
      const bool LatchScrLiveAtSet = occupiedAtSet(LatchScr);
      // Probe a SET-site dest that is neither Prefer nor live in the
      // preheader tail. Dropping the first tail-live hit without a retry
      // left Adj/imm stack-counter with no dest while a later priority
      // GPR was free (va-arg-22). Exclude and re-probe; occupancy miss
      // still refuses rather than SUBI/LD32 Prefer or copying LatchScr.
      auto pickPreheaderScrAtSet = [&](ArrayRef<Register> BaseExclude) {
        SmallVector<Register, 8> Excl(BaseExclude.begin(), BaseExclude.end());
        if (LatchScrLiveAtSet)
          Excl.push_back(LatchScr);
        Register Scr = findPostRAScratchNoSpill(
            *Preheader, Ins, /*PreferNotR12=*/true, {}, Excl);
        while (occupiedAtSet(Scr)) {
          LLVM_DEBUG(dbgs() << DebugPrefix << ": demote PreheaderScr "
                            << printReg(Scr, &TRI)
                            << " live in preheader tail after SET — "
                               "exclude and retry\n");
          Excl.push_back(Scr);
          Scr = findPostRAScratchNoSpill(
              *Preheader, Ins, /*PreferNotR12=*/true, {}, Excl);
        }
        return Scr;
      };
      Register PreheaderScr;
      if (HasImm)
        PreheaderScr = pickPreheaderScrAtSet(
            (Prefer.isPhysical() && !PreferLiveAfterLoop)
                ? ArrayRef<Register>{Prefer} : NoExclude);
      else if (LoopStartAdj != 0) {
        // D1.65: Adj dest must not be Prefer. LatchScr != Prefer can hold
        // Prefer+Adj only when !LatchScrLiveAtSet (occupiedAtSet, not
        // any-mention). Copying a preheader-tail live scratch clobbers the
        // tail. Cell (d) 5-boolean admit is unchanged (D1.19); skipped copy
        // plus the extra refuse below fails closed rather than ST32 the
        // full trip. AIE SetLoopCount dest is dedicated LC
        // (AIE2InstrInfo.cpp:1437, AIEBaseHardwareLoops.cpp:408-414) and
        // never Dest==Src on the trip GPR. No live-through overlay pair.
        PreheaderScr = pickPreheaderScrAtSet(
            Prefer.isPhysical() ? ArrayRef<Register>{Prefer} : NoExclude);
        if (!PreheaderScr.isPhysical() && LatchScr.isPhysical() &&
            LatchScr != Prefer && !LatchScrLiveAtSet)
          PreheaderScr = LatchScr;
        else if (!PreheaderScr.isPhysical() && LatchScrLiveAtSet)
          LLVM_DEBUG(dbgs() << DebugPrefix
                            << ": demote skip LatchScr->PreheaderScr "
                            << printReg(LatchScr, &TRI)
                            << " live at SET / preheader tail\n");
      }
      // D1.51: captured for the deferred emission block (see Elem above).
      CounterPreheaderScr = PreheaderScr;
      // Adj!=0 last-resort Prefer only when a PreheaderScr can hold
      // Prefer+Adj. Suffocation (no LatchScr, no PreheaderScr) is
      // occupancy miss, never LatchScr=Prefer steal of the full trip.
      if (!LatchScr.isPhysical() && preferLastResortClear() &&
          LoopStartAdj != 0 && !HasImm && PreheaderScr.isPhysical()) {
        LatchScr = Prefer;
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote latch scratch = Prefer "
                          << printReg(Prefer)
                          << " (Adj!=0 with PreheaderScr; value saved to "
                             "demote-save FI, restored at exit)\n");
      } else if (!LatchScr.isPhysical() && Prefer.isPhysical() &&
                 LoopStartAdj != 0 && !HasImm) {
        if (!PreheaderScr.isPhysical())
          LLVM_DEBUG(dbgs()
                     << DebugPrefix
                     << ": occupancy miss (Adj!=0 + no LatchScr + no "
                        "PreheaderScr); refuse LatchScr=Prefer\n");
        else
          skipPreferLastResortDbg();
      }
      // Empty LatchScr is occupancy miss (cell (a)). Never steal a
      // live-through, never SUBI32/LD32 Prefer, never overlay. Product
      // O2 last-resorts LatchScr=R0 (soft-zero, reserved, not in
      // GPR32NoSPNoLR) and restores XOR-zero at Header begin after the
      // latch window. Fixup demoteOrFatalOccupancyMiss stays the backstop
      // if this arm still cannot admit.
      if (!LatchScr.isPhysical()) {
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": occupancy miss ("
                          << (PreferHasBodyUse ? "PreferHasBodyUse + no "
                                                 "LatchScr"
                                               : "no skip-clear LatchScr")
                          << "); last-resort LatchScr=R0, restore "
                             "soft-zero at Header\n");
        LatchScr = Haydn::R0;
        RepairSoftZeroR0 = true;
      }
      // D1.19: admission is the closed case matrix in
      // haydn::hwloop::demoteStackCounterAdmissible (single law, gtest
      // seam). Cell (e) — Adj!=0 with no PreheaderScr and LatchScr==Prefer
      // — used to pass this gate and fall through to StoreSrc = Prefer,
      // ST32ing the FULL trip while the kernel runs Prefer+Adj = N-S.
      if (!haydn::hwloop::demoteStackCounterAdmissible(
              LatchScr.isPhysical(), HasImm, PreheaderScr.isPhysical(),
              LoopStartAdj != 0, LatchScr == Prefer)) {
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote refused — no spill-free non-R0 "
                             "scratch for stack-counter windows (latch "
                          << (LatchScr.isPhysical() ? "ok" : "NONE")
                          << ", preheader "
                          << (!HasImm || PreheaderScr.isPhysical() ? "ok"
                                                                   : "NONE")
                          << ", adj-store "
                          << (HasImm || LoopStartAdj == 0 ||
                                      PreheaderScr.isPhysical() ||
                                      LatchScr != Prefer
                                  ? "ok"
                                  : "NONE (Adj!=0, LatchScr==Prefer)")
                          << "); never a spill bracket over the counter\n");
        return false;
      }
      // D1.65: Cell (d) admits Adj!=0 with LatchScr!=Prefer assuming the
      // copy above produced PreheaderScr. If the copy was skipped because
      // LatchScr is occupied at SET, refuse rather than ST32 the full trip.
      // Occupancy is this extra gate, not a 6th demoteStackCounterAdmissible
      // input. Do not restamp occupiedAtSet as any-mention.
      if (LoopStartAdj != 0 && !HasImm && !PreheaderScr.isPhysical()) {
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote refused — Adj!=0 stack-counter has "
                             "no PreheaderScr dead at SET (not LatchScr "
                             "live in the preheader tail)\n");
        return false;
      }

      // D1.150: CB-162 save home is a dedicated per-latch pool FI.
      // Placement is decided here; peek/resolve runs after every D1.51
      // refusal (join/bypass, min-count, residual) so a refused demote
      // never take()s. Never PostRAScratchFI / BranchRelaxationScratchFI
      // / any counter-pool member. Frame deadline: no post-PEI
      // CreateStackObject. AIE has no stack save
      // (AIE2InstrInfo.cpp:1344 LCRegister=AIE2::LC).
      //
      // GR2.1: Prefer save home is resolved only when SavePlacement !=
      // NoSave (latch scratch IS Prefer). Skip-overlay is skip-only debug;
      // a NoSave demote must not be refused on a slot it never addresses
      // (bqriir32x32_df1).
      SavePlacement = demoteSavePlacement(LatchScr == Prefer,
                                          PreferRedefinedInBody);
      // Skip-only debug: no SET-site ST32 / Exit LD32 of LatchScr != Prefer.
      // Pass-1a/1b LatchScr is a dead temp. Occupancy miss last-resorts R0.
      if (LatchScr.isPhysical() && LatchScr != Prefer)
        LLVM_DEBUG(dbgs() << DebugPrefix << ": demote skip overlay of "
                          << printReg(LatchScr, &TRI) << "\n");
      // CB-162/CB-165 value-preserve law. The save/restore pair exists to
      // return to the exit the value Prefer must carry OUT of the loop.
      // Two sound shapes:
      //  * Prefer NOT redefined in the body AND Prefer is the latch
      //    scratch (the only thing that destroys the surviving trip):
      //    save the trip in the preheader, restore at exit (CB-162).
      //  * Prefer redefined in the body AND Prefer is the latch scratch:
      //    the value that must survive is the body's final def, so the save
      //    must execute at latch end (below, before the scratch window) —
      //    a preheader save would capture the stale trip.
      // When the latch scratch is a different register, nothing the demote
      // installs touches Prefer: no save, no restore. The old unconditional
      // pair reloaded the preheader trip over a live loop-carried body def
      // (SMS epilogue value) — CB-165 (pr51581-2 -O2: c[N-1] = trip).
      // (HasImm forms have no live Prefer to preserve.)
      // Placement was decided above, before the save home was resolved
      // (the home is only law-checked when a pair will be installed); the
      // deferred emission block below translates it into MIs.
      // D1.51 refusal-atomicity: NOTHING is emitted inside the decision
      // arms. The preheader save ST32, the imm materialize+ST32, and the
      // Adj ADDI+ST32 all moved to the post-preflight emission block —
      // a refusal downstream of this point once left a stored trip (and
      // a clobbered PreheaderScr) behind a returned-false.
      PendingSaveRestore = !HasImm && SavePlacement != HwLoopDemoteSaveKind::NoSave;

      UseStackCounter = true;
      InstallSoftLoop = true;
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote stack-counter FI#"
                        << StackCounterFI << "\n");
    }
  }

  if (!InstallSoftLoop) {
    // Live body, no free counter / unusable trip — refuse erase-only.
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote refused — no free counter "
                         "GPR for soft edge (body still live)\n");
    return false;
  }

  // D1.51 PREFLIGHT (refusal-atomicity; the pure-predicate seam class of
  // demoteStackCounterAdmissible): every decision that can still refuse
  // runs HERE, on the untouched function, before the first MIR or CFG
  // mutation. Previously the three decisions below sat AFTER
  // stripResidualCountdown / eraseHardwareLoopSetup / the latch terminator
  // sweep / the successor rewrite (and, in the two counter arms, after the
  // trip materialize / counter-FI store emissions): a refusal returned
  // false over a half-demoted function — SET gone, latch edges already
  // {Header, Exit}, trip state materialized — violating the pipeline
  // repair theorem and corrupting any retry/nonfatal caller. All three
  // consume only read-only measurements (estimateLayout* one-byte walks,
  // isBranchOffsetInRange, the D1.61 FunctionPhysLiveness fixed point).
  // Erase/strip are size-conservative: PseudoLoopEnd is isMeta (zero-size);
  // erased latch terminators sit at the walked Latch END so the site offset
  // is unchanged; erased SET lives in the PREHEADER, outside Header..Latch;
  // stripped residual countdowns live INSIDE the span, so the pre-strip
  // span is the LARGER one and a LongLatch flip from strip is conservative.
  // D1.149: that walk is not the post-emission backedge. Unrotated
  // Header-begin..Latch-end grows by in-span parcels this demote inserts
  // before the short BNEZ site (charged onto BackedgeDisp below). The
  // decisions stay hoisted — same order, same debug text, same laws —
  // only their position moved.
  //
  // D1.139: proven remaining trip below MinCount. Software latch is
  // test-at-bottom (SUBI32+BNEZ); N=0 would run the body once then wrap
  // ~2^32. Golden COUNT>=1 at activation and END-then-check
  // (VLIW_Engine_Compiler_Constraints.md:105-107). Do not invent skip-body
  // or wrap. AIE rejects min-iter at formation, not demote
  // (AIEBaseTargetTransformInfo.cpp:44-46/:322-331 MinIterCountHLReject=1;
  // AIEBaseHardwareLoops.cpp:311-316 early-return). Hexagon COPY trip
  // (HexagonHardwareLoops.cpp:1284-1291) and extends LOOP
  // (HexagonFixupHwLoops.cpp:137-144). RISC-V has no HWLoop TU
  // (RISCVInstrInfo.cpp:1433-1471 is D1.72, not COUNT). Haydn TTI already
  // overlays (HaydnTargetTransformInfo.cpp:422-430). Reuse
  // countMeetsMinLaw; do not refuse unproven reg/LoopStart remaining.
  // Over-field imm still demotes (GPR software counter). SET/CFG untouched.
  if (HasImm && !haydn::hwloop::countMeetsMinLaw(Imm)) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote refused — COUNT below MinCount (remaining="
                      << Imm << " MinCount=" << haydn::hwloop::MinCount
                      << ")\n");
    return false;
  }
  // D1.32 Exit==Header refusal: both latch edges would return to the
  // header (the BEQZ "exit" edge is the backedge; the JALR edge is the
  // backedge) — there is no exit path at all, so the counted loop cannot
  // terminate. Fail-closed before any latch emission.
  if (Exit == Header) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote refused — Exit == Header has no exit "
                         "path for the soft latch\n");
    return false;
  }
  // D1.72 join-exit: PendingSaveRestore reloads Prefer at the dedicated
  // exit begin. A join/bypass pred of the original Exit would execute
  // that LD32 without this loop's matching save. GR1.4 splits Latch→Exit
  // into a pred_size==1 block at the emission barrier (AIE
  // splitLoopEndJump AIEBaseHardwareLoops.cpp:232-272; RISC-V
  // insertIndirectBranch RestoreBB pred_size==1 RISCVInstrInfo.cpp:1433-1443).
  // Do not refuse here — the split is the first mutation past D1.51.
  // D1.34 signed-span law: the latch-end BNEZ site to the header start
  // is a SIGNED displacement on the one byte walk —
  //   * Header precedes Latch (unrotated): BACKWARD, the negated
  //     Header-begin..Latch-end span;
  //   * Latch precedes Header (MachineBlockPlacement rotation, embench
  //     nsichneu): FORWARD, the Latch-end..Header-begin distance on the
  //     SAME walk (entering-MBB pad included).
  // The old flat reading consumed the rotated sentinel as -(-1) = +1: an
  // unmeasured magnitude that masqueraded as short (a far-rotated
  // backedge then installed a BNEZ the post-stamp BR had to promote —
  // the CFG-wall class). A flat refusal was equally wrong: the
  // expand-path caller (expandRoleALoopStarts) treats a refused demote
  // as terminal on a body-resolvable shape, a fail-closed compile error
  // on legal rotated loops. With Header/Latch proven live above, exactly
  // one direction is measurable; only a dead/foreign block (neither walk
  // resolves) is an UNKNOWN span, and that refuses fail-closed.
  int64_t BackedgeDisp = 0;
  const int64_t BackedgeBytes = haydn::hwloop::estimateLayoutSpanBytes(
      MF, Header, Latch, TII);
  if (BackedgeBytes >= 0) {
    BackedgeDisp = -BackedgeBytes;
  } else {
    const int64_t ForwardBytes = haydn::hwloop::estimateLayoutMBBDistance(
        MF, Latch, Latch->end(), Header, TII);
    if (ForwardBytes < 0) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — unknown backedge span "
                           "(neither direction measurable; dead or foreign "
                           "block); never consumed as a short displacement\n");
      return false;
    }
    BackedgeDisp = ForwardBytes;
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote rotated backedge measured FORWARD "
                      << ForwardBytes << "B (latch precedes header)\n");
  }
  // D1.149 unrotated in-span emission growth: charge only parcels this
  // demote will insert before the short backedge site on Header-precedes-
  // Latch. Stack LD32+SUBI32+ST32 (3), LatchEndSave ST32 (1), occupancy-
  // miss XOR at Header begin (1). Bytes = productBundlesToBytes(Parcels);
  // BackedgeDisp -= that amount (more negative). Do not pre-add
  // BranchRelaxSafetyBuffer (D1.33 single-charge stays inside the TII
  // oracle). Do not add MaxHwLoopDemoteGrowthBytes /
  // PreS1PostStampGrowthBytes (D1.35 other-site net-new, including
  // preheader/exit). Do not charge free-arm SUBI32 (D1.33 pre-countdown).
  // Do not charge exit-B (after the BNEZ PC). Do not charge rotated
  // ForwardBytes (latch-end inserts shrink that walk). KeepExtra /
  // occupancy-miss LatchScr=R0 / no LongScr still force short BNEZ and
  // later BR; dedicated-exit remains Wave 4. Occupancy-miss LatchScr=R0
  // implies no computed-dead LongScr, so charging XOR cannot install
  // LongLatch on that arm. AIE splitLoopEndJump
  // (AIEBaseHardwareLoops.cpp:232-272) has no stack-counter/save path
  // and no BranchRelaxation, so no LongLatch-vs-emission-growth analog;
  // Wave 4 ports splitLoopEndJump at HardwareLoops entry. HexagonBranchRelaxation.cpp:108-
  // 111 charges known extender size into the offset map, then :164-166
  // Distance=|offset|+BranchRelaxSafetyBuffer once;
  // HexagonFixupHwLoops.cpp:137-144 measures actual InstOffset. RISC-V
  // has no HWLoop TU; RISCVInstrInfo.cpp:1433-1471 is D1.72, not a span
  // law. isBranchOffsetInRange remains the only buffer seat.
  if (BackedgeBytes >= 0) {
    unsigned Parcels = 0;
    if (UseStackCounter)
      Parcels += 3;
    if (PendingSaveRestore &&
        SavePlacement == HwLoopDemoteSaveKind::LatchEndSave)
      Parcels += 1;
    if (RepairSoftZeroR0)
      Parcels += 1;
    if (Parcels != 0) {
      const int64_t Growth = productBundlesToBytes(Parcels);
      BackedgeDisp -= Growth;
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote unrotated in-span growth " << Growth
                        << "B (" << Parcels
                        << " parcels) charged into backedge\n");
    }
  }
  // D1.33: TII.isBranchOffsetInRange is the single seat that inflates by
  // getBranchRelaxSafetyBuffer(). D1.149 already charged unrotated in-span
  // demote growth into BackedgeDisp; do not add the buffer here.
  bool LongLatch =
      !TII.isBranchOffsetInRange(Haydn::BNEZ_W, BackedgeDisp);
  // D1.32 scratch law: the JALR link scratch must be provably dead on every
  // post-rewrite latch out-edge (Header, Exit, kept Extra dests). JALR_W
  // DEFINES the scratch (link discard); any value live into a successor
  // would be silently destroyed every backedge — the free-counter arm's
  // countdown register (CountReg) is exactly such a value: it is a
  // loop-carried live-in of Header until the trip is exhausted. The
  // retired identity shortcut (LongScr = CountReg) both clobbered the
  // trip and (through the emitted order) let BEQZ test the LUI-clobbered
  // scratch instead of the countdown.
  //
  // Probe law (computed, never stored MBB livein lists — stale this late
  // after BranchRelaxation split tails): D1.61 consults the
  // whole-function physical-liveness fixed point
  // (haydn::hwloop::FunctionPhysLiveness) — alias-aware, edge-aware,
  // transitive — instead of the per-successor one-block walks. A
  // candidate is dead when it is not live-in of a post-rewrite out-edge.
  // Never the countdown register (CountReg / LatchScr),
  // never R0 (soft-zero), never R13 (SP) / R15 (LR), never reserved.
  Register LongScr;
  if (LongLatch) {
    // BOTH arms use the SAME fresh computed-dead probe. The countdown
    // register (CountReg in the free arm, LatchScr in the stack arm) is
    // read by BEQZ AFTER the LUI/ADDI32_W scratch writes in the corrected
    // order, so the scratch can never alias it (the stack arm's old
    // identity reuse LatchScr==LongScr made BEQZ test the HI12-clobbered
    // scratch — D1.32 defect 2). LatchScr's own dead-after-final-read
    // probe does not help: "dead after the window" says nothing about a
    // clobber BETWEEN the LD/SUBI/ST sequence and the final read.
    // D1.61: every allocatable legal GPR — the retired list omitted
    // R5/R6 (the fragmentation this owner closes); order among
    // proven-dead registers is QoR only.
    static const MCPhysReg LongCands[] = {
        Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8, Haydn::R7,
        Haydn::R6,  Haydn::R5,  Haydn::R4,  Haydn::R3, Haydn::R2,
        Haydn::R1,  Haydn::R12};
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    const Register CountdownReg = UseStackCounter ? LatchScr : CountReg;
    // Post-rewrite latch out-edges are PostRewriteSuccs: {Header, Exit}
    // plus kept Extra dests. Header==Latch bodies can still carry extra
    // early-exit successors this preflight that the rewrite DROPS —
    // those stay out of PostRewriteSuccs (KeepExtraSuccs=false) so they
    // cannot refuse a legal LongScr (nsichneu). Kept Extra dests are
    // obligations. The D1.61 owner answers the set through
    // liveUnderPostRewriteSuccessors (self entry is the restricted-
    // successor transfer, AIE LivePhysRegs::stepBackward iterated).
    // FPL is the single preflight
    // build shared with isLiveAfterLoop — no mutation sits between the
    // two seats (D1.51 emission barrier is below).
    SmallVector<const MachineBasicBlock *, 4> PostSuccs(
        PostRewriteSuccs.begin(), PostRewriteSuccs.end());
    for (MCPhysReg R : LongCands) {
      if (R == CountdownReg || R == Prefer || R == Haydn::R0 ||
          R == Haydn::R13 || R == Haydn::R15)
        continue;
      if (MRI.isReserved(R))
        continue;
      if (FPL.liveUnderPostRewriteSuccessors(*Latch, PostSuccs, R))
        continue;
      LongScr = R;
      break;
    }
    if (!LongScr) {
      // No computed-dead JALR scratch (nsichneu extra_03 @benchmark_body).
      // Do not fatal. HardwareLoops / Fixup both sit before a BranchRelaxation
      // that owns far conds. Fall back to short BNEZ; later BR relaxes.
      // Dedicated-exit split stays GR1.2/GR1.4. Never a silent spill arm.
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": no computed-dead GPR for the long-latch "
                           "JALR scratch — short BNEZ, later BR relaxes\n");
      LongLatch = false;
    } else {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote long-latch scratch "
                        << printReg(LongScr, &TRI) << " (computed-dead on "
                        << printMBBReference(*Header) << " and "
                        << printMBBReference(*Exit) << "; countdown "
                        << printReg(CountdownReg, &TRI) << ")\n");
    }
  }
  // D1.105 keep-extra + long-latch: extra conds are terminators; the
  // GR2.7 LUI/ADDI template is non-terminator and cannot follow them
  // (verifier). Dedicated-exit split is GR1.2/GR1.4 — skip. Product: drop
  // SET, keep Extra, short BNEZ (later BR), do not fatal, do not install
  // HWLOOP.
  if (KeepExtraSuccs && LongLatch) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": keep-extra with long latch — short BNEZ after "
                         "Extra, later BR; dedicated-exit is GR1.2/GR1.4\n");
    LongLatch = false;
  }

  // Header-side leftover ±1 of the register that will receive the latch
  // SUBI32 (CountReg, or LatchScr on the stack arm) plus a non-countdown
  // use is a one-iteration value shift. Prove latch-equivalence on the
  // untouched function (D1.51). Do not consult Prefer when Prefer is not
  // that register — Prefer is then a live IV, not a leftover of the
  // software counter.
  {
    const Register StripReg = UseStackCounter ? LatchScr : CountReg;
    if (StripReg.isPhysical() &&
        !residualCountdownEquivalentAtLatch(LoopBlocks, StripReg, Latch)) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — residual countdown of "
                        << printReg(StripReg, &TRI)
                        << " is not latch-equivalent\n");
      return false;
    }
  }

  // D1.150 / D1.154: last preflight step for an actual
  // PreheaderSave/LatchEndSave. Peek only (no take). Empty pool refuses —
  // CreateStackObject after PEI is a contract break. Product LoopStart
  // reserved a slot at PEI; formed-ZOL/refused still must not take it.
  // JOIN/bypass already refused above, so a refused demote never peeks
  // a home it will not bind. AIE has no stack save
  // (AIE2InstrInfo.cpp:1333-1346 LCRegister=AIE2::LC).
  if (PendingSaveRestore) {
    if (!TFL || !FuncInfo) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — no frame info for demote-save "
                           "home\n");
      return false;
    }
    const int Peeked = FuncInfo->peekHwLoopDemoteSaveFI();
    const int FI = resolveDemoteSaveHome(
        Peeked, FuncInfo->getPostRAScratchFI(),
        FuncInfo->getBranchRelaxationScratchFI(),
        FuncInfo->getHwLoopStackCounterPool());
    if (FI < 0) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — no dedicated demote-save FI "
                           "disjoint from PostRAScratchFI, "
                           "BranchRelaxationScratchFI, and every "
                           "stack-counter pool member\n");
      return false;
    }
    Register SaveFrameRegReg;
    int64_t SaveOff =
        TFL->getFrameIndexReference(MF, FI, SaveFrameRegReg).getFixed();
    if ((SaveOff % 4) != 0 || !isInt<6>(SaveOff / 4) ||
        SaveFrameRegReg != CounterFrameReg) {
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — save FI#" << FI << " offset "
                        << SaveOff
                        << " not a word-aligned simm6 element on the "
                           "counter frame register\n");
      return false;
    }
    SaveFI = FI;
    SaveFrameReg = SaveFrameRegReg;
    SaveElem = SaveOff / 4;
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote save home disjoint SaveFI#" << SaveFI
                      << " PostRAScratchFI#"
                      << FuncInfo->getPostRAScratchFI()
                      << " BranchRelaxationScratchFI#"
                      << FuncInfo->getBranchRelaxationScratchFI()
                      << " stack-counter FI#" << StackCounterFI << "\n");
  }

  // D1.154: Header/Latch/Exit must still be live before take+bind. The
  // post-erase early success-return used to skip the save/restore pair
  // check after the pool was consumed.
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch) || !isLiveMBB(MF, Exit)) {
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote refused — Header/Latch/Exit not live "
                         "at the emission barrier\n");
    return false;
  }

  // === D1.51 EMISSION BARRIER ==========================================
  // Every decision above is final: no refusal is reachable past this
  // point. The deferred trip-state emissions from the two counter arms
  // run first — same inserts, same order, same operands as when they
  // lived inside the decision arms — then the original mutation order is
  // preserved exactly.
  if (PendingSaveRestore &&
      (Exit->pred_size() != 1 || *Exit->pred_begin() != Latch))
    Exit = splitDedicatedLatchExit(*Latch, *Exit, TII);
  {
    MachineBasicBlock::iterator Ins = topLevelForLayout(SetMI).getIterator();
    if (!UseStackCounter) {
      // Free-counter arm. Two sub-shapes shared this arm:
      //   * reg trip (LoopStart / SET_HWLOOP_REG): CountReg == Prefer means
      //     canUsePreferAsCounter() won — the value is already in place,
      //     nothing to materialize; otherwise ADDI Prefer+Adj (Adj!=0 needs
      //     dest != Prefer) or a MOVE32 copy.
      //   * imm trip (SET_HWLOOP imm, never LoopStart, Adj==0): XOR-zero +
      //     ADDI32_W of the constant into CountReg (Prefer is invalid, so
      //     the CountReg != Prefer guard is vacuous here).
      if (HasImm)
        materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, Imm,
                             /*HasImm=*/true);
      else if (CountReg != Prefer) {
        if (LoopStartAdj != 0)
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W,
                           CountReg, [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Prefer).addImm(LoopStartAdj);
                           });
        else
          materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, 0,
                               /*HasImm=*/false);
      }
    } else {
      // D1.88 / D1.150: assign the peeked dedicated counter and save
      // homes only past the D1.51 barrier so a refused demote does not
      // consume either pool.
      {
        const int Taken = FuncInfo->takeHwLoopStackCounterFI();
        if (Taken < 0 || Taken != StackCounterFI)
          report_fatal_error(
              "Haydn: hwloop demote assigned a dedicated counter FI that "
              "does not match the peeked pre-PEI pool home",
              /*GenCrashDiag=*/false);
        StackCounterFI = Taken;
        FuncInfo->bindHwLoopStackCounterFI(Latch, Taken);
      }
      if (PendingSaveRestore) {
        const int TakenSave = FuncInfo->takeHwLoopDemoteSaveFI();
        if (TakenSave < 0 || TakenSave != SaveFI)
          report_fatal_error(
              "Haydn: hwloop demote assigned a dedicated save FI that "
              "does not match the peeked pre-PEI pool home",
              /*GenCrashDiag=*/false);
        SaveFI = TakenSave;
        FuncInfo->bindHwLoopDemoteSaveFI(Latch, TakenSave);
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote save home take+bind SaveFI#" << SaveFI
                          << " latch " << printMBBReference(*Latch) << "\n");
      }
      // Stack-counter arm, preheader group (original order):
      //   1. CB-162 value-preserve save ST32 (PreheaderSave placement).
      if (PendingSaveRestore &&
          SavePlacement == HwLoopDemoteSaveKind::PreheaderSave) {
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(Prefer).addReg(SaveFrameReg)
                            .addImm(SaveElem)
                            .addMemOperand(MF.getMachineMemOperand(
                                MachinePointerInfo::getFixedStack(
                                    MF, SaveFI),
                                MachineMemOperand::MOStore, 4,
                                MF.getFrameInfo().getObjectAlign(SaveFI)));
                      });
      }
      //   2. counter-FI store: imm materialize into the probed spill-free
      //      PreheaderScr then ST32, or the reg-trip Prefer(+Adj) store.
      //      No withPostRAScratch bracket: its NeedsSpill home aliases
      //      this same counter FI; the decision-time probe is the single
      //      mechanism that picked PreheaderScr.
      if (HasImm) {
        materializeTripCount(*Preheader, Ins, DL, TII, CounterPreheaderScr,
                             Prefer, Imm, /*HasImm=*/true);
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(CounterPreheaderScr, getKillRegState(true))
                            .addReg(CounterFrameReg)
                            .addImm(CounterFIElem)
                            .addMemOperand(MF.getMachineMemOperand(
                                MachinePointerInfo::getFixedStack(
                                    MF, StackCounterFI),
                                MachineMemOperand::MOStore, 4,
                                MF.getFrameInfo().getObjectAlign(
                                    StackCounterFI)));
                      });
      } else {
        // Prefer holds trip at SET; store remaining kernel trip Prefer+Adj.
        // ADDI into scratch; leave Prefer for CB-162/165 save.
        Register StoreSrc = Prefer;
        unsigned StoreFlags = 0;
        if (LoopStartAdj != 0 && CounterPreheaderScr.isPhysical() &&
            CounterPreheaderScr != Prefer) {
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W,
                           CounterPreheaderScr,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Prefer).addImm(LoopStartAdj);
                           });
          StoreSrc = CounterPreheaderScr;
          StoreFlags = getKillRegState(true);
        }
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(StoreSrc, StoreFlags)
                            .addReg(CounterFrameReg)
                            .addImm(CounterFIElem)
                            .addMemOperand(MF.getMachineMemOperand(
                                MachinePointerInfo::getFixedStack(
                                    MF, StackCounterFI),
                                MachineMemOperand::MOStore, 4,
                                MF.getFrameInfo().getObjectAlign(
                                    StackCounterFI)));
                      });
      }
    }
  }

  // Strip leftover ±1 of the register that receives the latch SUBI32,
  // before erasing SET / rewriting the latch. Walk every CFG loop block:
  // a proven CountReg+=-1 can sit in the header of a multi-BB diamond.
  // Prefer is a live IV when Prefer != that register — never strip it
  // on the stack arm (A-5).
  {
    const Register StripReg = UseStackCounter ? LatchScr : CountReg;
    if (StripReg.isPhysical())
      stripResidualCountdown(LoopBlocks, StripReg, TII);
  }

  // L1: erase hardware setup (SET + PLE)
  eraseHardwareLoopSetup(SetMI, DebugPrefix);
  // SetMI is gone; do not touch it again.

  // D1.154: eraseHardwareLoopSetup does not kill Header/Latch/Exit.
  // Do not return true past take+bind without the pair check; a dead
  // block here is a hard diagnostic, not a successful demote.
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch) || !isLiveMBB(MF, Exit)) {
    if (isLiveMBB(MF, Latch)) {
      if (std::string SaveViolation =
              haydn::hwloop::demoteSaveHomePairViolation(*Latch);
          !SaveViolation.empty())
        report_fatal_error(Twine("hwloop demote: ") + SaveViolation,
                           /*GenCrashDiag=*/false);
    }
    report_fatal_error("hwloop demote: Header/Latch/Exit not live after "
                       "setup erase",
                       /*GenCrashDiag=*/false);
  }

  // L2: rewrite latch to software counted back-edge
  if (!UseStackCounter) {
    auto ensureLiveIn = [](MachineBasicBlock *MBB, Register R) {
      if (!R.isPhysical() || !MBB)
        return;
      if (!MBB->isLiveIn(R))
        MBB->addLiveIn(R);
    };
    for (const MachineBasicBlock *B : LoopBlocks)
      ensureLiveIn(const_cast<MachineBasicBlock *>(B), CountReg);
    ensureLiveIn(Header, CountReg);
    ensureLiveIn(Latch, CountReg);
  }

  // Drop latch terminators, including BUNDLE interiors. A BUNDLE root is
  // not itself a terminator, so a top-level-only walk leaves
  // `BUNDLE { B Exit }` in place; later exact-commit SUBI32/BNEZ_W then
  // append after that first terminator (verifier break). Walk instrs() so
  // the interior B/cond/PLE is erased and the empty shell is dropped.
  // D1.105 keep-extra: leave conds that target Extra (SUBI32 is inserted
  // before the first terminator; counted BNEZ is appended after Extra).
  SmallVector<MachineInstr *, 8> Terms;
  for (MachineInstr &MI : Latch->instrs()) {
    if (MI.getOpcode() == TargetOpcode::BUNDLE)
      continue;
    if (!MI.isTerminator())
      continue;
    if (!KeepExtraSuccs) {
      Terms.push_back(&MI);
      continue;
    }
    if (MI.getOpcode() == Haydn::PseudoLoopEnd ||
        MI.getOpcode() == Haydn::LoopJNZ) {
      Terms.push_back(&MI);
      continue;
    }
    MachineBasicBlock *Dest = uncondBranchTarget(MI);
    if (!Dest) {
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isMBB()) {
          Dest = MO.getMBB();
          break;
        }
      }
    }
    if (Dest == Header || (Dest == Exit && MI.isUnconditionalBranch()))
      Terms.push_back(&MI);
  }
  for (MachineInstr *MI : Terms)
    eraseInstrSafe(MI);

  if (KeepExtraSuccs) {
    if (!Latch->isSuccessor(Header))
      Latch->addSuccessor(Header);
    if (Exit != Header && !Latch->isSuccessor(Exit))
      Latch->addSuccessor(Exit);
  } else {
    while (!Latch->succ_empty())
      Latch->removeSuccessor(Latch->succ_begin());
    Latch->addSuccessor(Header);
    if (Exit != Header)
      Latch->addSuccessor(Exit);
  }
  // Wave 4 H / GR2.10: CFG-changing demote is refused while the snapshot
  // is stamped (preflight above). No L5 admitted-transition record.

  // GR2.7 long-latch decision + D1.32/D1.33/D1.34 span & scratch laws all
  // ran in the preflight above (D1.51); only their decisions (LongLatch,
  // LongScr, BackedgeDisp) are consumed here. See the preflight block for
  // the laws.

  // Emit the GR2.7 terminal in-block long latch: address materialization,
  // near BEQZ to Exit, unconditional long backedge. Used by both counter
  // arms when LongLatch; reuses the already-emitted countdown/store.
  auto emitLongLatch = [&]() {
    // D1.37 identity ratchet: LongScr must not alias the countdown
    // (UseStackCounter ? LatchScr : CountReg). Preflight already skips
    // CountdownReg; this is the GOALS emission gate after mutations have
    // started — never a second picker and never return-false. Matches the
    // RISC-V insertIndirectBranch dead-link law (dest Define|Dead; a
    // live-out is never the JALR scratch).
    const Register CountdownReg = UseStackCounter ? LatchScr : CountReg;
    if (LongScr == CountdownReg)
      report_fatal_error("hwloop demote: long-latch JALR scratch aliases the "
                         "countdown",
                         /*GenCrashDiag=*/false);
    // D1.32 captured-iterator law: every emission takes ONE captured,
    // monotonically advanced insert point (never a re-evaluated
    // getFirstTerminator(), which lands each MI BEFORE previously
    // inserted terminators and produced the historical ADDI32,LUI,BEQZ,
    // JALR misorder: ADDI32_W read its LUI def's LO12 from garbage and
    // BEQZ_W tested the HI12-clobbered scratch instead of the countdown).
    // Final in-block order (the ONLY MachineVerifier-legal layout — the
    // generic "non-terminator after the first terminator" law forbids
    // LUI/ADDI after the BEQZ terminator, singleton BUNDLE roots
    // included; same shape as HaydnLongBranchNormalize Arm A):
    //   LUI(scr, Header) ; ADDI32_W(scr, scr, Header) ;
    //   BEQZ_W(countdown, Exit) ; JALR_W(scr, scr, 0).
    // Correctness laws this order fixes:
    //   * LUI strictly precedes ADDI32_W — the golden HI12/LO20
    //     +0x80000 pairing (HaydnRelocLayout RelocTrans::Hi12/Lo20; peer
    //     emitMBBAddr) computes HI12 first, then LO20 on the FULL value.
    //   * BEQZ_W reads the countdown register AFTER both scratch writes —
    //     the exit test is the true trip test, never the clobbered
    //     scratch; the scratch is a DIFFERENT register by the D1.32
    //     computed-dead probe, so the writes cannot touch the countdown.
    //   * JALR_W stays the final terminator (rs+imm12 register-indirect
    //     full address); its link write to the scratch is invisible on
    //     both out-edges by the same probe.
    // Packets execute in order, so no same-cycle RAW is introduced.
    MachineBasicBlock::iterator Ins = Latch->end();
    Ins = std::next(emitExactLateDef(*Latch, Ins, DL, TII, Haydn::LUI,
                                     LongScr,
                                     [&](MachineInstrBuilder MIB) {
                                       MIB.addMBB(Header);
                                     })
                         ->getIterator());
    Ins = std::next(emitExactLateDef(*Latch, Ins, DL, TII, Haydn::ADDI32_W,
                                     LongScr,
                                     [&](MachineInstrBuilder MIB) {
                                       MIB.addReg(LongScr).addMBB(Header);
                                     })
                         ->getIterator());
    Ins = std::next(emitExactLate(*Latch, Ins, DL, TII, Haydn::BEQZ_W,
                                  [&](MachineInstrBuilder MIB) {
                                    MIB.addReg(UseStackCounter ? LatchScr
                                                               : CountReg)
                                        .addMBB(Exit);
                                  })
                         ->getIterator());
    emitExactLate(*Latch, Ins, DL, TII, Haydn::JALR_W,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(LongScr, RegState::Define)
                        .addReg(LongScr)
                        .addImm(0);
                  });
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote GR2.7 long-latch LUI+ADDI32_W+BEQZ_W+"
                         "JALR_W on scratch "
                      << printReg(LongScr, &TRI) << " (backedge displacement "
                      << BackedgeDisp << "B over simm12)\n");
  };

  if (UseStackCounter) {
    Register FrameReg;
    int64_t Off =
        TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
    // D1.36 emission wall: admission already refused a bad Off (return
    // false above). If that gate drifts, Release must not emit LD32/ST32
    // against a truncated or misaligned element — the vanished NDEBUG
    // assert is now unconditional report_fatal_error.
    if ((Off % 4) != 0 || !isInt<6>(Off / 4))
      report_fatal_error("hwloop demote: stack-counter FI offset is not a "
                         "word-aligned simm6 element",
                         /*GenCrashDiag=*/false);
    const int64_t Elem = Off / 4;
    // Insert before any leftover terminator so LD/SUBI/ST stay in the
    // body (never after BNEZ_W / B). After the terminator sweep above
    // this is Latch->end().
    MachineBasicBlock::iterator LatchEnd = Latch->getFirstTerminator();
    // Stack-counter path builds several real MIs; exact-commit each singleton
    // so second BR / Verify see committed FormatID cycles (no residual bare
    // SUBI32/BNEZ_W for the late firewall to invent).
    //
    // Closed order: no scratch bracket at all in the latch.
    // LatchScr was probed spill-free and non-R0 against the POST-REWRITE
    // successors (Header, Exit, kept Extra dests) at decision time — a
    // withPostRAScratch
    // bracket here is structurally wrong twice: its restore (endSpill /
    // XOR-zero) lands between the decremented counter and BNEZ_W, and its
    // NeedsSpill home aliases the dedicated counter FI, destroying the
    // stored trip. Terminators come last; nothing follows BNEZ_W except the
    // optional exit branch. Large-FI R0 address-temp is refused above.
    //
    // CB-165 body-def value-preserve: when Prefer is redefined in the body
    // AND doubles as the latch scratch, the exit's live value is the body's
    // final def of Prefer — the scratch LD32 below is about to destroy it.
    // Save it here, at latch end, before the scratch window opens (the
    // preheader save shape would capture the stale trip instead). The
    // matching exit reload is PendingSaveRestore below.
    if (PendingSaveRestore &&
        SavePlacement == HwLoopDemoteSaveKind::LatchEndSave) {
      emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                    [&](MachineInstrBuilder MIB) {
                      MIB.addReg(Prefer).addReg(SaveFrameReg)
                          .addImm(SaveElem)
                          .addMemOperand(MF.getMachineMemOperand(
                              MachinePointerInfo::getFixedStack(MF, SaveFI),
                              MachineMemOperand::MOStore, 4,
                              MF.getFrameInfo().getObjectAlign(SaveFI)));
                    });
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote saved body-def "
                           << printReg(Prefer) << " to save FI#" << SaveFI
                        << " at latch end\n");
    }
    emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, LatchScr,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(FrameReg).addImm(Elem)
                           .addMemOperand(MF.getMachineMemOperand(
                               MachinePointerInfo::getFixedStack(
                                   MF, StackCounterFI),
                               MachineMemOperand::MOLoad, 4,
                               MF.getFrameInfo().getObjectAlign(
                                   StackCounterFI)));
                     });
    // Final-real countdown: counter -= 1; store back. No terminator here.
    emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::SUBI32, LatchScr,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(LatchScr).addImm(1);
                     });
    emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(LatchScr).addReg(FrameReg).addImm(Elem)
                        .addMemOperand(MF.getMachineMemOperand(
                            MachinePointerInfo::getFixedStack(
                                MF, StackCounterFI),
                            MachineMemOperand::MOStore, 4,
                            MF.getFrameInfo().getObjectAlign(
                                StackCounterFI)));
                  });
    // Counter read happens before the store that may follow on the exit
    // path; branch on the register, not on memory.
    if (LongLatch) {
      emitLongLatch();
    } else {
      emitExactLate(*Latch,
                    KeepExtraSuccs ? Latch->end()
                                   : Latch->getFirstTerminator(),
                    DL, TII, Haydn::BNEZ_W,
                    [&](MachineInstrBuilder MIB) {
                      MIB.addReg(LatchScr).addMBB(Header);
                    });
    }
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote stack-counter exact-commit "
                         "SUBI32+BNEZ_W FI#"
                      << StackCounterFI << "\n");
  } else {
    // Final-real soft edge: SUBI32 count,count,1 + BNEZ_W count, Header.
    // Residual countdown was stripped above; never double-dec.
    // Exact-commit each edge as a product singleton before second BR.
    // LongLatch swaps the short backedge for the terminal in-block
    // BEQZ-to-Exit + LUI+ADDI32_W+JALR_W form (GR2.7; see the hoisted
    // decision above).
    emitExactLateDef(*Latch, Latch->getFirstTerminator(), DL, TII,
                     Haydn::SUBI32, CountReg,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(CountReg).addImm(1);
                     });
    if (LongLatch) {
      emitLongLatch();
    } else {
      emitExactLate(*Latch,
                    KeepExtraSuccs ? Latch->end()
                                   : Latch->getFirstTerminator(),
                    DL, TII, Haydn::BNEZ_W,
                    [&](MachineInstrBuilder MIB) {
                      MIB.addReg(CountReg).addMBB(Header);
                    });
    }
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote exact-commit SUBI32+BNEZ_W on "
                      << printReg(CountReg) << "\n");
  }

  MachineFunction::iterator LatchIt = Latch->getIterator();
  MachineFunction::iterator NextIt = std::next(LatchIt);
  bool ExitIsLayoutFallthrough =
      (NextIt != MF.end()) && (&*NextIt == Exit);
  if (!ExitIsLayoutFallthrough && Exit != Header && !LongLatch) {
    // Exact-commit the unconditional exit edge so second BR charges
    // committed EncodedBytes. B has no PlacementAlternatives (wrap-only
    // Format E singleton commit). Exit stays the B CFG shell: uncond
    // latch/exit is B in gMIR (BEQZ-on-R0 is encode-only). BranchRelaxation
    // must find the latch analyzable (BNEZ_W conditional + B
    // barrier-unconditional). A direct BEQZ_W would be a SECOND
    // conditional after the counted back-edge and fixupConditionalBranch
    // asserts ("branches to be relaxed must be analyzable", nsichneu).
    // GR2.7 LongLatch already emits the near BEQZ_W exit edge inside the
    // latch template — an extra B after the unconditional JALR_W
    // backedge would be unreachable and unanalyzable.
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::B,
                  [&](MachineInstrBuilder MIB) { MIB.addMBB(Exit); });
  }

  // CB-162/CB-165 value-preserve: reload the saved live value into Prefer at
  // exit entry — the latch countdown (or Prefer-as-latch-scratch) destroyed
  // the in-register copy. D1.72 preflight required Exit pred_size()==1 and
  // the sole predecessor is Latch, so this Exit->begin() LD32 is not a
  // join/bypass unmatched restore. Exact-commit singleton at Exit begin.
  if (PendingSaveRestore && isLiveMBB(MF, Exit)) {
    emitExactLateDef(*Exit, Exit->begin(), DL, TII, Haydn::LD32, Prefer,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(SaveFrameReg).addImm(SaveElem)
                           .addMemOperand(MF.getMachineMemOperand(
                               MachinePointerInfo::getFixedStack(MF, SaveFI),
                               MachineMemOperand::MOLoad, 4,
                               MF.getFrameInfo().getObjectAlign(SaveFI)));
                     });
    if (!Exit->isLiveIn(Prefer))
      Exit->addLiveIn(Prefer);
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote restored live trip "
                      << printReg(Prefer) << " from save FI#" << SaveFI
                      << " at exit " << printMBBReference(*Exit) << "\n");
  }

  // Occupancy-miss last-resort LatchScr=R0: the latch window leaves R0
  // holding the remaining count (taken back-edge) or zero (exit). Soft-zero
  // law requires XOR-zero at Header begin so the body still reads R0 as 0.
  // Extra dests see a dirty R0 if KeepExtra took the early-exit after SUBI.
  // Exact-commit: Fixup sits before the last BR.
  if (RepairSoftZeroR0) {
    auto restoreR0 = [&](MachineBasicBlock *BB) {
      if (!isLiveMBB(MF, BB))
        return;
      emitExactLateDef(*BB, BB->begin(), DL, TII, Haydn::XOR32, Haydn::R0,
                       [&](MachineInstrBuilder MIB) {
                         MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                       });
    };
    restoreR0(Header);
    if (KeepExtraSuccs) {
      for (MachineBasicBlock *S : Latch->successors()) {
        if (S != Header && S != Exit)
          restoreR0(S);
      }
    }
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote restored soft-zero R0 at Header after "
                         "occupancy-miss LatchScr=R0\n");
  }

  // D1.36: ONE shared complete-order latch predicate (HaydnHWLoopDemote),
  // not a second in-producer machine. RequireLatch=true so limited
  // -run-pass pipelines that skip Verify still fail closed; the product
  // seat (HaydnVerifyBundles) walks every MBB with the default (false)
  // and the countdown-vocabulary discriminator.
  if (std::string LatchViolation =
          haydn::hwloop::countedSoftwareLatchViolation(*Latch,
                                                       /*RequireLatch=*/true);
      !LatchViolation.empty()) {
    report_fatal_error(Twine("hwloop demote: ") + LatchViolation,
                       /*GenCrashDiag=*/false);
  }
  if (std::string SaveViolation =
          haydn::hwloop::demoteSaveHomePairViolation(*Latch);
      !SaveViolation.empty()) {
    report_fatal_error(Twine("hwloop demote: ") + SaveViolation,
                       /*GenCrashDiag=*/false);
  }

  return true;
}
