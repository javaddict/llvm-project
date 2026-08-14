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
  // Optional XORI32 %cmp, 1 between CmpMI and EndLoop (GISel emitInvert01 /
  // CondOpt Pattern B). Null when the branch reads CmpMI's def directly.
  // Must stay stage-0 with CmpMI/EndLoop — staging it alone delays the exit
  // predicate by one iteration (20000605-1 overshoot → abort).
  MachineInstr *InvertMI = nullptr;
  // The induction-variable PHI register (the loop-carried counter). Left
  // invalid by analyzeCountableLoop today; callers that need it re-derive it.
  Register IVReg;
  // A runtime register holding the trip count (the IV's preheader init for a
  // decrementing IV, or the compare's non-IV operand for an incrementing IV).
  Register TripCountReg;
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
  // bare COPY is not product-cycle-safe — bundler skips, AsmPrinter drops).
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

  // Stack-slot recognition for RA tooling + MachineInstr::getSpillSize /
  // getRestoreSize (AsmPrinter spill comments / #<spill-kpi>). Port of
  // AIEBaseInstrInfo.cpp:1965-2028 isStackSlotMemoryAccess (FI base +
  // FixedStack MMO; Haydn SP = R13). PostFE peers use the same FixedStack
  // MMO predicate after eliminateFrameIndex (AArch64/X86 shape; needed so
  // getSpillSize sees CSR/RA spills at print time).
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  Register isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;
  Register isStoreToStackSlotPostFE(const MachineInstr &MI,
                                    int &FrameIndex) const override;

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

  // Scheduling boundary: call/branch/return, every standard BUNDLE root
  // (hard-bundle atomic membership through RA, including mixed GPR32/DR64),
  // frame-setup/destroy, CFI/debug, and unmodeled side effects. SET/LoopStart
  // are *not* boundaries — they are real post-RA DAG SUs; setup distance is
  // owned by ZOLSetupExitLatency (SetupIssueDistance), leaveRegion
  // handleRegionConflicts (ExitReady + inter-zone pads), and residual Fixup
  // pad (single-MI skip / short useful-window).
  bool isSchedulingBoundary(const MachineInstr &MI,
                           const MachineBasicBlock *MBB,
                           const MachineFunction &MF) const override;

  //===------------------------------------------------------------------===
  // Hardware-loop setup predicates (HWLOOP-SU)
  //===------------------------------------------------------------------===
  // One normalized predicate covering logical, wide, and selected-member
  // forms so mutations / Fixup / formation never diverge on opcode lists.

  /// True for any SET_HWLOOP form (logical/wide/member) or residual LoopStart.
  bool isHardwareLoopSetupInstr(const MachineInstr &MI) const;
  bool isHardwareLoopSetupOpcode(unsigned Opc) const;

  /// Register-trip forms (count in GPR): SET_HWLOOP_REG / F2_W / members.
  bool isHardwareLoopRegTripOpcode(unsigned Opc) const;
  /// Immediate-trip forms: SET_HWLOOP / SET_HWLOOP_W / members.
  bool isHardwareLoopImmTripOpcode(unsigned Opc) const;

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

  // Encoded size in bytes: BUNDLE → encodedBytesFor(committed Format E row)
  // plus named late-layout growth (JT R0 re-zero, hwloop setup pads,
  // same-slot serial); bare real → productParcelBytes(); INLINEASM →
  // conservative getInlineAsmLength. Shared with Fixup + HardwareLoops +
  // BranchRelaxation. Hexagon peer: getSize + computeOffset extender/align
  // (HexagonInstrInfo.cpp:4601; HexagonBranchRelaxation.cpp:95-114).
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
  // Product default: max(1, LastSrc-FirstDst+1) from memory-only sched classes
  // (Slot0_LS / Slot1_LD / Slot01_LD); nullopt when either cycle is unknown
  // (MemoryEdges falls back to latency 1). Soft soak-off
  // (-haydn-accurate-memory-latency=false) returns class-agnostic 1. Unit
  // tables + lit packing pins (product full-NOP bubble; soft adjacent
  // st32→ld32).
  std::optional<int> getMemoryLatency(unsigned SrcSchedClass,
                                      unsigned DstSchedClass) const;

  // Memory access cycle relative to issue (AIE getFirst/LastMemoryCycle peer).
  // Bodies are generated (HaydnGenMemoryCycles.inc) from the published
  // Slot0_LS / Slot1_LD / Slot01_LD latency-2 scaffold — AIE MemInstrItinData
  // + AIEMemoryCyclesEmitter.cpp:123-157. nullopt = non-memory / unknown
  // class. Product getMemoryLatency reads these by default.
  std::optional<int> getFirstMemoryCycle(unsigned SchedClass) const;
  std::optional<int> getLastMemoryCycle(unsigned SchedClass) const;
  int getMinFirstMemoryCycle() const;
  int getMaxFirstMemoryCycle() const;
  int getMinLastMemoryCycle() const;
  int getMaxLastMemoryCycle() const;

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

  /// Byte-offset / access-width oracle for LS ops. Offsets are always bytes.
  /// Register-base scaled immediates are element indices (ISel and post-PEI);
  /// FrameIndex extras stay bytes until PEI rewrites them.
  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
      int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;

  /// Same-base accesses whose byte ranges [off, off+width) do not overlap.
  bool areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                       const MachineInstr &MIb) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINSTRINFO_H
