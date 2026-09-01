//===- HaydnMachineAlignment.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License, v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Port of AIEMachineAlignment (AIEMachineAlignment.cpp:370-424 padRegions /
// padRegionForAlignment; seat AIE2TargetMachine.cpp:247 after
// createAIEFinalizeBundle). Haydn overlay vs AIE:
//
//   * AIE pads AND elongates existing bundles to consume the gap with
//     format-native NOPs (variable 2^n VLIWFormat sizes). Haydn's product
//     frontier encodes exactly one EncodedBytes parcel for both rows and
//     golden admits no underfill/top-pad, so there is no elongation
//     mechanism at all: pad-only, one idle row per parcel.
//   * AIE aligns every region boundary (jump targets, ZOL bodies) to the
//     fixed MachineBlockAlignment. Haydn W70.2 owns one boundary: the
//     FUNCTION entry (the deleted AsmPrinter emitFunctionEntryLabel
//     growth). MBB alignment gaps stay charged conservatively by
//     estimateMBBDistance (HaydnFixupHwLoops).
//   * The idle row is the existing committed full-slot architectural NOP
//     object: TII.insertNoop (the one NOP-insertion mechanism, Hexagon
//     peer) + finalizeBundle + stampBundleCommit with
//     selectCompletionForMembersAndPads(Row, 0, HasPadNop=true) =
//     AllEntriesReal. Identical to what LatencyStalls+Finalize produce for
//     a stall parcel — no new NOP form, no makeStallPlan StubIdle.
//
// Byte law: the extent walk is the size oracle (Kind B) — getInstSizeInBytes
// on committed BUNDLE roots (children/meta are 0 by construction). The
// inserted parcels are therefore charged automatically by every later byte
// consumer (freeze Verify, AsmPrinter/MC, and any repair loop that re-runs
// the BR/HWLoop distance walks).
//
// Default min/pref function alignment (Align 4) divides the product parcel
// (12), so ordinary functions are already on the grid and take zero pad;
// only user aligned(N) requests that do not divide the parcel stream pay
// idle rows. The reachable pad from a parcel-multiple extent is bounded by
// Align/gcd(Parcel, Align) parcels (the same lcm bound
// HaydnMCELFStreamer::emitCodeAlignment walks).
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineAlignment.h"
#include "Haydn.h"
#include "HaydnBundlePlan.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <numeric>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "haydn-machine-alignment"

static cl::opt<bool> SkipHaydnMachineAlignment(
    "skip-haydn-machine-alignment", cl::init(false), cl::Hidden,
    cl::desc("Use this option to skip the Haydn Machine Alignment Pass "
             "(AIE skip-machine-alignment peer)."));

STATISTIC(NumFunctionsPadded, "Functions padded with idle entry parcels");
STATISTIC(NumIdleParcelsInserted, "Idle parcels inserted at function entry");

namespace {

/// Required entry alignment for the function — the exact authority the
/// deleted AsmPrinter growth consulted (HaydnAsmPrinter::emitFunctionEntryLabel
/// before W70.2): max(MF alignment, TLI min function alignment, IR
/// aligned(N)). Min/Pref are the product parcel power-of-two (Align 4);
/// user aligned(N) is a language guarantee and is honored, not capped.
Align requiredEntryAlignment(const MachineFunction &MF) {
  const TargetLowering *TLI = MF.getSubtarget().getTargetLowering();
  Align A = std::max(MF.getAlignment(), TLI->getMinFunctionAlignment());
  if (MaybeAlign FnAlign = MF.getFunction().getAlign())
    A = std::max(A, *FnAlign);
  return A;
}

/// Committed function extent (Kind B size oracle). getInstSizeInBytes
/// already charges children (0), meta (0), and named late growth; bare
/// reals at this seat would be a closure violation the freeze verifier
/// reports.
uint64_t committedExtentBytes(const MachineFunction &MF,
                              const HaydnInstrInfo &TII) {
  uint64_t Bytes = 0;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      Bytes += TII.getInstSizeInBytes(MI);
  return Bytes;
}

/// Insert one committed idle parcel (full-slot architectural NOP row) in
/// front of \p InsertPt. Mirrors the LatencyStalls→Finalize stall-parcel
/// construction: insertNoop, wrap, stamp. Row for empty real membership is
/// the generated default E2 idle row; completion with a pad NOP is
/// AllEntriesReal (HaydnBundlePlan.h: "E2 idle is {NOP, NOP} — full-slot
/// architectural NOP fill").
void insertIdleParcel(MachineBasicBlock &Entry, HaydnInstrInfo &TII,
                      MachineBasicBlock::iterator InsertPt) {
  TII.insertNoop(Entry, InsertPt);
  MachineBasicBlock::instr_iterator NopIt =
      std::prev(InsertPt).getInstrIterator();
  assert(!NopIt->isBundledWithPred() && "fresh NOP must be a bare MI");
  finalizeBundle(Entry, NopIt, std::next(NopIt));
  MachineBasicBlock::instr_iterator RootIt = NopIt;
  while (RootIt->isInsideBundle())
    --RootIt;
  MachineInstr &Root = *RootIt;
  assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
  const haydn::bundle::BundleFormatRowID Row =
      haydn::bundle::ProductDefaultRowID;
  const haydn::bundle::CompletionStateID Completion =
      haydn::bundle::selectCompletionForMembersAndPads(
          Row, /*RealMembers=*/0, /*HasPadNop=*/true);
  assert(Completion == haydn::bundle::CompletionStateID::AllEntriesReal &&
         "pad NOP idle row must be full-slot real completion");
  assert(TII.getInstSizeInBytes(Root) ==
             haydn::bundle::productParcelBytes().Value &&
         "idle parcel must charge exactly one product parcel");
  haydn::bundle::stampBundleCommit(Root, Row, Completion);
  ++NumIdleParcelsInserted;
}

} // namespace

