//===- HaydnLatencyStalls.cpp - Exposed-pipeline RAW stall insertion ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See HaydnLatencyStalls.h for the ISA rule and why nothing else enforces it.
//
//===----------------------------------------------------------------------===//

#include "HaydnLatencyStalls.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHWLoopContracts.h"
#include "HaydnHWLoopDemote.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "haydn-latency-stalls"

STATISTIC(NumStallBundles, "Number of NOP stall bundles inserted");
STATISTIC(NumUnexpectedOptStallBundles,
          "NOP stall bundles inserted at -O1+ (scheduler/late-mutation gap)");
STATISTIC(NumRegeneratedStallParcels,
          "Previous dest-window stall parcels stripped before re-insert");
STATISTIC(NumStallBytesCharged,
          "Layout bytes charged for inserted stalls via getInstSizeInBytes");
STATISTIC(NumStallAlignPadBytes,
          "Min bundle-address alignment remainder at stall insert prefixes");
STATISTIC(NumLatencyStallResourceAdmissionPinsHeld,
          "Number of latency-stall functions that held fail-closed per-op "
          "resource admission (product closed until golden import)");
STATISTIC(NumWrapStallBundles,
          "NOP stall bundles inserted at a loop back-edge wrap point");
STATISTIC(NumStallsSkippedEncodedBytesCap,
          "Dest-window stall cycles the EncodedBytes cap would drop "
          "(fail-closed; never a silent under-pad)");

