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
//        expandRoleALoopStarts — preflight retained metadata/CFG/body, then
//          LoopStart → SET_HWLOOP_F2_W (product sel domain {0,1}) with
//          intervening setup pads, or reject incomplete seats fail-closed
//          before any mutation (fatal).
//   4. This pass owns software-loop demotion: if the retained body already
//      cannot encode Off2, the soft edge is installed here (before layout).
//   5. HaydnFixupHwLoops (pre-emit, flag-gated) pads/range-checks and calls
//      llvm::demoteHardwareLoopToSoftware (this TU) plus the shared
//      HaydnHWLoopDemote helpers for already-committed wide forms. Residual
//      generic SET_HWLOOP{,_REG} fatals at Fixup.
//
// Post-RA soft-branch convert and its reconstruction helpers are permanently
// deleted from this TU.
//
//===----------------------------------------------------------------------===//

#include "HaydnHardwareLoops.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFormatERecords.h"
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
using llvm::haydn::hwloop::MinBodyBundles;
using llvm::haydn::hwloop::MinSetupBytes;
using llvm::haydn::hwloop::MinSetupBundles;
using llvm::haydn::hwloop::SetupIssueDistance;
using llvm::haydn::bundle::ceilProductParcels;
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
     << Why;
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
// is legal CFG and illegal ZOL.
static void placeLoopBlocksForZOL(MachineBasicBlock *Preheader,
                                  MachineBasicBlock *Header,
                                  MachineBasicBlock *Latch,
                                  const haydn::hwloop::LoopBlockSet &Blocks) {
  if (!Preheader || !Header || !Latch || Header == Latch)
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

  SmallVector<MachineBasicBlock *, 8> Order;
  Order.push_back(Header);
  Order.append(Interiors.begin(), Interiors.end());
  Order.push_back(Latch);

  SmallDenseMap<MachineBasicBlock *, MachineBasicBlock *, 8> PrevFall;
  PrevFall[Preheader] = layoutSucc(Preheader);
  for (MachineBasicBlock *BB : Order)
    PrevFall[BB] = layoutSucc(BB);

  MachineBasicBlock *After = Preheader;
  for (MachineBasicBlock *BB : Order) {
    if (std::next(After->getIterator()) != BB->getIterator())
      BB->moveAfter(After);
    After = BB;
  }

  Preheader->updateTerminator(PrevFall[Preheader]);
  for (MachineBasicBlock *BB : Order)
    BB->updateTerminator(PrevFall[BB]);
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
  for (MachineBasicBlock::const_iterator I = std::next(SetMI.getIterator()),
                                         E = Preheader->end();
       I != E; ++I) {
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

    {
      unsigned FollowingBundles = countFollowingSizeBearing(*SetMI, *TII);
      if (FollowingBundles < HWLoopSetupPadBundles) {
        unsigned Deficit = HWLoopSetupPadBundles - FollowingBundles;
        MachineBasicBlock::iterator AfterSet =
            std::next(SetMI->getIterator());
        for (unsigned I = 0; I < Deficit; ++I)
          BuildMI(*Preheader, AfterSet, DL, TII->get(Haydn::NOP));
      }
    }

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

    if (Adj != 0) {
      Register Count =
          rematerializeAddImmForUse(*SetMI, /*UseOpIdx=*/3, Adj);
      (void)Count;
    }

    qualifyExpandedRoleA(LS, *SetMI, Header, PLE, LoopBlocks, *TII);

    LS->eraseFromParent();
    ++NumRoleAExpanded;
    Changed = true;
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
  Changed |= expandRoleALoopStarts(MF);
  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Role A expand-only complete\n");
  return Changed;
}


//===----------------------------------------------------------------------===//
// Formation-owned software-loop demotion / setup erase
//
// Encodability is decided here (or the soft edge is installed) before layout
// lock-in. Late Fixup may call the same helpers for already-committed wide
// forms. Residual generic SET_HWLOOP{,_REG} is not rewritten here or in Fixup.
//===----------------------------------------------------------------------===//
// MI-level demote/erase helpers shared with HaydnFixupHwLoops live in
// HaydnHWLoopDemote.{h,cpp} (single owner; previously duplicated verbatim).
// Body resolution here is CFG-only via resolveBodyMBBCore: formation must
// never resolve a body from layout order — incomplete retained state rejects
// fail-closed (resolveRoleABody law above).
using haydn::hwloop::LoopBlockSet;
using haydn::hwloop::collectLoopBlocks;
using haydn::hwloop::demoteSavePlacement;
using haydn::hwloop::resolveLoopStartLatch;
using haydn::hwloop::emitExactLate;
using haydn::hwloop::emitExactLateDef;
using haydn::hwloop::eraseInstrSafe;
using haydn::hwloop::eraseSetMemberAndRecommitSiblings;
using haydn::hwloop::isLiveMBB;
using haydn::hwloop::isSoundDemoteCounter;
using haydn::hwloop::materializeTripCount;
using haydn::hwloop::blockLiveInContains;
using haydn::hwloop::blockLiveInContainsFromSuccessors;
using haydn::hwloop::pickCounterReg;
using haydn::hwloop::regClobberedNonCountdownIn;
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

  // Snapshot soft-loop decision *before* erasing SetMI (operands die with it).
  Register CountReg;
  Register LatchScr; // stack-counter latch scratch (probed spill-free)
  bool InstallSoftLoop = false;
  bool UseStackCounter = false;
  int StackCounterFI = -1;
  // CB-162 value-preserve: set when the live trip value was saved to the
  // demote-save FI and must be reloaded into Prefer at the loop exit.
  bool PendingSaveRestore = false;
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
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();

  // Dead-after-loop: one-block computed live-ins of every loop-exit
  // successor (haydn::hwloop::blockLiveInContains). Stored MBB live-ins
  // are stale this late; do not keep a third copy of the walk.
  auto isLiveAfterLoop = [&](MCPhysReg R) -> bool {
    for (const MachineBasicBlock *B : LoopBlocks) {
      if (!B)
        continue;
      for (const MachineBasicBlock *S : B->successors()) {
        if (LoopBlocks.contains(S))
          continue;
        if (blockLiveInContains(*S, R))
          return true;
      }
    }
    return false;
  };

  auto canUsePreferAsCounter = [&]() -> bool {
    if (!Prefer.isPhysical() || Prefer == Haydn::R0 || Prefer == Haydn::R13 ||
        Prefer == Haydn::R15)
      return false;
    if (regClobberedNonCountdownIn(Prefer, LoopBlocks))
      return false;
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
    return isSoundDemoteCounter(Prefer.asMCReg(), LoopBlocks, Preheader, From,
                                MF, TRI);
  };

  auto resolveScratchFI = [&]() -> int {
    int FI = FuncInfo->getBranchRelaxationScratchFI();
    if (FI < 0)
      FI = FuncInfo->getPostRAScratchFI();
    return FI;
  };

  if ((IsLoopStart || TII.isHardwareLoopRegTripOpcode(Opc)) &&
      Prefer.isPhysical() && Prefer != Haydn::R0) {
    // Trip reg at LoopStart / SET_HWLOOP_REG.
    // Prefer is correct only if the body does not redefine it as a
    // non-countdown (e.g. S_LW_POST dest = trip). Residual Prefer+=-1 is OK
    // we strip it below and install a single SUBI32 dec.
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
    if (CountReg.isPhysical()) {
      // D1.51: decision only — the imm materialize is emitted after the
      // preflight (same deferred-emit law as the reg-trip arm above).
      InstallSoftLoop = true;
    }
  }

  // No free body-wide counter: keep trip on a post-RA scratch FI and reload
  // each latch with a short-lived scratch (does not steal body physregs).
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
      //     resolves to the same BranchRelaxation/PostRA FI as the counter
      //     (destroying the stored trip); an R0 borrow XOR-zeroes it. Both
      //     are silent wrong code (fixed-address / single-pass loop).
      //   Imm-trip preheader window: stores the trip into the counter FI —
      //     a NeedsSpill bracket here aliases the same FI.
      // Probe against the POST-REWRITE latch successors {Header, Exit}:
      // the pre-rewrite CFG hides the back-edge liveness inside
      // PseudoLoopEnd. No spill-free candidate → refuse demote (caller
      // fatal ladder), never fall back to a spill bracket.
      const ArrayRef<Register> NoExclude;
      // CB-162 value-preserve: when Prefer is live after the loop (the
      // reason no GPR countdown was sound), Prefer itself is a VALID latch
      // scratch — the demote saves its value to the dedicated save FI
      // before the loop and reloads it at the exit (below). The only
      // exclusion stays R0 (XOR-zero clobbers the soft-zero law).
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
      LatchScr = findPostRAScratchNoSpill(
          *Latch, Latch->end(), /*PreferNotR12=*/true, {Header, Exit},
          (Prefer.isPhysical() && !PreferLiveAfterLoop)
              ? ArrayRef<Register>{Prefer} : NoExclude);
      if (!LatchScr.isPhysical() && Prefer.isPhysical() &&
          Prefer != Haydn::R0 && Prefer != Haydn::R13 &&
          Prefer != Haydn::R15) {
        LatchScr = Prefer;
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote latch scratch = Prefer "
                          << printReg(Prefer)
                          << " (value saved to demote-save FI, restored at "
                             "exit)\n");
      }
      Register PreheaderScr;
      if (HasImm)
        PreheaderScr = findPostRAScratchNoSpill(
            *Preheader, Ins, /*PreferNotR12=*/true, {},
            (Prefer.isPhysical() && !PreferLiveAfterLoop)
                ? ArrayRef<Register>{Prefer} : NoExclude);
      else if (LoopStartAdj != 0) {
        // Adj dest must not be Prefer. Missing scratch is not a refuse —
        // LatchScr != Prefer can hold Prefer+Adj; copy already had its chance.
        PreheaderScr = findPostRAScratchNoSpill(
            *Preheader, Ins, /*PreferNotR12=*/true, {},
            Prefer.isPhysical() ? ArrayRef<Register>{Prefer} : NoExclude);
        if (!PreheaderScr.isPhysical() && LatchScr.isPhysical() &&
            LatchScr != Prefer)
          PreheaderScr = LatchScr;
      }
      // D1.51: captured for the deferred emission block (see Elem above).
      CounterPreheaderScr = PreheaderScr;
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

      // CB-162 value-preserve slot: share the PostRAScratchFI word when it
      // is disjoint from the counter FI (counter prefers the dedicated
      // BranchRelaxation scratch). The rare both-map-to-PostRA case uses the
      // dedicated HwLoopDemoteSaveFI reserved pre-PEI
      // (HaydnFrameLowering::processFunctionBeforeFrameFinalized — frame
      // deadline: no post-PEI CreateStackObject). Same word-aligned simm6
      // element law either way.
      //
      // GR2.1 fix: the save home is resolved and law-checked ONLY when a
      // save/restore pair will actually be installed (SavePlacement !=
      // NoSave, i.e. the latch scratch IS Prefer — the one window that
      // destroys the value the exit must see). Every consumer below
      // (preheader ST32, latch-end ST32, exit LD32) is guarded by the same
      // placement; a demote whose latch scratch is a different register
      // touches nothing near Prefer and must not be refused (or fatal) over
      // a slot it never addresses. First live case: pre-RA Kind-A SMS on
      // bqriir32x32_df1 — the prologue-peel grows Off1 past uimm6, demote
      // picks a non-Prefer spill-free latch scratch, and the old
      // unconditional check refused on the deeply negative PostRA offset
      // (element outside simm6), leaving no legal compiler exit.
      SavePlacement = demoteSavePlacement(LatchScr == Prefer,
                                          PreferRedefinedInBody);
      if (SavePlacement != HwLoopDemoteSaveKind::NoSave) {
        const int PostRASaveFI = FuncInfo->getPostRAScratchFI();
        if (PostRASaveFI >= 0 && PostRASaveFI != StackCounterFI) {
          SaveFI = PostRASaveFI;
        } else {
          SaveFI = FuncInfo->getHwLoopDemoteSaveFI();
          if (SaveFI < 0) {
            // Fail closed: the pre-PEI reservation should have covered every
            // function whose hwloop setup survived to this post-RA pass.
            // Reaching here means a setup opcode appeared after frame
            // finalization — a pipeline-contract violation, not a slot miss.
            report_fatal_error(
                "Haydn: hwloop demote needs HwLoopDemoteSaveFI after frame "
                "finalization (pre-PEI reservation missed a live setup)",
                /*GenCrashDiag=*/false);
          }
        }
        Register SaveFrameRegReg;
        int64_t SaveOff = TFL->getFrameIndexReference(MF, SaveFI,
                                                      SaveFrameRegReg)
                              .getFixed();
        if ((SaveOff % 4) != 0 || !isInt<6>(SaveOff / 4) ||
            SaveFrameRegReg != FrameReg) {
          LLVM_DEBUG(dbgs() << DebugPrefix
                            << ": demote refused — save FI#" << SaveFI
                            << " offset " << SaveOff
                            << " not a word-aligned simm6 element on the "
                               "counter frame register\n");
          return false;
        }
        SaveFrameReg = SaveFrameRegReg;
        SaveElem = SaveOff / 4;
      }
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
  // isBranchOffsetInRange, blockLiveInContains), which are invariant under
  // every mutation the demote itself performs (PseudoLoopEnd is isMeta —
  // zero-size; the erased latch terminators sit at the walked Latch END, so
  // the Latch-end site offset is the same before and after the sweep; the
  // erased SET lives in the PREHEADER, outside the Header..Latch span; the
  // stripped residual countdowns live INSIDE the span, so the pre-strip
  // span is the LARGER one — any LongLatch flip is in the conservative,
  // always-encodable direction). The decisions are hoisted VERBATIM — same
  // order, same debug text, same laws — only their position moved.
  //
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
  // D1.33: raw signed displacement only — the TII oracle is the single
  // seat that inflates by getBranchRelaxSafetyBuffer().
  const bool LongLatch =
      !TII.isBranchOffsetInRange(Haydn::BNEZ_W, BackedgeDisp);
  // D1.32 scratch law: the JALR link scratch must be provably dead on BOTH
  // post-rewrite latch out-edges {Header, Exit}. JALR_W DEFINES the scratch
  // (link discard); any value live into either successor would be silently
  // destroyed every backedge — the free-counter arm's countdown register
  // (CountReg) is exactly such a value: it is a loop-carried live-in of
  // Header until the trip is exhausted. The retired identity shortcut
  // (LongScr = CountReg) both clobbered the trip and (through the emitted
  // order) let BEQZ test the LUI-clobbered scratch instead of the
  // countdown.
  //
  // Probe law (computed, never stored MBB livein lists — stale this late
  // after BranchRelaxation split tails): a candidate is dead when the
  // one-block LivePhysRegs walk (computeBlockLiveIns) of BOTH successors
  // excludes it. Never the countdown register (CountReg / LatchScr),
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
    static const MCPhysReg LongCands[] = {
        Haydn::R11, Haydn::R10, Haydn::R9,  Haydn::R8, Haydn::R7,
        Haydn::R4,  Haydn::R3,  Haydn::R2,  Haydn::R1, Haydn::R12};
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    const Register CountdownReg = UseStackCounter ? LatchScr : CountReg;
    // Post-rewrite latch out-edges are {Header, Exit}. Header==Latch
    // bodies can still carry extra early-exit successors at this
    // preflight (Role-A refused them; the latch rewrite drops them).
    // Walking Header with those extra edges occupies every GPR and
    // refuses a legal LongScr — the nsichneu compile-fatal class.
    const MachineBasicBlock *PostSuccsArr[] = {Header, Exit};
    ArrayRef<const MachineBasicBlock *> PostSuccs = PostSuccsArr;
    for (MCPhysReg R : LongCands) {
      if (R == CountdownReg || R == Prefer || R == Haydn::R0 ||
          R == Haydn::R13 || R == Haydn::R15)
        continue;
      if (MRI.isReserved(R))
        continue;
      if (blockLiveInContains(*Exit, R))
        continue;
      if (Header != Latch) {
        if (blockLiveInContains(*Header, R))
          continue;
      } else if (blockLiveInContainsFromSuccessors(*Latch, PostSuccs, R)) {
        continue;
      }
      LongScr = R;
      break;
    }
    if (!LongScr) {
      // Fail-closed: no computed-dead GPR at a far-latch demote. An
      // FI-spill fallback would add per-iteration parcels beyond the
      // MaxHwLoopDemoteGrowthParcels vocabulary bound (D1.35: 13 after
      // the long-latch template re-enumeration); refusal is the recorded
      // decision, never a silent spill arm.
      LLVM_DEBUG(dbgs() << DebugPrefix
                        << ": demote refused — no computed-dead GPR for "
                           "the long-latch JALR scratch\n");
      return false;
    }
    LLVM_DEBUG(dbgs() << DebugPrefix
                      << ": demote long-latch scratch "
                      << printReg(LongScr, &TRI) << " (computed-dead on "
                      << printMBBReference(*Header) << " and "
                      << printMBBReference(*Exit) << "; countdown "
                      << printReg(CountdownReg, &TRI) << ")\n");
  }

  // === D1.51 EMISSION BARRIER ==========================================
  // Every decision above is final: no refusal is reachable past this
  // point. The deferred trip-state emissions from the two counter arms
  // run first — same inserts, same order, same operands as when they
  // lived inside the decision arms — then the original mutation order is
  // preserved exactly.
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
      // Stack-counter arm, preheader group (original order):
      //   1. CB-162 value-preserve save ST32 (PreheaderSave placement).
      if (!HasImm && SavePlacement == HwLoopDemoteSaveKind::PreheaderSave) {
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(Prefer).addReg(SaveFrameReg)
                            .addImm(SaveElem);
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
                            .addImm(CounterFIElem);
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
                            .addImm(CounterFIElem);
                      });
      }
    }
  }

  // Strip residual countdown of CountReg *before* erasing SET / rewriting
  // the latch, while the body is still intact. Walk every CFG loop block:
  // a proven Prefer+=-1 can sit in the header of a multi-BB diamond.
  if (!UseStackCounter)
    stripResidualCountdown(LoopBlocks, CountReg);
  else if (Prefer.isPhysical())
    stripResidualCountdown(LoopBlocks, Prefer);

  // L1: erase hardware setup (SET + PLE)
  eraseHardwareLoopSetup(SetMI, DebugPrefix);
  // SetMI is gone; do not touch it again.

  // Re-validate live blocks after erase (should still be live).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch) || !isLiveMBB(MF, Exit))
    return true;

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

  // Drop every latch terminator, including BUNDLE interiors. A BUNDLE
  // root is not itself a terminator, so a top-level-only walk leaves
  // `BUNDLE { B Exit }` in place; later exact-commit SUBI32/BNEZ_W
  // then append after that first terminator (verifier break). Walk
  // instrs() so the interior B/cond/PLE is erased and the empty shell
  // is dropped. Then the soft edge is the only terminator sequence.
  SmallVector<MachineInstr *, 8> Terms;
  for (MachineInstr &MI : Latch->instrs()) {
    if (MI.getOpcode() == TargetOpcode::BUNDLE)
      continue;
    if (MI.isTerminator())
      Terms.push_back(&MI);
  }
  for (MachineInstr *MI : Terms)
    eraseInstrSafe(MI);

  while (!Latch->succ_empty())
    Latch->removeSuccessor(Latch->succ_begin());
  Latch->addSuccessor(Header);
  if (Exit != Header)
    Latch->addSuccessor(Exit);
  // D1.40 Phase 2 / L5 admitted transition: this latch successor rewrite
  // is the ONE post-stamp successor mutation in the seat graph
  // (FixupHwLoops at addPreEmitPass and the HaydnLateConvergence inner
  // loop demote post-stamp; an unconditional edge freeze would false-fire
  // on every demote). Record the SOURCE block of the rewrite so the
  // postCommitCfgCreationViolation edge digest admits exactly this
  // transition. No-op unless the wall is armed (pre-stamp demotes at
  // addPreSched2 record nothing — there is no snapshot to diverge from),
  // and never a second recording site for any other successor mutation.
  if (FuncInfo->hasPostCommitBlockBudget())
    FuncInfo->recordPostCommitAdmittedEdgeTransition(MF, *Latch);

  // GR2.7 long-latch decision + D1.32/D1.33/D1.34 span & scratch laws all
  // ran in the preflight above (D1.51); only their decisions (LongLatch,
  // LongScr, BackedgeDisp) are consumed here. See the preflight block for
  // the laws.

  // Emit the GR2.7 terminal in-block long latch: address materialization,
  // near BEQZ to Exit, unconditional long backedge. Used by both counter
  // arms when LongLatch; reuses the already-emitted countdown/store.
  auto emitLongLatch = [&]() {
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
    // D1.36 one admission seat: the word-aligned simm6 law was already
    // enforced UNCONDITIONALLY at decision time above (the stack-counter
    // arm is only entered after that refusal gate); the former assert
    // mirror here was a second, weaker (assertions-only) copy.
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
    // successors {Header, Exit} at decision time — a withPostRAScratch
    // bracket here is structurally wrong twice: its restore (endSpill /
    // XOR-zero) lands between the decremented counter and BNEZ_W, and its
    // NeedsSpill home aliases the counter FI itself (both resolve
    // BranchRelaxationScratchFI ?: PostRAScratchFI), destroying the stored
    // trip. Terminators come last; nothing follows BNEZ_W except the
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
                          .addImm(SaveElem);
                    });
      LLVM_DEBUG(dbgs() << DebugPrefix << ": demote saved body-def "
                           << printReg(Prefer) << " to save FI#" << SaveFI
                        << " at latch end\n");
    }
    emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, LatchScr,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(FrameReg).addImm(Elem);
                     });
    // Final-real countdown: counter -= 1; store back. No terminator here.
    emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::SUBI32, LatchScr,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(LatchScr).addImm(1);
                     });
    emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(LatchScr).addReg(FrameReg).addImm(Elem);
                  });
    // Counter read happens before the store that may follow on the exit
    // path; branch on the register, not on memory.
    if (LongLatch) {
      emitLongLatch();
    } else {
      emitExactLate(*Latch, Latch->getFirstTerminator(), DL, TII,
                    Haydn::BNEZ_W,
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
      emitExactLate(*Latch, Latch->getFirstTerminator(), DL, TII,
                    Haydn::BNEZ_W,
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
  // Do not leave a bare B for late Finalize — exact-commit the
    // unconditional exit edge so second BR charges committed EncodedBytes.
    // B has no PlacementAlternatives (wrap-only Format E singleton commit).
    // Exit stays the B CFG shell here: BranchRelaxation must find the
    // latch analyzable (BNEZ_W conditional + B barrier-unconditional).
    // A direct BEQZ_W would be a SECOND conditional after the counted
    // back-edge and fixupConditionalBranch asserts ("branches to be
    // relaxed must be analyzable", nsichneu). The closure Finalize
    // expands B to BEQZ_W_MSP after the last BR.
    // GR2.7 LongLatch already emits the near BEQZ_W exit edge inside the
    // latch template — an extra B after the unconditional JALR_W
    // backedge would be unreachable and unanalyzable.
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::B,
                  [&](MachineInstrBuilder MIB) { MIB.addMBB(Exit); });
  }

  // CB-162/CB-165 value-preserve: reload the saved live value into Prefer at
  // exit entry — the latch countdown (or Prefer-as-latch-scratch) destroyed
  // the in-register copy. Exact-commit singleton at Exit begin, before any
  // of Exit's own code, so every later user reads the original value.
  if (PendingSaveRestore && isLiveMBB(MF, Exit)) {
    emitExactLateDef(*Exit, Exit->begin(), DL, TII, Haydn::LD32, Prefer,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(SaveFrameReg).addImm(SaveElem);
                     });
    if (!Exit->isLiveIn(Prefer))
      Exit->addLiveIn(Prefer);
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote restored live trip "
                      << printReg(Prefer) << " from save FI#" << SaveFI
                      << " at exit " << printMBBReference(*Exit) << "\n");
  }

  // D1.36 product-build order-aware latch law: UNCONDITIONAL
  // report_fatal_error (same class as every HaydnVerifyBundles law — the
  // old #ifndef NDEBUG wrapper let a misordered latch serialize wrong code
  // in Release), and it pins the COMPLETE latch sequence order, not just
  // the followers after the counted edge. Two vocabularies:
  //   short latch:  [countdown ops] ; BNEZ_W(count, Header) [; exit B]
  //   long  latch:  [countdown ops] ; LUI(scr,Header) ; ADDI32_W(scr,scr,
  //                 Header) ; BEQZ_W(count, Exit) ; JALR_W(scr,scr,0)
  // The long form's materialization MUST precede the BEQZ terminator: the
  // generic MachineVerifier forbids non-terminators after the first
  // terminator (singleton BUNDLE roots included), and ADDI32_W before its
  // LUI def reads an undefined LO12 (D1.32 wrong-code class).
  {
    // LongLatch expected sequence state machine (logical opcodes).
    enum LongStage {
      LS_Body,     // countdown ops / anything before the template
      LS_SeenLUI,  // LUI(scr, Header) seen
      LS_SeenADDI, // ADDI32_W(scr, scr, Header) seen
      LS_SeenBEQZ, // BEQZ_W(count, Exit) seen
      LS_SeenJALR  // JALR_W seen (final)
    } Stage = LS_Body;
    bool SeenCountedEdge = false;
    for (const MachineInstr &MI : Latch->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() ||
          MI.getOpcode() == TargetOpcode::BUNDLE)
        continue;
      const unsigned Log =
          haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
      // Template discriminator: the latch template's LUI/ADDI32_W carry
      // the Header MBB symbol operand (and the BEQZ the Exit MBB) — body
      // address materializations (LUI @arr etc.) do not.
      auto mbbOperandIs = [](const MachineInstr &MI, unsigned Idx,
                             const MachineBasicBlock *MBB) {
        return MI.getNumOperands() > Idx &&
               MI.getOperand(Idx).isMBB() &&
               MI.getOperand(Idx).getMBB() == MBB;
      };
      if (LongLatch) {
        // COMPLETE-order law: the template matches only in the exact
        // emitted order LUI(Header) ; ADDI32_W(Header) ; BEQZ(Exit) ;
        // JALR. The MBB-symbol operands discriminate the template from
        // the body's own LUI/ADDI address materializations, so a body
        // LUI+ADDI pair cannot fake the chain. ADDI32_W BEFORE the
        // template LUI (the historical misorder) is only detectable at
        // the template's own terms: the fatal below fires when the chain
        // itself is broken; a pre-template ADDI is body vocabulary.
        if (Stage == LS_Body && Log == Haydn::LUI &&
            mbbOperandIs(MI, 1, Header)) {
          Stage = LS_SeenLUI;
          continue;
        }
        if (Stage == LS_SeenLUI) {
          if ((Log == Haydn::ADDI32_W || Log == Haydn::ADDI32) &&
              mbbOperandIs(MI, 2, Header)) {
            Stage = LS_SeenADDI;
            continue;
          }
          if (Log == Haydn::NOP)
            continue; // pad tolerance
          report_fatal_error("hwloop demote: ADDI32_W(Header) must directly "
                             "follow the long-latch LUI (D1.32 order law)",
                             /*GenCrashDiag=*/false);
        }
        if (Stage == LS_SeenADDI) {
          if ((Log == Haydn::BEQZ_W || Log == Haydn::BEQZ) &&
              mbbOperandIs(MI, 1, Exit)) {
            Stage = LS_SeenBEQZ;
            SeenCountedEdge = true;
            continue;
          }
          if (Log == Haydn::NOP)
            continue;
          report_fatal_error("hwloop demote: the long-latch exit BEQZ(Exit) "
                             "must follow the address materialization (D1.32 "
                             "order law)",
                             /*GenCrashDiag=*/false);
        }
        if (Stage == LS_SeenBEQZ) {
          if (Log == Haydn::JALR_W || Log == Haydn::JALR) {
            Stage = LS_SeenJALR;
            continue;
          }
          if (Log == Haydn::NOP)
            continue;
          if (isGeneratedFormatEMemberName(TII.getName(MI.getOpcode())))
            continue;
          report_fatal_error("hwloop demote: only the JALR_W backedge may "
                             "follow the long-latch exit edge (D1.32 order "
                             "law)",
                             /*GenCrashDiag=*/false);
        }
        if (Stage == LS_SeenJALR) {
          if (Log == Haydn::NOP ||
              isGeneratedFormatEMemberName(TII.getName(MI.getOpcode())))
            continue;
          report_fatal_error("hwloop demote: nothing may follow the "
                             "long-latch JALR_W backedge (D1.32 order law)",
                             /*GenCrashDiag=*/false);
        }
        continue; // countdown/body ops before the template
      }
      // Short-latch law (unchanged vocabulary): counted back-edge then
      // only NOP pad and the exact-commit exit B.
      if (haydn::hwloop::isSoftLatchBnezOpcode(MI.getOpcode())) {
        SeenCountedEdge = true;
        continue;
      }
      if (!SeenCountedEdge)
        continue;
      // Exit-follower law: the demoter's own exit branch — the former
      // B shell, its W67 BEQZ_W/BEQZ_W_MSP forms, or the generated
      // BEQZ_e*_I12 member the late exact-commit already baked (logical
      // BEQZ on R0, the always-taken uncond shape). Architectural NOP
      // pad shares the cycle.
      const unsigned LateLog = Log;
      if (LateLog == Haydn::B || LateLog == Haydn::BEQZ_W ||
          LateLog == Haydn::NOP || MI.getOpcode() == Haydn::BEQZ_W_MSP)
        continue;
      if (isGeneratedFormatEMemberName(TII.getName(MI.getOpcode())) &&
          LateLog == Haydn::BEQZ && MI.getNumOperands() >= 1 &&
          MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == Haydn::R0)
        continue;
      report_fatal_error("hwloop demote: only an exit B may follow latch "
                         "BNEZ_W",
                         /*GenCrashDiag=*/false);
    }
    if (!SeenCountedEdge)
      report_fatal_error("hwloop demote: latch missing BNEZ_W/GR2.7 soft "
                         "edge",
                         /*GenCrashDiag=*/false);
  }

  return true;
}
