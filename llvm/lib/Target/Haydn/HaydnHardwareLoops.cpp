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
//   1. Generic IR HardwareLoops + Haydn TTI prove trip/CFG (single-BB only;
//      multi-BB is a separate measured SCEV/CFG extension, never post-RA
//      physical rediscovery).
//   2. GlobalISel lowers to LoopStart / PseudoLoopEnd logical pseudos.
//   3. This pass (post-RA, before physical scheduling):
//        stripEmptyZeroOverheadLoops — remove empty retained ZOLs only
//        expandRoleALoopStarts — preflight retained metadata/CFG/body, then
//          LoopStart → SET_HWLOOP_F2_W (product sel domain {0,1}) with
//          intervening setup pads, or reject incomplete seats fail-closed
//          before any mutation (fatal).
//   4. This pass owns software-loop demotion: if the retained body already
//      cannot encode Off2, the soft edge is installed here (before layout).
//   5. HaydnFixupHwLoops (pre-emit, flag-gated) pads/range-checks and may
//      call the same formation demote helper for already-committed wide
//      forms. Residual generic SET_HWLOOP{,_REG} fatals at Fixup.
//
// Post-RA soft-branch convert and its reconstruction helpers are permanently
// deleted from this TU.
//
//===----------------------------------------------------------------------===//

