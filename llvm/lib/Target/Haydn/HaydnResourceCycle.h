//===- HaydnResourceCycle.h - SMS resource model --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn SMS ResourceCycle: live CycleCandidateSet + exactTryAddProduct, same
// depth as post-RA HR CurrentCycleCandidates. AIE peer AIEResourceCycle
// (AIEHazardRecognizer.h:315-328, AIEHazardRecognizer.cpp:173-214). Design
// authority: topics/scheduling/TOPIC.md (matching frontier, ResMII oracles,
// SMS-HOOK/PORT, pre-RA StageCount>1 containment). Do not duplicate that
// topic here. This header stays implementation-bearing (move residual).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H

#include "HaydnBundle.h" // MachineBundle::isNoHazardMetaInstruction
#include "HaydnBundleFormatSolver.h"
#include "HaydnIntraCycleRAW.h" // shared no-forwarding RAW law (hard #7)
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h" // PortModel → MCTargetDesc (opcode + regclass enums)
#include "HaydnResourceRestrictionClasses.h" // II-wrap SMS-HOOK polarity
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h" // SmallSetVector for CurrentCycleLiveDefs
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ResourceCycle.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h" // getSubtarget().getRegisterInfo()
#include <algorithm>
#include <cstdint>

namespace llvm {

/// Accumulated same-issue-cycle pooled port demand for one SMS ResourceCycle.
struct HaydnCyclePortDemand {
  unsigned GPRReads = 0;
  unsigned GPRWrites = 0;
  unsigned DRReads = 0;
  unsigned DRWrites = 0;
  unsigned ARReads = 0;
  unsigned ARWrites = 0;

  HaydnCyclePortDemand &operator+=(const HaydnCyclePortDemand &O) {
    GPRReads += O.GPRReads;
    GPRWrites += O.GPRWrites;
    DRReads += O.DRReads;
    DRWrites += O.DRWrites;
    ARReads += O.ARReads;
    ARWrites += O.ARWrites;
    return *this;
  }

  bool fitsBudget() const {
    return GPRReads <= HAYDN_GPR_READ_PORTS &&
           GPRWrites <= HAYDN_GPR_WRITE_PORTS &&
           DRReads <= HAYDN_DR_READ_PORTS &&
           DRWrites <= HAYDN_DR_WRITE_PORTS &&
           ARReads <= HAYDN_AR_READ_PORTS &&
           ARWrites <= HAYDN_AR_WRITE_PORTS;
  }

  bool canAdd(const HaydnCyclePortDemand &O) const {
    HaydnCyclePortDemand T = *this;
    T += O;
    return T.fitsBudget();
  }
};

/// Classify a Haydn register-class ID into the GPR / DR / AR port bank.
/// Subclasses of GPR32 (NoSPNoLR, Lo) charge the GPR pool.
inline void haydnClassifyPortBankClassID(int RegClassID, bool IsDef,
                                         HaydnCyclePortDemand &D) {
  if (RegClassID < 0)
    return;
  switch (RegClassID) {
  case Haydn::GPR32RegClassID:
  case Haydn::GPR32NoSPNoLRRegClassID:
  case Haydn::GPR32LoRegClassID:
    if (IsDef)
      ++D.GPRWrites;
    else
      ++D.GPRReads;
    break;
  case Haydn::DR64RegClassID:
    if (IsDef)
      ++D.DRWrites;
    else
      ++D.DRReads;
    break;
  case Haydn::ARRegClassID:
    if (IsDef)
      ++D.ARWrites;
    else
      ++D.ARReads;
    break;
  default:
    break;
  }
}

/// Descriptor-derived port demand (SMS placement path — no MI operands).
/// Counts each explicit reg operand independently (no same-reg dedup across
/// operand slots). Tied use/def appear as separate ops and correctly charge
/// one read and one write. Unknown / non-reg operands are ignored.
inline HaydnCyclePortDemand
estimateHaydnPortsFromDesc(const MCInstrDesc &MID) {
  HaydnCyclePortDemand D;
  const unsigned NumDefs = MID.getNumDefs();
  for (unsigned I = 0, E = MID.getNumOperands(); I != E; ++I) {
    const MCOperandInfo &OI = MID.operands()[I];
    if (OI.OperandType != MCOI::OPERAND_REGISTER)
      continue;
    // Optional defs beyond NumDefs still write.
    const bool IsDef =
        I < NumDefs || (MID.hasOptionalDef() && OI.isOptionalDef());
    haydnClassifyPortBankClassID(OI.RegClass, IsDef, D);
  }
  return D;
}

/// Exact MI port demand via shared PortModel (MRI-correct for vregs).
inline HaydnCyclePortDemand countHaydnPortsFromMI(const MachineInstr &MI) {
  HaydnCyclePortDemand D;
  auto [GR, GW] = countGPRPorts(MI);
  auto [DR, DW] = countDRPorts(MI);
  auto [AR, AW] = countARPorts(MI);
  D.GPRReads = GR;
  D.GPRWrites = GW;
  D.DRReads = DR;
  D.DRWrites = DW;
  D.ARReads = AR;
  D.ARWrites = AW;
  return D;
}

//===----------------------------------------------------------------------===//
// MOVE32-class MI-versus-descriptor port / placement shapes (SMS ownership)
//===----------------------------------------------------------------------===//
// MOVE32 is tablegen'd as (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2) so the
// MCInstrDesc always exposes one def + two use slots. copyPhysReg emits
// `MOVE32 rd, rs, rs`. PortModel MI accounting (count*Ports / ResMII DFA)
// dedupes same-reg sources → 1R1W. Descriptor-only placement
// (estimateHaydnPortsFromDesc / ResourceCycle MID overload) has no operand
// identity → always 2R1W and conservatively overcounts.
//
// This is intentional: placement never under-reserves relative to the MI
// path. It is not an operand-dependent format predicate and does not
// fail-close SMS-HOOK. Pre-RA list-sched uses only the MI PortModel path
// (sibling surface). Constants live here so SMS packing tests do not depend
// on PreRASchedStrategy ownership.

/// MI PortModel demand for MOVE32 rd, rs, rs after same-reg read dedup.
inline constexpr unsigned HaydnMove32ClassMiRepeatedSrcGprReads = 1;
inline constexpr unsigned HaydnMove32ClassMiRepeatedSrcGprWrites = 1;
/// Descriptor-only shape (1 def + 2 use slots) with no same-reg identity.
inline constexpr unsigned HaydnMove32ClassDescShapeGprReads = 2;
inline constexpr unsigned HaydnMove32ClassDescShapeGprWrites = 1;

/// True when descriptor-shape reads strictly overcount the MI repeated-src
/// form (canonical MOVE32-class differential).
inline constexpr bool haydnMove32ClassDescOvercountsMiPorts() {
  return HaydnMove32ClassDescShapeGprReads >
             HaydnMove32ClassMiRepeatedSrcGprReads &&
         HaydnMove32ClassDescShapeGprWrites ==
             HaydnMove32ClassMiRepeatedSrcGprWrites;
}

/// Port demand for one MOVE32-class op under the MI repeated-source model.
inline HaydnCyclePortDemand haydnMove32ClassMiRepeatedSrcDemand() {
  HaydnCyclePortDemand D;
  D.GPRReads = HaydnMove32ClassMiRepeatedSrcGprReads;
  D.GPRWrites = HaydnMove32ClassMiRepeatedSrcGprWrites;
  return D;
}

/// Port demand for one MOVE32-class op under the descriptor-shape model
/// (SMS MID placement path).
inline HaydnCyclePortDemand haydnMove32ClassDescShapeDemand() {
  HaydnCyclePortDemand D;
  D.GPRReads = HaydnMove32ClassDescShapeGprReads;
  D.GPRWrites = HaydnMove32ClassDescShapeGprWrites;
  return D;
}

/// True when N MI-shape MOVE32 fit the GPR read pool while N descriptor-shape
/// MOVE32 do not (N=3 → MI 3R OK, desc 6R over under 4R).
inline bool haydnMove32ClassDescSaturatesReadPoolEarlier(unsigned N) {
  const unsigned MiR = N * HaydnMove32ClassMiRepeatedSrcGprReads;
  const unsigned DescR = N * HaydnMove32ClassDescShapeGprReads;
  return MiR <= HAYDN_GPR_READ_PORTS && DescR > HAYDN_GPR_READ_PORTS;
}

//===----------------------------------------------------------------------===
// Same-phase WAW (FE5B simultaneous same-bank / same-reg defs)
//===----------------------------------------------------------------------===
// Peer of HaydnHazardRecognizer::hasSameBundleWAW / appendDefs. ResourceCycle
// already enforces no-forwarding RAW via CurrentCycleLiveDefs; WAW is the dual
// for two writers of the same register (or physreg alias) in one modulo phase.
// Dead defs still WAW-collide (spec forbids dual write regardless of liveness).
// SFR is included: product law is one SFR writer per cycle (dead flag
// side-effects count). Used by the MI reserve path and by the pure
// periodic-certificate same-reg DefRegKey pin.

template <typename DefSet>
bool haydnHasIntraCycleWAW(const MachineInstr &MI, const DefSet &Defs,
                           const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (Reg.isVirtual()) {
      if (Defs.contains(Reg))
        return true;
      continue;
    }
    if (!Reg.isPhysical() || !TRI)
      continue;
    for (Register D : Defs)
      if (D.isPhysical() && TRI->regsOverlap(Reg, D))
        return true;
  }
  return false;
}

