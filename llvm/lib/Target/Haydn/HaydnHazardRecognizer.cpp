//===-- HaydnHazardRecognizer.cpp - Haydn scoreboard hazard recognizer ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the Haydn scoreboard hazard recognizer. See the header
// (HaydnHazardRecognizer.h) for the design and the rationale for why this
// path is safe vs the / UAF in VLIWMachineScheduler.
//
//===----------------------------------------------------------------------===//

#include "HaydnHazardRecognizer.h"
#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePortBudget.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnIntraCycleRAW.h"
#include "HaydnIntraCycleWAW.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "HaydnResourceRestrictionClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <functional>

using namespace llvm;
using namespace llvm::haydn::bundle;

//===----------------------------------------------------------------------===//
// applyFormatOrdering / getFieldOrderedMembers — AIEHazardRecognizer.cpp:278-314
// peer
//===----------------------------------------------------------------------===//
// Rebuild BUNDLE children in Format.getSlots() field order. Product Format E
// FormatSlotData is entry-slot order (HaydnGenFormats.inc). Call site is
// finalizeLegalMultiMI when size()>1 (AIE applyBundles 343-344).
//
// getFieldOrderedMembers is the single source of truth for the member order
// the encoder / AsmPrinter / hardware observe. commitExactMultiMIProductCycle
// validates the no-forwarding intra-bundle RAW law on THIS order (not only on
// schedule order): a legal same-cycle WAR in schedule order (reader before
// writer) can be permuted by the field order into writer-before-reader, which
// finalizeBundle would then mark IsInternalRead — modeling a same-cycle read
// of a same-cycle def, i.e. the no-forwarding hazard. Any change to the slot
// iteration here is automatically reflected in that commit-time check.
SmallVector<MachineInstr *, 3>
llvm::getFieldOrderedMembers(const Haydn::MachineBundle &Bundle,
                             const VLIWFormat &Format) {
  SmallVector<MachineInstr *, 3> Out;
  HaydnMCFormats Fmts;
  auto already = [&](const MachineInstr *MI) {
    return llvm::is_contained(Out, MI);
  };
  auto collect = [&](MCSlotKind Slot, bool AssertSlotInfo) {
    if (AssertSlotInfo) {
      const MCSlotInfo *SlotInfo = Fmts.getSlotInfo(Slot);
      assert(SlotInfo && "getFieldOrderedMembers: format slot has no SlotInfo");
      (void)SlotInfo;
    }
    MachineInstr *MI = Bundle.at(Slot);
    if (MI && !already(MI))
      Out.push_back(MI);
  };
  // PacketFormats list Format E entries high-to-low (E3_2, E3_1, E3_0 /
  // E2_1, E2_0 — HaydnGenFormats.inc FormatSlotData). That is the same
  // S2→S1→S0 materialize order selectPreferredCandidate uses
  // (HaydnBundleFormatSolver.h). AIE walks Format.getSlots() as-is
  // (AIEHazardRecognizer.cpp:300-314); Haydn overlay keeps that walk and
  // also collects residual S2/S1/S0 keys so a mixed E*/S* SlotMap still
  // emits high-to-low. Emitting low-to-high would flip a legal WAR
  // (reader issued first, writer on a lower entry) into writer-before-
  // reader and reject the pack as a false no-forwarding RAW.
  for (MCSlotKind Slot : Format.getSlots())
    collect(Slot, /*AssertSlotInfo=*/true);
  static const MCSlotKind ResidualFieldOrder[] = {
      MCSlotKind(MCSlotKind::Haydn_SLOT_S2),
      MCSlotKind(MCSlotKind::Haydn_SLOT_S1),
      MCSlotKind(MCSlotKind::Haydn_SLOT_S0)};
  for (MCSlotKind Slot : ResidualFieldOrder)
    collect(Slot, /*AssertSlotInfo=*/false);
  for (const auto &KV : Bundle.getSlotMap())
    if (KV.second && !already(KV.second))
      Out.push_back(KV.second);
  return Out;
}

void llvm::applyFormatOrdering(Haydn::MachineBundle &Bundle,
                               const VLIWFormat &Format,
                               MachineBasicBlock::iterator InsertPoint) {
  assert(Bundle.getSlotMap().size() == Bundle.getInstrs().size() &&
         "Bundle has instructions without slot");
  if (Bundle.empty())
    return;

  // Post-RA only (phase firewall / PIPE-31): format ordering finalizes BUNDLE
  // identity. Release-visible: virtual register operands prove a pre-RA call
  // site; product must not form durable BUNDLE order before physical allocation.
  for (const MachineInstr *MI : Bundle.getInstrs()) {
    for (const MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.getReg().isVirtual())
        report_fatal_error(
            "applyFormatOrdering is post-RA only (virtual register operand)",
            /*GenCrashDiag=*/false);
    }
  }

  MachineBasicBlock &MBB = *Bundle.getInstrs()[0]->getParent();

  SmallVector<MachineInstr *, 3> Ordered =
      getFieldOrderedMembers(Bundle, Format);
  assert(!Ordered.empty() && "applyFormatOrdering: format had no members");

  MachineInstr *FirstMI = nullptr;
  for (MachineInstr *Instr : Ordered) {
    Instr->removeFromBundle();
    MBB.insert(InsertPoint, Instr);
    if (!FirstMI)
      FirstMI = Instr;
    else
      Instr->bundleWithPred();
  }

  // AIE skips finalize on AIE1; Haydn always finalizes so kill flags and
  // bundle-header properties are correct (AIEHazardRecognizer.cpp:309-312).
  assert(FirstMI && "applyFormatOrdering: format had no members");
  finalizeBundle(MBB, FirstMI->getIterator());
}

#define DEBUG_TYPE "haydn-hazard-rec"

namespace {
// CSRW↔SET_HWLOOP same-bundle hazard (golden
// VLIW_Engine_Compiler_Constraints.md):
// CSRW must not target CSR addresses 0x20-0x25 (HWLR_BEGIN, HWLR_END,
// HWLR_COUNT for loop 0 and loop 1) within the same bundle as a SET_HWLOOP,
// SET_HWLOOP_F2, or SET_HWLOOP_REG instruction.
// Writing those CSRs via CSRW in the same cycle as a SET_HWLOOP variant
// would race the implicit HWLR update; the packetizer must put the two in
// separate bundles (cycles).
// These helpers identify both sides of the hazard. They cover the
// SET_HWLOOP opcode variants Constraints.md names, including logical
// SET_HWLOOP_W / SET_HWLOOP_F2_W / SET_HWLOOP_REG_W (Format E encode)
// and residual SET_HWLOOP / SET_HWLOOP_REG pseudos lowered by AsmPrinter.
// HR no longer blank-skips MI.isPseudo(), so a residual SET_HWLOOP
// pseudo that reaches the scoreboard is subject to the same
// CSRW↔SET_HWLOOP gate as the real *_W forms.

// AIE peer (AIEHazardRecognizer.cpp:365-368; AIEBundle.h:206-213):
// true zero-resource meta is hazard-exempt. Never use MI.isPseudo() as a
// hazard exemption — MultiSlot_Pseudo is isPseudo=1 but must book issue,
// itinerary stages, and ports. Debug / BUNDLE / remaining LLVM meta
// (CFI, LIFETIME, …) stay zero-resource; isMetaInstruction covers
// IMPLICIT_DEF/KILL too (redundant with Bundle twin; kept for CFI/etc.).
bool isNoHazardMeta(const MachineInstr &MI) {
  if (MI.isDebugInstr() || MI.isBundle())
    return true;
  if (Haydn::MachineBundle::isNoHazardMetaInstruction(MI.getOpcode()))
    return true;
  // LLVM meta that is not MultiSlot_Pseudo (CFI, LIFETIME, PHI, …).
  return MI.isMetaInstruction();
}

// If MI is a CSRW whose CSR operand is in the HWLR range 0x20-0x25,
// return that CSR address; otherwise return -1.
// Operand layout: CSRW / CSRW_W / CSRW_S0 / CSRW_W_S0 are 2-op
// (uimm8, rs); $uimm8 is operand 0. The CSR operand is a uimm8
// immediate; we read getImm directly. The .td marks it uimm8 so a
// well-formed MI always carries an immediate here; we still guard
// isImm against malformed/MIR test input.
int getHwloopCsrAddr(const MachineInstr &MI) { return haydnHwloopCsrAddr(MI); }

// LUI / ADDI32_W stay e0-alone in the HR so a second real cannot steal the
// materialize entry. HI12 FieldLsb is still not "e0 ⇒ table 32": E3 e0 ALU2
// LUI is abs[21:32]. resolveFieldLsb owns the window.
bool isAbsMaterializeOp(unsigned Opcode) {
  return haydnIsAbsMaterializeOpcode(Opcode);
}
} // namespace

