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
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPortModel.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
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

} // namespace

char HaydnLatencyStalls::ID = 0;

HaydnLatencyStalls::HaydnLatencyStalls() : MachineFunctionPass(ID) {}

void HaydnLatencyStalls::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnLatencyStalls::runOnMachineFunction(MachineFunction &MF) {
  // No skipFunction: latency is correctness, not quality. Plain O0 without
  // optnone still runs postmisched (and may hold multi-MI packs); optnone
  // quality-skips postmisched only. Both need this net so Data_Latency
  // windows never leak into the next issue cycle. Inserted bare stall NOPs
  // are committed by the following Finalize (no MC uncommitted escape).
  //
  // Consume the same availability-aware record as pre-RA / ordinary post-RA /
  // SMS / hazard recognizer. Itinerary Data_Latency stays the stall authority;
  // the record only binds the admitted aggregate scaffold (CompleteModel
  // closed, no competitive per-op invent).
  if (!ResourceAdmissionPinned) {
    ResourceAdmissionPinned = true;
    if (!haydnAvailabilityAwareConsumePinsHold())
      report_fatal_error(
          "Haydn latency-stall resource admission pins failed",
          /*GenCrashDiag=*/false);
  }
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

    // Dest-window authority: reuse the HR dest-read / SIN_COS windows
    // (DestReadPending / DestWritePending), not a second pass-local map.
    // Intra-block O1+ insertions stay unexpected-auditor if a later
    // mutation reopens a window the scheduler already serialized.
    HaydnHazardRecognizer DestHR(&TII, Itin, /*IsPreRA=*/false);
    DestHR.Reset();

    auto insertStalls = [&](MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                            unsigned Stalls, StringRef Why, bool Unexpected) {
      LLVM_DEBUG(dbgs() << "HaydnLatencyStalls: " << Stalls
                        << " stall bundle(s) " << Why << "\n");
      for (unsigned I = 0; I < Stalls; ++I) {
        // One NOP-insertion mechanism — TII.insertNoop (Hexagon
        // HexagonInstrInfo.cpp:1667-1671 peer), not a second local BuildMI.
        TII.insertNoop(MBB, InsertPt);
        MachineInstr &Nop = *std::prev(InsertPt);
        pinStallParcel(Nop, MF, TII, DL);
      }
      // Prefix + min bundle-address remainder — same size interface range
      // checks consume (getInstSizeInBytes), not a second byte model.
      const uint64_t Prefix = layoutBytesTo(MBB, TII, InsertPt);
      const unsigned AlignPad = alignmentPadBytes(Prefix);
      NumStallAlignPadBytes += AlignPad;
      LLVM_DEBUG(dbgs() << "  prefix=" << Prefix << " align-pad=" << AlignPad
                        << "\n");
      NumStallBundles += Stalls;
      if (AuditUnexpected && Unexpected) {
        NumUnexpectedOptStallBundles += Stalls;
        LLVM_DEBUG(dbgs() << "  (unexpected at -O1+; scheduler/late gap)\n");
      }
      Changed = true;
    };

    for (Cycle &C : Cycles) {
      unsigned Stalls = 0;
      for (const MachineInstr *MI : C.Members)
        Stalls = std::max(Stalls, DestHR.destWindowStallNeed(*MI));
      if (Stalls) {
        insertStalls(C.Boundary, C.Members.front()->getDebugLoc(), Stalls,
                     "before next consumer", /*Unexpected=*/true);
        for (unsigned I = 0; I < Stalls; ++I)
          DestHR.advanceDestWindows();
      }

      // This cycle retires one pipeline step, then publishes its own defs.
      DestHR.advanceDestWindows();
      for (const MachineInstr *MI : C.Members)
        DestHR.emitForDestWindow(*MI);
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
    const LatchForm Form = latchFormOf(MBB);
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
          insertStalls(WrapPt, DebugLoc(), Wrap,
                       ("at wrap of bb." + Twine(MBB.getNumber())).str(),
                       /*Unexpected=*/false);
          NumWrapStallBundles += Wrap;
          for (unsigned I = 0; I < Wrap; ++I)
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
                   /*Unexpected=*/false);
    }
  }

  // Identity-bake remaining FieldSlot/logicals AFTER the dest-window
  // computation. Baking earlier replaces the logical Desc (e.g. ST32_POST,
  // itinerary Slot1_LD, OperandCycles [2]) with the generated member
  // (S_SW_POST_IMM_E2_E0_..., Slot0_LS_WbLat, OperandCycles [1]) and the
  // writeback read of the post-incremented base then sees latency 1 — the
  // stall parcel the exposed pipeline requires disappears (stack-align
  // regression). The bake is layout identity only; it must never change
  // which cycles the stall authority charges.
  haydn::bundle::applyFinalDirectCompatibleMembers(MF);

  return Changed;
}

INITIALIZE_PASS(HaydnLatencyStalls, DEBUG_TYPE,
                "Haydn Exposed-Pipeline Latency Stalls", false, false)

FunctionPass *llvm::createHaydnLatencyStallsPass() {
  return new HaydnLatencyStalls();
}