template <typename DefSet>
void haydnAppendCycleDefs(const MachineInstr &MI, DefSet &Defs) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (!Reg.isPhysical() && !Reg.isVirtual())
      continue;
    Defs.insert(Reg);
  }
}

//===----------------------------------------------------------------------===
// FE5B whole-kernel periodic certificate lifecycle (WP4)
//===----------------------------------------------------------------------===
// Half-enabled multi-stage is forbidden. A product rewrite may discard the
// original loop only after Accepted; Rejected always requires recoverable
// rollback when rewrite has already mutated the CFG. Live MachinePipeliner
// multi-stage naive remains rejected until WP1–WP4 product evidence lands
// (WP5 policy flip is out of scope for this surface).

enum class SMSCertLifecycle : uint8_t {
  None = 0,              ///< No certificate in flight.
  OriginalRetained = 1,  ///< Original loop present; cert not final.
  PreRewriteProved = 2,  ///< MI-aware periodic proof OK before rewrite.
  PostRewriteValid = 3,  ///< Recoverable validation after rewrite OK.
  Accepted = 4,          ///< Final accept — original may be discarded.
  Rejected = 5,          ///< Fail-closed; rollback if rewritten.
};

/// One kernel placement for the pure periodic certificate oracle.
/// NormalizedPhase is AbsCycle % II. StageCycles models itinerary width
/// (product == 1). DefBank: 0=none, 1=GPR, 2=DR, 3=AR. DefRegKey is an opaque
/// same-reg identity for WAW within a phase (0 = no def tracked).
struct SMSCertPhaseOp {
  unsigned NormalizedPhase = 0;
  unsigned Opcode = 0;
  unsigned StageCycles = 1;
  uint8_t DefBank = 0;
  unsigned DefRegKey = 0;
};

/// Transactional certificate state + pure lifecycle transitions.
struct SMSPeriodicCertificate {
  unsigned II = 0;
  unsigned KernelID = 0;
  SMSCertLifecycle Status = SMSCertLifecycle::None;

  static constexpr SMSCertLifecycle begin() {
    return SMSCertLifecycle::OriginalRetained;
  }

  static constexpr bool originalLoopMustRemain(SMSCertLifecycle S) {
    return S == SMSCertLifecycle::OriginalRetained ||
           S == SMSCertLifecycle::PreRewriteProved ||
           S == SMSCertLifecycle::PostRewriteValid;
  }

  static constexpr bool mayDiscardOriginalLoop(SMSCertLifecycle S) {
    return S == SMSCertLifecycle::Accepted;
  }

  static constexpr bool mustRollback(SMSCertLifecycle S) {
    return S == SMSCertLifecycle::Rejected;
  }

  static constexpr bool isTerminal(SMSCertLifecycle S) {
    return S == SMSCertLifecycle::Accepted || S == SMSCertLifecycle::Rejected;
  }

  static constexpr SMSCertLifecycle onPreRewriteProof(SMSCertLifecycle Cur,
                                                      bool Ok) {
    if (Cur != SMSCertLifecycle::OriginalRetained)
      return SMSCertLifecycle::Rejected;
    return Ok ? SMSCertLifecycle::PreRewriteProved
              : SMSCertLifecycle::Rejected;
  }

  static constexpr SMSCertLifecycle
  onPostRewriteValidation(SMSCertLifecycle Cur, bool Ok) {
    if (Cur != SMSCertLifecycle::PreRewriteProved)
      return SMSCertLifecycle::Rejected;
    return Ok ? SMSCertLifecycle::PostRewriteValid
              : SMSCertLifecycle::Rejected;
  }

  static constexpr SMSCertLifecycle onFinalAccept(SMSCertLifecycle Cur) {
    if (Cur != SMSCertLifecycle::PostRewriteValid)
      return SMSCertLifecycle::Rejected;
    return SMSCertLifecycle::Accepted;
  }

  static constexpr SMSCertLifecycle fail() {
    return SMSCertLifecycle::Rejected;
  }
};

