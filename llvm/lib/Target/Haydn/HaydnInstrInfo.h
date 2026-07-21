//===-- HaydnInstrInfo.h - Haydn Instruction Information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Haydn implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNINSTRINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNINSTRINFO_H

#include "HaydnRegisterInfo.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <optional>

#define GET_INSTRINFO_HEADER
#include "HaydnGenInstrInfo.inc"

namespace llvm {

class HaydnSubtarget;
class ScheduleDAGMI;
class HaydnInstrInfo;

// Result of recognizing a countable single-BB loop in PHI-form MIR (the shape
// the MachinePipeliner sees, pre-PHIElimination).
// Produced by HaydnInstrInfo::analyzeCountableLoop. Fields are valid only when
// the method returns true. Declared early (forward) so HaydnInstrInfo's method
// can reference it; defined after the class.
struct HaydnCountableLoop {
  // The loop's conditional-branch terminator (the latch back-edge/exit test).
  // A hardware-loop pass removes this once the loop is converted.
  MachineInstr *EndLoop = nullptr;
  // The SEQ32/SLT32/SLTU32 compare that defines EndLoop's condition register.
  // Removed by a hardware-loop pass once the branch is gone.
  MachineInstr *CmpMI = nullptr;
  // The induction-variable PHI register (the loop-carried counter). Left
  // invalid by analyzeCountableLoop today; callers that need it re-derive it.
  Register IVReg;
  // A runtime register holding the trip count (the IV's preheader init for a
  // decrementing IV, or the compare's non-IV operand for an incrementing IV).
  Register TripCountReg;
};

// Target-specific loop info for the MachinePipeliner (Swing Modulo Scheduling).
// Mirrors the ARM structure (see ARMBaseInstrInfo.cpp): identify the loop's
// conditional terminator (EndLoop) and the comparison instruction that sets
// its condition register (CmpMI), plus a runtime trip-count register
// (TripCountReg). Trip-count handling is delegated to the target-independent
// ModuloScheduleExpander via createTripCountGreaterCondition
// adjustTripCount, both of which ALWAYS emit a runtime comparison and return
// nullopt -- there is no static-trip-count shortcut. The hand-rolled
// `(limit - init) / step` derivation was a silent wrong-code bug (Blocker-1
// lesson): it returned a compile-time bool that drove
// `PeelingModuloScheduleExpander::fixupBranches` into the static-false
// (`KernelDisposed`) branch, collapsing countable loops to ~1 iteration with
// `-verify-machineinstrs` still green. See and lesson.
// Trip-count source : for a DECREMENTING IV (the shape LSR
// produces for real DSP loops -- `iv -= step` until `iv == 0`), TripCountReg
// is the IV's preheader *init* value (the IV counts N, N-1,..., 0, so its
// initial value IS the trip count). For an INCREMENTING IV (`iv < limit`)
// TripCountReg is the compare's non-IV operand (the upper bound). Taking the
// compare's non-IV operand unconditionally was the matrix_test crash root
// cause: for decrementing loops that operand is a function-wide materialized
// zero (`ADDI32 $r0, 0`), and adjustTripCount's replaceRegWith on it
// corrupted every compare/PHI-init in the function. See AIE's DownCountLoop
// (sms-packetizer-deep-dive.md §2c) for the same init-based derivation.
class HaydnPipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  MachineFunction *MF;
  const HaydnInstrInfo *HII;
  MachineInstr *EndLoop;
  MachineInstr *CmpMI;
  Register TripCountReg;
  // Cached loop basic block — set in the constructor because setPreheader
  // may erase EndLoop (when the expander clones the kernel)
  // making EndLoop->getParent unsafe in later adjustTripCount calls.
  MachineBasicBlock *LoopBB;
  DebugLoc DL;
  // ZOL (Zero-Overhead Loop) mode. When true, the loop is already in
  // hardware-loop form (LoopStart in preheader + PseudoLoopEnd in latch).
  // SMS pipelines it directly, editing LoopStart's adj operand for trip-count
  // adjustment. Mirrors AIE's ZeroOverheadLoop
  // (AIEBasePipelinerLoopInfo.cpp:644-835).
  bool IsZOL = false;
  MachineInstr *LoopStart = nullptr;

public:
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *CmpMI,
                         Register TripCountReg)
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(CmpMI),
        TripCountReg(TripCountReg), LoopBB(EndLoop->getParent()),
        DL(EndLoop->getDebugLoc()) {}

  // ZOL constructor — for loops already in hardware-loop form.
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *LoopStart)
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(nullptr),
        TripCountReg(), LoopBB(EndLoop->getParent()),
        DL(EndLoop->getDebugLoc()), IsZOL(true), LoopStart(LoopStart) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override;

  // Reject schedules that produce no pipeline overlap (StageCount <= 1)
  // PPS-3 stage-count gate (AIE canAcceptII MaxStageCount), and PPS-3
  // reg-pressure gate (AIE canAcceptII/canAllocate TrackRegPressure) into
  // this hook. Mirrors AIEBasePipelinerLoopInfo::shouldUseSchedule
  // canAcceptII.
  bool shouldUseSchedule(SwingSchedulerDAG &SSD, SMSchedule &SMS) override;

  void recordSuccessfulSMS(MachineFunction &MF, MachineBasicBlock *KernelBB,
                           unsigned ResMII, unsigned RecMII, unsigned MII,
                           unsigned StageCount, unsigned NumOps,
                           unsigned ScheduledII) override;

  std::optional<bool>
  createTripCountGreaterCondition(int TC, MachineBasicBlock &MBB,
                                  SmallVectorImpl<MachineOperand> &Cond) override;

  void adjustTripCount(int TripCountAdjust) override;

  void setPreheader(MachineBasicBlock *NewPreheader) override;
};