#include "HaydnHardwareLoops.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnFrameLowering.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
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
    MachineBasicBlock *Body = nullptr;
    MachineInstr *PLE = nullptr;

    auto findPLE = [](MachineBasicBlock *BB) -> MachineInstr * {
      if (!BB)
        return nullptr;
      for (MachineInstr &MI : *BB)
        if (MI.getOpcode() == Haydn::PseudoLoopEnd)
          return &MI;
      return nullptr;
    };
    for (const MachineOperand &MO : LS->operands()) {
      if (MO.isMBB()) {
        Body = MO.getMBB();
        break;
      }
    }
    if (Body)
      PLE = findPLE(Body);
    if (!Body || !PLE) {
      Body = nullptr;
      PLE = nullptr;
      for (MachineBasicBlock *Succ : Preheader->successors()) {
        if (MachineInstr *Cand = findPLE(Succ)) {
          Body = Succ;
          PLE = Cand;
          break;
        }
      }
    }
    if (!Body || !PLE)
      continue;
    if (classifyZOLBody(*Body) != ZOLBodyKind::Empty)
      continue;

    MachineBasicBlock *ExitBB = nullptr;
    for (MachineBasicBlock *Succ : Body->successors()) {
      if (Succ != Body) {
        ExitBB = Succ;
        break;
      }
    }
    if (!ExitBB) {
      MachineFunction::iterator NextIt = std::next(Body->getIterator());
      if (NextIt != MF.end())
        ExitBB = &*NextIt;
    }
    if (!ExitBB)
      continue;

    LLVM_DEBUG(dbgs() << "HaydnHWLoops: stripping empty IR ZOL "
                      << printMBBReference(*Body) << " (LoopStart in "
                      << printMBBReference(*Preheader) << ")\n");

    DebugLoc DL = LS->getDebugLoc();
    LS->eraseFromParent();
    PLE->eraseFromParent();

    if (Body->isSuccessor(Body))
      Body->removeSuccessor(Body);
    if (!Body->isSuccessor(ExitBB))
      Body->addSuccessor(ExitBB);

    bool HasExitBranch = false;
    for (const MachineInstr &MI : Body->terminators()) {
      if (MI.getOpcode() == Haydn::B) {
        HasExitBranch = true;
        break;
      }
    }
    if (!HasExitBranch) {
      MachineFunction::iterator NextIt = std::next(Body->getIterator());
      bool ExitIsFallthrough =
          (NextIt != MF.end()) && (&*NextIt == ExitBB);
      if (!ExitIsFallthrough)
        TII->insertBranch(*Body, ExitBB, /*FBB=*/nullptr,
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

// Resolve the Role-A body solely from preheader CFG successors that carry a
// PseudoLoopEnd. Never invent a body from layout order: a next-MBB that is not
// a successor is incomplete retained state and must reject fail-closed.
static void resolveRoleABody(MachineInstr *LS, MachineBasicBlock *&Body,
                             MachineInstr *&PLE) {
  Body = nullptr;
  PLE = nullptr;
  if (!LS || !LS->getParent())
    return;
  MachineBasicBlock *Preheader = LS->getParent();
  for (MachineBasicBlock *Succ : Preheader->successors()) {
    if (MachineInstr *Cand = findPseudoLoopEnd(Succ)) {
      Body = Succ;
      PLE = Cand;
      return;
    }
  }
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
    break;
  }
  const MachineFunction *MF = MI.getMF();
  if (!MF)
    return false;
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const std::string Log =
      haydn::format_e::peelLogicalOpcodeName(TII->getName(Opc));
  return StringRef(Log).equals_insensitive("SET_HWLOOP") ||
         StringRef(Log).equals_insensitive("SET_HWLOOP_F2") ||
         StringRef(Log).equals_insensitive("SET_HWLOOP_REG");
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

static bool preflightRoleABody(MachineBasicBlock *Body, MachineInstr *PLE,
                               std::string &Why) {
  if (!Body) {
    Why = "no body MBB";
    return false;
  }
  if (!PLE || PLE->getParent() != Body) {
    Why = "PseudoLoopEnd missing or not on body";
    return false;
  }
  if (!Body->isSuccessor(Body)) {
    Why = "body missing self back-edge";
    return false;
  }
  unsigned NonSelfSucc = 0;
  for (const MachineBasicBlock *Succ : Body->successors()) {
    if (Succ != Body)
      ++NonSelfSucc;
  }
  if (NonSelfSucc != 1) {
    Why = "body must have exactly one non-self exit";
    return false;
  }
  ZOLBodyKind Kind = classifyZOLBody(*Body);
  if (Kind == ZOLBodyKind::Empty) {
    Why = "empty body (should have been stripped)";
    return false;
  }
  if (Kind == ZOLBodyKind::NotZOL) {
    Why = "body is not ZOL form";
    return false;
  }
  unsigned PLECount = 0;
  for (const MachineInstr &MI : *Body) {
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::PseudoLoopEnd) {
      ++PLECount;
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
  }
  if (PLECount != 1) {
    Why = "body must carry exactly one PseudoLoopEnd";
    return false;
  }
  return true;
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

    MachineBasicBlock *Body = nullptr;
    MachineInstr *PLE = nullptr;
    resolveRoleABody(LS, Body, PLE);

    if (LS->getNumOperands() < 2 || !LS->getOperand(0).isReg() ||
        !LS->getOperand(1).isImm()) {
      rejectIncompleteRoleA(LS, Body, PLE, "incomplete LoopStart operands");
    }

    MachineBasicBlock *Preheader = LS->getParent();
    Register TripReg = LS->getOperand(0).getReg();
    int64_t Adj = LS->getOperand(1).getImm();
    DebugLoc DL = LS->getDebugLoc();

    if (!TripReg.isPhysical() || TripReg == Haydn::R0) {
      rejectIncompleteRoleA(LS, Body, PLE, "trip reg not a physical GPR");
    }

    std::string Why;
    if (!preflightRoleABody(Body, PLE, Why)) {
      rejectIncompleteRoleA(LS, Body, PLE, Why);
    }

    {
      int64_t BodyBytes = 0;
      for (const MachineInstr &MI : *Body) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::B)
          continue;
        if (MI.isMetaInstruction() || MI.isDebugInstr() ||
            MI.isImplicitDef() || MI.isKill() || MI.isPosition())
          continue;
        BodyBytes += TII->getInstSizeInBytes(MI);
      }
      if (MinSetupBytes + BodyBytes > MaxHWLoopEndOffsetBytes) {
        if (!demoteHardwareLoopToSoftware(*LS, *TII))
          rejectIncompleteRoleA(
              LS, Body, PLE,
              "unencodable body cannot install software loop");
        Changed = true;
        continue;
      }
    }

    MachineBasicBlock *Header = Body;
    MachineBasicBlock *Latch = Body;
    Header->setLabelMustBeEmitted();
    Latch->setLabelMustBeEmitted();

    // Product selector domain is {0,1}. Innermost Role-A expand always arms
    // sel=1 via SET_HWLOOP only — never invent free HWLR CSR addresses or
    // out-of-domain selectors. Fixup demotes residual out-of-domain seats.
    static_assert(isProductSelector(InnermostProductSelector),
                  "Role-A expand selector must stay in product domain");
    static_assert(isProductSelector(InnermostProductSelector) &&
                      !isProductSelector(2) && !isProductSelector(3),
                  "expand must not invent out-of-domain selectors");
    MachineBasicBlock::iterator InsertPt = LS->getIterator();
    MachineInstr *SetMI =
        BuildMI(*Preheader, InsertPt, DL, TII->get(Haydn::SET_HWLOOP_F2_W))
            .addImm(InnermostProductSelector)
            .addMBB(Header)
            .addMBB(Latch)
            .addReg(TripReg);

    {
      unsigned FollowingBundles = 0;
      for (MachineBasicBlock::iterator I = std::next(SetMI->getIterator()),
                                       E = Preheader->end();
           I != E; ++I) {
        if (I->isMetaInstruction() || I->isDebugInstr() ||
            I->isImplicitDef() || I->isKill())
          continue;
        if (I->isTerminator() && !I->isCall())
          break;
        unsigned Bytes = TII->getInstSizeInBytes(*I);
        if (Bytes == 0)
          continue;
        FollowingBundles += ceilProductParcels(Bytes);
      }
      if (FollowingBundles < HWLoopSetupPadBundles) {
        unsigned Deficit = HWLoopSetupPadBundles - FollowingBundles;
        MachineBasicBlock::iterator AfterSet =
            std::next(SetMI->getIterator());
        for (unsigned I = 0; I < Deficit; ++I)
          BuildMI(*Preheader, AfterSet, DL, TII->get(Haydn::NOP));
      }
    }

    {
      unsigned BodyParcels = 0;
      for (const MachineInstr &MI : *Body) {
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::B)
          continue;
        if (MI.isMetaInstruction() || MI.isDebugInstr() ||
            MI.isImplicitDef() || MI.isKill() || MI.isPosition())
          continue;
        unsigned Bytes = TII->getInstSizeInBytes(MI);
        if (Bytes == 0)
          continue;
        BodyParcels += ceilProductParcels(Bytes);
      }
      if (BodyParcels < MinBodyBundles) {
        unsigned Deficit = MinBodyBundles - BodyParcels;
        MachineBasicBlock::iterator BeforePLE = PLE->getIterator();
        for (unsigned I = 0; I < Deficit; ++I)
          BuildMI(*Body, BeforePLE, DL, TII->get(Haydn::NOP));
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

    LS->eraseFromParent();
    ++NumRoleAExpanded;
    Changed = true;
  }
  return Changed;
}