/// Two-iteration LCD / RecMII math for the post-RA multi-stage host.
struct SMSTwoCopyEdge { int Src = 0; int Dst = 0; int Latency = 1; bool LoopCarried = false; };
struct SMSLCDEdge { int Def = 0; int Use = 0; int Latency = 1; int Distance = 1; };
struct SMSLCDTwoIteration {
  static int recMIIFromEdges(ArrayRef<SMSLCDEdge> Edges) {
    int Best = 0;
    for (const SMSLCDEdge &E : Edges) {
      int Dist = std::max(1, E.Distance), Lat = std::max(0, E.Latency);
      Best = std::max(Best, (Lat + Dist - 1) / Dist);
    }
    return Best;
  }
  static int recMIIFromTwoCopy(int N, ArrayRef<SMSTwoCopyEdge> Edges) {
    if (N <= 0)
      return 0;
    const int Unset = -1000000;
    int Best = 0;
    for (int Src = 0; Src < N; ++Src) {
      SmallVector<int, 32> Dist(static_cast<size_t>(2 * N), Unset);
      Dist[Src] = 0;
      bool Changed = true;
      int Guard = 2 * N + 4;
      while (Changed && Guard--) {
        Changed = false;
        for (const SMSTwoCopyEdge &E : Edges) {
          if (E.Src < 0 || E.Dst < 0 || E.Src >= N || E.Dst >= N)
            continue;
          const int Lat = std::max(0, E.Latency);
          if (E.LoopCarried) {
            if (Dist[E.Src] > Unset && Dist[E.Src] + Lat > Dist[E.Dst + N]) {
              Dist[E.Dst + N] = Dist[E.Src] + Lat;
              Changed = true;
            }
          } else {
            if (Dist[E.Src] > Unset && Dist[E.Src] + Lat > Dist[E.Dst]) {
              Dist[E.Dst] = Dist[E.Src] + Lat;
              Changed = true;
            }
            if (Dist[E.Src + N] > Unset &&
                Dist[E.Src + N] + Lat > Dist[E.Dst + N]) {
              Dist[E.Dst + N] = Dist[E.Src + N] + Lat;
              Changed = true;
            }
          }
        }
      }
      if (Guard < 0)
        return std::max(Best, N);
      if (Dist[Src + N] > 0)
        Best = std::max(Best, Dist[Src + N]);
    }
    return Best;
  }
  static bool scheduleHolds(int II, ArrayRef<int> Cycle, ArrayRef<SMSLCDEdge> Edges) {
    if (II < 1) return false;
    for (const SMSLCDEdge &E : Edges) {
      if (E.Def < 0 || E.Use < 0 || size_t(E.Def) >= Cycle.size() || size_t(E.Use) >= Cycle.size())
        return false;
      int Dist = std::max(1, E.Distance);
      if (Cycle[E.Use] + Dist * II < Cycle[E.Def] + std::max(0, E.Latency))
        return false;
    }
    return true;
  }
};


/// ARCTAN / SIN_COS issue alone in their cycle (PackLegality rule 4 / HR peer).
/// Thin wrapper: the opcode list lives only in PortModel
/// haydnOpcodeIssuesAloneInCycle. Descriptor-derived (opcode only) —
/// same-cycle class-1 capacity, not class-3 (draft multi-cycle lock is not
/// product-enabled; see restriction catalog).
inline bool isHaydnSMSAloneOpcode(unsigned Opcode) {
  return haydnOpcodeIssuesAloneInCycle(Opcode);
}

class HaydnResourceCycle : public ResourceCycle {
public:
  /// Shared admission gate with PortModel: competitive per-op resource
  /// claims stay closed until golden publishes the complete table into
  /// generated records. Aggregate pooled ceilings remain enforceable now.
  /// One authority with pre-RA / ordinary post-RA — no local callback mirror.
  static constexpr bool hasAdmittedPerOpRecords() {
    return haydnHasAdmittedPerOpResourceRecords();
  }

  static constexpr unsigned completeModelPin() {
    return haydnSchedCompleteModelPin();
  }

  static const HaydnAdmittedPerOpResourceRecord *
  lookupAdmittedPerOpRecord(unsigned Opcode) {
    return haydnLookupAdmittedPerOpResourceRecord(Opcode);
  }

  static bool competitivePerOpClaimsAllowed(unsigned Opcode) {
    return haydnCompetitivePerOpResourceClaimsAllowed(Opcode);
  }

  /// Availability-aware record from the current golden (PortModel authority).
  static HaydnAvailabilityAwareResourceRecord
  availabilityAwareRecord(unsigned Opcode) {
    return haydnMakeAvailabilityAwareResourceRecord(Opcode);
  }

  static HaydnGoldenAggregateResourceSurface aggregateResourceSurface() {
    return haydnCurrentGoldenAggregateResourceSurface();
  }

private:
  HaydnMCFormats Fmts;
  /// Live nondominated packing states — peer of
  /// HaydnHazardRecognizer::CurrentCycleCandidates (matching frontier).
  haydn::bundle::CycleCandidateSet Candidates;
  /// Accumulated pooled port demand for this modulo issue cycle.
  HaydnCyclePortDemand Ports;
  /// True once an ARCTAN/SIN_COS has been reserved in this cycle.
  bool HasAloneOp = false;

  /// LIVE destination registers written in this modulo issue cycle (peer of
  /// HaydnHazardRecognizer::CurrentCycleLiveDefs). SMS placement now calls the
  /// MI overload (D999 — operand-aware), so the no-forwarding intra-bundle RAW
  /// law is enforceable here, identical to post-RA HR. Each DFAResources[phase]
  /// object accumulates the live defs of every MI that will co-issue in that
  /// modulo phase's runtime bundle; a consumer reading a live def already in
  /// this set may NOT join the same cycle (no intra-bundle forwarding — Haydn
  /// spec §Constraints) and is rejected, slipping to a later cycle. Uses
  /// `Register` (not MCRegister) so pre-RA virtual defs are tracked by identity.
  SmallSetVector<Register, 8> CurrentCycleLiveDefs;
  /// ALL destination registers written this modulo phase (peer of
  /// HaydnHazardRecognizer::CurrentCycleDefs). Same-phase WAW fail-closes even
  /// for dead defs — FE5B simultaneous same-reg / same-bank interference.
  SmallSetVector<Register, 8> CurrentCycleDefs;
  /// Lazily cached register info (the adapter has no MachineFunction at
  /// construction; resolved from the first MI seen — same pattern as HR).
  const TargetRegisterInfo *TRI = nullptr;

  // AIE Bundle twin (IMPLICIT_DEF/KILL) + BUNDLE root. Never treat
  // MultiSlot_Pseudo (isPseudo=1) as no-hazard — it must tryAdd/book slots.
  static bool isNoHazardMetaOpcode(unsigned Opcode) {
    if (Opcode == TargetOpcode::BUNDLE)
      return true;
    return Haydn::MachineBundle::isNoHazardMetaInstruction(Opcode);
  }

  const haydn::bundle::CycleState &preferred() const {
    return haydn::bundle::selectPreferredCandidate(Candidates);
  }

  /// True when no member has been reserved yet (ports + alone clear; every
  /// surviving matching is still the empty seed). Preferred alone is not
  /// enough — a mask/first-fit view can look empty while a sibling matching
  /// already holds a member.
  bool isPackingEmpty() const {
    if (HasAloneOp)
      return false;
    if (Ports.GPRReads || Ports.GPRWrites || Ports.DRReads || Ports.DRWrites ||
        Ports.ARReads || Ports.ARWrites)
      return false;
    for (const haydn::bundle::CycleState &S : Candidates) {
      if (!S.empty() || S.OccupiedSlots != 0)
        return false;
    }
    return true;
  }

  /// Lazily cache and return the TargetRegisterInfo (the adapter has no
  /// MachineFunction at construction; resolved from the first MI seen). Mirrors
  /// HaydnHazardRecognizer::getTRI. May return null before the first MI and
  /// between clearResources resets; the no-forwarding RAW predicate treats a
  /// null TRI as "physreg alias checks disabled" (vreg identity checks still
  /// work), exactly like the HR peer.
  const TargetRegisterInfo *getTRI(const MachineInstr &MI) {
    if (!TRI)
      TRI = MI.getMF()->getSubtarget().getRegisterInfo();
    return TRI;
  }

