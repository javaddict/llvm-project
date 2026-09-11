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
#include "llvm/Support/CommandLine.h"
#include <optional>

#define GET_INSTRINFO_HEADER
#include "HaydnGenInstrInfo.inc"

namespace llvm {

class HaydnSubtarget;
class ScheduleDAGMI;
class HaydnInstrInfo;

// AIE aie-loop-min-tripcount peer: floor MinTripCount for SMS candidates
// (-1 = disabled). Defined in HaydnInstrInfo.cpp; the post-RA multi-stage
// host honors the same single option (F39 static-trip proof floor).
extern cl::opt<int> HaydnLoopMinTripCount;

// Result of recognizing a countable single-BB loop in PHI-form MIR (the shape
// the MachinePipeliner sees, pre-PHIElimination).
// Produced by HaydnInstrInfo::analyzeCountableLoop. Fields are valid only when
// the method returns true. Declared early (forward) so HaydnInstrInfo's method
// can reference it; defined after the class.
// D1.117 owner record. AIE/RISCV leave indirect unanalyzable
// (AIEBaseInstrInfo.cpp:186-188, RISCVInstrInfo.cpp:1325-1327). Hexagon
// walks instr_iterator (HexagonInstrInfo.cpp:435-508) with no LUI+ADDI
// pair. Haydn overlay: complete LUI+ADDI %bb chain from JALR rs, or a
// named refusal at retarget (D1.124). Dest may still name the first
// recovered %bb so cond+JALR stays analyzable.
struct HaydnJalrAddrMaterializeChain {
  MachineInstr *Control = nullptr;
  MachineInstr *Lui = nullptr;
  MachineInstr *Addi = nullptr;
  MachineBasicBlock *Dest = nullptr;
  Register JumpReg;
  bool Complete = false;
  bool Incoherent = false;
};

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

// D1.74: member→logical peel via msp::logicalOpcodeForMspClone (slot-map
// MultiSlot leftovers). D1.130 gMIR roles (B, JALR_CALL, JAL_TCO, JALR_TCO) are not
// peeled here. Catalog BEQZ/JAL/JALR stay cond / call / RET.
unsigned haydnLogicalOpcode(unsigned Opc);

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

  // Dest-less CFG JALR/RET/computed-goto is unanalyzable. LUI/ADDI %bb dest
  // recovery keeps cond+long JALR two-way. JALR_CALL is a returning call, not
  // a CFG jump (dest-scan miss continues). A coissued short uncond B stays
  // the FBB when unwrap prefers the cond. JAL_W / JAL_TCO / JALR_TCO are not
  // analyzable uncond terminators.
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

  // Short PC-relative B / cond / hwloop latch only. Long-form
  // LUI+ADDI32_W+JALR_W is insertIndirectBranch (branches promote; JALR
  // sites never regress through this API). Inherits DL onto each new MI.
  // CFG successors and probabilities stay with the caller
  // (AIEBaseInstrInfo.cpp:271-307).
  //
  // GR2.7 phase law: BEFORE the first Finalize run stamps the postcommit
  // block budget, bare emission is legal (S1 commits later). AFTER the
  // stamp, every REAL conditional-branch emission self-commits as a
  // committed singleton packet at emission time (bake +
  // finalizeExactLateSingleton); representation shells (B) and hwloop
  // metas (PseudoLoopEnd/LoopJNZ) stay bare.
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  // Strip short B/cond terminators (including bundled solo/coissue). Leave
  // LUI+ADDI32_W+JALR_W sites intact so they cannot shrink back to B.
  // Post-stamp coissued survivors stay on the same root (same-row NOP
  // of the vacated child). Pre-stamp restamp is one product cycle or
  // one singleton wrap (no singleton-split; never leaves bare real encode).
  unsigned removeBranch(MachineBasicBlock &MBB,
                       int *BytesRemoved = nullptr) const override;

  // Erase a selected branch (bare MI, BUNDLE root, or bundled member).
  // Bare: erase the MI. Solo BUNDLE (branch ± padding): erase the whole
  // cycle. Coissued BUNDLE{branch, real…}:
  //   * post-stamp: neutralize the selected child with the generated
  //     same-row NOP (HexagonConstPropagation.cpp:2508-2512 replaceWithNop
  //     peer; pipeline.md preserve-or-extend). Do not unbundle, do not
  //     singleton-split, do not re-choose the row.
  //   * pre-stamp: strip only that member (LongBranchNormalize far uncond)
  //     and restamp survivors as one product cycle or one singleton wrap
  //     (HaydnHWLoopDemote.cpp:860-890 recommitSurvivingCycleMembers
  //     unbundle). No singleton-split fallback.
  //   * BUNDLE root: strip every branch child in that cycle (generic
  //     removeBranch per-cycle) under the same post-/pre-stamp law
  // Does not walk preceding cycles — generic removeBranch still reverse-
  // walks the trailing analyzable tail (AIEBaseInstrInfo.cpp:237-266;
  // HexagonInstrInfo.cpp:605-625; RISCVInstrInfo.cpp:1361-1390).
  unsigned eraseSelectedBranch(MachineInstr &MI,
                               int *BytesRemoved = nullptr) const;