//===----------------------------------------------------------------------===//
// HaydnFuncUnitWrapper
//===----------------------------------------------------------------------===//

const HaydnBaseMCFormats *HaydnFuncUnitWrapper::FormatInterface = nullptr;

HaydnFuncUnitWrapper::HaydnFuncUnitWrapper(const InstrStage &IS) {
  // InstrStage Units_ bit N is set iff FU index N is in the choice set.
  // FU indices match HaydnExecUnit / HaydnSchedule.td seven-unit order.
  // AIE peer: FuncUnitWrapper(const InstrStage &) — Required vs Reserved.
  const uint64_t Units = IS.getUnits();
  ResourceSet *Target = nullptr;
  if (IS.getReservationKind() == InstrStage::Required)
    Target = &Required;
  else if (IS.getReservationKind() == InstrStage::Reserved)
    Target = &Reserved;
  if (!Target)
    return;
  for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit)
    if ((Units >> Bit) & 1u)
      Target->set(Bit);
}

bool HaydnFuncUnitWrapper::conflict(const HaydnFuncUnitWrapper &Other) const {
  // Unit injectivity on exclusive Required: both require the SAME single
  // unit (|Required|==1 and same bit). Multi-unit choice-sets (logical
  // possible-unit menus) never Required-conflict alone — format legality
  // and issue count govern packing. Simple Required.overlap would
  // over-serialize choice-set ALU vs a single committed unit.
  if (!Required.empty() && !Other.Required.empty()) {
    if (Required.count() == 1 && Other.Required.count() == 1 &&
        Required == Other.Required)
      return true;
  }

  // AIE FuncUnitWrapper::conflict Req/Res law (AIEHazardRecognizer.cpp:128-133):
  // Required overlaps Other.Reserved OR Reserved overlaps Other.Required.
  // Res/Res is intentionally legal — several instructions may reserve the
  // same unit (InstrStage::Reserved comment in MCInstrItineraries.h).
  if (Required.overlap(Other.Reserved) || Reserved.overlap(Other.Required))
    return true;

  // Max issue == generated E3 entry capacity (Haydn::ISSUE_SLOT_COUNT).
  if (IssueCount + Other.IssueCount > Haydn::ISSUE_SLOT_COUNT)
    return true;

  // GPR 4R2W register-file port budget.
  if (GPRReads + Other.GPRReads > HAYDN_GPR_READ_PORTS)
    return true;
  if (GPRWrites + Other.GPRWrites > HAYDN_GPR_WRITE_PORTS)
    return true;

  // DR64 7R3W register-file port budget.
  if (DRReads + Other.DRReads > HAYDN_DR_READ_PORTS)
    return true;
  if (DRWrites + Other.DRWrites > HAYDN_DR_WRITE_PORTS)
    return true;

  // AR 2R2W register-file port budget.
  if (ARReads + Other.ARReads > HAYDN_AR_READ_PORTS)
    return true;
  if (ARWrites + Other.ARWrites > HAYDN_AR_WRITE_PORTS)
    return true;

  // SFR 2R1W register-file port budget (also enforces single SFR writer).
  if (SFRReads + Other.SFRReads > HAYDN_SFR_READ_PORTS)
    return true;
  if (SFRWrites + Other.SFRWrites > HAYDN_SFR_WRITE_PORTS)
    return true;

  // AIE FuncUnitWrapper::conflict (AIEHazardRecognizer.cpp:136-138)
  // same-kind memory-object overlap is not overlaid: golden dual-load
  // of one object (LOADSTORE0 + LOAD1) is legal. Wait-cycle same-object
  // reject is an unadmitted ISA row and must not serialize p[0]/p[1].
  static_assert(!haydnMemoryObjectWaitCyclesAdmitted(),
                "memory-object wait-cycle reject stays fail-closed");

  // AIE FuncUnitWrapper::conflict (AIEHazardRecognizer.cpp:147-150):
  // don't check formats unless both have occupied slots. A blocked cycle
  // (Slots = ~0) then conflicts without needing the LUT contents.
  if (Slots && Other.Slots) {
    const SlotBits Combined = Slots | Other.Slots;
    if (!FormatInterface)
      return true;
    // LUT is the AIE isFormatAvailable peer. A miss is a conflict even
    // when productCovers' FieldSlots transitional accept would say yes.
    if (!FormatInterface->isFormatAvailable(Combined))
      return true;
    // Fail-closed PacketFormats cover: getFormat is getFormatOrNull's
    // first arm (AIEBundle.h:150-156). productCovers may only reject.
    const PacketFormats &PF = FormatInterface->getPacketFormats();
    if (!PF.getFormat(Combined) && !haydn::bundle::productCovers(PF, Combined))
      return true;
  }

  return false;
}

void HaydnFuncUnitWrapper::dump() const {
  dbgs() << "{req:";
  bool First = true;
  for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit) {
    if (Required.test(Bit)) {
      dbgs() << (First ? "" : "|") << Bit;
      First = false;
    }
  }
  if (First)
    dbgs() << "-";
  dbgs() << " rsrv:";
  First = true;
  for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit) {
    if (Reserved.test(Bit)) {
      dbgs() << (First ? "" : "|") << Bit;
      First = false;
    }
  }
  if (First)
    dbgs() << "-";
  dbgs() << " issue:" << IssueCount << " slots:" << Slots << " gpr:" << GPRReads
         << "R/" << GPRWrites << "W dr:" << DRReads << "R/" << DRWrites
         << "W ar:" << ARReads << "R/" << ARWrites << "W sfr:" << SFRReads
         << "R/" << SFRWrites << "W lobj:" << LoadMemObjectsBits
         << " sobj:" << StoreMemObjectsBits << "}";
}

//===----------------------------------------------------------------------===//
// HaydnHazardRecognizer
//===----------------------------------------------------------------------===//

HaydnHazardRecognizer::HaydnHazardRecognizer(const TargetInstrInfo *TII,
                                             const InstrItineraryData *ItinData,
                                             bool IsPreRA,
                                             HaydnAlternateDescriptors *AltDescs)
    : TII(TII), ItinData(ItinData), IsPreRA(IsPreRA), AltDescs(AltDescs) {
  // AIE FuncUnitWrapper::setFormatInterface from the HR ctor.
  HaydnFuncUnitWrapper::setFormatInterface(&haydnDefaultMCFormats());
  // Compute the scoreboard depth from the itineraries so the window covers
  // the deepest pipeline + max result latency. For Haydn's current
  // single-stage, latency-1 itineraries this is small (1-2), but Stream C
  // enrichments will make it larger and this computation adapts automatically.
  computeMaxLatency();
  const int Depth = std::max(getPipelineDepth(), 1);
  Scoreboard.reset(Depth);
  ScoreboardAtCycleStart.reset(Depth);
  MaxLookAhead = static_cast<unsigned>(Depth);
}