bool HaydnHardwareLoops::runOnMachineFunction(MachineFunction &MF) {
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

static void eraseEmptyBundleRoot(MachineInstr *BundleRoot) {
  if (!BundleRoot || !BundleRoot->isBundle() || !BundleRoot->getParent())
    return;
  MachineBasicBlock::instr_iterator Next =
      std::next(BundleRoot->getIterator());
  MachineBasicBlock *MBB = BundleRoot->getParent();
  if (Next != MBB->instr_end() && Next->isBundledWithPred())
    return;
  BundleRoot->eraseFromParent();
}

static void eraseInstrSafe(MachineInstr *MI) {
  if (!MI || !MI->getParent())
    return;
  MachineInstr *BundleRoot = nullptr;
  if (MI->isBundledWithPred()) {
    BundleRoot = &*getBundleStart(MI->getIterator());
    MI->unbundleFromPred();
  }
  if (MI->isBundledWithSucc())
    MI->unbundleFromSucc();
  MI->eraseFromParent();
  eraseEmptyBundleRoot(BundleRoot);
}

static bool isLiveMBB(const MachineFunction &MF, const MachineBasicBlock *MBB) {
  return MBB && MBB->getParent() == &MF && MBB->getNumber() >= 0;
}

static MachineBasicBlock *resolveBodyMBB(MachineInstr &SetMI) {
  const MachineFunction *MF =
      SetMI.getParent() ? SetMI.getParent()->getParent() : nullptr;
  if (!MF)
    return nullptr;
  unsigned Opc = SetMI.getOpcode();
  if (Opc != Haydn::LoopStart && SetMI.getNumOperands() >= 3 &&
      SetMI.getOperand(1).isMBB()) {
    MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
    return isLiveMBB(*MF, H) ? H : nullptr;
  }
  if (Opc == Haydn::LoopStart) {
    MachineBasicBlock *Pre = SetMI.getParent();
    for (MachineBasicBlock *Succ : Pre->successors()) {
      if (!isLiveMBB(*MF, Succ))
        continue;
      for (const MachineInstr &T : Succ->terminators()) {
        if (T.getOpcode() == Haydn::PseudoLoopEnd)
          return Succ;
      }
    }
    if (Pre->succ_size() == 1) {
      MachineBasicBlock *S = *Pre->succ_begin();
      return isLiveMBB(*MF, S) ? S : nullptr;
    }
  }
  return nullptr;
}

using LoopBlockSet = SmallPtrSet<const MachineBasicBlock *, 8>;

static unsigned lateMemberOpcode(unsigned LogicalOpc) {
  return haydn::bundle::lateProductMemberOpcode(LogicalOpc);
}

static void finalizeExactLateSingleton(MachineInstr &MI) {
  haydn::bundle::finalizeExactLateSingleton(MI);
}

/// After SET-member erase from a multi-member product cycle, re-exact-commit
/// surviving coissued siblings so the BUNDLE root is rebuilt (consolidated
/// defs/uses, kill flags, FormatID). Bare survivors would leave residual
/// real MIs for the late firewall and stale root operands.
static void recommitSurvivingCycleMembers(ArrayRef<MachineInstr *> Keep,
                                          const HaydnInstrInfo &TII) {
  if (Keep.empty())
    return;

  for (MachineInstr *K : Keep) {
    if (!K || !K->getParent())
      continue;
    if (K->isBundledWithPred())
      K->unbundleFromPred();
    if (K->isBundledWithSucc())
      K->unbundleFromSucc();
  }

  SmallVector<MachineInstr *, 3> Live;
  Live.reserve(Keep.size());
  for (MachineInstr *K : Keep)
    if (K && K->getParent())
      Live.push_back(K);
  if (Live.empty())
    return;

  if (Live.size() == 1) {
    MachineInstr *MI = Live[0];
    unsigned Member = lateMemberOpcode(MI->getOpcode());
    if (Member != MI->getOpcode())
      MI->setDesc(TII.get(Member));
    finalizeExactLateSingleton(*MI);
    return;
  }

  // Multi-survivor coissue: transactional multi-MI exact commit rebuilds
  // root operands/kills/internal-reads + Format E row/completion. Fall back to
  // per-member singletons rather than leave bare reals if membership is
  // no longer one legal product cycle after SET removal.
  if (!haydn::bundle::commitExactMultiMIProductCycle(Live)) {
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: coissue survivors not one "
                         "legal multi-MI cycle after SET erase — "
                         "singleton exact-commit each\n");
    for (MachineInstr *MI : Live) {
      unsigned Member = lateMemberOpcode(MI->getOpcode());
      if (Member != MI->getOpcode())
        MI->setDesc(TII.get(Member));
      finalizeExactLateSingleton(*MI);
    }
  }
}

