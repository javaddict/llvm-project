//===-- HaydnFixupHwLoops.cpp - Post-layout HW loop validation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Late pass (after BranchRelaxation): re-check SET_HWLOOP / LoopStart under
// the final size model, enforce Following >= InterveningCycles
// (SetupIssueDistance=3), body >= MinBodyBundles with strict END>BEGIN
// (END = last body cycle), and keep Off1/Off2 inside the encoder's
// uimm6/uimm12/4 fields.
//
// Numeric limits: HaydnHWLoopContracts.h (shared with formation).
//
// Opt before RA; post-RA Fixup is correctness only.
//
// Peer-realignment 2026-08-22 (topics/hwloop PM2): no peer reorders user
// code for hwloop timing. AIE reserves setup room via scheduler exit-edge
// latency and satisfies byte distance by PADDING only — empty bundles
// materialize NoOps (AIEMachineScheduler.cpp:843-859 insertNoop) and bundle
// elongation NOP-fills (AIEMachineAlignment.cpp:62-101); the only MI AIE
// ever moves is the branch for delay-slot distance. Hexagon's fixup is
// offset math + extender opcode swap. The former free-lift rescue
// (tryShortenStartOffset) is DELETED; Fixup never reorders user code.
//
// Product narrative: demote-first, NOT erase-only.
// Generic HardwareLoops + Role A expand (HaydnHardwareLoops) already replaced
// the software back-edge with LoopStart/PseudoLoopEnd. Erasing SET alone on a
// *live* body yields a once-through fallthrough (wrong-code).
// Product recovery is final-real SUBI32+BNEZ_W when a free counter exists;
// live demote failure is fatal. demote OFF is debug-only and is also
// fatal on a live body (never erase-only once-through).
//
// Scheduling-unit / layout contracts:
// • Bundle-preserving: never unconditional SET unbundle; erase SET member only.
// • Final-real demotion (no residual LoopDec/LoopJNZ after late commit).
// • Residual generic SET_HWLOOP{,_REG} is fatal. ExpandPseudos owns the
//   rewrite to SET_HWLOOP_{W,F2_W}; Fixup is Hexagon-style range recheck /
//   pad / fatal only (HexagonFixupHwLoops.cpp:75-81, 136-148).
// • Sum still-relaxable branch growth in SET→BEGIN/SET→END vs Off margins.
// • Every Fixup-created real MI (deficit NOP pads, demote trip materialize,
//   stack-counter LD/ST glue, SUBI32+BNEZ_W soft edge, exit B) uses the shared
//   exact-commit surface (commitLateProductCycle → setDesc member →
//   finalizeBundle + FormatID) so the second BranchRelaxation charges
//   committed EncodedBytes, not bare MIs.
//
// Closed contracts:
//
// 1. Live MBB operands
// SET_HWLOOP{,_REG} carries Header/Latch as MBB operands. Later CFG
// edits can erase those blocks while leaving the SET behind
// (MIR shows `%bb.-1`). Touching a dead MBB is undefined. Rule: if
// Header or Latch is not a live member of this MachineFunction, erase
// the SET only (loop body is gone / peeled). Never demote soft-loop
// against a dead pointer.
//
// 2. Off1/Off2 range
// Off1 = uimm6×4 ≤ 252 B (safety margin → MaxOff1BytesSafe).
// Off2 = uimm12×4 ≤ 16380 B. Distance is measured forward in layout
// from the SET cycle's parcel base — the same PC anchor the encoder uses
// (HaydnAsmBackend::evaluateFixup % Parcel seeding; CB-164). If Header is
// not after SET in layout, Off is unknown → treat as range-bad.
//
// 3. Recoverability ladder (correctness only; peer-aligned 2026-08-22)
// a. Pad setup gap only (deficit-only InterveningCycles NOPs after SET → BEGIN).
// b. padBodyToMinLaw — NOP-pad a short body up to MinBodyBundles before
//    demoting (the AIE materializeNoop analog; never reorder user code —
//    the former tryShortenStartOffset free-lift leg is deleted, see the
//    file header).
// c. demote (llvm::demoteHardwareLoopToSoftware) when hard Off1/Off2 illegal.
// Soft-loop restore is *closed* (total, like AIE expand):
// • Collect loop blocks by CFG (reverse from Latch to Header)
// never by layout range — layout can put Latch before Header.
// • CountReg = trip Prefer if body does not non-countdown-def it;
// else scavenge a reg not mentioned in any loop block.
// • Materialize trip into CountReg at the SET site when needed.
// • Strip residual countdown of CountReg from the latch (generic
// HardwareLoops LoopDec / leftover Prefer+=-1 that Role A expand does
// not consume), then always install SUBI32+BNEZ_W.
// Never BNEZ-only, never double-dec.
// • SET_HWLOOP imm: materialize into a free reg via ADDI, then
// SUBI32+BNEZ_W.
// d. If demote cannot install a correct soft edge on a *live* body
// report_fatal_error — never erase-only once-through.
// Dead Header/Latch still allow erase-setup only (body gone).
// Debug only: -haydn-enable-hwloop-demote=false is still fatal on a
// live body (never erase-only once-through).
// 4. Pipeline
// BranchRelaxation → FixupHwLoops → BranchRelaxation (standalone PreEmit
// row) and the same Fixup re-invocation inside HaydnLateConvergence after
// S2/stalls. Each invocation rewrites Off1/Off2 from the live inventory
// (inner-first nested cascade). Fixup growth cannot leave branches past
// simm12 because BranchRelaxation is last in the mutating iteration.
//
// AsmPrinter is the emit-side twin: no START/END temp symbols without a
// real body instruction to flush them.
//
//===----------------------------------------------------------------------===//