HaydnHazardRecognizer::MatchingFrontierScore
HaydnHazardRecognizer::scoreMatchingFrontier(ArrayRef<CycleState> Base,
                                             unsigned LogicalOpc) {
  MatchingFrontierScore Score;
  if (Base.empty())
    return Score;

  // Process-wide singleton — never a per-call HaydnMCFormats local. Pre-RA
  // tryCandidate probes this twice per compare; a local formats view was
  // compile-time overhead on large ready sets (IIR / CoreMark / Dhrystone).
  const HaydnMCFormats &LocalFmts = haydnDefaultMCFormats();
  auto FillFromPreferred = [&](const CycleCandidateSet &Cands) {
    const CycleState &Pref = selectPreferredCandidate(Cands);
    Score.SuccessorMatchings = static_cast<unsigned>(Cands.size());
    Score.FreeSlotsPreferred = llvm::popcount(
        static_cast<unsigned>(kIssueSlotUniverse & ~Pref.OccupiedSlots));
    Score.FeasibleFormatMask = Pref.FeasibleFormatMask;
  };

  // Ops without PlacementAlternatives do not expand the set; they stay
  // format-feasible as long as the base frontier is non-empty (ports/issue
  // are gated separately by getHazardType).
  if (!hasPlacementAlternatives(LocalFmts, LogicalOpc)) {
    Score.Feasible = true;
    CycleCandidateSet View(Base.begin(), Base.end());
    FillFromPreferred(View);
    return Score;
  }

  if (!canExactTryAddProduct(Base, LocalFmts, LogicalOpc)) {
    Score.Feasible = false;
    return Score;
  }

  CycleCandidateSet Next(Base.begin(), Base.end());
  (void)exactTryAddProduct(Next, LocalFmts, LogicalOpc);
  Score.Feasible = true;
  FillFromPreferred(Next);
  return Score;
}

namespace {
// AIE anyStage peer (AIEHazardRecognizer.cpp:154-170): walk itinerary stages
// and invoke Action(relativeCycle, stageWrapper) for each occupied cycle.
// Returns true as soon as Action returns true.
using HaydnFuncUnitWrapperAction =
    std::function<bool(int, const HaydnFuncUnitWrapper &)>;

bool anyStage(const InstrItineraryData *ItinData, unsigned SchedClass,
              HaydnFuncUnitWrapperAction Action) {
  if (!ItinData || ItinData->isEmpty())
    return false;
  int Cycle = 0;
  for (const InstrStage *IS = ItinData->beginStage(SchedClass),
                        *E = ItinData->endStage(SchedClass);
       IS != E; ++IS) {
    const HaydnFuncUnitWrapper ThisCycle(*IS);
    for (unsigned C = 0; C < IS->getCycles(); ++C) {
      if (Action(Cycle + static_cast<int>(C), ThisCycle))
        return true;
    }
    Cycle += static_cast<int>(IS->getNextCycles());
  }
  return false;
}
} // namespace

void HaydnHazardRecognizer::computeMaxLatency() {
  if (!ItinData || ItinData->isEmpty()) {
    MaxLatency = 1;
    PipelineDepth = 1;
    return;
  }
  int MaxPipelineDepth = 1;
  int MaxOpLatency = 1;
  // Walk every scheduling class.
  for (unsigned SchedClass = 0; !ItinData->isEndMarker(SchedClass); ++SchedClass) {
    // Pipeline depth from the stage chain.
    unsigned StartCycle = 0;
    unsigned StageDepth = 0;
    for (const InstrStage *IS = ItinData->beginStage(SchedClass),
                          *E = ItinData->endStage(SchedClass);
         IS != E; ++IS) {
      StageDepth = std::max(StageDepth, StartCycle + IS->getCycles());
      StartCycle += IS->getNextCycles();
    }
    MaxPipelineDepth = std::max(MaxPipelineDepth, static_cast<int>(StageDepth));

    // Operand/result latencies.
    int FirstOp = ItinData->Itineraries[SchedClass].FirstOperandCycle;
    int LastOp = ItinData->Itineraries[SchedClass].LastOperandCycle;
    if (FirstOp < LastOp) {
      for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx) {
        const unsigned Lat = ItinData->OperandCycles[OpIdx];
        if (Lat > 0)
          MaxOpLatency = std::max(
              MaxOpLatency,
              static_cast<int>(haydn::restriction::clampPublishedDataLatency(
                  Lat)));
      }
    }
  }
  PipelineDepth = MaxPipelineDepth;
  MaxLatency = MaxOpLatency;
  // Scoreboard ring must cover the SIN_COS/ARCTAN occupancy window
  // (uimm4_max+2). OperandCycles 17 still do not inflate MaxLatency.
  PipelineDepth = std::max(
      PipelineDepth,
      static_cast<int>(haydn::restriction::SinCosScaffoldDataLatency));
}

void HaydnHazardRecognizer::captureCycleStartScoreboard() {
  ScoreboardAtCycleStart = Scoreboard;
}

void HaydnHazardRecognizer::reenterCurrentCycleScoreboard() {
  // Restore residuals from prior cycles, then re-book every MI issued this
  // cycle with post-rematch selected member schedclasses (single-slot FU bits).
  Scoreboard = ScoreboardAtCycleStart;
  for (MachineInstr *Placed : CurrentCyclePlacedMIs)
    enterResources(Scoreboard, *Placed, /*DeltaCycles=*/0);
  assert(Scoreboard[0].getIssueCount() == CurrentCyclePlacedMIs.size() &&
         "reenterCurrentCycleScoreboard IssueCount must match placed MIs");
}

void HaydnHazardRecognizer::Reset() {
  // Shared PortModel availability-aware record pin (one authority with
  // pre-RA / ordinary post-RA / LatencyStalls). Aggregate ceilings only
  // while the complete per-op table is closed; competitive claims stay
  // fail-closed. Member, not a function-local static.
  if (!ResourceAdmissionPinned) {
    ResourceAdmissionPinned = true;
    if (!haydnAvailabilityAwareConsumePinsHold())
      report_fatal_error(
          "Haydn hazard recognizer resource admission pins failed",
          /*GenCrashDiag=*/false);
  }

  Scoreboard.clear();
  captureCycleStartScoreboard();
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleHasAbsMaterialize = false;
  CurrentCycleHasNonAbsReal = false;
  DestReadPending.clear();
  DestWritePending.clear();
  CurrentCycleCandidates = makeProductCandidateSet();
  CurrentCyclePlacedMIs.clear();
  TRI = nullptr;
}

unsigned
HaydnHazardRecognizer::resolveBookingOpcode(const MachineInstr &MI) const {
  // Post-rematch: AltDescs holds the selected format-member opcode. Use it
  // so stage FU bits are single-slot (member itinerary) rather than logical
  // multi-bit FieldSlots. Pre-selection / pre-RA: logical opcode.
  if (AltDescs) {
    if (std::optional<unsigned> Sel =
            AltDescs->getSelectedOpcode(const_cast<MachineInstr *>(&MI)))
      return *Sel;
  }
  return MI.getOpcode();
}

SlotBits
HaydnHazardRecognizer::occupancySlots(const MachineInstr &MI) const {
  // AIE getSlotSet(Desc) after MultiSlot materialize
  // (AIEHazardRecognizer.cpp:549/564). Selected / pinned member only.
  // Unplaced alts stay untracked (0): preferred-member slot OR of
  // LD32 (E3 e1) + ST32 (E2 e0) is not a legal mask and would serialize
  // a rematchable store+load before exactTryAdd can rematch.
  const unsigned Booked = resolveBookingOpcode(MI);
  const MCSlotKind Kind = Fmts.getSlotKind(Booked);
  if (Kind == MCSlotKind())
    return 0;
  const MCSlotInfo *SI = Fmts.getSlotInfo(Kind);
  return SI ? SI->getSlotSet() : 0;
}

bool MemoryObjectEnumerator::isFull() const {
  return ObjectCounter == sizeof(MemoryObjectsBits) * CHAR_BIT;
}

std::optional<unsigned>
MemoryObjectEnumerator::getObjectNumber(const Value *Object) {
  auto ItNumber = ObjectNumberingMap.find(Object);
  if (ItNumber != ObjectNumberingMap.end())
    return ItNumber->second;

  const Value *ParentObject = getUnderlyingObject(Object);
  auto ItParent = ObjectNumberingMap.find(ParentObject);
  if (ItParent != ObjectNumberingMap.end()) {
    ObjectNumberingMap[Object] = ItParent->second;
    return ItParent->second;
  }
  if (isFull())
    return std::nullopt;

  const unsigned ObjectNumber = ObjectCounter++;
  ObjectNumberingMap[ParentObject] = ObjectNumber;
  ObjectNumberingMap[Object] = ObjectNumber;
  return ObjectNumber;
}