namespace {

/// One "cycle" of the exposed pipeline: either a BUNDLE root plus its children
/// (already packed by post-RA leaveRegion) or a single standalone MI that
/// the following Finalize will wrap.
struct Cycle {
  MachineBasicBlock::iterator Boundary; // insertion point for a stall
  SmallVector<MachineInstr *, 4> Members;
};

/// Collect the block as a list of cycles in program order.
static void collectCycles(MachineBasicBlock &MBB,
                          SmallVectorImpl<Cycle> &Cycles) {
  for (MachineBasicBlock::instr_iterator I = MBB.instr_begin(),
                                         E = MBB.instr_end();
       I != E;) {
    MachineInstr &MI = *I;
    if (MI.isBundledWithPred()) {
      // Defensive: a child without a visited root (should not happen).
      ++I;
      continue;
    }
    Cycle C;
    C.Boundary = MachineBasicBlock::iterator(I);
    if (MI.isBundle()) {
      // BUNDLE root carries no encoding itself; the children are the ops.
      ++I;
      while (I != E && I->isBundledWithPred()) {
        if (!I->isMetaInstruction() && !I->isDebugInstr() && !I->isPosition())
          C.Members.push_back(&*I);
        ++I;
      }
    } else {
      if (!MI.isMetaInstruction() && !MI.isDebugInstr() && !MI.isPosition())
        C.Members.push_back(&MI);
      ++I;
    }
    if (!C.Members.empty())
      Cycles.push_back(std::move(C));
  }
}

/// Logical NOP, including generated Format E members (AIE slot NOP overlay).
static bool isLogicalNop(const MachineInstr &MI) {
  return haydn::format_e::logicalOpcodeOrSelf(MI.getOpcode()) == Haydn::NOP;
}

/// Dest-window stall identity. Scheduler/HWLoop insertNoop does not set
/// NoMerge; only this pass marks parcels it owns so a later invocation can
/// strip them (regenerate) without eating resource-idle NOPs.
static bool isRegenerableStallParcel(const MachineInstr &MI) {
  return isLogicalNop(MI) && MI.getFlag(MachineInstr::NoMerge);
}

static unsigned alignmentPadBytes(uint64_t Size) {
  constexpr unsigned A = haydn::format::MinBundleAddressAlignBytes;
  return unsigned((A - (Size % A)) % A);
}

static bool isRealCycleMember(const MachineInstr &MI) {
  return !MI.isMetaInstruction() && !MI.isDebugInstr() && !MI.isPosition();
}

/// D1.16 latch classification. A latch is an MBB that is its own successor.
/// The ZOL form carries a PseudoLoopEnd terminator (HW wraps from the last
/// body bundle straight to BEGIN — the back edge is not a branch); the soft
/// form's wrap point IS the backedge branch terminator. A latch whose
/// terminator set contains an indirect branch is not classifiable (unknown
/// successors; the exit arm's conservative end-of-block pad still covers
/// every outgoing path).
enum class LatchForm { None, ZOL, Soft };

static LatchForm latchFormOf(const MachineBasicBlock &MBB) {
  const bool SelfSucc = llvm::is_contained(MBB.successors(), &MBB);
  if (!SelfSucc)
    return LatchForm::None;
  bool SawIndirect = false;
  bool SawLoopEnd = false;
  for (const MachineInstr &T : MBB.terminators()) {
    if (T.getOpcode() == Haydn::PseudoLoopEnd)
      SawLoopEnd = true;
    if (T.isIndirectBranch())
      SawIndirect = true;
  }
  if (SawIndirect)
    return LatchForm::None;
  return SawLoopEnd ? LatchForm::ZOL : LatchForm::Soft;
}

/// WRAP insertion point (pin b of the wrap law). For a ZOL latch the pads
/// must execute INSIDE [BEGIN,END] AND after every def whose window crosses
/// the wrap: a pad before the last def-bearing parcel shifts that def with
/// the pad, so its window still crosses the wrap unchanged (fir_convol /
/// vec_bexp product kernels: pad-before-load left remaining 1 at block end
/// and the freeze verifier fired). The pad seat is therefore after the last
/// real cycle and before the latch metas (PseudoLoopEnd / soft-exit
/// terminators). This stays inside the executed body: END is inclusive and
/// DYNAMICALLY re-anchored at the last size-bearing body parcel by
/// HaydnAsmPrinter::getLastRealInstr ("body trailing pads ... may leave
/// this last real as a pad"), and HaydnFixupHwLoops charges parcels up to
/// PseudoLoopEnd into Off2 — pads before PseudoLoopEnd grow the [BEGIN,END]
/// window, they do not sit past END on the exit path. For a soft latch the
/// backedge branch IS the wrap point and also sits on the exit path; pads
/// go before the first terminator (the exit arm's anchor) — inserted once
/// by max, never twice. Returns end() when the ZOL insert seat cannot be
/// identified (never guess an insertion point; the exit arm still pads).
static MachineBasicBlock::iterator
wrapInsertPoint(MachineBasicBlock &MBB, LatchForm Form,
                const SmallVectorImpl<Cycle> &Cycles) {
  if (Form == LatchForm::Soft)
    return MBB.getFirstTerminator();
  // ZOL: after the last collected real cycle (BUNDLE root / standalone MI
  // head — never a bundle child), at the first latch meta/terminator.
  // Cycles excludes meta/debug and terminator-only heads, so an empty list
  // means no identifiable last real cycle: fail closed to the exit arm.
  // (Any trailing idle NOP parcels are themselves cycles in Cycles and have
  // already retired the pending windows — a nonzero Wrap implies there are
  // none, so the seat is exactly the first latch meta/terminator.)
  if (Cycles.empty())
    return MBB.end();
  MachineBasicBlock::iterator AfterLast = Cycles.back().Boundary;
  ++AfterLast;
  return AfterLast;
}

/// Strip stall parcels this pass previously inserted. All-NOP BUNDLEs whose
/// real children all carry the stall pin are idle cycles we own (S2 may have
/// wrapped a bare stall). Mixed BUNDLEs keep stall-flagged NOP children as
/// legal format completion — they do not form extra issue cycles.
static bool eraseRegeneratedStalls(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
    MachineInstr &MI = *I;
    if (MI.isBundledWithPred()) {
      ++I;
      continue;
    }
    if (MI.isBundle()) {
      bool AllStall = true;
      bool AnyReal = false;
      MachineBasicBlock::instr_iterator C = std::next(MI.getIterator());
      while (C != MBB.instr_end() && C->isBundledWithPred()) {
        if (isRealCycleMember(*C)) {
          AnyReal = true;
          if (!isRegenerableStallParcel(*C))
            AllStall = false;
        }
        ++C;
      }
      MachineBasicBlock::iterator Next(C);
      if (!AnyReal || !AllStall) {
        I = Next;
        continue;
      }
      SmallVector<MachineInstr *, 8> BundleMIs;
      for (MachineBasicBlock::instr_iterator B = std::next(MI.getIterator());
           B != MBB.instr_end() && B->isBundledWithPred();)
        BundleMIs.push_back(&*B++);
      for (MachineInstr *K : BundleMIs) {
        if (isRegenerableStallParcel(*K))
          ++NumRegeneratedStallParcels;
        if (K->isBundledWithPred())
          K->unbundleFromPred();
        if (K->isBundledWithSucc())
          K->unbundleFromSucc();
        K->eraseFromParent();
      }
      MI.eraseFromParent();
      Changed = true;
      I = Next;
      continue;
    }
    if (isRegenerableStallParcel(MI)) {
      I = MBB.erase(I);
      ++NumRegeneratedStallParcels;
      Changed = true;
      continue;
    }
    ++I;
  }
  return Changed;
}