/// SET/LoopStart-member erase that preserves coissued siblings as an exact
/// product cycle. Dissolves the old root, erases only the setup member, then
/// recommits remaining children so consolidated root operands match the
/// surviving membership (plan hard-root operand recommit peer for Fixup).
static void eraseSetMemberAndRecommitSiblings(MachineInstr &SetMI,
                                              const HaydnInstrInfo &TII) {
  MachineBasicBlock *MBB = SetMI.getParent();
  if (!MBB)
    return;

  SmallVector<MachineInstr *, 3> Keep;
  if (SetMI.isBundledWithPred() || SetMI.isBundledWithSucc()) {
    MachineInstr *Root = &*getBundleStart(SetMI.getIterator());
    for (MachineBasicBlock::instr_iterator I = std::next(Root->getIterator());
         I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
      if (&*I != &SetMI)
        Keep.push_back(&*I);
    }
    // Dissolve every child from the old root before erasing the header so no
    // pass observes a half-unbundled multi-member shell.
    for (MachineInstr *K : Keep) {
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    if (SetMI.isBundledWithPred())
      SetMI.unbundleFromPred();
    if (SetMI.isBundledWithSucc())
      SetMI.unbundleFromSucc();
    Root->eraseFromParent();
  }

  SetMI.eraseFromParent();
  recommitSurvivingCycleMembers(Keep, TII);
}

/// Build a late singleton with exact-commit member opcode (dest form).
static MachineInstrBuilder
buildExactLateDef(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                  const DebugLoc &DL, const TargetInstrInfo &TII,
                  unsigned LogicalOpc, Register Dest) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)),
                 Dest);
}

/// Build a late singleton with exact-commit member opcode (no dest).
static MachineInstrBuilder
buildExactLate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
               const DebugLoc &DL, const TargetInstrInfo &TII,
               unsigned LogicalOpc) {
  return BuildMI(MBB, InsertPt, DL, TII.get(lateMemberOpcode(LogicalOpc)));
}