MemoryObjectPair
HaydnHazardRecognizer::getMemoryObjectsBits(const MachineInstr *MI) const {
  // AIEHazardRecognizer.cpp:829-862. Wait-cycle avoidance, not a golden
  // bank invent. Pre-RA must not tighten the ready set further. Bits are
  // dump-only while haydnMemoryObjectWaitCyclesAdmitted is false —
  // conflict() must not consult them.
  MemoryObjectPair Result;
  if (!MI || IsPreRA || (!MI->mayLoad() && !MI->mayStore()))
    return Result;
  if (MI->memoperands_empty())
    return Result;

  MemoryObjectsBits Objects = 0;
  for (const MachineMemOperand *MMO : MI->memoperands()) {
    const Value *BaseObject = MMO->getValue();
    if (!BaseObject)
      continue;
    if (auto ObjectNumber = ObjectEnumerator.getObjectNumber(BaseObject))
      Objects |= (MemoryObjectsBits(1) << *ObjectNumber);
  }
  if (MI->mayLoad())
    Result.Load = Objects;
  if (MI->mayStore())
    Result.Store = Objects;
  return Result;
}

unsigned
HaydnHazardRecognizer::resolveSchedClass(const MachineInstr &MI) const {
  if (!TII)
    return 0;
  return TII->get(resolveBookingOpcode(MI)).getSchedClass();
}

// Issue-cycle footprint: ports + IssueCount + stage-0 unit bits (for
// getInstrFootprint / pure conflict probes). Multi-cycle stages are booked
// only via checkConflict/enterResources (AIE anyStage).
//
// Processor-resource identity is the seven execution units from the
// itinerary. PlacementAlternative FieldSlots encode entry/field geometry for
// the format solver and must not be merged into unit Required bits (slots
// are not units).
HaydnFuncUnitWrapper
HaydnHazardRecognizer::buildCandidate(const MachineInstr &MI) const {
  HaydnFuncUnitWrapper Candidate;
  Candidate.setIssueCountOne();
  Candidate.setSlots(occupancySlots(MI));

  const unsigned BookingOpc = resolveBookingOpcode(MI);
  const unsigned SchedClass =
      TII ? TII->get(BookingOpc).getSchedClass() : 0;

  if (ItinData && !ItinData->isEmpty() && SchedClass != 0) {
    // Only fold stage-relative cycle 0 into the issue-cycle footprint.
    (void)anyStage(ItinData, SchedClass,
                   [&](int StageCycle, const HaydnFuncUnitWrapper &Stage) {
                     if (StageCycle == 0)
                       Candidate |= Stage;
                     return false;
                   });
  }

  auto [Reads, Writes] = countGPRPorts(MI);
  Candidate.setGPRPorts(Reads, Writes);
  auto [DRReads, DRWrites] = countDRPorts(MI);
  Candidate.setDRPorts(DRReads, DRWrites);
  auto [ARReads, ARWrites] = countARPorts(MI);
  Candidate.setARPorts(ARReads, ARWrites);
  auto [SFRReads, SFRWrites] = countSFRPorts(MI);
  Candidate.setSFRPorts(SFRReads, SFRWrites);
  const MemoryObjectPair Mem = getMemoryObjectsBits(&MI);
  Candidate.setMemoryObjectBits(Mem.Load, Mem.Store);
  return Candidate;
}

bool HaydnHazardRecognizer::checkConflict(
    const ResourceScoreboard<HaydnFuncUnitWrapper> &SB, const MachineInstr &MI,
    int DeltaCycles) const {
  // AIEHazardRecognizer::checkConflict peer (AIEHazardRecognizer.cpp:567-616):
  // issue-cycle ports/issue at DeltaCycles; each itinerary stage at
  // DeltaCycles+StageCycle. No second ring / legality table.
 // : isNoHazardMeta only — MultiSlot_Pseudo books stages/ports.
  if (isNoHazardMeta(MI))
    return false;
  if (!SB.isInRange(DeltaCycles))
    return false;

  // Issue-cycle ports + issue count + Format E Slots (AIE EmissionCycle
  // SlotSet at AIEHazardRecognizer.cpp:583-585). Unit bits come from
  // itinerary stages so they are not double-counted here.
  HaydnFuncUnitWrapper IssueOnly;
  IssueOnly.setIssueCountOne();
  IssueOnly.setSlots(occupancySlots(MI));
  auto [Reads, Writes] = countGPRPorts(MI);
  IssueOnly.setGPRPorts(Reads, Writes);
  auto [DRReads, DRWrites] = countDRPorts(MI);
  IssueOnly.setDRPorts(DRReads, DRWrites);
  auto [ARReads, ARWrites] = countARPorts(MI);
  IssueOnly.setARPorts(ARReads, ARWrites);
  auto [SFRReads, SFRWrites] = countSFRPorts(MI);
  IssueOnly.setSFRPorts(SFRReads, SFRWrites);
  // AIE EmissionCycle books memory-object bits here
  // (AIEHazardRecognizer.cpp:561-584). Same-kind overlap is not a
  // same-cycle reject (golden dual-load); bits stay for dump / future
  // wait-cycle once an ISA row is admitted.
  const MemoryObjectPair Mem = getMemoryObjectsBits(&MI);
  IssueOnly.setMemoryObjectBits(Mem.Load, Mem.Store);
  if (SB[DeltaCycles].conflict(IssueOnly))
    return true;

  // Residual logical itineraries (MOVE32_DR Slot0_ALU, TSFlags "slot 0")
  // are FieldSlot leftovers, not bound Format E units. After a member
  // books ALU0, the next unplaced MOVE32_DR still looks like Required
  // ALU0 and checkConflict would serialize a pair the solver can place
  // on ALU0+ALU1. Same-cycle unit injectivity is exactTryAddProduct.
  //
  // PA-N1 / W35: do not early-return here. That skipped every itinerary
  // stage at DeltaCycles==0, including StageCycle>0 occupancy. Multi-stage
  // fitInInterval / resourcesConverged uses checkConflict as its only
  // placement gate. Skip only leftover StageCycle==0 unit bits; still
  // walk StageCycle>0. Ports/issue already checked above.
  const bool UnplacedAltsSameCycle =
      DeltaCycles == 0 && resolveBookingOpcode(MI) == MI.getOpcode() &&
      hasPlacementAlternatives(Fmts, MI.getOpcode());

  const unsigned SchedClass = resolveSchedClass(MI);
  bool SawStage = false;
  if (ItinData && !ItinData->isEmpty() && SchedClass != 0) {
    LLVM_DEBUG({
      if (UnplacedAltsSameCycle)
        dbgs() << "checkConflict: same-cycle itinerary occupancy "
                  "(skip leftover StageCycle==0 unit bits)\n";
    });
    if (anyStage(
            ItinData, SchedClass,
            [&](int StageCycle, const HaydnFuncUnitWrapper &ThisCycle) {
              if (UnplacedAltsSameCycle && StageCycle == 0) {
                SawStage = true;
                return false;
              }
              SawStage = true;
              const int ScoreboardCycle = DeltaCycles + StageCycle;
              if (!SB.isInRange(ScoreboardCycle))
                return false; // out-of-window = empty
              if (ThisCycle.conflict(SB[ScoreboardCycle])) {
                LLVM_DEBUG(dbgs()
                           << "*** Hazard in cycle=" << ScoreboardCycle
                           << " EC=" << StageCycle << ":\n";
                           ThisCycle.dump(); dbgs() << "\n");
                return true;
              }
              return false;
            }))
      return true;
  }
  if (!SawStage) {
    // No itinerary stages: ports/issue only from buildCandidate.
    // Unplaced same-cycle alts must not revive leftover StageCycle==0
    // unit bits through this fallback.
    if (UnplacedAltsSameCycle)
      return false;
    HaydnFuncUnitWrapper Cand = buildCandidate(MI);
    if (SB[DeltaCycles].conflict(Cand))
      return true;
  }
  return false;
}