class HaydnInstrInfo : public HaydnGenInstrInfo {
  const HaydnRegisterInfo RegInfo;
  const HaydnSubtarget &STI;

public:
  explicit HaydnInstrInfo(const HaydnSubtarget &STI);

  const HaydnRegisterInfo &getRegisterInfo() const { return RegInfo; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  // OR64/OR32 rd, rs, rs is the DR/GPR bank-copy idiom (rd = rs|rs = rs).
  // MOVE32 rd, rs, rs is the canonical GPR move. Recognize as copies so
  // CSE/peepholes can treat them without rewriting to bare COPY (post-RA
  // bare COPY is not Bundle128-safe — bundler skips, AsmPrinter drops).
  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  // SSA EarlyIfConversion support. Cond comes from
  // analyzeBranch: [Imm(branch-opc), CondReg] for BEQZ_W/BNEZ_W. Emits
  // MOVT32/MOVF32 (tied False seed) — same contract as G_SELECT isel.
  bool canInsertSelect(const MachineBasicBlock &MBB,
                       ArrayRef<MachineOperand> Cond, Register DstReg,
                       Register TrueReg, Register FalseReg, int &CondCycles,
                       int &TrueCycles, int &FalseCycles) const override;

  void insertSelect(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    const DebugLoc &DL, Register DstReg,
                    ArrayRef<MachineOperand> Cond, Register TrueReg,
                    Register FalseReg) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                       int *BytesRemoved = nullptr) const override;

  // Reverse a branch condition, swapping the target basic blocks.
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  // Expand pseudo instructions after register allocation.
  bool expandPostRAPseudo(MachineInstr &MI) const override;

  // Check if this is a scheduling boundary (e.g., call, branch, barrier).
  bool isSchedulingBoundary(const MachineInstr &MI,
                           const MachineBasicBlock *MBB,
                           const MachineFunction &MF) const override;

  //===------------------------------------------------------------------===
  // Branch relaxation hooks (used by generic BranchRelaxation pass)
  //===------------------------------------------------------------------===