  bool canReserveFormatAndAlone(unsigned Opcode) const {
    if (isNoHazardMetaOpcode(Opcode))
      return true;
    // Alone ops refuse any non-empty cycle; nothing co-issues after them.
    if (HasAloneOp)
      return false;
    if (isHaydnSMSAloneOpcode(Opcode) && !isPackingEmpty())
      return false;
    if (hasPlacementAlternatives(Fmts, Opcode))
      // Probe expands every survivor × every alt — matching frontier, not
      // preferred-only first-fit.
      return haydn::bundle::canExactTryAddProduct(Candidates, Fmts, Opcode);
    // No-alt opcodes: Bundle empty standalone escape peer (AIEBundle.h:71-73)
    // — accept only on a truly empty cycle; do not consume slots.
    return isPackingEmpty();
  }

  void reserveFormatAndAlone(unsigned Opcode) {
    assert(canReserveFormatAndAlone(Opcode) && "reserve without canReserve");
    if (isNoHazardMetaOpcode(Opcode))
      return;
    if (isHaydnSMSAloneOpcode(Opcode))
      HasAloneOp = true;
    if (hasPlacementAlternatives(Fmts, Opcode)) {
      bool Ok = haydn::bundle::exactTryAddProduct(Candidates, Fmts, Opcode);
      assert(Ok && "canReserve true but exactTryAddProduct failed");
      (void)Ok;
      return;
    }
    assert(isPackingEmpty());
  }

  bool canReserveWithPorts(unsigned Opcode,
                           const HaydnCyclePortDemand &Demand) const {
    if (!Ports.canAdd(Demand))
      return false;
    return canReserveFormatAndAlone(Opcode);
  }

  void reserveWithPorts(unsigned Opcode, const HaydnCyclePortDemand &Demand) {
    assert(canReserveWithPorts(Opcode, Demand) && "reserve without canReserve");
    // Format / alone first, then ports. canReserveFormatAndAlone / isPackingEmpty
    // read Ports: charging ports before reserve makes no-alt empty-escape and
    // alone-on-empty checks fail the post-mutate re-assert (SMS crash class:
    // hwloop-pointer-iv / swpipeline-*-schedule-found).
    reserveFormatAndAlone(Opcode);
    Ports += Demand;
  }

public:
  HaydnResourceCycle()
      : Candidates(haydn::bundle::makeProductCandidateSet(
            Fmts.getPacketFormats())) {}

  void clearResources() override {
    // Reset ProductFormatMask + empty members from generated PacketFormats
 // () and clear port / alone state.
    Candidates =
        haydn::bundle::makeProductCandidateSet(Fmts.getPacketFormats());
    Ports = HaydnCyclePortDemand{};
    HasAloneOp = false;
    // No-forwarding RAW + same-phase WAW bookkeeping: a fresh cycle/bundle has
    // no defs. TRI is lazily re-resolved from the next MI (see getTRI).
    CurrentCycleLiveDefs.clear();
    CurrentCycleDefs.clear();
    TRI = nullptr;
  }

  // head-LLVM's SMS ResourceManager calls the MCInstrDesc overload for
  // placement (MachinePipeliner.cpp canReserveResources(&SU.getInstr->getDesc)).
 // : descriptor-derived ports + format tryAdd + alone-op gate.
  bool canReserveResources(const MCInstrDesc *MID) override {
    assert(MID && "null MCInstrDesc");
    return canReserveWithPorts(MID->getOpcode(),
                               estimateHaydnPortsFromDesc(*MID));
  }
  void reserveResources(const MCInstrDesc *MID) override {
    assert(MID && "null MCInstrDesc");
    reserveWithPorts(MID->getOpcode(), estimateHaydnPortsFromDesc(*MID));
  }

  // MachineInstr overload: used by calculateResMIIDFA packing AND (after D999)
  // by SMS placement, which now passes the MI instead of its descriptor so the
  // operand-aware no-forwarding intra-bundle RAW law can be enforced here.
  // Prefer exact MRI-correct port demand (vreg regclass → bank); format still
  // opcode-keyed. RAW (HaydnIntraCycleRAW, hard #7) and same-phase WAW (FE5B
  // simultaneous same-reg defs — peer of HR hasSameBundleWAW) run BEFORE
  // accepting. Live/all defs accumulate per modulo phase (DFAResources[phase]
  // is one runtime bundle).
  bool canReserveResources(MachineInstr &MI) override {
    const TargetRegisterInfo *LocalTRI = getTRI(MI);
    if (haydnHasIntraCycleWAW(MI, CurrentCycleDefs, LocalTRI))
      return false;
    if (haydnHasIntraCycleRAW(MI, CurrentCycleLiveDefs, LocalTRI))
      return false;
    return canReserveWithPorts(MI.getOpcode(), countHaydnPortsFromMI(MI));
  }
  void reserveResources(MachineInstr &MI) override {
    reserveWithPorts(MI.getOpcode(), countHaydnPortsFromMI(MI));
    // Record defs AFTER a successful commit so subsequent same-cycle
    // producers/consumers see them (dual of the canReserve WAW/RAW checks).
    haydnAppendCycleDefs(MI, CurrentCycleDefs);
    haydnAppendLiveDefs(MI, CurrentCycleLiveDefs);
  }

  // For debug/inspection: preferred occupied slots in the current cycle.
  SlotBits getOccupiedSlots() const { return preferred().OccupiedSlots; }

  // Preferred FormatID mask for SMS ResMII / cycle occ inspection.
  // AIE ResourceCycle is Bundle-backed without an explicit mask; Haydn exposes
  // FeasibleFormatMask so SMS matches post-RA HR (not
  // productFeasibleFormatMask(Occupied) rebuild alone — member Compatible
  // intersections can shrink the live mask under N-format alts).
  // Not the full matching frontier: see getCandidates() / getMatchingFrontierSize().
  uint64_t getFeasibleFormatMask() const {
    return preferred().FeasibleFormatMask;
  }

  /// Union of FeasibleFormatMask over the live nondominated set. Under product
  /// size-1 Full this equals preferred(); multi-row product can diverge.
  uint64_t getMatchingFrontierFormatMask() const {
    uint64_t M = 0;
    for (const haydn::bundle::CycleState &S : Candidates)
      M |= S.FeasibleFormatMask;
    return M;
  }

  /// Preferred CycleState (unit tests / ResMII probes). No setDesc.
  const haydn::bundle::CycleState &getCycleState() const { return preferred(); }

  /// Live nondominated candidate set (exact matching frontier).
  const haydn::bundle::CycleCandidateSet &getCandidates() const {
    return Candidates;
  }

  /// Number of surviving nondominated partial matchings (frontier width).
  unsigned getMatchingFrontierSize() const {
    return static_cast<unsigned>(Candidates.size());
  }

  unsigned getMemberCount() const { return preferred().memberCount(); }

  /// Accumulated port demand (unit tests / ResMII probes).
  const HaydnCyclePortDemand &getPortDemand() const { return Ports; }

  bool hasAloneOp() const { return HasAloneOp; }

  // Opcode-keyed reserve without an MCInstrDesc (unit tests / local probes).
  // Format + alone only — no port pressure (callers without operand shapes).
  // Production SMS uses MID/MI overloads which always charge ports.
  bool canReserveByOpcode(unsigned Opcode) {
    return canReserveFormatAndAlone(Opcode);
  }