void HaydnHazardRecognizer::enterResources(
    ResourceScoreboard<HaydnFuncUnitWrapper> &SB, const MachineInstr &MI,
    int DeltaCycles) const {
  // AIEHazardRecognizer::enterResources peer (AIEHazardRecognizer.cpp:661-696).
 // : isNoHazardMeta only — MultiSlot_Pseudo books stages/ports.
  if (isNoHazardMeta(MI))
    return;
  if (!SB.isInRange(DeltaCycles))
    return;

  HaydnFuncUnitWrapper IssueOnly;
  IssueOnly.setIssueCountOne();
  IssueOnly.setSlots(occupancySlots(MI));
  auto [Reads, Writes] = countGPRPorts(MI);
  IssueOnly.setGPRPorts(Reads, Writes);
  auto [DRReads, DRWrites] = countDRPorts(MI);
  IssueOnly.setDRPorts(DRReads, DRWrites);
  auto [ARReads, ARWrites] = countARPorts(MI);
  IssueOnly.setARPorts(ARReads, ARWrites);
  auto [SFRReads, SFRWrites] = countSFRPorts(MI);
  IssueOnly.setSFRPorts(SFRReads, SFRWrites);
  const MemoryObjectPair Mem = getMemoryObjectsBits(&MI);
  IssueOnly.setMemoryObjectBits(Mem.Load, Mem.Store);
  SB[DeltaCycles] |= IssueOnly;

  const unsigned SchedClass = resolveSchedClass(MI);
  bool BookedStage = false;
  if (ItinData && !ItinData->isEmpty() && SchedClass != 0) {
    (void)anyStage(
        ItinData, SchedClass,
        [&](int StageCycle, const HaydnFuncUnitWrapper &ThisCycle) {
          const int ScoreboardCycle = DeltaCycles + StageCycle;
          if (SB.isInRange(ScoreboardCycle)) {
            SB[ScoreboardCycle] |= ThisCycle;
            BookedStage = true;
          }
          return false;
        });
  }
  if (!BookedStage) {
    // No itinerary: unit Required stays empty; ports/issue already booked.
    HaydnFuncUnitWrapper Cand = buildCandidate(MI);
    SB[DeltaCycles].mergeRequired(Cand.getRequired());
    SB[DeltaCycles].mergeReserved(Cand.getReserved());
  }
}

void HaydnHazardRecognizer::emitInScoreboard(
    ResourceScoreboard<HaydnFuncUnitWrapper> &SB, const MachineInstr &MI,
    int Cycle) const {
  enterResources(SB, MI, Cycle);
  // SIN_COS/ARCTAN occupancy (Constraints:140): book Reserved on the
  // selected unit for cycles Cycle+1 .. Cycle+uimm4+1 so a later Required
  // of the same unit conflicts (NOP-on-unit). Dest-writer stays on the
  // list-scheduler emit path (DestWritePending); SMS placement sees the
  // unit lock through this scoreboard.
  const unsigned Occupancy = sinCosWindowOccupancy(MI);
  if (Occupancy < 2)
    return;
  const unsigned SchedClass = resolveSchedClass(MI);
  HaydnFuncUnitWrapper::ResourceSet Units;
  if (ItinData && !ItinData->isEmpty() && SchedClass != 0) {
    (void)anyStage(ItinData, SchedClass,
                   [&](int StageCycle, const HaydnFuncUnitWrapper &Stage) {
                     if (StageCycle == 0)
                       Units |= Stage.getRequired();
                     return false;
                   });
  }
  if (Units.empty())
    return;
  for (unsigned K = 1; K < Occupancy; ++K) {
    const int Cyc = Cycle + static_cast<int>(K);
    if (!SB.isInRange(Cyc))
      break;
    SB[Cyc].mergeReserved(Units);
  }
}

const TargetRegisterInfo *
HaydnHazardRecognizer::getTRI(const MachineInstr &MI) {
  if (!TRI)
    TRI = MI.getMF()->getSubtarget().getRegisterInfo();
  return TRI;
}

bool HaydnHazardRecognizer::hasSameBundleWAW(const MachineInstr &MI) const {
  // same-bundle destination-register WAW, delegating to the ONE shared
  // no-dual-write law (HaydnIntraCycleWAW.h — W39 unification). The
  // candidate's defs are checked here (not in buildCandidate, which carries
  // only port counts) for overlap against CurrentCycleDefs via regsOverlap —
  // the same alias semantic as MachineInstr::modifiesRegister (the retired
  // packetizer's hasWAWHazard). Virtual registers (pre-RA) match by Register
  // identity; physregs use TRI::regsOverlap for aliases/subregs.
  // FE5B WP4: dual of HaydnResourceCycle same-phase WAW / certificate
  // same-reg DefRegKey fail-close (productSamePhaseWAWFailsClosed).
  //
  // R0 is *soft*-zero (not hardwired): XOR32 R0,R0,R0 and LD into R0 are real
  // write-port consumers. Excluding R0 allowed illegal packets such as
  //   { xor32 r0,r0,r0; ld32 r0, base, 0; ... }
  // which BundleSim correctly rejects as WRITE_CONFLICT.
  // SFR is included: product law is one SFR writer per cycle — dead
  // implicit-def $sfr still collides with a second SFR def (WAW).
  // Returns false if TRI is not yet cached: that only happens before the first
  // emit, when CurrentCycleDefs is empty and no WAW is possible anyway.
  // TRI is required for physreg alias checks. Vreg identity checks work
  // without it (pre-RA). Empty CurrentCycleDefs short-circuits either way.
  return haydnHasIntraCycleWAW(MI, CurrentCycleDefs, TRI);
}

bool HaydnHazardRecognizer::hasSameBundleRAW(const MachineInstr &MI) const {
  // same-bundle RAW (the dual of WAW). Haydn spec §Constraints:
  // "All instructions within the same bundle read their source registers
  // simultaneously" — there is no intra-bundle forwarding, so a reader that
  // shares a cycle with a writer of the same register observes the OLD value.
  //
  // BUT a same-bundle read+write is only a hazard when it is a TRUE RAW — i.e.
  // the reader consumes the writer's value. If the write is DEAD (no consumer)
  // the reader correctly observes the OLD value and the bundle is legal (WAR
  // style). So we check the candidate's reads against CurrentCycleLiveDefs
  // (live defs only), not the full CurrentCycleDefs. The dead flag is the
  // scheduling-DAG/liveness verdict on "does this def have a consumer"; checking
  // it is equivalent to consulting the SDag for a real RAW edge, without
  // storing SUnit pointers (UAF avoidance).
  //
  // The scheduler issues writers before their RAW readers (topological order)
  // so a live writer is in CurrentCycleLiveDefs by the time its consumer-reader
  // is evaluated. WAR (reader issued before writer) does not trip: the writer
  // candidate does not read that reg, and WAR in a bundle is legal on Haydn
  // anyway. SFR is still excluded from the live-def RAW set (dead flag
  // side-effects have no consumer; live SFR RAW is rare and port-gated).
  // R0 is NOT excluded: soft-zero is a real register (borrow/restore); a
  // same-bundle reader of a live R0 write would observe the OLD value.
  // Vregs (pre-RA) match by Register identity; physregs use regsOverlap.
  //
  // The predicate itself is the ONE shared no-forwarding mechanism
  // (HaydnIntraCycleRAW.h) also used by SMS HaydnResourceCycle, so post-RA
  // commit and SMS placement enforce the identical law (hard constraint #7).
  return haydnHasIntraCycleRAW(MI, CurrentCycleLiveDefs, TRI);
}

void HaydnHazardRecognizer::appendDefs(const MachineInstr &MI) {
  // Record destination registers. SFR is included: product law is one SFR
  // writer per cycle (dead implicit-def $sfr still counts). R0 is tracked:
  // soft-zero restores and R0-borrow loads are real defs. Every def goes into
  // CurrentCycleDefs for the WAW check; only LIVE non-SFR defs go into
  // CurrentCycleLiveDefs for the RAW check (a dead write has no consumer and
  // a same-bundle reader correctly observes the OLD value). Virtual defs
  // (pre-RA) are tracked by Register identity.
  if (!TRI)
    (void)getTRI(MI);
  // WAW set: ALL defs (live or dead), including SFR — the spec forbids two
  // writes to one register regardless of liveness. Delegated to the shared
  // no-dual-write mechanism (HaydnIntraCycleWAW.h — W39; same predicate SMS
  // HaydnResourceCycle and the materialize commit path use), not part of the
  // shared no-forwarding RAW law.
  haydnAppendCycleDefs(MI, CurrentCycleDefs);
  // RAW live-def set: delegated to the shared no-forwarding mechanism
  // (HaydnIntraCycleRAW.h) — same predicate SMS HaydnResourceCycle uses.
  haydnAppendLiveDefs(MI, CurrentCycleLiveDefs);
}