/// Pin a freshly inserted stall: empty MMOs, inherited DebugLoc, no extra
/// liveness, layout charge via the same getInstSizeInBytes range checks use.
static unsigned pinStallParcel(MachineInstr &Nop, MachineFunction &MF,
                               const TargetInstrInfo &TII, const DebugLoc &DL) {
  Nop.setDebugLoc(DL);
  Nop.setFlag(MachineInstr::NoMerge);
  Nop.setMemRefs(MF, {});
  if (Nop.getNumOperands() != 0)
    report_fatal_error(
        "Haydn latency stall NOP grew operands; liveness would be unspecified",
        /*GenCrashDiag=*/false);
  if (!Nop.memoperands_empty())
    report_fatal_error(
        "Haydn latency stall NOP carries MMOs; NOP is not a memory op",
        /*GenCrashDiag=*/false);
  const unsigned Bytes = TII.getInstSizeInBytes(Nop);
  if (Bytes == 0)
    report_fatal_error(
        "Haydn latency stall charges 0 bytes via getInstSizeInBytes; "
        "branch/HWLoop range checks would miss the prefix growth",
        /*GenCrashDiag=*/false);
  NumStallBytesCharged += Bytes;
  return Bytes;
}

static uint64_t layoutBytesTo(MachineBasicBlock &MBB, const TargetInstrInfo &TII,
                             MachineBasicBlock::iterator End) {
  uint64_t Bytes = 0;
  for (MachineBasicBlock::iterator I = MBB.begin(); I != End; ++I)
    Bytes += TII.getInstSizeInBytes(*I);
  return Bytes;
}

/// True when one stall parcel at \p InsertPt sits strictly between a
/// forward branch and its dest, or at/after dest through the backward
/// branch (insert-before-src grows a backedge).
static bool insertGrowsBranchSpan(int64_t InsertAbs, int64_t SrcAbs,
                                  int64_t DestAbs) {
  if (DestAbs >= SrcAbs)
    return InsertAbs > SrcAbs && InsertAbs < DestAbs;
  return InsertAbs >= DestAbs && InsertAbs <= SrcAbs;
}