  void reserveByOpcode(unsigned Opcode) { reserveFormatAndAlone(Opcode); }

  /// Unit-test helper: format + explicit port demand (no MI/MID required).
  bool canReserveByOpcodeWithPorts(unsigned Opcode,
                                   const HaydnCyclePortDemand &Demand) {
    return canReserveWithPorts(Opcode, Demand);
  }
  void reserveByOpcodeWithPorts(unsigned Opcode,
                                const HaydnCyclePortDemand &Demand) {
    reserveWithPorts(Opcode, Demand);
  }

  //===--------------------------------------------------------------------===//
  // Qualification packability metrics (no hard cycle groups)
  //===--------------------------------------------------------------------===//
  // Pure product oracles (shared BundleFormatSolver depth). SMS analyzeLoop
  // logs these as qualification evidence that accepted kernels remain
  // post-RA packable under exact no-split commit. Never freezes FormatID,
  // never stamps setDesc/member opcodes, never materializes BUNDLE roots.
  // Sibling pre-RA surface (HaydnPreRASchedStrategy) owns list-sched metrics;
  // this adapter owns the SMS ResourceCycle / analyzeLoop side.

  /// True iff \p Opcodes form one legal product cycle under exact matching
  /// (alts + rematch). Empty is vacuously true. Pure; no MIR mutation.
  static bool qualKernelFormsOneExactCycle(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    const HaydnMCFormats &LocalFmts = haydnDefaultMCFormats();
    return haydn::bundle::exactCanFormOneProductCycle(LocalFmts, Opcodes);
  }