bool HaydnHazardRecognizer::opcodeIssuesAloneInCycle(unsigned Opcode) {
  // One classify site (PortModel / logicalOpcodeOrSelf). Members peel to
  // the logical; do not re-list ARCTAN/SIN_COS here.
  return haydnOpcodeIssuesAloneInCycle(Opcode);
}

unsigned HaydnHazardRecognizer::sinCosWindowOccupancy(const MachineInstr &MI) {
  return haydnSinCosWindowOccupancy(MI);
}

bool HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(
    const MachineInstr &Cand, ArrayRef<const MachineInstr *> Occupied) {
  return haydn::pack::cycleViolatesNamedSameCycleLaws(Cand, Occupied);
}

unsigned HaydnHazardRecognizer::architecturalDefLatency(
    const InstrItineraryData *Itin, const MachineInstr &MI, unsigned DefOpIdx) {
  if (unsigned Occ = sinCosWindowOccupancy(MI))
    return Occ;
  if (!Itin || Itin->isEmpty())
    return 1;
  const unsigned SchedClass = MI.getDesc().getSchedClass();
  if (std::optional<unsigned> Cycle =
          Itin->getOperandCycle(SchedClass, DefOpIdx))
    if (*Cycle != 0)
      return haydn::restriction::clampPublishedDataLatency(*Cycle);
  unsigned Max = 1;
  const int FirstOp = Itin->Itineraries[SchedClass].FirstOperandCycle;
  const int LastOp = Itin->Itineraries[SchedClass].LastOperandCycle;
  for (int OpIdx = FirstOp; OpIdx < LastOp; ++OpIdx)
    Max = std::max(Max, Itin->OperandCycles[OpIdx]);
  return haydn::restriction::clampPublishedDataLatency(Max);
}

bool HaydnHazardRecognizer::isLockedSlotDspOp(const MachineInstr &MI) const {
  // Class-1 alone-in-cycle (PackLegality rule 4). Check the MI opcode and the
  // selected AltDesc member so post-setDesc recommit keeps the law.
  if (opcodeIssuesAloneInCycle(MI.getOpcode()))
    return true;
  const unsigned Booked = resolveBookingOpcode(MI);
  return Booked != MI.getOpcode() && opcodeIssuesAloneInCycle(Booked);
}

void HaydnHazardRecognizer::bookSinCosWindow(const MachineInstr &MI) {
  // Pre-RA is OnlyBottomUp and recedes. Dest/unit windows are post-RA
  // top-down no-interlock overlays; DAG SDep carries pre-RA latency.
  if (IsPreRA)
    return;
  const unsigned Occupancy = sinCosWindowOccupancy(MI);
  if (Occupancy < 2)
    return;

  // Selected-unit occupancy: book Reserved on cycles 1..Occupancy-1 so a
  // later Required of the same unit conflicts (NOP-on-unit). After
  // commitPlacementForEmit the booked member is a single ALU1/ALU2 bit.
  const unsigned SchedClass = resolveSchedClass(MI);
  HaydnFuncUnitWrapper::ResourceSet Units;
  if (ItinData && !ItinData->isEmpty() && SchedClass != 0) {
    (void)anyStage(ItinData, SchedClass,
                   [&](int StageCycle, const HaydnFuncUnitWrapper &Stage) {
                     if (StageCycle == 0)
                       Units |= Stage.getRequired();
                     return false;
                   });
  }
  if (!Units.empty()) {
    for (unsigned K = 1; K < Occupancy; ++K) {
      const int Cyc = static_cast<int>(K);
      if (!Scoreboard.isInRange(Cyc))
        break;
      Scoreboard[Cyc].mergeReserved(Units);
    }
  }

  // No dest-writer during the occupancy window (dest commits at cycle
  // Occupancy). Overlap uses TRI when available.
  (void)getTRI(MI);
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef() || !MO.getReg())
      continue;
    // Status regs are exclusive-writer / port-gated; they are not the
    // architectural dest the occupancy window names.
    if (haydnIsSimplifiableReservedReg(MO.getReg()))
      continue;
    unsigned &Remain = DestWritePending[MO.getReg()];
    Remain = std::max(Remain, Occupancy - 1);
  }
}

void HaydnHazardRecognizer::bookDestReadWindow(const MachineInstr &MI) {
  if (IsPreRA)
    return;
  (void)getTRI(MI);
  for (unsigned OpIdx = 0, E = MI.getNumOperands(); OpIdx != E; ++OpIdx) {
    const MachineOperand &MO = MI.getOperand(OpIdx);
    if (!MO.isReg() || !MO.isDef() || !MO.getReg())
      continue;
    // Dead defs have no consumer. SFR/CBR are status — exclusive writer /
    // port law, not Data_Latency dests. Booking them serializes every later
    // implicit-SFR user and invents idle parcels.
    if (MO.isDead() || haydnIsSimplifiableReservedReg(MO.getReg()))
      continue;
    const unsigned Lat = architecturalDefLatency(ItinData, MI, OpIdx);
    if (Lat <= 1)
      continue;
    unsigned &Remain = DestReadPending[MO.getReg()];
    Remain = std::max(Remain, Lat - 1);
  }
}

void HaydnHazardRecognizer::tickDestWindows(int Delta) {
  // Expire as time moves away from the emit cycle in either direction.
  // Growing remaining on RecedeCycle (bottom-up) made every overlapping
  // candidate a permanent Hazard and the scheduler receded forever
  // (pre-RA OnlyBottomUp / post-RA Bot — IIR/MAC/FIR/CoreMark SIGKILL).
  const unsigned Step =
      Delta >= 0 ? static_cast<unsigned>(Delta) : static_cast<unsigned>(-Delta);
  if (Step == 0)
    return;
  auto tick = [Step](DenseMap<Register, unsigned> &Pending) {
    for (auto It = Pending.begin(), E = Pending.end(); It != E;) {
      if (It->second <= Step) {
        auto Erase = It++;
        Pending.erase(Erase);
        continue;
      }
      It->second -= Step;
      ++It;
    }
  };
  tick(DestReadPending);
  tick(DestWritePending);
}

bool HaydnHazardRecognizer::hasDestWindowHazard(const MachineInstr &MI,
                                                int DeltaCycles) const {
  if (IsPreRA)
    return false;
  if (DestReadPending.empty() && DestWritePending.empty())
    return false;
  const unsigned Need = DeltaCycles >= 0 ? static_cast<unsigned>(DeltaCycles) : 0;
  auto overlapsPending = [&](const DenseMap<Register, unsigned> &Pending,
                             Register Reg) {
    for (const auto &KV : Pending) {
      if (KV.second <= Need)
        continue;
      if (Reg == KV.first)
        return true;
      if (TRI && Reg.isPhysical() && KV.first.isPhysical() &&
          TRI->regsOverlap(Reg, KV.first))
        return true;
    }
    return false;
  };
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg())
      continue;
    if (MO.isUse() && !MO.isUndef() &&
        overlapsPending(DestReadPending, MO.getReg()))
      return true;
    if (MO.isDef() && overlapsPending(DestWritePending, MO.getReg()))
      return true;
  }
  return false;
}

void HaydnHazardRecognizer::emitForDestWindow(const MachineInstr &MI) {
  if (IsPreRA || isNoHazardMeta(MI))
    return;
  if (isLockedSlotDspOp(MI))
    bookSinCosWindow(MI);
  bookDestReadWindow(MI);
}

void HaydnHazardRecognizer::advanceDestWindows() {
  tickDestWindows(/*Delta=*/1);
}

unsigned HaydnHazardRecognizer::destWindowStallNeed(const MachineInstr &MI) const {
  if (IsPreRA)
    return 0;
  if (DestReadPending.empty() && DestWritePending.empty())
    return 0;
  unsigned Worst = 0;
  auto remaining = [&](const DenseMap<Register, unsigned> &Pending,
                       Register Reg) -> unsigned {
    unsigned R = 0;
    for (const auto &KV : Pending) {
      if (KV.second == 0)
        continue;
      if (Reg == KV.first)
        R = std::max(R, KV.second);
      else if (TRI && Reg.isPhysical() && KV.first.isPhysical() &&
               TRI->regsOverlap(Reg, KV.first))
        R = std::max(R, KV.second);
    }
    return R;
  };
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg())
      continue;
    if (MO.isUse() && !MO.isUndef())
      Worst = std::max(Worst, remaining(DestReadPending, MO.getReg()));
    if (MO.isDef())
      Worst = std::max(Worst, remaining(DestWritePending, MO.getReg()));
  }
  return Worst;
}