#include "HaydnFixupHwLoops.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnFrameLowering.h"
#include "HaydnHardwareLoops.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>

using namespace llvm;

// MI-level demote/erase helpers shared with formation live in
// HaydnHWLoopDemote.{h,cpp} (single owner; previously duplicated verbatim
// in this TU). The demote/erase entry points themselves are the exported
// llvm::demoteHardwareLoopToSoftware / llvm::eraseHardwareLoopSetup
// (HaydnHardwareLoops.cpp); fixupOne calls those directly.
using haydn::hwloop::buildExactLate;
using haydn::hwloop::finalizeExactLateSingleton;
using haydn::hwloop::isLiveMBB;
using haydn::hwloop::lateMemberOpcode;

#define DEBUG_TYPE "haydn-fixup-hwloops"

// Product demote policy lives in HaydnHWLoopDemote.cpp
// (isHwLoopDemoteEnabled). Default ON — demote-first, not erase-only.
// demoteHardwareLoopToSoftware refuses a live body when the flag is OFF
// so recoverRangeOrOrder fatals (never silent erase-only once-through).
namespace {

// Opcode lists live on TII (isHardwareLoopSetupOpcode / Reg/Imm).
// Thin locals keep call sites short; no second divergent table.

// Aliases from HaydnHWLoopContracts.h / BundlePlan EncodedBytes.
// Ceil byte→parcel and setup distances use productParcelBytes /
// ceilProductParcels — not a second hard-coded parcel size.
// Following floor is InterveningCycles (2), not SetupIssueDistance (3).
// MinSetupBundles remains the compatibility alias of InterveningCycles.
static constexpr unsigned InterveningCycles =
    haydn::hwloop::InterveningCycles;
static constexpr unsigned MinSetupBundles = haydn::hwloop::MinSetupBundles;
static_assert(MinSetupBundles == InterveningCycles,
              "Fixup Following floor must be InterveningCycles");
// CB-164: offsets anchor at the SET parcel base, so the setup timing floor
// is MinSetupIssueBytes (= MinSetupBytes + one SET parcel) — not the bare
// after-SET intervening span.
static_assert(haydn::hwloop::MinSetupIssueBytes ==
                  haydn::hwloop::MinSetupBytes +
                      haydn::bundle::productBundlesToBytes(1),
              "SET-anchored setup floor = intervening span + SET parcel");
static constexpr int64_t MaxOff1Bytes = haydn::hwloop::MaxStartOffsetBytes;
static constexpr int64_t MaxOff2Bytes = haydn::hwloop::MaxEndOffsetBytes;
static constexpr int64_t MaxOff1BytesSafe =
    haydn::hwloop::MaxStartOffsetBytesSafe;

} // namespace

char HaydnFixupHwLoops::ID = 0;

INITIALIZE_PASS(HaydnFixupHwLoops, DEBUG_TYPE, "Haydn Hardware Loop Fixup",
                false, false)

HaydnFixupHwLoops::HaydnFixupHwLoops() : MachineFunctionPass(ID) {
  initializeHaydnFixupHwLoopsPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnFixupHwLoopsPass() {
  return new HaydnFixupHwLoops();
}

//===----------------------------------------------------------------------===//
// shared exact-commit for late Fixup-created singletons
//===----------------------------------------------------------------------===//
//
// Declared late creators must call the shared exact no-split surface before
// the next layout consumer (second BranchRelaxation). Shape matches
// HaydnFinalizeBundle / PostRAScratch remat glue:
//   commitLateProductCycle(Logical) → member setDesc target
//   BuildMI(member)
//   finalizeBundle + stampBundleCommit (Format E row + completion)
// Idempotent with the late finalize firewall (already-bundled roots skipped).
//
// Formation pads (HaydnHardwareLoops, pre-pack) stay bare logical NOPs so the
// post-RA pack owns the first commit. Fixup is post-pack / PreEmit only and
// must not leave residual bare real MIs for the firewall to invent.

// Shared exact-late surface lives in HaydnBundleMaterialize.h
// (lateProductMemberOpcode / finalizeExactLateSingleton). Fixup pads and
// demotion use it so the second BR charges committed EncodedBytes for every
// late product cycle. insertBranch stays bare (one product parcel).


// Next *bundle-boundary* iterator after \p MI. `std::next(MI.getIterator)`
// advances one instruction and can land on a BUNDLE interior
// (`isBundledWithPred`); constructing `MachineBasicBlock::iterator` from
// that asserts (— CoreMark/moddi3 after PostRA co-issue).
// When SET is mid-bundle, skip to after the whole coissued cycle
// so Following counts subsequent issue cycles, not co-members.
static MachineBasicBlock::iterator
nextBundleBoundary(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  assert(MBB && "MI must be inserted");
  MachineBasicBlock::instr_iterator II = std::next(MI.getIterator());
  while (II != MBB->instr_end() && II->isBundledWithPred())
    ++II;
  if (II == MBB->instr_end())
    return MBB->end();
  return MachineBasicBlock::iterator(II);
}

// Top-level MI for layout edits when \p MI may be a BUNDLE interior.
// Bundle-preserving Fixup splices/pads relative to this root, never unbundles
// a legal coissued SET cycle just to walk iterators.

unsigned HaydnFixupHwLoops::countFollowingBundles(
    MachineInstr &SetMI, const HaydnInstrInfo &TII) const {
  unsigned Bundles = 0;
  MachineBasicBlock *MBB = SetMI.getParent();
  for (MachineBasicBlock::iterator I = nextBundleBoundary(SetMI),
                                   E = MBB->end();
       I != E; ++I) {
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isImplicitDef())
      continue;
    if (I->isKill())
      continue;
    unsigned Bytes = TII.getInstSizeInBytes(*I);
    if (Bytes == 0)
      continue;
  // Parcel count via product EncodedBytes (generated Full Size).
    Bundles += haydn::bundle::ceilProductParcels(Bytes);
    // W61: the first size-bearing terminator (the unconditional B to the
    // header, a guarding conditional branch) is itself an intervening issue
    // cycle between SET and BEGIN — it executes on every activation path and
    // the byte-law walk (estimateMBBDistance → StartOff) already charges it.
    // Count it, then stop: parcels past the first terminator may be skipped
    // on a taken edge. Matches the scheduler tail credit
    // (countSizeBearingTailParcels) so a credited gap is not re-padded here.
    if (I->isTerminator())
      break;
  }
  return Bundles;
}