  // Reverse a branch condition, swapping the target basic blocks.
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  // Expand pseudo instructions after register allocation.
  bool expandPostRAPseudo(MachineInstr &MI) const override;

  // Representation expansion after MBP (HaydnExpandPseudos) and leftover
  // wrap (HaydnFinalizeBundle). B stays B through MIR (encoder peels to
  // BEQZ rs=R0). RET/BR_JT rebuild JALR_W. PseudoCALLIndirect becomes
  // JALR_CALL. Returns the surviving MI, or nullptr when \p MI is not
  // RET/BR_JT/PseudoCALLIndirect.
  MachineInstr *expandRepresentationPseudo(MachineInstr &MI) const;

  // Format E CB members list dest2 as an input only (HaydnFormatsE96Members
  // D_LDW_CB_IMM_E2). rewriteFieldSlotToMember drops the logical AGU
  // writeback dest and keeps the implicit tail (HaydnFinalizeBundle.cpp).
  // After RA, add implicit-def of that physreg so chained CB uses stay
  // defined. AIE keeps the tied dest on the member (AIETiedRegOperands).
  void preserveCircularBufferWritebackDefs(MachineFunction &MF) const;

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

  // D1.33 one-buffer law: the SOLE accessor for the
  // -haydn-branch-relax-safety-buffer runtime value. It is consumed INSIDE
  // isBranchOffsetInRange below — the single inflation seat. Every
  // far-deciding caller (the pre-S1 normalization far-test, the
  // hwloop-demote LongLatch decision, generic BranchRelaxation) passes the
  // RAW signed offset and must never pre-add the buffer: a pre-add
  // double-charges the allowance and steals the near-boundary short band.
  // Default ties to haydn::hwloop::BranchRelaxSafetyBufferBytes (one
  // insertIndirectBranch sequence); the static_assert in the .cpp pins
  // that tie.
  uint32_t getBranchRelaxSafetyBuffer() const;

  // WIDE_BranchSImm12 byte PC+imm. Forward and backward offsets each charge
  // one insertIndirectBranch sequence (MaxSingleBranchGrowthBytes). JALR_W
  // is always in range so a long-form site is never re-relaxed or shrunk.
  // This is the ONLY seat that inflates a displacement by
  // getBranchRelaxSafetyBuffer() (D1.33 single-inflation law).
  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  // D1.142: explicit site unwrap. Opcode-only BUNDLE stays always-in-range
  // unless getBranchDestBlock just armed the same MI (generic BR pair at
  // BranchRelaxation.cpp:739-740). Haydn callers pass the site MI and never
  // the thread_local handshake. AIE has no BR; RISC-V
  // RISCVInstrInfo.cpp:1751-1758 and Hexagon isJumpWithinBranchRange take
  // the MI / opcode they already unwrapped — no hidden adjacency flag.
  bool isBranchOffsetInRange(const MachineInstr &MI, int64_t BrOffset) const;

  // Destination MBB of a short B/cond, or of the LUI/ADDI32_W pair that
  // materializes a long-form JALR target. Null if the JALR has no address MI
  // or is a returning JALR_CALL.
  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  // One LUI+ADDI chain owner from JALR rs (D1.117). Complete requires both
  // halves, same %bb dest, and ADDI rs = JumpReg. Retarget named-refuses
  // a missing or dest-mismatched pair (D1.124).
  HaydnJalrAddrMaterializeChain
  getJalrAddrMaterializeChain(const MachineInstr &Jalr) const;