/// One stall cycle is one product parcel after wrap (bare NOP already
/// charges getInstSizeInBytes = product EncodedBytes). Return false when
/// the cycle would walk a still-short site past simm12/simm20 or a
/// formation-kept ZOL past Off1/Off2 — the caller fail-closes rather
/// than silently under-pad. Idle NOP rows already in collectCycles
/// retire dest windows; this cap does not invent a second range
/// authority (D1.33: TII.isBranchOffsetInRange is the far seat).
/// AIE/Hexagon have no EncodedBytes stall-cap peer.
static bool stallParcelFitsLandedWindows(MachineFunction &MF,
                                         const HaydnInstrInfo &TII,
                                         MachineBasicBlock &InsertMBB,
                                         MachineBasicBlock::iterator InsertPt) {
  const int64_t Parcel =
      static_cast<int64_t>(haydn::bundle::productParcelBytes().Value);
  SmallVector<int64_t, 32> Starts;
  haydn::hwloop::computeLayoutBlockStarts(MF, TII, Starts);
  auto absOfMI = [&](const MachineBasicBlock &B,
                     const MachineInstr &MI) -> int64_t {
    const int N = B.getNumber();
    if (N < 0 || N >= static_cast<int>(Starts.size()) || Starts[N] < 0)
      return -1;
    return Starts[N] + haydn::hwloop::estimateLayoutInstrOffset(
                           B, MachineBasicBlock::const_iterator(MI), TII);
  };
  auto absOfIt = [&](const MachineBasicBlock &B,
                     MachineBasicBlock::iterator It) -> int64_t {
    const int N = B.getNumber();
    if (N < 0 || N >= static_cast<int>(Starts.size()) || Starts[N] < 0)
      return -1;
    return Starts[N] + haydn::hwloop::estimateLayoutInstrOffset(
                           B, MachineBasicBlock::const_iterator(It), TII);
  };
  const int64_t InsertAbs = absOfIt(InsertMBB, InsertPt);
  // Unknown insert layout cannot prove Off1/Off2 / simm12 still hold.
  if (InsertAbs < 0)
    return false;

  for (MachineBasicBlock &B : MF) {
    for (MachineInstr &MI : B) {
      MachineBasicBlock *Dest = TII.getBranchDestBlock(MI);
      if (!Dest)
        continue;
      // Consume D1.142 BUNDLE handshake so this walk cannot arm BR.
      if (MI.isBundle())
        (void)TII.isBranchOffsetInRange(TargetOpcode::BUNDLE, 0);
      if (!haydn::hwloop::isLiveMBB(MF, &B) ||
          !haydn::hwloop::isLiveMBB(MF, Dest))
        continue;
      const int64_t SrcAbs = absOfMI(B, MI);
      const int DN = Dest->getNumber();
      if (SrcAbs < 0 || DN < 0 || DN >= static_cast<int>(Starts.size()) ||
          Starts[DN] < 0)
        continue;
      const int64_t DestAbs = Starts[DN];
      const int64_t Cur = DestAbs - SrcAbs;
      if (!insertGrowsBranchSpan(InsertAbs, SrcAbs, DestAbs))
        continue;
      const int64_t Grown =
          DestAbs >= SrcAbs ? Cur + Parcel : Cur - Parcel;
      // Only the sites this insert actually lengthens. An already-far
      // unrelated site must not veto dest-window stalls elsewhere.
      if (!TII.isBranchOffsetInRange(MI, Grown)) {
        LLVM_DEBUG(dbgs() << "  EncodedBytes cap: insert would grow " << MF.getName()
                          << " bb." << B.getNumber() << " -> bb."
                          << Dest->getNumber() << " off " << Cur << " -> "
                          << Grown << "\n");
        return false;
      }
    }
  }

  for (MachineBasicBlock &B : MF) {
    for (MachineInstr &MI : B.instrs()) {
      if (!TII.isHardwareLoopSetupInstr(MI))
        continue;
      MachineBasicBlock *StartMBB = nullptr;
      MachineBasicBlock *EndMBB = nullptr;
      if (MI.getNumOperands() >= 3 && MI.getOperand(1).isMBB() &&
          MI.getOperand(2).isMBB()) {
        StartMBB = MI.getOperand(1).getMBB();
        EndMBB = MI.getOperand(2).getMBB();
      } else {
        // Product SET_HWLOOP_F2_W carries imm Off1/Off2, not MBB operands.
        // Use the same body resolution Fixup uses so the cap cannot skip
        // a kept ZOL and walk END past Off2.
        StartMBB = haydn::hwloop::resolveBodyMBBCore(MI);
        if (!StartMBB)
          StartMBB = haydn::hwloop::resolveBodyMBBFixup(MI);
        if (StartMBB)
          EndMBB = haydn::hwloop::resolveLoopStartLatch(StartMBB, MI.getParent());
        if (!EndMBB)
          EndMBB = StartMBB;
      }
      if (!StartMBB || !EndMBB || !haydn::hwloop::isLiveMBB(MF, StartMBB) ||
          !haydn::hwloop::isLiveMBB(MF, EndMBB))
        return false;
      MachineInstr &SetCycle = haydn::hwloop::topLevelForLayout(MI);
      MachineBasicBlock *Pre = SetCycle.getParent();
      if (!Pre)
        return false;
      MachineBasicBlock::iterator AfterSet(&SetCycle);
      ++AfterSet;
      int64_t StartOff = haydn::hwloop::estimateLayoutMBBDistance(
          MF, Pre, AfterSet, StartMBB, TII);
      const int64_t SetParcel = static_cast<int64_t>(
          haydn::bundle::committedEncodedBytes(SetCycle).Value);
      StartOff = haydn::hwloop::anchoredFromAfterSet(StartOff, SetParcel);
      const int64_t EndOff = haydn::hwloop::estimateLastBodyCycleOffset(
          MF, StartMBB, EndMBB, Pre, StartOff, TII);
      if (StartOff < 0 || EndOff < 0)
        return false;
      const int64_t SetAbs = absOfMI(*Pre, SetCycle);
      const int SN = StartMBB->getNumber();
      const int EN = EndMBB->getNumber();
      if (SetAbs < 0 || SN < 0 || EN < 0 ||
          SN >= static_cast<int>(Starts.size()) ||
          EN >= static_cast<int>(Starts.size()) || Starts[SN] < 0 ||
          Starts[EN] < 0)
        return false;
      const int64_t BeginAbs = Starts[SN];
      const int64_t EndWindowAbs =
          Starts[EN] + haydn::hwloop::estimateLayoutInstrOffset(
                           *EndMBB, EndMBB->getFirstTerminator(), TII);
      int64_t NewStart = StartOff;
      int64_t NewEnd = EndOff;
      if (InsertAbs > SetAbs && InsertAbs < BeginAbs)
        NewStart += Parcel;
      if (InsertAbs > SetAbs && InsertAbs <= EndWindowAbs)
        NewEnd += Parcel;
      if ((NewStart != StartOff || NewEnd != EndOff) &&
          !haydn::hwloop::offsetsMeetImmRelocLaw(NewStart, NewEnd))
        return false;
    }
  }
  return true;
}

} // namespace