// Conservative layout pad when entering a later MBB. Hexagon aligns the
// running offset to MBB.getAlignment(); Haydn can emit only whole product
// EncodedBytes parcels, so the gap is charged as ceilProductParcels ×
// generated EncodedBytes. No magic 12/16 quantum.
static int64_t padLayoutBytesForMBBAlign(int64_t Bytes,
                                         const MachineBasicBlock &MBB) {
  const Align A = MBB.getAlignment();
  if (A == Align(1) || Bytes < 0)
    return Bytes;
  const uint64_t Need = alignTo(static_cast<uint64_t>(Bytes), A);
  if (Need <= static_cast<uint64_t>(Bytes))
    return Bytes;
  const unsigned Gap =
      static_cast<unsigned>(Need - static_cast<uint64_t>(Bytes));
  return Bytes + haydn::bundle::productBundlesToBytes(
                     haydn::bundle::ceilProductParcels(Gap));
}

// Conservative layout distance From→To in layout order (only forward).
int64_t HaydnFixupHwLoops::estimateMBBDistance(
    const MachineFunction &MF, const MachineBasicBlock *FromMBB,
    MachineBasicBlock::const_iterator FromIt, const MachineBasicBlock *ToMBB,
    const HaydnInstrInfo &TII) const {
  if (!isLiveMBB(MF, FromMBB) || !isLiveMBB(MF, ToMBB))
    return -1;
  int64_t Bytes = 0;
  bool Started = false;
  for (const MachineBasicBlock &MBB : MF) {
    if (&MBB == FromMBB)
      Started = true;
    if (!Started)
      continue;
    // Already inside FromMBB (AfterSet). Charge alignment only when
    // entering a subsequent MBB in layout order.
    if (&MBB != FromMBB)
      Bytes = padLayoutBytesForMBBAlign(Bytes, MBB);
    auto Begin = (&MBB == FromMBB) ? FromIt : MBB.begin();
    for (auto I = Begin, E = MBB.end(); I != E; ++I) {
      if (&MBB == ToMBB && I == ToMBB->begin())
        return Bytes;
      Bytes += TII.getInstSizeInBytes(*I);
    }
    if (&MBB == ToMBB)
      return Bytes;
  }
  return -1; // To not after From in layout.
}

bool HaydnFixupHwLoops::computeOffsets(MachineInstr &SetMI,
                                       const HaydnInstrInfo &TII,
                                       int64_t &StartOff, int64_t &EndOff,
                                       MachineBasicBlock *&StartMBB,
                                       MachineBasicBlock *&EndMBB) const {
  StartOff = EndOff = -1;
  StartMBB = EndMBB = nullptr;

  const MachineFunction *MF = SetMI.getParent() ? SetMI.getParent()->getParent()
                                                : nullptr;
  if (!MF)
    return false;

  // LoopStart (IR ZOL): Header from CFG-or-layout-tail; Latch is END
  // (single-BB Header==Latch). Multi-BB residual LoopStart must not
  // treat Header as END — that under-counts Off2. Expand normally
  // rewrites to SET_HWLOOP_F2_W with Header/Latch operands first.
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

  // After the SET *cycle*: next top-level MI past the whole coissued BUNDLE
  // Bundle-preserving: never hand a mid-bundle iterator to
  // MachineInstrBundleIterator (asserts isBundledWithPred).
  MachineBasicBlock *Pre = SetMI.getParent();
  MachineBasicBlock::iterator AfterSet = nextBundleBoundary(SetMI);
  StartOff = estimateMBBDistance(*MF, Pre, AfterSet, StartMBB, TII);
  EndOff = estimateMBBDistance(*MF, Pre, AfterSet, EndMBB, TII);
  // CB-164: MC anchors HWLoopOff1/Off2 at the SET parcel base, not after
  // the SET cycle. HaydnAsmBackend::evaluateFixup seeds Value = Abs % Parcel
  // so MCAssembler's PC-rel subtract lands on align_down(fixup_loc, Parcel)
  // — the parcel the SET member encodes in. Charge the SET cycle's committed
  // EncodedBytes so the accepted value IS the encoded displacement (measured-
  // after-SET accepted a 252 B Off1 that encoded as 264 B → uimm66 > 63).
  // Child-in-bundle has size 0 and the root is the SET cycle: topLevelForLayout
  // resolves the coissued root either way (single size oracle, no new table).
  MachineInstr &SetCycle = haydn::hwloop::topLevelForLayout(SetMI);
  const int64_t SetParcelBytes = static_cast<int64_t>(
      haydn::bundle::committedEncodedBytes(SetCycle).Value);
  StartOff = haydn::hwloop::anchoredFromAfterSet(StartOff, SetParcelBytes);
  EndOff = haydn::hwloop::anchoredFromAfterSet(EndOff, SetParcelBytes);
  // HWLR_END is the start of the last size-bearing non-terminator cycle
  // in EndMBB (golden: last body bundle). One closed walk: skip
  // terminators — PseudoLoopEnd and post-PLE soft-exit B/cond sit past
  // END (AsmPrinter getLastRealInstr). No second fallback path.
  if (EndMBB && EndOff >= 0) {
    int64_t Cursor = EndOff;
    int64_t LastCycleStart = -1;
    for (const MachineInstr &MI : *EndMBB) {
      if (MI.isTerminator())
        continue;
      unsigned Bytes = TII.getInstSizeInBytes(MI);
      if (Bytes == 0)
        continue;
      LastCycleStart = Cursor;
      Cursor += static_cast<int64_t>(Bytes);
    }
    if (LastCycleStart < 0)
      return false; // empty body — no END cycle
    EndOff = LastCycleStart;
  }
  return true;
}