  /// Qualification co-issue packability: size fits one issue cycle and exact
  /// matching packs the full multiset (e.g. ADD32+XOR32+OR32, ADD32+2×ADD64
  /// rematch). Metrics only — does not claim a durable handoff group.
  static bool qualKernelCoissuePackable(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    if (Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
      return false;
    return qualKernelFormsOneExactCycle(Opcodes);
  }

  /// Body-level qualification packability under the exhaustive ≤3 format
  /// oracle: no greedy overestimate (when N is inside the exact DP bound),
  /// and a finite product cover exists. For N >
  /// MaxExhaustiveProductResMIIOps the exhaustive oracle falls back to
  /// greedy (same contract as SMS-RESMII): do **not** fail-close on the
  /// inexact oracle — a finite greedy cover remains product-legal. Does
  /// **not** create hard BUNDLE membership — metrics only.
  /// Sibling of productQualKernelExactlyPackable on the pre-RA surface.
  static bool qualKernelExactlyPackable(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    // Finite exhaustive cover is product-legal. Greedy overestimate is a
    // conservative II floor, not un-packable under Option A containment.
    return haydn::bundle::computeExhaustiveProductResMII(Opcodes) >= 1u;
  }

  //===--------------------------------------------------------------------===//
  // Soft-exit QoR — II floors (format × ports); RecMII stays DDG/SMS
  //===--------------------------------------------------------------------===//
  // Metrics-only soft-exit surface for the format-SMS qualification corpus.
  // Sibling of HaydnPreRASchedStrategy::productSoftExitIIFloor (list-sched
  // track). Dual-run freeze of product vs generic residual ranking is owned by
  // sms-format-generic-baseline.ll (§8.4 #11 G2 MAC/acc) and
  // sms-format-ilp-crit-dual-run.ll (G3 ILP/critical residual attribution);
  // these helpers are pure floors. Never freezes FormatID, never stamps
  // setDesc, never invents BUNDLE membership or RecMII numbers (acc→acc
  // recurrence is itinerary/DDG).

  /// Pure port-pressure lower bound on issue cycles (ceil demand / budget).
  /// Three independent GPR writes → ≥2 under HAYDN_GPR_WRITE_PORTS=2.
  static unsigned portLowerBoundResMII(unsigned GPRReads, unsigned GPRWrites,
                                      unsigned DRReads = 0,
                                      unsigned DRWrites = 0,
                                      unsigned ARReads = 0,
                                      unsigned ARWrites = 0) {
    return haydnPortLowerBoundResMII(GPRReads, GPRWrites, DRReads, DRWrites,
                                    ARReads, ARWrites);
  }

  /// Soft-exit II lower bound for a qualification multiset: max of exhaustive
  /// product format ResMII and pure port-pressure ResMII.
  ///
  /// Ports bind when format-only packing still reports 1 (classic 3×1W GPR
  /// write body under HAYDN_GPR_WRITE_PORTS=2). Metrics-only — never freezes
  /// FormatID, never stamps setDesc, never invents BUNDLE or RecMII.
  static unsigned softExitIIFloor(ArrayRef<unsigned> Opcodes,
                                  unsigned GPRReads, unsigned GPRWrites,
                                  unsigned DRReads = 0,
                                  unsigned DRWrites = 0,
                                  unsigned ARReads = 0,
                                  unsigned ARWrites = 0) {
    if (Opcodes.empty())
      return portLowerBoundResMII(GPRReads, GPRWrites, DRReads, DRWrites,
                                 ARReads, ARWrites);
    const unsigned FormatII =
        haydn::bundle::computeExhaustiveProductResMII(Opcodes);
    const unsigned PortII = portLowerBoundResMII(
        GPRReads, GPRWrites, DRReads, DRWrites, ARReads, ARWrites);
    return FormatII > PortII ? FormatII : PortII;
  }

  //===--------------------------------------------------------------------===//
  // Format-acceptance differential — live ResourceCycle ≡ pure exact ≡ HR
  //===--------------------------------------------------------------------===//
  // Plan §8.4 #7 SMS surface. Descriptor-derived format legality is pure
  // exactTryAddProduct — the same depth post-RA HR commitPlacementForEmit and
  // pre-RA scoreMatchingFrontier use. Live canReserveByOpcode/reserveByOpcode
  // must agree with pure exactCanPackProductSequence on every qualification
  // multiset; preferred OccupiedSlots / member LogicalOpcode+FieldSlots must
  // match selectPreferredCandidate after the same sequential reserves.
  // Format is opcode-keyed → MI and descriptor opcode sequences agree.
  // Ports (including MOVE32-class MI-vs-desc) are orthogonal and must not be
  // misread as a format differential. Metrics-only; no setDesc / BUNDLE invent.

  /// Sequential live ResourceCycle format packing: true iff every opcode in
  /// order joins one product cycle under canReserveByOpcode/reserveByOpcode
  /// (format + alone only — no port pressure).
  static bool formatCanPackSequence(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    HaydnResourceCycle RC;
    for (unsigned Opc : Opcodes) {
      if (!RC.canReserveByOpcode(Opc))
        return false;
      RC.reserveByOpcode(Opc);
    }
    return true;
  }

  /// Pure exactTryAddProduct peer of formatCanPackSequence (post-RA HR
  /// CurrentCycleCandidates / commitPlacement depth).
  static bool formatPureExactCanPackSequence(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    const HaydnMCFormats &LocalFmts = haydnDefaultMCFormats();
    return haydn::bundle::exactCanPackProductSequence(LocalFmts, Opcodes);
  }

  /// Set oracle (N ≤ issue width): some permutation packs under exact matching.
  static bool formatPureExactCanPackSet(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    if (Opcodes.size() > Haydn::ISSUE_SLOT_COUNT)
      return false;
    const HaydnMCFormats &LocalFmts = haydnDefaultMCFormats();
    return haydn::bundle::exactCanPackProductSet(LocalFmts, Opcodes);
  }

  /// Live ResourceCycle open-new-cycle greedy count (format-only, no ports).
  /// Mirrors calculateResMIIDFA open-on-canReserve-false for format depth.
  static unsigned formatSequentialCycleCount(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return 0;
    unsigned Cycles = 0;
    HaydnResourceCycle RC;
    bool CycleOpen = false;
    for (unsigned Opc : Opcodes) {
      if (!RC.canReserveByOpcode(Opc)) {
        RC.clearResources();
        ++Cycles;
        CycleOpen = false;
        if (!RC.canReserveByOpcode(Opc))
          return ~0u;
      }
      RC.reserveByOpcode(Opc);
      CycleOpen = true;
    }
    if (CycleOpen)
      ++Cycles;
    return Cycles;
  }

  /// Live RC sequence acceptance ≡ pure exactTryAddProduct (HR peer polarity).
  static bool formatAcceptanceMatchesPureExact(ArrayRef<unsigned> Opcodes) {
    return formatCanPackSequence(Opcodes) ==
           formatPureExactCanPackSequence(Opcodes);
  }

  /// After packing \p Opcodes in one cycle, live preferred CycleState agrees
  /// with pure exactTryAddProduct preferred candidate (member count, slots,
  /// LogicalOpcode + FieldSlots order). Requires both paths accept the seq.
  static bool
  formatPreferredStateAgreesWithPureExact(ArrayRef<unsigned> Opcodes) {
    if (Opcodes.empty())
      return true;
    if (!formatCanPackSequence(Opcodes) ||
        !formatPureExactCanPackSequence(Opcodes))
      return false;
    HaydnResourceCycle RC;
    const HaydnMCFormats &LocalFmts = haydnDefaultMCFormats();
    haydn::bundle::CycleCandidateSet Pure =
        haydn::bundle::makeProductCandidateSet(LocalFmts.getPacketFormats());
    for (unsigned Opc : Opcodes) {
      RC.reserveByOpcode(Opc);
      bool Ok = haydn::bundle::exactTryAddProduct(Pure, LocalFmts, Opc);
      assert(Ok && "pure exact failed after formatPureExactCanPackSequence");
      (void)Ok;
    }
    const haydn::bundle::CycleState &PrefRC = RC.getCycleState();
    const haydn::bundle::CycleState &PrefPure =
        haydn::bundle::selectPreferredCandidate(Pure);
    if (PrefRC.memberCount() != PrefPure.memberCount() ||
        PrefRC.OccupiedSlots != PrefPure.OccupiedSlots ||
        PrefRC.FeasibleFormatMask != PrefPure.FeasibleFormatMask)
      return false;
    for (unsigned I = 0, E = PrefRC.memberCount(); I != E; ++I) {
      if (PrefRC.Members[I].LogicalOpcode != PrefPure.Members[I].LogicalOpcode ||
          PrefRC.Members[I].FieldSlots != PrefPure.Members[I].FieldSlots)
        return false;
    }
    return true;
  }

  /// Format accept/reject polarity for two opcode sequences (MI vs desc
  /// multisets). Format is opcode-keyed: equal opcodes always agree.
  static bool formatAcceptanceAgrees(ArrayRef<unsigned> A,
                                     ArrayRef<unsigned> B) {
    return formatCanPackSequence(A) == formatCanPackSequence(B);
  }

  /// Product pin for §8.4 #7 SMS format-acceptance shapes: live RC ≡ pure
  /// exact ≡ preferred-state HR peer; three ADD32 pack; two ST32 need 2
  /// cycles; rematch triple packs; co-issue packs; MI ≡ desc for same ops.
  /// Alone-op capacity (ARCTAN/SIN_COS) is descriptor-derived class-1 and is
  /// modeled on live RC/HR but not by pure field tryAdd — pinned separately.
  static bool formatAcceptanceDifferentialPins() {
    unsigned ThreeADD[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
    unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    unsigned DualLoadMac[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
    unsigned AloneThenALU[] = {Haydn::ARCTAN, Haydn::ADD32};

    // Field-format sequences: live RC canReserveByOpcode ≡ pure exactTryAdd.
    auto checkFieldSeq = [](ArrayRef<unsigned> Ops, bool ExpectPack,
                            unsigned ExpectCycles) {
      if (formatCanPackSequence(Ops) != ExpectPack)
        return false;
      if (formatPureExactCanPackSequence(Ops) != ExpectPack)
        return false;
      if (!formatAcceptanceMatchesPureExact(Ops))
        return false;
      if (ExpectPack && !formatPreferredStateAgreesWithPureExact(Ops))
        return false;
      if (formatSequentialCycleCount(Ops) != ExpectCycles)
        return false;
      return true;
    };

    if (!checkFieldSeq(ThreeADD, /*ExpectPack=*/true, /*ExpectCycles=*/1))
      return false;
    if (!checkFieldSeq(TwoST, /*ExpectPack=*/false, /*ExpectCycles=*/2))
      return false;
    if (!checkFieldSeq(Rematch, true, 1))
      return false;
    if (!checkFieldSeq(Coissue, true, 1))
      return false;
    if (!checkFieldSeq(DualLoadMac, true, 1))
      return false;

    // Alone-op class-1 capacity (RC/HR peer, not pure field tryAdd): co-issue
    // after ARCTAN rejects; sequential needs two cycles. Pure exact may still
    // field-pack ARCTAN+ADD32 — that is not a format-acceptance disagreement.
    {
      if (formatCanPackSequence(AloneThenALU))
        return false;
      if (formatSequentialCycleCount(AloneThenALU) != 2u)
        return false;
      HaydnResourceCycle RC;
      if (!RC.canReserveByOpcode(Haydn::ARCTAN))
        return false;
      RC.reserveByOpcode(Haydn::ARCTAN);
      if (!RC.hasAloneOp() || RC.canReserveByOpcode(Haydn::ADD32))
        return false;
    }

    if (!formatPureExactCanPackSet(ThreeADD) ||
        formatPureExactCanPackSet(TwoST) ||
        !formatPureExactCanPackSet(Rematch))
      return false;

    // MI ≡ desc for same opcodes (format opcode-keyed, not port-shaped).
    if (!formatAcceptanceAgrees(Rematch, Rematch) ||
        !formatAcceptanceAgrees(TwoST, TwoST) ||
        !formatAcceptanceAgrees(ThreeADD, ThreeADD))
      return false;

    // MID placement path (SMS ResourceManager primary) ≡ opcode format path
    // on the rematch triple (descriptor ports do not bind this shape).
    {
      HaydnResourceCycle RCmid;
      MCInstrDesc D32{}, D64{};
      D32.Opcode = Haydn::ADD32;
      D64.Opcode = Haydn::ADD64;
      if (!RCmid.canReserveResources(&D32))
        return false;
      RCmid.reserveResources(&D32);
      if (!RCmid.canReserveResources(&D64))
        return false;
      RCmid.reserveResources(&D64);
      if (!RCmid.canReserveResources(&D64))
        return false;
      RCmid.reserveResources(&D64);
      if (RCmid.getMemberCount() != 3u ||
          RCmid.getOccupiedSlots() != SlotBits(Haydn::SLOT_ALL))
        return false;
      // Preferred rematch: ADD32 on S0 so both ADD64 take S1|S2.
      if (RCmid.getCycleState().Members[0].FieldSlots !=
          SlotBits(Haydn::SLOT0))
        return false;
    }

    // Fourth ADD32 after a full cycle must reject on live RC (format fill).
    {
      HaydnResourceCycle RC;
      for (unsigned I = 0; I < 3; ++I) {
        if (!RC.canReserveByOpcode(Haydn::ADD32))
          return false;
        RC.reserveByOpcode(Haydn::ADD32);
      }
      if (RC.canReserveByOpcode(Haydn::ADD32))
        return false;
      if (RC.getOccupiedSlots() != SlotBits(Haydn::SLOT_ALL) ||
          RC.getMemberCount() != 3u)
        return false;
    }
    return true;
  }

  //===--------------------------------------------------------------------===//
  // II-wrap false-accept differential (issue-time-only ResourceCycle pin)
  //===--------------------------------------------------------------------===//
  // ResourceManager keeps one independent ResourceCycle per modulo phase and
  // only books the issue phase. Multi-cycle occupancy that wraps under II is
  // invisible here: two alone-ops reserved on two independent cycles both
  // succeed, even though a 2-cycle FU lock from phase 0 under II=2 would also
  // claim phase 1 and make the second alone-op illegal. This helper *proves*
  // that issue-time-only false acceptance; it does not implement multi-cycle
  // booking. SMS-HOOK fail-closes multi-cycle stages (catalog pin
  // smsHookRejectsIIWrapFalseAccept) so product SMS never relies on this gap.

  /// Differential pin: two independent ResourceCycles (SMS per-phase model)
  /// both accept an issue-alone op. Combined with the catalog II-wrap
  /// predicate for StageCycles=2 / II=2, this is the issue-time-only
  /// false-accept hazard SMS-HOOK must reject. Pure; no MIR mutation.
  static bool issueTimeOnlyFalseAcceptsIIWrapAloneConflict() {
    using namespace haydn::restriction;
    // Alone-op capacity is class-1 same-cycle (one per ResourceCycle). Two
    // independent cycles model phases 0 and 1 under II=2.
    HaydnResourceCycle C0;
    HaydnResourceCycle C1;
    if (!C0.canReserveByOpcode(Haydn::ARCTAN) ||
        !C1.canReserveByOpcode(Haydn::ARCTAN))
      return false;
    C0.reserveByOpcode(Haydn::ARCTAN);
    C1.reserveByOpcode(Haydn::ARCTAN);
    // Both cycles hold an alone-op → issue-time-only accepts the concurrent
    // use that a 2-cycle alone lock wrapping under II=2 would forbid.
    if (!C0.hasAloneOp() || !C1.hasAloneOp())
      return false;
    // Catalog: multi-cycle stage under II=2 is an SMS-HOOK reject so product
    // never ships this false-accept as acceptance.
    return smsHookRejectsIIWrapFalseAccept(/*StageCycles=*/2, /*II=*/2) &&
           smsIIWrapOccupiesPhase(/*Issue=*/0, /*Stage=*/2, /*II=*/2,
                                  /*Query=*/1);
  }

  //===--------------------------------------------------------------------===
  // FE5B whole-kernel periodic certificate (WP4)
  //===--------------------------------------------------------------------===
  // MI-aware periodic proof over II independent phase ResourceCycles before
  // rewrite; lifecycle retains the original loop until Accepted; Rejected
  // is recoverable rollback. II-wrap / long occupancy fail-closed under
  // product class-3 law. Same-bank simultaneous defs fail-closed via write-
  // port budget and same-reg DefRegKey WAW. Does not flip WP5 multi-stage
  // product policy.

  /// Product pin: multi-cycle / II-wrap long occupancy is not product-enabled.
  static constexpr bool
  productIIWrapLongOccupancyFailsClosed(unsigned StageCycles, unsigned II) {
    using namespace haydn::restriction;
    return smsHookRejectsMultiCycleStage(StageCycles) ||
           smsHookRejectsIIWrapFalseAccept(StageCycles, II) ||
           smsIIWrapSelfConflicts(StageCycles, II);
  }

  /// True when pure same-phase same-reg WAW keys collide (DefRegKey != 0).
  static bool samePhaseSameRegWAWConflicts(ArrayRef<SMSCertPhaseOp> Ops,
                                           unsigned II, unsigned Phase) {
    if (II == 0)
      return false;
    for (size_t I = 0, E = Ops.size(); I != E; ++I) {
      const SMSCertPhaseOp &A = Ops[I];
      if (A.DefRegKey == 0 || (A.NormalizedPhase % II) != Phase)
        continue;
      for (size_t J = I + 1; J != E; ++J) {
        const SMSCertPhaseOp &B = Ops[J];
        if (B.DefRegKey == A.DefRegKey && (B.NormalizedPhase % II) == Phase)
          return true;
      }
    }
    return false;
  }

  /// True when pure same-phase bank write counts exceed product port budgets.
  /// DefBank: 1=GPR (2W), 2=DR (3W), 3=AR (2W).
  static bool samePhaseBankWritesExceedBudget(ArrayRef<SMSCertPhaseOp> Ops,
                                              unsigned II, unsigned Phase) {
    if (II == 0)
      return false;
    unsigned GPRW = 0, DRW = 0, ARW = 0;
    for (const SMSCertPhaseOp &Op : Ops) {
      if ((Op.NormalizedPhase % II) != Phase)
        continue;
      switch (Op.DefBank) {
      case 1:
        ++GPRW;
        break;
      case 2:
        ++DRW;
        break;
      case 3:
        ++ARW;
        break;
      default:
        break;
      }
    }
    return GPRW > HAYDN_GPR_WRITE_PORTS || DRW > HAYDN_DR_WRITE_PORTS ||
           ARW > HAYDN_AR_WRITE_PORTS;
  }

  /// Simultaneous same-bank defs fail-closed when same-reg WAW collides or
  /// bank write ports are exceeded in any phase under \p II.
  static bool sameBankSimultaneousDefsFailClosed(ArrayRef<SMSCertPhaseOp> Ops,
                                                 unsigned II) {
    if (II == 0)
      return true; // invalid II — fail closed
    for (unsigned P = 0; P < II; ++P) {
      if (samePhaseSameRegWAWConflicts(Ops, II, P))
        return true;
      if (samePhaseBankWritesExceedBudget(Ops, II, P))
        return true;
    }
    return false;
  }

  /// Whole-kernel MI-aware periodic proof: for every phase in [0, II),
  /// pack opcodes with an independent ResourceCycle (same depth as SMS
  /// placement) and enforce II-wrap / long-occupancy + same-bank laws.
  /// Empty ops with II>=1 is vacuously true (no body). Pure; no MIR mutation.
  static bool proveWholeKernelPeriodicPhases(unsigned II,
                                             ArrayRef<SMSCertPhaseOp> Ops) {
    using namespace haydn::restriction;
    if (II == 0)
      return false;
    // Bound pure oracle depth; product SMS IIs are small.
    if (II > 64u)
      return false;

    for (const SMSCertPhaseOp &Op : Ops) {
      if (productIIWrapLongOccupancyFailsClosed(Op.StageCycles, II))
        return false;
      // Occupancy that spans beyond issue under any II is class-3 — fail closed
      // even when StageCycles==II (exact cover still needs approved booking).
      if (smsIIWrapSpansBeyondIssuePhase(Op.StageCycles, II))
        return false;
    }

    if (sameBankSimultaneousDefsFailClosed(Ops, II))
      return false;

    for (unsigned P = 0; P < II; ++P) {
      HaydnResourceCycle RC;
      for (const SMSCertPhaseOp &Op : Ops) {
        if ((Op.NormalizedPhase % II) != P)
          continue;
        if (Op.Opcode == 0)
          continue; // bank/WAW-only probe rows
        if (!RC.canReserveByOpcode(Op.Opcode))
          return false;
        RC.reserveByOpcode(Op.Opcode);
      }
    }
    return true;
  }

  /// Drive retain → pre-proof → post-validation → final accept (or reject).
  /// \p PostRewriteStillValid models recoverable validation after expand
  /// (false → Rejected with mustRollback; original must still be retained
  /// until this transition when rewrite is transactional).
  static SMSCertLifecycle runPeriodicCertificate(unsigned II,
                                                 ArrayRef<SMSCertPhaseOp> Ops,
                                                 bool PostRewriteStillValid) {
    SMSCertLifecycle S = SMSPeriodicCertificate::begin();
    assert(SMSPeriodicCertificate::originalLoopMustRemain(S) &&
           "begin retains original loop");
    const bool PreOK = proveWholeKernelPeriodicPhases(II, Ops);
    S = SMSPeriodicCertificate::onPreRewriteProof(S, PreOK);
    if (SMSPeriodicCertificate::mustRollback(S))
      return S;
    S = SMSPeriodicCertificate::onPostRewriteValidation(S,
                                                        PostRewriteStillValid);
    if (SMSPeriodicCertificate::mustRollback(S))
      return S;
    return SMSPeriodicCertificate::onFinalAccept(S);
  }

  /// Product law pins for WP4: original retained until Accepted; II-wrap
  /// false-accept fail-closed; classic legal single-cycle kernel certifies;
  /// multi-cycle / same-reg WAW / bank over-budget reject; half-enabled
  /// multi-stage remains forbidden (WP5 not flipped here).
  static bool productPeriodicCertificatePins() {
    using namespace haydn::restriction;

    // Lifecycle: retain until final accept; reject rolls back.
    if (!SMSPeriodicCertificate::originalLoopMustRemain(
            SMSCertLifecycle::OriginalRetained) ||
        !SMSPeriodicCertificate::originalLoopMustRemain(
            SMSCertLifecycle::PreRewriteProved) ||
        !SMSPeriodicCertificate::originalLoopMustRemain(
            SMSCertLifecycle::PostRewriteValid) ||
        SMSPeriodicCertificate::mayDiscardOriginalLoop(
            SMSCertLifecycle::OriginalRetained) ||
        !SMSPeriodicCertificate::mayDiscardOriginalLoop(
            SMSCertLifecycle::Accepted) ||
        !SMSPeriodicCertificate::mustRollback(SMSCertLifecycle::Rejected))
      return false;

    // II-wrap / long occupancy fail-closed under product class-3.
    if (!productIIWrapLongOccupancyFailsClosed(/*StageCycles=*/2, /*II=*/2) ||
        productIIWrapLongOccupancyFailsClosed(/*StageCycles=*/1, /*II=*/2) ||
        !issueTimeOnlyFalseAcceptsIIWrapAloneConflict())
      return false;

    // Legal single-cycle two-phase kernel (ADD32 @0, XOR32 @1 under II=2).
    {
      const SMSCertPhaseOp Legal[] = {
          {/*Phase=*/0, Haydn::ADD32, /*Stage=*/1, /*GPR=*/1, /*Key=*/1},
          {/*Phase=*/1, Haydn::XOR32, /*Stage=*/1, /*GPR=*/1, /*Key=*/2},
      };
      if (!proveWholeKernelPeriodicPhases(/*II=*/2, Legal))
        return false;
      if (runPeriodicCertificate(/*II=*/2, Legal, /*Post=*/true) !=
          SMSCertLifecycle::Accepted)
        return false;
      // Post-rewrite validation fail → Rejected (rollback), original was still
      // retained through PreRewriteProved.
      if (runPeriodicCertificate(/*II=*/2, Legal, /*Post=*/false) !=
          SMSCertLifecycle::Rejected)
        return false;
    }

    // Multi-cycle stage under II=2 fail-closed.
    {
      const SMSCertPhaseOp Multi[] = {
          {0, Haydn::ADD32, /*Stage=*/2, 1, 1},
      };
      if (proveWholeKernelPeriodicPhases(/*II=*/2, Multi))
        return false;
      if (runPeriodicCertificate(/*II=*/2, Multi, true) !=
          SMSCertLifecycle::Rejected)
        return false;
    }

    // Same-phase same-reg WAW fail-closed.
    {
      const SMSCertPhaseOp Waw[] = {
          {0, Haydn::ADD32, 1, 1, /*Key=*/7},
          {0, Haydn::XOR32, 1, 1, /*Key=*/7},
      };
      if (proveWholeKernelPeriodicPhases(/*II=*/1, Waw))
        return false;
      if (!sameBankSimultaneousDefsFailClosed(Waw, /*II=*/1))
        return false;
    }

    // Same-phase bank write-port over-budget (3×GPR write under 2W).
    {
      const SMSCertPhaseOp Ports[] = {
          {0, Haydn::ADD32, 1, 1, 1},
          {0, Haydn::XOR32, 1, 1, 2},
          {0, Haydn::OR32, 1, 1, 3},
      };
      if (proveWholeKernelPeriodicPhases(/*II=*/1, Ports))
        return false;
    }

    // Half-enabled multi-stage still forbidden: certificate does not claim
    // product multi-stage ON; class-3 remains empty.
    if (ProductCrossCycleCapacityEnabled || ProductClass3RestrictionCount != 0u)
      return false;

    // Per-op resource import remains closed until golden admits the complete
    // table. Aggregate PortModel ceilings stay product-safe; competitive
    // per-op claims must not open early. Availability-aware record must
    // report AggregateCeilingsOnly for a representative opcode.
    if (!haydnProductResourceAdmissionPinsHold() || hasAdmittedPerOpRecords() ||
        completeModelPin() != 0u ||
        lookupAdmittedPerOpRecord(Haydn::ADD32) != nullptr ||
        competitivePerOpClaimsAllowed(Haydn::ADD32))
      return false;
    {
      const HaydnAvailabilityAwareResourceRecord Rec =
          availabilityAwareRecord(Haydn::ADD32);
      if (Rec.CompetitiveClaimsAllowed ||
          Rec.Availability == HaydnResourceRecordAvailability::PerOpAdmitted)
        return false;
      const HaydnGoldenAggregateResourceSurface Surf =
          aggregateResourceSurface();
      if (Surf.PerOpRecordsAdmitted || Surf.CompleteModel != 0u)
        return false;
      if (Surf.GPRWritePorts != HAYDN_GPR_WRITE_PORTS ||
          Surf.GPRReadPorts != HAYDN_GPR_READ_PORTS)
        return false;
    }

    return true;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNRESOURCECYCLE_H