unsigned HaydnHazardRecognizer::destWindowExitLeak() const {
  unsigned Leak = 0;
  for (const auto &KV : DestReadPending)
    Leak = std::max(Leak, KV.second);
  for (const auto &KV : DestWritePending)
    Leak = std::max(Leak, KV.second);
  return Leak;
}

void HaydnHazardRecognizer::commitPlacementForEmit(MachineInstr *MI) {
  // Port of AIEResourceCycle::reserveResources alt try
  // (AIEHazardRecognizer.cpp:197-214): getAlternateInstsOpcode + Bundle.add.
 // Haydn maps that to exactTryAddProduct on the nondominated candidate
  // set (no first-fit freeze). Post-RA stamps MemberOpcode via
  // setAlternateDescriptor (AIE AIEHazardRecognizer.cpp:389) so leaveRegion
 // materializeMultiOpcodeInstrs can MI.setDesc. Pre-RA still tracks
  // occupancy via tryAdd but MUST NOT stamp AltDescs / member opcodes —
  // phase identity: logical only through RA.
  // Placement after materialize is getSlotKind on the member Desc
  // (AIEBaseMCFormats.cpp:66-75). Alts-only — no getLegalSlots fallback.
  //
 // Always track MI in CurrentCyclePlacedMIs so reenterCurrentCycle
  // scoreboard booking covers no-alt ops too (standalone / fixed-slot).
  const unsigned Opc = MI->getOpcode();
  if (!hasPlacementAlternatives(Fmts, Opc)) {
    CurrentCyclePlacedMIs.push_back(MI);
    return;
  }
  if (!exactTryAddProduct(CurrentCycleCandidates, Fmts, Opc)) {
    // AIE ResourceCycle::reserveResources llvm_unreachable on alt miss.
    // Silent return left MI issued with zero occupancy; later same-cycle
    // ops saw a phantom-free cycle. getHazardType must reject first.
    report_fatal_error(
        "HaydnHazardRecognizer: commitPlacementForEmit exactTryAddProduct "
        "failed (getHazardType must reject; no unbooked issue)",
        /*GenCrashDiag=*/false);
  }
  CurrentCyclePlacedMIs.push_back(MI);
  // Post-RA only: re-stamp every *alts-bearing* placed MI from the preferred
  // surviving matching. Pref.Members tracks only exactTryAdd members — not
  // no-alt ops — so walk alts-bearing placed MIs in issue order.
  if (IsPreRA || !AltDescs || !TII)
    return;
  const CycleState &Pref = selectPreferredCandidate(CurrentCycleCandidates);
  unsigned MemberIdx = 0;
  for (MachineInstr *Placed : CurrentCyclePlacedMIs) {
    if (!hasPlacementAlternatives(Fmts, Placed->getOpcode()))
      continue;
    assert(MemberIdx < Pref.Members.size());
    // AIE AIEHazardRecognizer.cpp:389 — record selected member opcode for
    // leaveRegion setDesc materialize (AIEAlternateDescriptors.h:39-44).
    AltDescs->setAlternateDescriptor(Placed, Pref.Members[MemberIdx].MemberOpcode,
                                     *TII);
    ++MemberIdx;
  }
  assert(MemberIdx == Pref.Members.size());
}

ScheduleHazardRecognizer::HazardType
HaydnHazardRecognizer::getHazardType(SUnit *SU, int DeltaCycles) {
  if (!SU)
    return NoHazard;
  MachineInstr *MI = SU->getInstr();
  if (!MI)
    return NoHazard;

 // AIE: only true no-hazard meta is exempt (not blanket isPseudo).
  if (isNoHazardMeta(*MI))
    return NoHazard;

 // : stage-relative checkConflict (AIE anyStage) — ports at DeltaCycles,
  // each itinerary stage at DeltaCycles+StageCycle. Selected AltDesc member
  // schedclass when stamped; else logical.
  if (checkConflict(Scoreboard, *MI, DeltaCycles)) {
    LLVM_DEBUG({
      dbgs() << "Hazard for ";
      MI->print(dbgs());
      dbgs() << " at delta " << DeltaCycles << " (cycle has ";
      Scoreboard[DeltaCycles].dump();
      dbgs() << ", candidate needs ";
      buildCandidate(*MI).dump();
      dbgs() << ")\n";
    });
    return Hazard;
  }

  // same-bundle destination-register WAW (see CurrentCycleDefs doc).
  // Only meaningful at DeltaCycles == 0 (CurrentCycleDefs records instrs already
  // issued THIS cycle). Returning Hazard defers the candidate so its consumers
  // are placed with correct latencies. Cache TRI up front for hasSameBundleWAW.
  (void)getTRI(*MI);
  if (DeltaCycles == 0 && hasSameBundleWAW(*MI)) {
    LLVM_DEBUG({
      dbgs() << "WAW hazard for ";
      MI->print(dbgs());
      dbgs() << " (cycle already writes an overlapping def)\n";
    });
    return Hazard;
  }

  // same-bundle RAW (see hasSameBundleRAW doc). The dual of the WAW
  // gate: Haydn has no intra-bundle forwarding (all reads happen in ID before
  // any write commits), so a reader cannot share a cycle with its writer — it
  // would observe the OLD value. This is the true root cause of (the
  // strcmp-loop miscompile was a broken PROLOGUE constant-init store bundle
  // not the loop). Same family as; prior revision only patched the
  // materializeBundles splice path — this gates the scheduler's cycle
  // assignment directly.
  if (DeltaCycles == 0 && hasSameBundleRAW(*MI)) {
    LLVM_DEBUG({
      dbgs() << "RAW hazard for ";
      MI->print(dbgs());
      dbgs() << " (cycle already writes a source this instr reads; Haydn has "
                "no intra-bundle forwarding)\n";
    });
    return Hazard;
  }

  // Named SF1 same-cycle laws (alone / CSRW↔SET / e0-alone). Occupied
  // set is CurrentCyclePlacedMIs — same predicate ResourceCycle flags
  // and fitInInterval conjunct. Flags below stay as emit bookkeeping.
  if (DeltaCycles == 0 && !CurrentCyclePlacedMIs.empty()) {
    SmallVector<const MachineInstr *, 4> Occ(CurrentCyclePlacedMIs.begin(),
                                             CurrentCyclePlacedMIs.end());
    if (cycleViolatesNamedSameCycleLaws(*MI, Occ)) {
      LLVM_DEBUG({
        dbgs() << "Named same-cycle hazard for ";
        MI->print(dbgs());
        dbgs() << " (" << namedSameCycleLawsTag() << ")\n";
      });
      return Hazard;
    }
  }

  // Shared RF port-budget predicate (W24 / commit+verify). Incremental
  // scoreboard ports stay the fast AIE path; this is the same
  // haydnCycleMembersExceedPortBudget commit consults so HR cannot accept
  // a pack leaveMBB would sequentialize.
  if (DeltaCycles == 0 && !CurrentCyclePlacedMIs.empty()) {
    SmallVector<MachineInstr *, 4> Cycle(CurrentCyclePlacedMIs.begin(),
                                         CurrentCyclePlacedMIs.end());
    Cycle.push_back(MI);
    if (haydnCycleMembersExceedPortBudget(Cycle)) {
      LLVM_DEBUG({
        dbgs() << "Shared port-budget hazard for ";
        MI->print(dbgs());
        dbgs() << "\n";
      });
      return Hazard;
    }
  }

  // Dest-read / SIN_COS dest-writer windows (no interlock). DeltaCycles
  // is the candidate's offset from now: remaining > DeltaCycles is a hit.
  if (hasDestWindowHazard(*MI, DeltaCycles)) {
    LLVM_DEBUG({
      dbgs() << "Dest-window hazard for ";
      MI->print(dbgs());
      dbgs() << " at delta " << DeltaCycles << "\n";
    });
    return Hazard;
  }

  // ARCTAN/SIN_COS: issue alone this cycle; multi-cycle unit occupancy is
  // booked as Reserved on emit (checkConflict Req↔Res).
  if (DeltaCycles == 0) {
    bool IsLocked = isLockedSlotDspOp(*MI);
    if (IsLocked && Scoreboard[DeltaCycles].getIssueCount() > 0) {
      LLVM_DEBUG({
        dbgs() << "Locked-slot hazard for ";
        MI->print(dbgs());
        dbgs() << " (ARCTAN/SIN_COS must issue alone)\n";
      });
      return Hazard;
    }
    if (!IsLocked && CurrentCycleHasLockedSlotOp) {
      LLVM_DEBUG({
        dbgs() << "Locked-slot hazard for ";
        MI->print(dbgs());
        dbgs() << " (cycle already issued an ARCTAN/SIN_COS)\n";
      });
      return Hazard;
    }
  }

  // Placement authority (current cycle only): product CycleCandidateSet
  // exactTryAddProduct — AIEHazardRecognizer.cpp:174-214 alt try /
 // AIEBundle.h:62-105 canAdd, exact nondominated expand.
  // Alts-only (no getLegalSlots fallback). Ops without PlacementAlternatives
  // skip the gate (standalone / no-slot).
  if (DeltaCycles == 0) {
    const unsigned Opc = MI->getOpcode();
    if (hasPlacementAlternatives(Fmts, Opc) &&
        !canExactTryAddProduct(CurrentCycleCandidates, Fmts, Opc)) {
      LLVM_DEBUG({
        dbgs() << "tryAdd hazard for ";
        MI->print(dbgs());
        dbgs() << " (no free PlacementAlternative field this cycle; occ="
               << currentCyclePreferred().OccupiedSlots << ")\n";
      });
      return Hazard;
    }
  }

  // Post-RA emission probe (AIE applyBundles size()>1 peer at
  // AIEHazardRecognizer.cpp:326-352). Only consult the batch bake for
  // already-assigned members: incremental exactTryAdd owns logicals with
  // PlacementAlternatives. A batch bake of still-logical ALUs can assign
  // two independent ops to the same unit and reject a pack leaveMBB would
  // accept. COPY is not a cycle member (PostRA skippable).
  if (!IsPreRA && DeltaCycles == 0 && !CurrentCyclePlacedMIs.empty() &&
      !MI->isCopy()) {
    bool AnyLogicalAlt = hasPlacementAlternatives(Fmts, MI->getOpcode());
    SmallVector<MachineInstr *, 4> Cycle;
    Cycle.reserve(CurrentCyclePlacedMIs.size() + 1);
    for (MachineInstr *P : CurrentCyclePlacedMIs) {
      if (!P || P->isCopy())
        continue;
      if (hasPlacementAlternatives(Fmts, P->getOpcode()))
        AnyLogicalAlt = true;
      Cycle.push_back(P);
    }
    Cycle.push_back(MI);
    if (!AnyLogicalAlt && Cycle.size() >= 2 &&
        !canCoissueProductCycle(Cycle)) {
      LLVM_DEBUG({
        dbgs() << "Product coissue hazard for ";
        MI->print(dbgs());
        dbgs() << " (baked-member emission probe rejects this cycle;\n"
                  " sequentialize is recovery only)\n";
      });
      return Hazard;
    }
  }

  return NoHazard;
}