/// Emit one exact-committed singleton (dest form) and stamp Format E commit.
template <typename AddOpsFn>
static MachineInstr *
emitExactLateDef(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                 const DebugLoc &DL, const TargetInstrInfo &TII,
                 unsigned LogicalOpc, Register Dest, AddOpsFn AddOps) {
  MachineInstrBuilder MIB =
      buildExactLateDef(MBB, InsertPt, DL, TII, LogicalOpc, Dest);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}

/// Emit one exact-committed singleton (no dest) and stamp Format E commit.
template <typename AddOpsFn>
static MachineInstr *
emitExactLate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
              const DebugLoc &DL, const TargetInstrInfo &TII,
              unsigned LogicalOpc, AddOpsFn AddOps) {
  MachineInstrBuilder MIB = buildExactLate(MBB, InsertPt, DL, TII, LogicalOpc);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}
// Bundle-preserving Fixup splices/pads relative to this root, never unbundles
// a legal coissued SET cycle just to walk iterators.
static MachineInstr &topLevelForLayout(MachineInstr &MI) {
  if (MI.isBundledWithPred() || MI.isBundledWithSucc())
    return *getBundleStart(MI.getIterator());
  return MI;
}
static void collectLoopBlocks(const MachineBasicBlock *Header,
                                          const MachineBasicBlock *Latch,
                                          const MachineBasicBlock *Preheader,
                                          LoopBlockSet &Out) {
  Out.clear();
  if (!Header || !Latch)
    return;
  Out.insert(Header);
  Out.insert(Latch);
  if (Header == Latch)
    return;

  // Reverse CFG from Latch: every predecessor that is not Preheader and not
  // outside the loop. Cap work to avoid runaway on malformed CFG.
  SmallVector<const MachineBasicBlock *, 8> Work(Latch->pred_begin(),
                                                 Latch->pred_end());
  unsigned Guard = 0;
  while (!Work.empty() && Guard++ < 256) {
    const MachineBasicBlock *B = Work.pop_back_val();
    if (!B || B == Preheader)
      continue;
    if (!Out.insert(B).second)
      continue;
    if (B == Header)
      continue;
    for (const MachineBasicBlock *P : B->predecessors())
      Work.push_back(P);
  }
}

static bool isCountdownStepOf(const MachineInstr &MI,
                                          Register Reg) {
  if (!Reg.isPhysical() || MI.isMetaInstruction() || MI.isDebugInstr() ||
      MI.isBundle())
    return false;
  // Must def Reg.
  bool Defs = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
      Defs = true;
      break;
    }
  }
  if (!Defs)
    return false;

  unsigned Opc = MI.getOpcode();
  if (Opc == Haydn::LoopDec)
    return true;
  if (Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) {
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm() &&
           MI.getOperand(2).getImm() == -1;
  }
  if (Opc == Haydn::SUBI32) {
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg && MI.getOperand(2).isImm() &&
           MI.getOperand(2).getImm() == 1;
  }
  if (Opc == Haydn::ADD32 || Opc == Haydn::SUB32) {
    // Prefer = Prefer + K / Prefer - K (K often a phys holding -1).
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == Reg;
  }
  // Flex / slot-suffixed forms (ADD32_S0, …): operand-shape fallback.
  if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isReg() &&
      MI.getOperand(1).getReg() == Reg) {
    if (MI.getOperand(2).isImm()) {
      int64_t Imm = MI.getOperand(2).getImm();
      return Imm == -1 || Imm == 1;
    }
    if (MI.getOperand(2).isReg())
      return true;
  }
  return false;
}

static bool regMentionedInBlocks(Register Reg,
                                             const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
      }
    }
  }
  return false;
}

static bool regClobberedNonCountdownIn(Register Reg,
                                                   const LoopBlockSet &Blocks) {
  if (!Reg.isPhysical())
    return false;
  for (const MachineBasicBlock *MBB : Blocks) {
    if (!MBB)
      continue;
    for (const MachineInstr &MI : MBB->instrs()) {
      if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isBundle())
        continue;
      bool Defs = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
          Defs = true;
          break;
        }
      }
      if (!Defs)
        continue;
      if (isCountdownStepOf(MI, Reg))
        continue;
      return true;
    }
  }
  return false;
}

static void stripResidualCountdown(MachineBasicBlock *Latch,
                                               Register Reg) {
  if (!Latch || !Reg.isPhysical())
    return;
  SmallVector<MachineInstr *, 4> Kill;
  for (MachineInstr &MI : Latch->instrs()) {
    if (isCountdownStepOf(MI, Reg) && MI.getOpcode() != Haydn::LoopDec)
      Kill.push_back(&MI);
  }
  for (MachineInstr *MI : Kill)
    eraseInstrSafe(MI);
}