  // Check if a branch with the given opcode can reach an offset.
  // Conditional branches use simm16 (±32KB); JAL uses simm20 (±512KB);
  // unconditional branches (BEQZ R0) share the simm16 range.
  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  // Return the destination basic block of a branch instruction.
  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  // Insert an indirect branch (for far unconditional branches that exceed
  // the direct branch range). Uses JALR to jump via a register.
  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset = 0,
                            RegScavenger *RS = nullptr) const override;

  // Return the encoded size of a machine instruction in bytes.
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  // Insert a standalone NOP at \p MI. Required for AIE-style cycle-level NOP
  // padding in the post-RA scheduler's leaveMBB (Phase B2). The base
  // TargetInstrInfo::insertNoop is `llvm_unreachable`, so this override is
  // mandatory before any code path calls it.
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  // Create the SMS-facing resource model (a ResourceCycle that tracks
  // functional-unit/slot usage per cycle).
  // Default : a HaydnResourceCycle backed by Haydn::Bundle
  // alternative-aware slot pressure so SWPS packs dual LD64 (slot0+slot1).
  // The DFA fallback (DFAPacketizer over itineraries) is choice-set-naive and
  // inflates ResMII on dual-load loops; reachable via
  // haydn-hr-resource-cycle=0 for diagnosis only.
  ResourceCycle *CreateTargetScheduleState(
      const TargetSubtargetInfo &STI) const override;

  // Return the scoreboard hazard recognizer used by the MachineScheduler
  // (Stream B, ). Installed for the POST-RA scheduler so it
  // respects slot exclusivity, the 3-issue cap, and the GPR 4R2W port
  // budget during scheduling. The framework calls this from both
  // GenericScheduler::initialize (pre-RA) and PostGenericScheduler::initialize
  // (post-RA); we gate the recognizer to post-RA only in Phase B1 (pre-RA
  // keeps the existing VLIWMachineScheduler path that works). Returns nullptr
  // for pre-RA regions to preserve current behavior.
  ScheduleHazardRecognizer *CreateTargetMIHazardRecognizer(
      const InstrItineraryData *ItinData, const ScheduleDAGMI *DAG) const override;

  //===--------------------------------------------------------------------===
  // AIE dual-sched mutation helpers
  //===--------------------------------------------------------------------===

  // Memory→memory edge latency for post-RA MemoryEdges mutation (AIE peer).
  // Uses getFirst/LastMemoryCycle when both known; else conservative 1.
  // Mutation default is OFF (see haydn-postra-memory-edges) until IB/PP KPI.
  std::optional<int> getMemoryLatency(unsigned SrcSchedClass,
                                      unsigned DstSchedClass) const;

  // Memory access cycle relative to issue (AIE getFirst/LastMemoryCycle peer).
  // LoadLatency=2 (HaydnSchedModel / ISA §55): first=0, last=1 for load-class
  // itineraries; stores issue at cycle 0. nullopt = non-memory / unknown class.
  std::optional<int> getFirstMemoryCycle(unsigned SchedClass) const;
  std::optional<int> getLastMemoryCycle(unsigned SchedClass) const;
  int getMinFirstMemoryCycle() const { return 0; }
  int getMaxFirstMemoryCycle() const { return 0; }
  int getMinLastMemoryCycle() const { return 0; }
  int getMaxLastMemoryCycle() const { return 1; } // LoadLatency - 1

  // Max result latency for MI from itinerary OperandCycles (for RegionEndEdges
  // ExitSU artificial edges). Floor 1; loads use at least LoadLatency.
  unsigned getMaxResultLatency(const MachineInstr &MI) const;

  // True if MI is a branch/call that needs delay-slot spacing to ExitSU
  // (Haydn has no architectural delay slots; return 0 — post-RA NOPs cover
  // pack. Hook exists for AIE-shaped RegionEndEdges).
  unsigned getNumDelaySlots(const MachineInstr &MI) const;

  // Analyze a single-BB loop for software pipelining.
  // Returns a PipelinerLoopInfo object if the loop can be pipelined, or
  // nullptr if the loop structure is not understood.
  std::unique_ptr<PipelinerLoopInfo>
  analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const override;

  // Recognize a countable single-BB loop in PHI-form MIR and fill \p Out on
  // success. Shared between the MachinePipeliner path (analyzeLoopForPipelining)
  // so there is ONE loop recognizer for SMS. The same
  // predicate and trip-count derivation apply: decrementing IVs
  // take the trip count from the IV's preheader init; incrementing IVs from
  // the compare's non-IV operand. Returns false (Out untouched) if the loop
  // is not analyzable -- has calls/unmodeled side effects, no recognized IV
  // no usable trip count, or an ambiguous compare. See HaydnInstrInfo.cpp.
  bool analyzeCountableLoop(MachineBasicBlock *LoopBB,
                            HaydnCountableLoop &Out) const;

  // Dual-load placement hint (single authority).
  // Over [Begin, End), when an LD32/LD64 follows a slot-0 LS occupant, record
  // slot=1 on AltDescs only — **never** `setDesc(LD*_S1)`. Logical opcodes
  // stay LD32/LD64; the encoder materializes the S1 Flex window from
  // HaydnMCFlags. Bundle position is the public slot; `_S*` is encode-private.
  // LD32/LD64 use Slot01_LD so the HR can auction S0|S1 without a second
  // opcode family. Called by post-RA enterMBB before scheduling.
  void promoteLoadsToSlot1(MachineBasicBlock::iterator Begin,
                           MachineBasicBlock::iterator End) const;

  // record \p MI's VLIW placement slot without baking it into the
  // opcode. Slot is written to `HaydnMachineFunctionInfo::getAltDescs`
  // (`HaydnAlternateDescriptors::setSlot`) and survives through AsmPrinter
  // MCInstLower (`HaydnMCFlags`). The encoder materializes Flex/private
  // encode opcodes only at encode time — `MI.setDesc(*_S*)` is gone.
  // When \p Slot is given, it is used directly (the HR-recorded slot). When
  // \p Slot is std::nullopt, the slot is derived from FlexMap legal slots
  // (prefer S0, then S1) for MIs the HR never auctioned.
  // \returns true if a placement slot was recorded on AltDescs; false if the
  // MI is not placeable (bundle/pseudo/debug, or no legal slot). The MachineInstr
  // opcode is never rewritten (I1: logical ops after ISel).
  bool commitSlotFlexVariant(MachineInstr &MI,
                             std::optional<unsigned> Slot = std::nullopt) const;

  //===------------------------------------------------------------------===
  // Addressing-mode hooks for SMS (MachinePipeliner) and mem clustering.
  // Cover fused pre/post-inc/dec loads+stores (AGU writeback) and the
  // LD/ST*_POST_INC pseudos. Post-dec / pre-dec are the same opcodes with a
  // negative scaled imm. See HaydnInstrInfo.cpp for operand layouts.
  //===------------------------------------------------------------------===

  bool isPostIncrement(const MachineInstr &MI) const override;

  // Haydn-local: true for PRE_* forms (rs += delta, then access). Upstream
  // SMS only queries isPostIncrement; this exists for target peeps/tests.
  bool isPreIncrement(const MachineInstr &MI) const;

  bool getBaseAndOffsetPosition(const MachineInstr &MI, unsigned &BasePos,
                                unsigned &OffsetPos) const override;

  bool getIncrementValue(const MachineInstr &MI, int &Value) const override;

  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
      int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINSTRINFO_H