bool HaydnFixupHwLoops::fixupOne(MachineInstr &SetMI,
                                   const HaydnInstrInfo &TII) {
  bool Changed = false;
  DebugLoc DL = SetMI.getDebugLoc();
  MachineBasicBlock *Pre = SetMI.getParent();
  if (!Pre)
    return false;
  const MachineFunction &MF = *Pre->getParent();

  // Bundle-preserving Fixup. Never unconditional unbundle of a legal
  // coissued SET cycle. Layout helpers (nextBundleBoundary) walk around
  // BUNDLE interiors; eraseHardwareLoopSetup
  // removes only the SET member via the shared recommit path.

  // Dead body / stale MBB operands (contract §1)
  // SET_HWLOOP with %bb.-1: body was erased after convert. Erase setup only.
  // Product selector domain is {0,1}: out-of-domain sel stays unavailable and
  // demotes/erases fail-closed (never invent extra CSR/selector identities).
  {
    unsigned Opc = SetMI.getOpcode();
    if (TII.isHardwareLoopSetupOpcode(Opc) && Opc != Haydn::LoopStart) {
      if (SetMI.getNumOperands() < 4 || !SetMI.getOperand(0).isImm() ||
          !SetMI.getOperand(1).isMBB() || !SetMI.getOperand(2).isMBB()) {
        report_fatal_error(
            "HaydnFixupHwLoops: malformed SET_HWLOOP; refusing erase-only "
            "once-through",
            /*gen_crash_diag=*/false);
      }
      const int64_t Sel = SetMI.getOperand(0).getImm();
      if (!haydn::hwloop::isProductSelector(Sel)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: out-of-domain selector "
                          << Sel << " — demote-first / fatal fail-closed\n");
        if (demoteHardwareLoopToSoftware(SetMI, TII, "HaydnFixupHwLoops",
                                         haydn::hwloop::resolveBodyMBBFixup))
          return true;
        report_fatal_error(
            "HaydnFixupHwLoops: unsupported SET_HWLOOP selector cannot "
            "demote to software loop; refusing erase-only once-through",
            /*gen_crash_diag=*/false);
      }
      MachineBasicBlock *H = SetMI.getOperand(1).getMBB();
      MachineBasicBlock *L = SetMI.getOperand(2).getMBB();
      if (!isLiveMBB(MF, H) || !isLiveMBB(MF, L)) {
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: stale Header/Latch "
                             "(%bb.-1 or foreign) — erase SET only\n");
        return eraseHardwareLoopSetup(SetMI, "HaydnFixupHwLoops",
                                  haydn::hwloop::resolveBodyMBBFixup);
      }
      // Product programs HWLR only through SET. A body CSRW to the
      // unpublished 0x20-0x25 window is fail-closed: demote would leave
      // the write, so fatal (same law as formation scanRoleABlock).
      // Walk BUNDLE interiors — post-RA pack can hide CSRW as a child.
      haydn::hwloop::LoopBlockSet Blocks;
      haydn::hwloop::collectLoopBlocks(H, L, Pre, Blocks);
      if (haydn::hwloop::loopBlocksContainUnpublishedHwlrCsr(Blocks))
        report_fatal_error(
            "HaydnFixupHwLoops: unpublished HWLR CSR write in "
            "hardware-loop body; product programs HWLR only through "
            "SET_HWLOOP",
            /*gen_crash_diag=*/false);
    } else if (Opc == Haydn::LoopStart) {
      MachineBasicBlock *Body = haydn::hwloop::resolveBodyMBBFixup(SetMI);
      if (!Body) {
        // Unresolved LoopStart: cannot prove the body is dead. Hexagon
        // FixupHwLoops.cpp:137-148 converts or leaves LOOP; it never
        // erases to a once-through fallthrough. Overlay: debug demote
        // OFF fatals; product demote ON may L1-erase only when no latch
        // was found (no back-edge remains).
        if (!haydn::hwloop::isHwLoopDemoteEnabled())
          report_fatal_error(
              "HaydnFixupHwLoops: LoopStart with unresolved body; "
              "demote disabled; refusing erase-only once-through",
              /*gen_crash_diag=*/false);
        LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: LoopStart with no live body "
                             "— erase setup\n");
        return eraseHardwareLoopSetup(SetMI, "HaydnFixupHwLoops",
                                  haydn::hwloop::resolveBodyMBBFixup);
      }
    }
  }

  // Setup gap: Following >= InterveningCycles (SetupIssueDistance=3).
  // Deficit-only NOPs after the SET cycle (bundle root if coissued).
  // leaveRegion handleRegionConflicts owns ExitReady + inter-zone trailing
  // pads for multi-MI scheduled regions; this residual path covers single-MI
  // skip regions and short useful-window fill. Pad-drop of formation sprays
  // stays deferred until both owners prove redundant together.
  // Each pad is exact-committed (shared commitLateProductCycle surface) so the
  // second BranchRelaxation charges committed EncodedBytes.
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
      MachineInstr *Pad =
          buildExactLate(*MBB, InsertPt, DL, TII, Haydn::NOP);
      finalizeExactLateSingleton(*Pad);
    }
    Changed = true;
  }
