//===-- HaydnHardwareLoops.cpp - Role-A Hardware Loop Expansion ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Product path is SCEV-proven retained-state expand only. Product default
// -haydn-enable-hwloops is OFF.
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
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
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
static_assert(!llvm::HaydnTargetMachine::hardwareLoopsProductDefaultEnabled(),
              "hardware-loop product default stays OFF until independent "
              "then combined qualification and a separate policy-only flip");
// Inserted only when EnableHaydnHardwareLoops (product default OFF).
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
  const unsigned Opc = MI.getOpcode();
  switch (haydn::format_e::logicalOpcodeOrSelf(Opc)) {
  case Haydn::LoopStart:
  case Haydn::SET_HWLOOP:
  case Haydn::SET_HWLOOP_REG:
  case Haydn::SET_HWLOOP_W:
  case Haydn::SET_HWLOOP_F2_W:
  case Haydn::SET_HWLOOP_REG_W:
    return true;
  default:
    return false;
  }
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
    if (I->isTerminator() && !I->isCall())
      break;
    unsigned Bytes = TII.getInstSizeInBytes(*I);
    if (Bytes == 0)
      continue;
    FollowingBundles += ceilProductParcels(Bytes);
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
      rejectIncompleteRoleA(LS, Header, PLE, Why);
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
  // Product insert is EnableHaydnHardwareLoops (default OFF). hasHWLoop()
  // is ISA capability only — +hwloop does not flip product policy.
  // Never skipFunction here: product-off is the pipeline insert gate.
  const auto &STI = MF.getSubtarget<HaydnSubtarget>();
  if (!STI.hasHWLoop())
    return false;

  LLVM_DEBUG(dbgs() << "HaydnHWLoops: Running on " << MF.getName()
                    << " (Role A expand only; post-RA rediscovery deleted)\n");

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
using haydn::hwloop::resolveLoopStartLatch;
using haydn::hwloop::emitExactLate;
using haydn::hwloop::emitExactLateDef;
using haydn::hwloop::eraseInstrSafe;
using haydn::hwloop::eraseSetMemberAndRecommitSiblings;
using haydn::hwloop::isLiveMBB;
using haydn::hwloop::materializeTripCount;
using haydn::hwloop::pickCounterReg;
using haydn::hwloop::regClobberedNonCountdownIn;
using haydn::hwloop::stripResidualCountdown;
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
  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  const HaydnFrameLowering *TFL = ST.getFrameLowering();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();

  auto canUsePreferAsCounter = [&]() -> bool {
    return Prefer.isPhysical() && Prefer != Haydn::R0 && Prefer != Haydn::R13 &&
           Prefer != Haydn::R15 &&
           !regClobberedNonCountdownIn(Prefer, LoopBlocks);
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
    if (canUsePreferAsCounter()) {
      CountReg = Prefer;
      InstallSoftLoop = true;
    } else {
      CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins,
                        DebugPrefix);
      if (CountReg.isPhysical()) {
        materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, 0,
                             /*HasImm=*/false);
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
      materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, Imm,
                           /*HasImm=*/true);
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
      LatchScr = findPostRAScratchNoSpill(
          *Latch, Latch->end(), /*PreferNotR12=*/true, {Header, Exit},
          Prefer.isPhysical() ? ArrayRef<Register>{Prefer} : NoExclude);
      Register PreheaderScr;
      if (HasImm)
        PreheaderScr = findPostRAScratchNoSpill(
            *Preheader, Ins, /*PreferNotR12=*/true, {},
            Prefer.isPhysical() ? ArrayRef<Register>{Prefer} : NoExclude);
      if (!(LatchScr.isPhysical() &&
            (!HasImm || PreheaderScr.isPhysical()))) {
        LLVM_DEBUG(dbgs() << DebugPrefix
                          << ": demote refused — no spill-free non-R0 "
                             "scratch for stack-counter windows (latch "
                          << (LatchScr.isPhysical() ? "ok" : "NONE")
                          << ", preheader "
                          << (!HasImm || PreheaderScr.isPhysical() ? "ok"
                                                                   : "NONE")
                          << "); never a spill bracket over the counter\n");
        return false;
      }

      if (HasImm) {
        // Materialize imm into the probed spill-free temp, then store to FI.
        // No withPostRAScratch bracket: its NeedsSpill home aliases this
        // same counter FI. The probe above is the single mechanism that
        // picked PreheaderScr.
        materializeTripCount(*Preheader, Ins, DL, TII, PreheaderScr, Prefer,
                             Imm, /*HasImm=*/true);
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(PreheaderScr, getKillRegState(true))
                            .addReg(FrameReg)
                            .addImm(Elem);
                      });
      } else {
        // Prefer holds trip at SET; store it to FI before erase.
        emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                      [&](MachineInstrBuilder MIB) {
                        MIB.addReg(Prefer).addReg(FrameReg).addImm(Elem);
                      });
      }
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

  if (UseStackCounter) {
    Register FrameReg;
    int64_t Off =
        TFL->getFrameIndexReference(MF, StackCounterFI, FrameReg).getFixed();
    assert((Off % 4) == 0 && isInt<6>(Off / 4) &&
           "stack-counter FI refused unless word-aligned simm6");
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
    emitExactLate(*Latch, Latch->getFirstTerminator(), DL, TII, Haydn::BNEZ_W,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(LatchScr).addMBB(Header);
                  });
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote stack-counter exact-commit "
                         "SUBI32+BNEZ_W FI#"
                      << StackCounterFI << "\n");
  } else {
    // Final-real soft edge: SUBI32 count,count,1 + BNEZ_W count, Header.
    // Residual countdown was stripped above; never double-dec.
  // Exact-commit each edge as a product singleton before second BR.
    emitExactLateDef(*Latch, Latch->getFirstTerminator(), DL, TII,
                     Haydn::SUBI32, CountReg,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(CountReg).addImm(1);
                     });
    emitExactLate(*Latch, Latch->getFirstTerminator(), DL, TII, Haydn::BNEZ_W,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(CountReg).addMBB(Header);
                  });
    LLVM_DEBUG(dbgs() << DebugPrefix << ": demote exact-commit SUBI32+BNEZ_W on "
                      << printReg(CountReg) << "\n");
  }

  MachineFunction::iterator LatchIt = Latch->getIterator();
  MachineFunction::iterator NextIt = std::next(LatchIt);
  bool ExitIsLayoutFallthrough =
      (NextIt != MF.end()) && (&*NextIt == Exit);
  if (!ExitIsLayoutFallthrough && Exit != Header) {
  // Do not leave a bare B for late Finalize — exact-commit the
    // unconditional exit edge so second BR charges committed EncodedBytes.
    // B has no PlacementAlternatives (wrap-only Format E singleton commit).
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::B,
                  [&](MachineInstrBuilder MIB) { MIB.addMBB(Exit); });
  }

#ifndef NDEBUG
  // Latch law: the counted back-edge is residual BNEZ_W or the golden
  // Format E BNEZ member after late exact-commit. The only instructions
  // that may follow it are architectural NOP pad in the same cycle and
  // an exact-commit exit B. A scratch restore, spill, or XOR-zero after
  // the terminator is the per-iteration SP leak / R0 clobber.
  {
    bool SeenBnez = false;
    for (const MachineInstr &MI : Latch->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() ||
          MI.getOpcode() == TargetOpcode::BUNDLE)
        continue;
      if (haydn::hwloop::isSoftLatchBnezOpcode(MI.getOpcode())) {
        SeenBnez = true;
        continue;
      }
      if (!SeenBnez)
        continue;
      const unsigned LateLog =
          haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
      if (LateLog == Haydn::B || LateLog == Haydn::NOP)
        continue;
      llvm_unreachable(
          "hwloop demote: only an exit B may follow latch BNEZ_W");
    }
    assert(SeenBnez && "hwloop demote: latch missing BNEZ_W soft edge");
  }
#endif

  return true;
}