static bool insertExposedPipelineStallsImpl(MachineFunction &MF) {
  // No skipFunction: latency is correctness, not quality. Since GR2.4
  // postmisched runs for every function incl. optnone
  // (forcePostRAScheduling) and may hold multi-MI packs. All shapes need
  // this net so Data_Latency windows never leak into the next issue cycle.
  // Product S1 runs this once before stamp (logical itinerary, then one
  // identity bake of remaining bares). Do not pair it with a second
  // dest-window walk — D1.33/D1.35 single-inflation. EncodedBytes cap
  // is fail-closed: a wrap/exit pad that would walk a still-short site
  // past simm12/simm20 or a formation-kept ZOL past Off1/Off2 is a named
  // fatal, not a silent under-pad. Size-neutral same-row NOP wrap cannot
  // add a cycle. AIE/Hexagon have no EncodedBytes stall-cap peer.
  //
  // Consume the same availability-aware record as pre-RA / ordinary post-RA /
  // SMS / hazard recognizer. Itinerary Data_Latency stays the stall authority;
  // the record only binds the admitted aggregate scaffold (CompleteModel
  // closed, no competitive per-op invent).
  if (!haydnAvailabilityAwareConsumePinsHold())
    report_fatal_error(
        "Haydn latency-stall resource admission pins failed",
        /*GenCrashDiag=*/false);
  ++NumLatencyStallResourceAdmissionPinsHeld;

  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  const HaydnInstrInfo &TII = *STI.getInstrInfo();
  const InstrItineraryData *Itin = STI.getInstrItineraryData();
  if (!Itin || Itin->isEmpty()) {
    // Empty-itinerary arm still owes the identity bake (layout identity
    // only; no stall computation runs here).
    haydn::bundle::applyFinalDirectCompatibleMembers(MF);
    return false;
  }

  // -O1+: insertions are unexpected if schedulers already saw architectural
  // latency; still insert for correctness and count for the auditor.
  const bool AuditUnexpected =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None;

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Accumulate-vs-regenerate pin: drop this pass's previous stall parcels
    // before recomputing dest-window need on the current inventory.
    Changed |= eraseRegeneratedStalls(MBB);

    SmallVector<Cycle, 32> Cycles;
    collectCycles(MBB, Cycles);
    if (Cycles.empty())
      continue;

    // EncodedBytes cap on wrap/exit already keeps Off1/Off2. Intra-block
    // dest-window / leftover store→load stalls in a latch also grow the
    // body; post-stamp Fixup cannot demote (Wave 4 H). Cap those stalls
    // the same way so TSVC s212-class ZOLs stay in range.
    const LatchForm Form = latchFormOf(MBB);

    // Dest-window authority: reuse the HR dest-read / SIN_COS windows
    // (DestReadPending / DestWritePending), not a second pass-local map.
    // Intra-block O1+ insertions stay unexpected-auditor if a later
    // mutation reopens a window the scheduler already serialized.
    HaydnHazardRecognizer DestHR(&TII, Itin, /*IsPreRA=*/false);
    DestHR.Reset();

    auto insertStalls = [&](MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                            unsigned Stalls, StringRef Why, bool Unexpected,
                            bool CapLayout) -> unsigned {
      LLVM_DEBUG(dbgs() << "HaydnLatencyStalls: " << Stalls
                        << " stall bundle(s) " << Why << "\n");
      unsigned Inserted = 0;
      for (unsigned I = 0; I < Stalls; ++I) {
        // EncodedBytes cap is wrap/exit only (ZOL Off1/Off2 and still-short
        // simm12/simm20). Intra-block consecutive-cycle dest-window is
        // correctness; size-neutral same-row NOP wrap cannot add a cycle.
        // Dropping residual need is a no-interlock miscompile (non-latch
        // exit leaks are unverified). Fail closed; do not invent a
        // EncodedBytes-neutral stall packet.
        if (CapLayout &&
            !stallParcelFitsLandedWindows(MF, TII, MBB, InsertPt)) {
          NumStallsSkippedEncodedBytesCap += Stalls - Inserted;
          report_fatal_error(
              "Haydn: EncodedBytes stall cap would drop dest-window pads",
              /*GenCrashDiag=*/false);
        }
        // One NOP-insertion mechanism — TII.insertNoop (Hexagon
        // HexagonInstrInfo.cpp:1667-1671 peer), not a second local BuildMI.
        TII.insertNoop(MBB, InsertPt);
        MachineInstr &Nop = *std::prev(InsertPt);
        pinStallParcel(Nop, MF, TII, DL);
        // Wrap as a complete packet now so EncodedBytes (not a later
        // wrap) is what Off1/Off2 / simm12 walks charge.
        haydn::bundle::applyFinalDirectCompatibleSingleton(
            Nop, haydnDefaultMCFormats(), TII);
        haydn::bundle::finalizeExactLateSingleton(Nop);
        ++Inserted;
      }
      if (!Inserted)
        return 0;
      // Prefix + min bundle-address remainder — same size interface range
      // checks consume (getInstSizeInBytes), not a second byte model.
      const uint64_t Prefix = layoutBytesTo(MBB, TII, InsertPt);
      const unsigned AlignPad = alignmentPadBytes(Prefix);
      NumStallAlignPadBytes += AlignPad;
      LLVM_DEBUG(dbgs() << "  prefix=" << Prefix << " align-pad=" << AlignPad
                        << " inserted=" << Inserted << "\n");
      NumStallBundles += Inserted;
      if (AuditUnexpected && Unexpected) {
        NumUnexpectedOptStallBundles += Inserted;
        LLVM_DEBUG(dbgs() << "  (unexpected at -O1+; scheduler/late gap)\n");
      }
      Changed = true;
      return Inserted;
    };

    const Cycle *Prev = nullptr;
    for (Cycle &C : Cycles) {
      unsigned Stalls = 0;
      for (const MachineInstr *MI : C.Members)
        Stalls = std::max(Stalls, DestHR.destWindowStallNeed(*MI));
      // Leftover bares skip the scheduler MemoryEdges. Store→load on
      // consecutive leftover cycles still owes getMemoryLatency-1 (D1.160).
      // Store→store stays consecutive (Wave 4 FileCheck).
      if (Prev) {
        for (const MachineInstr *S : Prev->Members) {
          if (!S->mayStore())
            continue;
          const unsigned SrcSC = S->getDesc().getSchedClass();
          for (const MachineInstr *D : C.Members) {
            if (!D->mayLoad())
              continue;
            // Disjoint MMOs (stack.5 store then @d load) are not a
            // store→load RAW. Stalling them blew TSVC s212 Off1.
            if (!S->mayAlias(static_cast<AAResults *>(nullptr), *D,
                             /*UseTBAA=*/false))
              continue;
            if (auto Lat = TII.getMemoryLatency(SrcSC, D->getDesc().getSchedClass())) {
              if (*Lat > 1)
                Stalls = std::max(Stalls, unsigned(*Lat - 1));
            }
          }
        }
      }
      if (Stalls) {
        const unsigned Inserted =
            insertStalls(C.Boundary, C.Members.front()->getDebugLoc(), Stalls,
                         "before next consumer", /*Unexpected=*/true,
                         /*CapLayout=*/false);
        for (unsigned I = 0; I < Inserted; ++I)
          DestHR.advanceDestWindows();
      }

      // This cycle retires one pipeline step, then publishes its own defs.
      DestHR.advanceDestWindows();
      for (const MachineInstr *MI : C.Members)
        DestHR.emitForDestWindow(*MI);
      Prev = &C;
    }

    // D1.16 loop back-edge wrap law + conservative block exit, one shared
    // predicate (destWindowExitLeak == destWindowWrapPadNeed — same max
    // remaining arithmetic, different insertion point).
    //
    // WRAP: a latch MBB (self-successor) re-executes its own cycle 0 right
    // after the last body cycle. A dest window still pending at block end
    // (e.g. a post-inc load at the END parcel whose data dest and base
    // writeback are consumed at the next iteration's BEGIN parcel) crosses
    // the HWLR_END -> HWLR_BEGIN wrap. Pad INSIDE the executed body so the
    // wrap distance covers the window: before the END-anchored parcel for
    // ZOL (parcels after END run only on loop exit), before the backedge
    // branch for soft loops (the branch is the wrap point and also sits on
    // the exit path).
    //
    // EXIT: a latency window must not leak into a successor (we cannot know
    // which one runs, and a successor may also be reached from elsewhere).
    // Haydn terminators have no Data_Latency > 1 def (JAL/JALR are
    // Branch_Penalty only), so padding in front of them is sufficient.
    //
    // A latch with a real exit successor needs both seams covered; the two
    // anchors coincide for soft latches (first terminator), so the pass
    // inserts at each distinct anchor at most once, and pads placed before
    // the ZOL END-anchored parcel cover the exit path too (they execute on
    // every path through the body).
    // Post-walk pending — the same value the freeze walker's post-walk
    // query computes on the committed layout (verifyMBBDestWindowSeams).
    // The walk retires every cycle INCLUDING terminator cycles (a soft
    // latch's backedge branch is a real pipeline step on the wrap path),
    // so what remains after the last cycle is exactly the window that
    // still crosses the wrap.
    const unsigned Wrap =
        Form == LatchForm::None ? 0 : DestHR.destWindowWrapPadNeed();
    if (Form == LatchForm::ZOL || Form == LatchForm::Soft) {
      MachineBasicBlock::iterator WrapPt = wrapInsertPoint(MBB, Form, Cycles);
      if (WrapPt != MBB.end()) {
        if (MachineInstr *Root = haydn::bundle::bundleRootOf(*WrapPt))
          WrapPt = MachineBasicBlock::iterator(Root->getIterator());
        if (Wrap) {
          const unsigned Inserted = insertStalls(
              WrapPt, DebugLoc(), Wrap,
              ("at wrap of bb." + Twine(MBB.getNumber())).str(),
              /*Unexpected=*/false, /*CapLayout=*/true);
          NumWrapStallBundles += Inserted;
          for (unsigned I = 0; I < Inserted; ++I)
            DestHR.advanceDestWindows();
        }
        // Soft latch: the wrap anchor IS the exit anchor — inserting again
        // below would double-pad the same point. Advance already covered the
        // remaining windows; the exit arm is skipped.
        if (Form == LatchForm::Soft)
          continue;
      } else if (Wrap) {
        // Unidentifiable ZOL last real cycle with a nonzero need: never
        // guess an insertion point. The exit arm below still pads the block
        // end (conservative for both seams — pads before the terminators
        // cover the exit path; the ZOL wrap itself is covered by END-label
        // re-anchoring only when Fixup re-derives END from the grown body,
        // which insertStalls layout charging feeds).
      }
    }

    // EXIT seam.
    if (unsigned Leak = DestHR.destWindowExitLeak()) {
      // Pad before the BUNDLE ROOT holding the first terminator, never before
      // a bundle child — that would split the packet.
      MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
      if (InsertPt != MBB.end()) {
        if (MachineInstr *Root = haydn::bundle::bundleRootOf(*InsertPt))
          InsertPt = MachineBasicBlock::iterator(Root->getIterator());
      }
      insertStalls(InsertPt, DebugLoc(), Leak,
                   ("at exit of bb." + Twine(MBB.getNumber())).str(),
                   /*Unexpected=*/false, /*CapLayout=*/true);
    }
  }

  // Identity-bake remaining FieldSlot/logicals AFTER the dest-window
  // computation. Baking earlier replaces the logical Desc (e.g. ST32_POST,
  // itinerary Slot1_LD, OperandCycles [2]) with the generated member
  // (S_SW_POST_IMM_E2_E0_..., Slot0_LS_WbLat, OperandCycles [1]) and the
  // writeback read of the post-incremented base then sees latency 1 — the
  // stall parcel the exposed pipeline requires disappears (stack-align
  // regression). The bake is layout identity only; it must never change
  // which cycles the stall authority charges. One bake, after the one
  // logical-itinerary charge.
  haydn::bundle::applyFinalDirectCompatibleMembers(MF);

  return Changed;
}

bool llvm::haydnInsertExposedPipelineStalls(MachineFunction &MF) {
  return insertExposedPipelineStallsImpl(MF);
}