#ifndef NDEBUG
  assert(countFollowingBundles(SetMI, TII) >= InterveningCycles &&
         "Following >= InterveningCycles after deficit-only pad");
#endif

  // Range re-check (begin + end) — one path for SET_* and LoopStart.
  // Product (default demote ON): demote-first on unencodable/range-bad SET.
  // Debug only (demote OFF): still fatal on a live body (never erase-only).
  auto recoverRangeOrOrder = [&](const char *Why) -> bool {
    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: " << Why
                      << (haydn::hwloop::isHwLoopDemoteEnabled()
                              ? " — demote-first (product)\n"
                              : " — demote disabled; refuse erase-only\n"));
    if (demoteHardwareLoopToSoftware(SetMI, TII, "HaydnFixupHwLoops",
                                     haydn::hwloop::resolveBodyMBBFixup))
      return true;
    // Live body, soft edge not installable (or demote off). Never
    // silent single-pass body.
    report_fatal_error(
        "HaydnFixupHwLoops: out-of-range/invalid SET_HWLOOP cannot demote to "
        "software loop (no free counter GPR or usable exit); refusing "
        "erase-only once-through",
        /*gen_crash_diag=*/false);
  };

  int64_t StartOff = -1, EndOff = -1;
  MachineBasicBlock *StartMBB = nullptr, *EndMBB = nullptr;
  if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB)) {
    return recoverRangeOrOrder("computeOffsets failed");
  }

  // Body floor residual: size-bearing parcels BEGIN..END inclusive must meet
  // MinBodyBundles. Formation may leave short bodies after post-RA pack;
  // pad with exact-commit NOP cycles before demoting (product keep path).
  auto padBodyToMinLaw = [&]() -> bool {
    if (!EndMBB || StartOff < 0 || EndOff < 0)
      return false;
    unsigned Parcels = 0;
    if (EndOff > StartOff)
      Parcels = haydn::hwloop::bodyParcelsFromOffsets(StartOff, EndOff);
    else if (EndOff == StartOff)
      Parcels = 1; // single size-bearing body cycle (END == BEGIN start)
    else
      return false;
    if (Parcels >= haydn::hwloop::MinBodyBundles)
      return false;
    unsigned Deficit = haydn::hwloop::MinBodyBundles - Parcels;
    // Insert before the first terminator (PseudoLoopEnd / RET / soft edge).
    // Never append after a terminator — that fails the machine verifier.
    MachineBasicBlock::iterator InsertPt = EndMBB->getFirstTerminator();
    if (InsertPt == EndMBB->end()) {
      // No terminator yet: still prefer ZOL latch metas if present bare.
      for (MachineInstr &MI : *EndMBB) {
        if (MI.isBundledWithPred())
          continue;
        unsigned Opc = MI.getOpcode();
        if (Opc == Haydn::PseudoLoopEnd || Opc == Haydn::LoopJNZ ||
            Opc == Haydn::LoopDec) {
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
    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      return recoverRangeOrOrder("computeOffsets failed after body pad");
  }

  auto rangeBad = [&](bool HardOff1Only) {
    if (StartOff < 0 || EndOff < 0)
      return true;
    // Soft margin miss: NOP-pad + re-check (lift deleted). Hard past
    // uimm6: demote.
    int64_t Off1Lim = HardOff1Only ? MaxOff1Bytes : MaxOff1BytesSafe;
    if (StartOff > Off1Lim)
      return true;
    if (EndOff > MaxOff2Bytes)
      return true;
    // Reloc law: Off1/Off2 encode byte_delta >> 2. Non-scale or over-field
    // distances stay unavailable and demote fail-closed (no invent).
    if (!haydn::hwloop::offsetsMeetImmRelocLaw(StartOff, EndOff))
      return true;
    // Strict END > BEGIN; body parcels BEGIN..END inclusive >= MinBodyBundles.
    if (!haydn::hwloop::bodyMeetsMinLaw(StartOff, EndOff))
      return true;
    // MinSetupBytes = InterveningCycles × productParcelBytes (after-SET
    // span). CB-164: offsets now anchor at the SET parcel base, so the
    // equivalent floor is MinSetupIssueBytes = MinSetupBytes + one parcel
    // (the SET cycle itself) — same accepted geometry, same timing law.
    if (StartOff < haydn::hwloop::MinSetupIssueBytes)
      return true;
    // Imm trip COUNT must fit the uimm16 field and meet MinCount.
    // Over-field values demote (or fatal when demote is off) rather
    // than reaching MC as an unencodable immediate.
    if (TII.isHardwareLoopImmTripOpcode(SetMI.getOpcode()) &&
        SetMI.getNumOperands() >= 4 && SetMI.getOperand(3).isImm() &&
        !haydn::hwloop::countMeetsFieldLaw(SetMI.getOperand(3).getImm()))
      return true;
    return false;
  };

  if (rangeBad(/*HardOff1Only=*/false)) {
    // Peer-aligned recovery (2026-08-22, topics/hwloop PM2): no peer reorders
    // user code for hwloop timing — AIE pads (AIEMachineScheduler.cpp:843-859
    // insertNoop; AIEMachineAlignment.cpp:62-101 elongation NOP fill) and
    // never reorders user code for hwloop timing. The former free-lift leg
    // (tryShortenStartOffset) is deleted; NOP-pad the body, then re-check.
    if (padBodyToMinLaw())
      Changed = true;

    if (!computeOffsets(SetMI, TII, StartOff, EndOff, StartMBB, EndMBB))
      return recoverRangeOrOrder("computeOffsets failed after body pad");

    // Soft miss OK if hard uimm6 holds. Else demote (never break remat).
    if (rangeBad(/*HardOff1Only=*/true)) {
      LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: hard Off1 still bad startOff="
                        << StartOff << " endOff=" << EndOff
                        << " (no lift — peer law; demote)\n");
      return recoverRangeOrOrder("range still bad after NOP pad");
    }
  }
  // Second BranchRelaxation may still grow short PC-relative branches in
  // SET→BEGIN and SET→END. Sum the contracts per-site expansion budget over
  // still-relaxable sites only; demote if residual Off1/Off2 margin cannot
  // absorb it. Constant: haydn::hwloop::MaxSingleBranchGrowthBytes
  // (LUI+ADDI32_W+JALR_W+pad parcels × product EncodedBytes). Second BR must
  // not invalidate acceptance.
  //
  // Not still-relaxable (zero further layout growth under second BR):
  //   already-indirect JALR*, long-reach JAL*, pure calls, ZOL latch metas.
  // Charging those would false-demote dense but already-final control flow.
  {
    using haydn::hwloop::BranchRelaxSafetyBufferBytes;
    using haydn::hwloop::MaxSingleBranchGrowthBytes;
    static_assert(BranchRelaxSafetyBufferBytes == MaxSingleBranchGrowthBytes,
                  "BR safety buffer is the contracts growth budget");

    auto isStillRelaxableShortBranch = [](const MachineInstr &Br) -> bool {
      if (!Br.isBranch())
        return false;
      // Already-indirect forms cannot grow further under BranchRelaxation.
      if (Br.isIndirectBranch())
        return false;
      // Calls (including JAL_W) are long-reach / not the short simm12 path.
      if (Br.isCall())
        return false;
      switch (Br.getOpcode()) {
      // ZOL / software-latch metas: not PC-relative BR subjects (see
      // HaydnInstrInfo::isBranchOffsetInRange). Fixup owns their lowering.
      case Haydn::PseudoLoopEnd:
      case Haydn::LoopJNZ:
      case Haydn::LoopDec:
      case Haydn::LoopStart:
      // Long-reach / already-final control (member forms included by
      // isIndirectBranch / isCall above; list logical/wide bases for clarity).
      case Haydn::JAL:
      case Haydn::JAL_W:
      case Haydn::JALR:
      case Haydn::JALR_W:
      case Haydn::PseudoCALL:
      case Haydn::BR_JT:
      case Haydn::RET:
        return false;
      default:
        // Bare/member short cond + B (simm12). Second BR may expand each to
        // an inverted near + trampoline or LUI+ADDI+JALR sequence.
        return true;
      }
    };

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
    // Inclusive=false: stop at ToMBB begin (BEGIN label = first real).
    // Inclusive=true: charge through the last body cycle only. HWLR_END is the
    // last size-bearing body parcel; soft-exit B/cond after PseudoLoopEnd sit
    // past END and cannot grow Off2. Charging them false-demotes dense Role-A
    // single-BB forms that keep an explicit B to a non-layout soft exit.
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
          // Inclusive END window ends at the ZOL latch meta: do not charge
          // PseudoLoopEnd or any terminator after it (post-END soft exit).
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
      return recoverRangeOrOrder("second-BR growth exceeds SET Off margin");
    }
  }

  LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: startOff=" << StartOff
                    << " endOff=" << EndOff
                    << " sameMBB=" << (StartMBB == EndMBB)
                    << " followingBundles="
                    << countFollowingBundles(SetMI, TII) << "\n");

  return Changed;
}