void HaydnHazardRecognizer::emitInstruction(SUnit *SU, int DeltaCycles) {
  if (!SU)
    return;
  MachineInstr *MI = SU->getInstr();
 // : MultiSlot_Pseudo emits into issue/stages/ports; meta does not.
  if (!MI || isNoHazardMeta(*MI))
    return;

  // record this instr's defs so a later same-cycle candidate is checked
  // for a destination-register WAW overlap.
  if (DeltaCycles == 0) {
    appendDefs(*MI);
 // Commit field via exact product expand BEFORE scoreboard booking
 // so stages use the selected AltDesc member schedclass.
    // setDesc happens in leaveRegion materializeMultiOpcodeInstrs.
    commitPlacementForEmit(MI);
    // Rematch may reassign earlier members' slots: restore cycle-start
    // residuals and re-enter every placed MI with selected member FUs.
    reenterCurrentCycleScoreboard();
    // ARCTAN/SIN_COS: alone this cycle; book uimm4+2 unit occupancy + dest lock.
    if (isLockedSlotDspOp(*MI)) {
      CurrentCycleHasLockedSlotOp = true;
      bookSinCosWindow(*MI);
    }
    bookDestReadWindow(*MI);
    // track CSRW↔SET_HWLOOP same-bundle hazard (spec §5.10).
    if (static_cast<const HaydnInstrInfo *>(TII)->isHardwareLoopSetupOpcode(
            MI->getOpcode()))
      CurrentCycleHasHwloopSetup = true;
    if (getHwloopCsrAddr(*MI) >= 0)
      CurrentCycleHasHwloopCsrw = true;
    if (isAbsMaterializeOp(MI->getOpcode()))
      CurrentCycleHasAbsMaterialize = true;
    else
      CurrentCycleHasNonAbsReal = true;
  } else {
    // Non-zero delta: no current-cycle rematch; book once at relative cycle.
    enterResources(Scoreboard, *MI, DeltaCycles);
  }
  LLVM_DEBUG({
    dbgs() << "Emit ";
    MI->print(dbgs());
    dbgs() << " at delta " << DeltaCycles << " -> cycle now ";
    Scoreboard[DeltaCycles].dump();
    dbgs() << "\n";
  });
}

void HaydnHazardRecognizer::EmitInstruction(SUnit *SU) {
  // The MachineScheduler calls this overload with DeltaCycles implicit (the
  // framework tracks the SUnit's emit cycle and has already consulted
  // getHazardType at the chosen delta). We record at DeltaCycles=0 (current
  // cycle); if a future scheduler variant passes an explicit delta, use
  // emitInstruction(SU, Delta) instead.
  emitInstruction(SU, /*DeltaCycles=*/0);
}

void HaydnHazardRecognizer::EmitInstruction(MachineInstr *MI) {
  // Non-scheduling-pass overload: same rematch-then-reenter path as
 // emitInstruction(SU, 0). : isNoHazardMeta only (not isPseudo).
  if (!MI || isNoHazardMeta(*MI))
    return;
  appendDefs(*MI);
  commitPlacementForEmit(MI);
  reenterCurrentCycleScoreboard();
  if (isLockedSlotDspOp(*MI)) {
    CurrentCycleHasLockedSlotOp = true;
    bookSinCosWindow(*MI);
  }
  bookDestReadWindow(*MI);
  if (static_cast<const HaydnInstrInfo *>(TII)->isHardwareLoopSetupOpcode(
          MI->getOpcode()))
    CurrentCycleHasHwloopSetup = true;
  if (getHwloopCsrAddr(*MI) >= 0)
    CurrentCycleHasHwloopCsrw = true;
  if (isAbsMaterializeOp(MI->getOpcode()))
    CurrentCycleHasAbsMaterialize = true;
  else
    CurrentCycleHasNonAbsReal = true;
}

void HaydnHazardRecognizer::AdvanceCycle() {
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleHasAbsMaterialize = false;
  CurrentCycleHasNonAbsReal = false;
  CurrentCycleCandidates = makeProductCandidateSet();
  CurrentCyclePlacedMIs.clear();
  tickDestWindows(/*Delta=*/1);
  Scoreboard.advance();
  captureCycleStartScoreboard();
}

void HaydnHazardRecognizer::RecedeCycle() {
  CurrentCycleDefs.clear();
  CurrentCycleLiveDefs.clear();
  CurrentCycleHasLockedSlotOp = false;
  CurrentCycleHasHwloopSetup = false;
  CurrentCycleHasHwloopCsrw = false;
  CurrentCycleHasAbsMaterialize = false;
  CurrentCycleHasNonAbsReal = false;
  CurrentCycleCandidates = makeProductCandidateSet();
  CurrentCyclePlacedMIs.clear();
  tickDestWindows(/*Delta=*/-1);
  Scoreboard.recede();
  captureCycleStartScoreboard();
}

bool HaydnHazardRecognizer::atIssueLimit() const {
  return Scoreboard[0].getIssueCount() >= IssueLimit;
}