static Register pickCounterReg(
    const LoopBlockSet &Blocks, Register Prefer, const HaydnSubtarget &ST,
    MachineBasicBlock &Preheader, MachineBasicBlock::iterator InsertPt) {
  // Free counter must be:
  // 1) not mentioned in any CFG loop block (lc_dp_lis: layout range missed
  // latch earlier in the function — CFG Blocks is required);
  // 2) available at the SET insert point (LivePhysRegs — AIE/RISC-V style
  // post-RA scavenge, not "first preferred even if live").
  // Fail-closed: return invalid Register rather than Prefer/R11 when both
  // are live (old code clobbered live-through temps under demote).
  const TargetRegisterInfo &TRI = *ST.getRegisterInfo();
  const MachineRegisterInfo &MRI = Preheader.getParent()->getRegInfo();

  LivePhysRegs LPR(TRI);
  LPR.addLiveOuts(Preheader);
  for (MachineBasicBlock::iterator II = Preheader.end(); II != InsertPt;) {
    --II;
    LPR.stepBackward(*II);
  }

  auto isUsable = [&](Register R) -> bool {
    if (!R.isPhysical() || R == Haydn::R0 || R == Haydn::R13 || R == Haydn::R15)
      return false;
    if (MRI.isReserved(R))
      return false;
    if (regMentionedInBlocks(R, Blocks))
      return false;
    if (!LPR.available(MRI, R))
      return false;
    return true;
  };

  if (isUsable(Prefer))
    return Prefer;

  // Call-clobbered temps first. R12 last (normal GPR; AIE: no free AT).
  static const MCPhysReg CandsGPR[] = {
      Haydn::R11, Haydn::R10, Haydn::R9, Haydn::R8, Haydn::R7,
      Haydn::R4,  Haydn::R3,  Haydn::R2, Haydn::R1, Haydn::R12};
  for (MCPhysReg R : CandsGPR) {
    if (Prefer.isPhysical() && R == Prefer)
      continue;
    if (isUsable(R))
      return R;
  }
  LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: pickCounterReg — no free GPR "
                       "(LivePhysRegs + loop mention); demote refuses "
                       "erase-only on live body\n");
  return Register();
}
static void materializeTripCount(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt, DebugLoc DL,
    const HaydnInstrInfo &TII, Register Dst, Register SrcReg, int64_t SrcImm,
    bool HasImm) {
  // Every demote trip-materialize MI is exact-committed before the
  // second BranchRelaxation (shared commitLateProductCycle surface).
  if (!HasImm) {
    if (SrcReg == Dst)
      return;
    // Post-RA: real MOVE32 (not generic COPY — expand-pseudos already ran).
    // Canonical MOVE32 encoding needs rs1=rs2=Src.
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::MOVE32, Dst,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(SrcReg).addReg(SrcReg);
                     });
    return;
  }

  // uimm16 trip counts: XOR-zero then ADDI32_W (expandPostRA already ran).
  if (SrcImm == 0) {
    emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                     });
    return;
  }
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::XOR32, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                   });
  emitExactLateDef(MBB, InsertPt, DL, TII, Haydn::ADDI32_W, Dst,
                   [&](MachineInstrBuilder MIB) {
                     MIB.addReg(Dst).addImm(SrcImm);
                   });
}
bool llvm::eraseHardwareLoopSetup(MachineInstr &SetMI) {
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
    collectPLE(resolveBodyMBB(SetMI));
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
  eraseSetMemberAndRecommitSiblings(SetMI, TII);

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
bool llvm::demoteHardwareLoopToSoftware(MachineInstr &SetMI,
                                           const HaydnInstrInfo &TII) {
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

  if (IsLoopStart) {
    if (!SetMI.getOperand(0).isReg())
      return eraseHardwareLoopSetup(SetMI);
    Prefer = SetMI.getOperand(0).getReg();
    Header = resolveBodyMBB(SetMI);
    Latch = Header;
  } else {
    if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(1).isMBB() ||
        !SetMI.getOperand(2).isMBB())
      return eraseHardwareLoopSetup(SetMI);
    Header = SetMI.getOperand(1).getMBB();
    Latch = SetMI.getOperand(2).getMBB();
    if (TII.isHardwareLoopRegTripOpcode(Opc)) {
      if (!SetMI.getOperand(3).isReg())
        return eraseHardwareLoopSetup(SetMI);
      Prefer = SetMI.getOperand(3).getReg();
    } else {
      if (!SetMI.getOperand(3).isImm())
        return eraseHardwareLoopSetup(SetMI);
      Imm = SetMI.getOperand(3).getImm();
      HasImm = true;
    }
  }

  // Contract §1: dead MBB operands → L1 erase only (body gone).
  if (!isLiveMBB(MF, Header) || !isLiveMBB(MF, Latch)) {
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote L1-only — Header/Latch "
                         "not live in MF (stale %bb.-1 or erased body)\n");
    return eraseHardwareLoopSetup(SetMI);
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
      // Allow empty blocks and pure far-jump materialization (LUI/ADDI/JALR).
      for (const MachineInstr &MI : Cur->instrs()) {
        if (MI.isMetaInstruction() || MI.isCFIInstruction() || MI.isKill() ||
            MI.isImplicitDef())
          continue;
        if (MI.isBundle())
          continue;
        unsigned Opc = MI.getOpcode();
        const unsigned Log = haydn::format_e::logicalOpcodeOrSelf(Opc);
        if (Log == Haydn::NOP || Log == Haydn::LUI || Log == Haydn::ADDI32_W ||
            Log == Haydn::JALR_W || Log == Haydn::JALR || Log == Haydn::B)
          continue;
        // Real work — not a trampoline.
        return false;
      }
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
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote refused — no live exit "
                         "(body still live)\n");
    return false;
  }

  // Closed demote model (AIE expand-style: total restore, no half state):
  // Decide CountReg *before* any erase. Only then L1 erase + L2 soft edge.
  // Never erase SET when soft edge cannot be installed on a live body.

  LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demoting hwloop header="
                    << printMBBReference(*Header) << " latch="
                    << printMBBReference(*Latch) << " exit="
                    << printMBBReference(*Exit)
                    << (IsLoopStart ? " (LoopStart)\n" : "\n"));

  // CFG loop blocks (not layout range — Latch may precede Header).
  LoopBlockSet LoopBlocks;
  collectLoopBlocks(Header, Latch, Preheader, LoopBlocks);

  // Snapshot soft-loop decision *before* erasing SetMI (operands die with it).
  Register CountReg;
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
      CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins);
      if (CountReg.isPhysical()) {
        materializeTripCount(*Preheader, Ins, DL, TII, CountReg, Prefer, 0,
                             /*HasImm=*/false);
        InstallSoftLoop = true;
        LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote trip "
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
    CountReg = pickCounterReg(LoopBlocks, Prefer, ST, *Preheader, Ins);
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
      // FI ref is bytes; ST32/LD32 take word element indices (imm<<2).
      assert((Off % 4) == 0 && "stack-counter FI must be word-aligned");
      const int64_t Elem = Off / 4;
      if (HasImm) {
        // Materialize imm into a free/scratch temp, then store to FI.
  // ST/address glue exact-committed (trip mat already is).
        withPostRAScratch(
            *Preheader, Ins, DL, TII, ST, /*PreferNotR12=*/true,
            [&](Register Scr) {
              materializeTripCount(*Preheader, Ins, DL, TII, Scr, Prefer, Imm,
                                   /*HasImm=*/true);
              if (isInt<6>(Elem)) {
                emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                              [&](MachineInstrBuilder MIB) {
                                MIB.addReg(Scr, getKillRegState(true))
                                    .addReg(FrameReg)
                                    .addImm(Elem);
                              });
              } else {
                // Rare large FI: use R0 as address temp (xor-zero after).
                // ADDI takes byte offset; ST32 at [R0+0].
                emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W,
                                 Haydn::R0, [&](MachineInstrBuilder MIB) {
                                   MIB.addReg(FrameReg).addImm(Off);
                                 });
                emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                              [&](MachineInstrBuilder MIB) {
                                MIB.addReg(Scr, getKillRegState(true))
                                    .addReg(Haydn::R0)
                                    .addImm(0);
                              });
                emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::XOR32,
                                 Haydn::R0, [&](MachineInstrBuilder MIB) {
                                   MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                                 });
              }
            },
            /*Exclude=*/Prefer.isPhysical() ? ArrayRef<Register>{Prefer}
                                            : ArrayRef<Register>{});
      } else {
        // Prefer holds trip at SET; store it to FI before erase.
        if (isInt<6>(Elem)) {
          emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Prefer).addReg(FrameReg).addImm(Elem);
                        });
        } else {
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::ADDI32_W, Haydn::R0,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(FrameReg).addImm(Off);
                           });
          emitExactLate(*Preheader, Ins, DL, TII, Haydn::ST32,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Prefer).addReg(Haydn::R0).addImm(0);
                        });
          emitExactLateDef(*Preheader, Ins, DL, TII, Haydn::XOR32, Haydn::R0,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                           });
        }
      }
      UseStackCounter = true;
      InstallSoftLoop = true;
      LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote stack-counter FI#"
                        << StackCounterFI << "\n");
    }
  }

  if (!InstallSoftLoop) {
    // Live body, no free counter / unusable trip — refuse erase-only.
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote refused — no free counter "
                         "GPR for soft edge (body still live)\n");
    return false;
  }

  // Strip residual countdown of CountReg *before* erasing SET / rewriting
  // latch, while Latch is still intact. (prior remat may leave Prefer+=-1.)
  if (!UseStackCounter)
    stripResidualCountdown(Latch, CountReg);
  else if (Prefer.isPhysical())
    stripResidualCountdown(Latch, Prefer);

  // L1: erase hardware setup (SET + PLE)
  eraseHardwareLoopSetup(SetMI);
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

  // Drop existing top-level terminators, then final-real SUBI32+BNEZ_W +
 // optional B Exit. : no residual LoopDec/LoopJNZ after late commit.
  SmallVector<MachineInstr *, 4> Terms;
  for (MachineInstr &MI : Latch->instrs()) {
    if (MI.isBundledWithPred())
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
    assert((Off % 4) == 0 && "stack-counter FI must be word-aligned");
    const int64_t Elem = Off / 4;
    MachineBasicBlock::iterator LatchEnd = Latch->end();
    // Stack-counter path builds several real MIs; exact-commit each singleton
    // so second BR / Verify see committed FormatID cycles (no residual bare
    // SUBI32/BNEZ_W for the late firewall to invent).
    withPostRAScratch(
        *Latch, LatchEnd, DL, TII, ST, /*PreferNotR12=*/true,
        [&](Register Scr) {
          if (isInt<6>(Elem)) {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, Scr,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Elem);
                             });
          } else {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::ADDI32_W,
                             Haydn::R0, [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Off);
                             });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::LD32, Scr,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addImm(0);
                             });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::XOR32, Haydn::R0,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                             });
          }
          // Final-real countdown: counter -= 1; branch if nonzero.
          emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::SUBI32, Scr,
                           [&](MachineInstrBuilder MIB) {
                             MIB.addReg(Scr).addImm(1);
                           });
          if (isInt<6>(Elem)) {
            emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                          [&](MachineInstrBuilder MIB) {
                            MIB.addReg(Scr).addReg(FrameReg).addImm(Elem);
                          });
          } else {
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::ADDI32_W,
                             Haydn::R0, [&](MachineInstrBuilder MIB) {
                               MIB.addReg(FrameReg).addImm(Off);
                             });
            emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::ST32,
                          [&](MachineInstrBuilder MIB) {
                            MIB.addReg(Scr).addReg(Haydn::R0).addImm(0);
                          });
            emitExactLateDef(*Latch, LatchEnd, DL, TII, Haydn::XOR32, Haydn::R0,
                             [&](MachineInstrBuilder MIB) {
                               MIB.addReg(Haydn::R0).addReg(Haydn::R0);
                             });
          }
          emitExactLate(*Latch, LatchEnd, DL, TII, Haydn::BNEZ_W,
                        [&](MachineInstrBuilder MIB) {
                          MIB.addReg(Scr, getKillRegState(true)).addMBB(Header);
                        });
        });
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote stack-counter exact-commit "
                         "SUBI32+BNEZ_W FI#"
                      << StackCounterFI << "\n");
  } else {
    // Final-real soft edge: SUBI32 count,count,1 + BNEZ_W count, Header.
    // Residual countdown was stripped above; never double-dec.
  // Exact-commit each edge as a product singleton before second BR.
    emitExactLateDef(*Latch, Latch->end(), DL, TII, Haydn::SUBI32, CountReg,
                     [&](MachineInstrBuilder MIB) {
                       MIB.addReg(CountReg).addImm(1);
                     });
    emitExactLate(*Latch, Latch->end(), DL, TII, Haydn::BNEZ_W,
                  [&](MachineInstrBuilder MIB) {
                    MIB.addReg(CountReg).addMBB(Header);
                  });
    LLVM_DEBUG(dbgs() << "HaydnHardwareLoops: demote exact-commit SUBI32+BNEZ_W on "
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

  return true;
}