/// Sequentialize multi-member shells that coissue SET_HWLOOP with a producer
/// of its trip/Off GPRs (snapshot no-forwarding). Remat glue and post-pipeliner
/// trip adjust can stamp ADDI+SET after a preheader leaveMBB has already run;
/// this is the late owned correctness net before Off recompute / demote.
static bool sequentializeIllegalHwloopTripCoissue(MachineFunction &MF,
                                                  const HaydnInstrInfo &TII) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  SmallVector<MachineInstr *, 8> Roots;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBundle() || MI.isBundledWithPred())
        continue;
      if (haydn::bundle::members(MI).size() >= 2)
        Roots.push_back(&MI);
    }
  }

  for (MachineInstr *Root : Roots) {
    if (!Root || !Root->getParent())
      continue;
    MachineBasicBlock &MBB = *Root->getParent();
    SmallVector<MachineInstr *, 3> Kids = haydn::bundle::members(*Root);
    if (Kids.size() < 2)
      continue;
    if (!haydn::bundle::cycleMembersHaveHwloopTripConflict(Kids, TII, TRI))
      continue;

    LLVM_DEBUG(dbgs() << "HaydnFixupHwLoops: sequentialize SET trip/Off "
                         "coissue in bb."
                      << MBB.getNumber() << "\n");
    for (MachineInstr *K : Kids) {
      if (!K)
        continue;
      for (MachineOperand &MO : K->operands()) {
        if (MO.isReg() && MO.isInternalRead())
          MO.setIsInternalRead(false);
      }
      if (K->isBundledWithPred())
        K->unbundleFromPred();
      if (K->isBundledWithSucc())
        K->unbundleFromSucc();
    }
    Root->eraseFromParent();
    // Keep schedule/def-before-use order; re-commit each as a late singleton
    // so EncodedBytes stay Format E product parcels for Off measurement.
    for (MachineInstr *K : Kids) {
      if (!K || !K->getParent())
        continue;
      unsigned Member = lateMemberOpcode(K->getOpcode());
      if (Member != K->getOpcode())
        K->setDesc(TII.get(Member));
      finalizeExactLateSingleton(*K);
    }
    Changed = true;
  }
  return Changed;
}