  // Long-form far jump: LUI+ADDI32_W+JALR_W into the pre-S1 BR trampoline
  // (RISCVInstrInfo.cpp:1294-1323 AUIPC+JALR vocabulary; AIE has empty
  // addPreEmitPass / no BR at AIE2TargetMachine.cpp:92). Inherits DL onto
  // every new MI. Updates Dest PHIs onto the trampoline. Does not add CFG
  // successors (BranchRelaxation owns that after return). RestoreBB is
  // unused and stays empty so generic BR erases it
  // (BranchRelaxation.cpp:687-688). No R11 spill. No free GPR is
  // fail-closed — RISC-V RestoreBB (RISCVInstrInfo.cpp:1433-1498) is the
  // CFG form Haydn does not grow.
  //
  // GR2.7 phase law: once the first Finalize run stamped the postcommit
  // block budget, this CFG-creating callback REFUSES (named fatal) —
  // long-form promotion is LBN in-block templates. Unstamped (pre-S1
  // seat, -run-pass probes) emits the trampoline sequence only.
  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset = 0,
                            RegScavenger *RS = nullptr) const override;

  // Encoded size in bytes: BUNDLE → encodedBytesFor(committed Format E row)
  // plus named late-layout growth (JT R0 re-zero, hwloop setup pads,
  // same-slot serial); bare real → productParcelBytes(); INLINEASM →
  // exact typed getInlineAsmLength (empty metadata = 0; public mnemonic or
  // braced packet = one product parcel; .space N = N; opaque text is
  // fatal). Shared with Fixup + HardwareLoops + BranchRelaxation.
  // Hexagon peer: getSize + getInlineAsmLength
  // (HexagonInstrInfo.cpp:1847, 4601; HexagonBranchRelaxation.cpp:95-114).
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  // Exact Format E layout size of INLINEASM / INLINEASM_BR text. Hexagon
  // peer HexagonInstrInfo.cpp:1847; Haydn overlay is exact parcels rather
  // than MaxInstLength × statement count (generic TargetInstrInfo.cpp:113).
  unsigned getInlineAsmLength(
      const char *Str, const MCAsmInfo &MAI,
      const TargetSubtargetInfo *STI = nullptr) const override;

  // Insert a standalone NOP at \p MI. Required for AIE-style cycle-level NOP
  // padding in the post-RA scheduler's leaveMBB (Phase B2). The base
  // TargetInstrInfo::insertNoop is `llvm_unreachable`, so this override is
  // mandatory before any code path calls it.
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  // Create the SMS-facing resource model (a ResourceCycle that tracks
  // functional-unit/slot usage per cycle).
  // GR2.1 contract boundary: the pre-RA pipeliner seat returns the Kind-A
  // HaydnIssueWidthCycle — the generated IssueWidth entry cap plus the shared
  // same-cycle RAW/WAW dependency laws, nothing else. Exact capacity, unit,
  // port, and hazard matching is post-RA HR business (HaydnResourceCycle is
  // the post-RA HR peer depth / statics / unit-test surface; no production
  // instantiation at this seat).
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
  // (Slot0_LS / Slot1_LD / Slot01_LD / Slot2_LS); nullopt when either cycle is
  // unknown. Product MemoryEdges fatals on a Slot*_LS / Slot*_LD class whose
  // cycles are missing (W21 ExactLatencies law) instead of silently falling
  // back to 1. Soft soak-off (-haydn-accurate-memory-latency=false) returns
  // class-agnostic 1. Unit tables + lit packing pins (product full-NOP
  // bubble; soft adjacent st32→ld32).
  std::optional<int> getMemoryLatency(unsigned SrcSchedClass,
                                      unsigned DstSchedClass) const;

  // True if SchedClass belongs to the published load/store itinerary family
  // (Slot*_LS / Slot*_LD) — the classes whose MemoryCycle rows are product
  // latency truth. Used by MemoryEdges to separate "non-memory class" (fine,
  // latency stays 1) from "memory class with no published First/Last row"
  // (a generator hole; must abort, not invent latency on a no-interlock
  // machine). Names, not enums: the family is exactly the MEMORY_ITIN_NAMES
  // set of generate_sched_records.py.
  static bool isPublishedMemoryItinerary(unsigned SchedClass);

  // Memory access cycle relative to issue (AIE getFirst/LastMemoryCycle peer).
  // Bodies are generated (HaydnGenMemoryCycles.inc) from the published
  // Slot0_LS / Slot1_LD / Slot01_LD / Slot2_LS latency-2 scaffold — AIE
  // MemInstrItinData + AIEMemoryCyclesEmitter.cpp:123-157. nullopt =
  // non-memory / unknown class. Product getMemoryLatency reads these by
  // default.
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

  /// Same-object non-overlapping accesses. Update-AM writeback is never
  /// disjoint. Distinct GEP Values use AIE SameValue MMOs
  /// (AIEBaseInstrInfo.cpp:2083-2109); unknown-address same-base falls back
  /// to the RISCV operand oracle (RISCVInstrInfo.cpp:3522-3552).
  bool areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                       const MachineInstr &MIb) const override;
};

/// HexagonConstPropagation.cpp:2508-2512 replaceWithNop (setDesc + strip).
/// Generated same-row NOP of the committed packet; do not unbundle.
void neutralizeSameRowNop(MachineInstr &MI, const HaydnInstrInfo &TII);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNINSTRINFO_H