char HaydnMachineAlignment::ID = 0;

HaydnMachineAlignment::HaydnMachineAlignment() : MachineFunctionPass(ID) {
  initializeHaydnMachineAlignmentPass(*PassRegistry::getPassRegistry());
}

void HaydnMachineAlignment::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnMachineAlignment::runOnMachineFunction(MachineFunction &MF) {
  if (SkipHaydnMachineAlignment)
    return false;
  // No skipFunction: entry alignment is layout, not optimization; the
  // AsmPrinter growth this pass replaces ran at O0/optnone too.

  const Align A = requiredEntryAlignment(MF);
  if (A == Align(1)) {
    LLVM_DEBUG(dbgs() << "HaydnMachineAlignment: " << MF.getName()
                      << " needs no entry alignment\n");
    return false;
  }

  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  HaydnInstrInfo &TII = *const_cast<HaydnInstrInfo *>(STI.getInstrInfo());

  MachineBasicBlock &Entry = *MF.begin();
  // Pad before the first size-bearing parcel of the entry block: the
  // entry prologue executes unconditionally, so idle rows in front of it
  // are architecturally transparent, and the pad is inside every layout
  // distance measured from the function label.
  MachineBasicBlock::iterator InsertPt = Entry.begin();

  const uint64_t Parcel = haydn::bundle::productParcelBytes().Value;
  assert(Parcel != 0 && "product EncodedBytes must be non-zero");

  const uint64_t Extent = committedExtentBytes(MF, TII);
  if (Extent % A.value() == 0) {
    LLVM_DEBUG(dbgs() << "HaydnMachineAlignment: " << MF.getName()
                      << " extent " << Extent << " already aligned\n");
    return false;
  }

  // Whole-parcel pad to the alignment grid. The target offset must be 0
  // mod lcm(Align, Parcel) — a multiple of the parcel stream AND of the
  // alignment (the same grid law HaydnMCELFStreamer::emitCodeAlignment
  // walks). From a parcel-multiple extent the pad is at most
  // Align/gcd(Parcel, Align) parcels.
  const uint64_t Grid = std::lcm(A.value(), Parcel);
  const uint64_t Target = alignTo(Extent, Grid);
  assert(Target > Extent && "unaligned extent must have a larger grid point");
  const uint64_t Parcels = (Target - Extent) / Parcel;
  assert(Parcels != 0 && "grid gap must be a non-zero parcel multiple");
  const uint64_t MaxParcels = A.value() / std::gcd(Parcel, A.value());
  if (Parcels > MaxParcels) {
    // Unreachable from a parcel-multiple extent; kept fail-closed so a
    // future bare-MI leak or extent bug can never loop or underfill.
    report_fatal_error(
        Twine("HaydnMachineAlignment: cannot align ") + MF.getName() +
            " (extent " + Twine(Extent) + ", align " + Twine(A.value()) +
            ") with legal idle parcels",
        /*GenCrashDiag=*/false);
  }

  LLVM_DEBUG(dbgs() << "HaydnMachineAlignment: " << MF.getName()
                    << " extent " << Extent << " align " << A.value()
                    << " -> " << Parcels << " idle parcel(s)\n");
  for (uint64_t I = 0; I < Parcels; ++I)
    insertIdleParcel(Entry, TII, InsertPt);

  assert(committedExtentBytes(MF, TII) % A.value() == 0 &&
         "pad must land the extent on the alignment grid");
  ++NumFunctionsPadded;
  return true;
}

INITIALIZE_PASS(HaydnMachineAlignment, DEBUG_TYPE, "Haydn Machine Alignment",
                false, false)

FunctionPass *llvm::createHaydnMachineAlignmentPass() {
  return new HaydnMachineAlignment();
}