void HaydnFixupHwLoops::collectRetainedSetups(
    MachineFunction &MF, const HaydnInstrInfo &TII,
    SmallVectorImpl<MachineInstr *> &Sets) const {
  Sets.clear();
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      if (TII.isHardwareLoopSetupInstr(MI))
        Sets.push_back(&MI);
    }
  }
}

bool HaydnFixupHwLoops::setupWindowContains(
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

void HaydnFixupHwLoops::sortInnermostFirst(
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

void HaydnFixupHwLoops::gatePostDemotePreservation(
    MachineFunction &MF, const HaydnInstrInfo &TII,
    MachineBasicBlock *Preheader, MachineBasicBlock *Header,
    MachineBasicBlock *Latch, unsigned FrameObjectsBefore) {
  (void)TII;
  const unsigned FrameObjectsAfter = MF.getFrameInfo().getNumObjects();
  if (FrameObjectsAfter > FrameObjectsBefore)
    report_fatal_error(
        "HaydnFixupHwLoops: hwloop demote created a frame object; demote "
        "homes are preallocated before PEI",
        /*gen_crash_diag=*/false);

  if (!haydn::hwloop::isLiveMBB(MF, Header) ||
      !haydn::hwloop::isLiveMBB(MF, Latch))
    return; // L1 erase-only (dead body) — no soft-edge membership to gate.

  bool HeaderSucc = false;
  for (const MachineBasicBlock *S : Latch->successors()) {
    if (S == Header) {
      HeaderSucc = true;
      break;
    }
  }
  if (!HeaderSucc)
    report_fatal_error(
        "HaydnFixupHwLoops: demote lost loop membership (latch does not "
        "succeed to header)",
        /*gen_crash_diag=*/false);

  bool SeenBnez = false;
  bool SeenCountdown = false;
  for (const MachineInstr &MI : Latch->instrs()) {
    if (MI.getOpcode() == TargetOpcode::BUNDLE)
      continue;
    if (haydn::hwloop::isSoftLatchBnezOpcode(MI.getOpcode()))
      SeenBnez = true;
    if (haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode()) == Haydn::SUBI32)
      SeenCountdown = true;
  }
  if (!SeenBnez || !SeenCountdown)
    report_fatal_error(
        "HaydnFixupHwLoops: demote lost live trip-value (latch missing "
        "SUBI32+BNEZ countdown)",
        /*gen_crash_diag=*/false);

  // Exact FixedStack MMO on late LD/ST that address a preallocated demote
  // home (HaydnInstrInfo storeRegToStackSlot / loadRegFromStackSlot peer).
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  const HaydnFrameLowering *TFL =
      MF.getSubtarget<HaydnSubtarget>().getFrameLowering();
  SmallVector<int, 3> Homes;
  if (FuncInfo) {
    if (FuncInfo->getHwLoopDemoteSaveFI() >= 0)
      Homes.push_back(FuncInfo->getHwLoopDemoteSaveFI());
    if (FuncInfo->getPostRAScratchFI() >= 0)
      Homes.push_back(FuncInfo->getPostRAScratchFI());
    if (FuncInfo->getBranchRelaxationScratchFI() >= 0)
      Homes.push_back(FuncInfo->getBranchRelaxationScratchFI());
  }
  if (Homes.empty() || !TFL)
    return;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<MachineBasicBlock *, 4> Blocks;
  auto pushLive = [&](MachineBasicBlock *BB) {
    if (haydn::hwloop::isLiveMBB(MF, BB))
      Blocks.push_back(BB);
  };
  pushLive(Preheader);
  pushLive(Header);
  pushLive(Latch);
  for (MachineBasicBlock *S : Latch->successors())
    pushLive(S);

  for (MachineBasicBlock *BB : Blocks) {
    for (MachineInstr &MI : BB->instrs()) {
      if (MI.getOpcode() == TargetOpcode::BUNDLE)
        continue;
      const unsigned Log =
          haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode());
      const bool IsLoad = Log == Haydn::LD32;
      const bool IsStore = Log == Haydn::ST32;
      if (!IsLoad && !IsStore)
        continue;
      // Late demote glue: (dst,) base, imm-element. Need a base+imm pair.
      Register Base;
      int64_t Elem = 0;
      bool HaveAddr = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.isUse() && !MO.isImplicit()) {
          Base = MO.getReg();
        } else if (MO.isImm() && HaveAddr == false && Base.isValid()) {
          Elem = MO.getImm();
          HaveAddr = true;
        }
      }
      if (!HaveAddr || !Base.isPhysical())
        continue;
      for (int FI : Homes) {
        Register FrameReg;
        const int64_t Off =
            TFL->getFrameIndexReference(MF, FI, FrameReg).getFixed();
        if ((Off % 4) != 0 || FrameReg != Base || (Off / 4) != Elem)
          continue;
        bool Exact = false;
        for (const MachineMemOperand *MMO : MI.memoperands()) {
          const PseudoSourceValue *PSV = MMO->getPseudoValue();
          if (const auto *FS = dyn_cast_or_null<FixedStackPseudoSourceValue>(PSV))
            Exact |= FS->getFrameIndex() == FI;
        }
        if (Exact)
          break;
        if (!MI.memoperands_empty())
          report_fatal_error(
              "HaydnFixupHwLoops: demote stack access MMO is not the "
              "preallocated FixedStack home",
              /*gen_crash_diag=*/false);
        MachineMemOperand *MMO = MF.getMachineMemOperand(
            MachinePointerInfo::getFixedStack(MF, FI),
            IsLoad ? MachineMemOperand::MOLoad : MachineMemOperand::MOStore, 4,
            MFI.getObjectAlign(FI));
        MI.addMemOperand(MF, MMO);
        break;
      }
    }
  }
}

bool HaydnFixupHwLoops::revalidateRetainedSetups(MachineFunction &MF,
                                                 const HaydnInstrInfo &TII) {
  SmallVector<MachineInstr *, 8> Sets;
  collectRetainedSetups(MF, TII, Sets);
  const unsigned Initial = Sets.size();
  const unsigned Bound = haydn::hwloop::nestedCascadeBound(Initial);
  bool Changed = false;

  for (unsigned Wave = 0; Wave < Bound; ++Wave) {
    collectRetainedSetups(MF, TII, Sets);
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
      MachineBasicBlock *Pre = MI->getParent();
      MachineBasicBlock *Header = nullptr;
      MachineBasicBlock *Latch = nullptr;
      const unsigned Opc = MI->getOpcode();
      if (TII.isHardwareLoopSetupOpcode(Opc) && Opc != Haydn::LoopStart) {
        if (MI->getNumOperands() >= 3 && MI->getOperand(1).isMBB() &&
            MI->getOperand(2).isMBB()) {
          Header = MI->getOperand(1).getMBB();
          Latch = MI->getOperand(2).getMBB();
        }
      } else if (Opc == Haydn::LoopStart) {
        Header = haydn::hwloop::resolveBodyMBBFixup(*MI);
        Latch = haydn::hwloop::resolveLoopStartLatch(Header, Pre);
      }
      const unsigned FrameObjectsBefore = MF.getFrameInfo().getNumObjects();
      const bool One = fixupOne(*MI, TII);
      WaveChanged |= One;
      if (One && !MI->getParent())
        gatePostDemotePreservation(MF, TII, Pre, Header, Latch,
                                   FrameObjectsBefore);
    }
    Changed |= WaveChanged;
    if (!WaveChanged)
      break;
    if (Wave + 1 == Bound) {
      collectRetainedSetups(MF, TII, Sets);
      if (!Sets.empty() && WaveChanged)
        report_fatal_error(
            "HaydnFixupHwLoops: nested cascade exhausted without a "
            "no-mutation wave",
            /*gen_crash_diag=*/false);
    }
  }
  return Changed;
}

bool HaydnFixupHwLoops::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const auto &TII =
      *static_cast<const HaydnInstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Changed = false;
  // Residual generic SET_HWLOOP{,_REG} is ExpandPseudos' rewrite. Hexagon
  // Fixup matches architectural LOOP only (HexagonFixupHwLoops.cpp:75-81)
  // then range-rechecks (136-148). Haydn Fixup fatals on leftovers; MIR that
  // injects generics must run ExpandPseudos first.
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

  // Layout ownership only: sequentialize illegal SET trip-reg coissue before
  // Off/Following walks. Semantic multi-stage peel repair is forbidden —
  // post-pipeliner either refuses before mutation or commits a valid loop.
  Changed |= sequentializeIllegalHwloopTripCoissue(MF, TII);

  // Post-S2 inventory: inner-first revalidate. A retained loop that S2 (or
  // an inner demote/pad) grew out of range is rewritten from current
  // layout, never treated as layout-stable from a prior acceptance.
  Changed |= revalidateRetainedSetups(MF, TII);

  return Changed;
}
