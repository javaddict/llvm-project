//===-- HaydnPipelinerLoopInfo.h - SMS PipelinerLoopInfo ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn PipelinerLoopInfo. Peer: AIEBasePipelinerLoopInfo.h / .cpp
// (AIEBasePipelinerLoopInfo.h:24 class; AIEBasePipelinerLoopInfo.cpp:1-14
// file split from InstrInfo; AIE2InstrInfo.cpp:59-64 FormatInterface).
// Product pre-RA SMS / hwloop stay default-gated in shouldUseSchedule.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DebugLoc.h"
#include <optional>

namespace llvm {

class HaydnInstrInfo;
class SwingSchedulerDAG;
class SMSchedule;

/// Advisory pre-RA format-union cycle estimate (contracts/pipeline.md, "the
/// only pre-RA format API"). Cycles is a lower bound on the body's issue
/// cycles packed at the admitted widest row (E3) entry capacity: it answers
/// "does this logical body have coverage in at least one available Format E
/// format" (via nullopt) and gives a conservative cycle floor for proposal
/// ranking. It is NOT emitted-II truth with virtual registers: the final
/// physical schedule may be worse, and that delta is QoR evidence, not a
/// correctness failure.
///
/// Deliberately witness-free: it carries only a cycle count, never the
/// format, row, alternate, placement, or bytes, so no selected row/entry/unit
/// or container that could later reveal them crosses RA. SEAM for W68.2
/// constraint bundle formats (which must preserve the chosen phase): price the
/// estimate per (format, phase) by widening this record additively — today the
/// admitted family is phase-free E96 (E2/E3 both 12 bytes), so the scalar
/// E3-widest union bound is complete.
struct CycleEstimate {
  unsigned Cycles;
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
  // Optional XORI invert on the latch condition path (see HaydnCountableLoop).
  MachineInstr *InvertMI = nullptr;
  Register TripCountReg;
  // No cached loop-MBB or DebugLoc members: the classic expander erases the
  // original block (cleanup), the soft adjustTripCount must not insert into
  // it (F41), and every hook that still emits uses a locally derived DebugLoc
  // (see createTripCountGreaterCondition).

  // ZOL (Zero-Overhead Loop) mode. When true, the loop is already in
  // hardware-loop form (LoopStart in preheader + PseudoLoopEnd in latch).
  // SMS pipelines it directly, editing LoopStart's adj operand for trip-count
  // adjustment. Mirrors AIE's ZeroOverheadLoop
  // (AIEBasePipelinerLoopInfo.cpp:644-835).
  bool IsZOL = false;
  MachineInstr *LoopStart = nullptr;
  // AIE MinTripCount peer (AIEBasePipelinerLoopInfo). 0 = unknown/unbounded.
  // ZOL SMS is only safe when MinTripCount is known and large enough to cover
  // prologue stages without a dynamic guard (ZOL cannot reverse its exit) —
  // OR when ZOLTripReg below carries the runtime count register feeding
  // LoopStart (CB-166: the Hexagon J2_loop0r dynamic-guard law; the
  // per-prologue condition is emitted on that register in
  // createTripCountGreaterCondition).
  int64_t MinTripCount = 0;
  // Runtime trip-count register feeding LoopStart $src (invalid when the
  // trip is a compile-time constant; then only the static MinTripCount law
  // applies). The value in this register is exactly what SET_HWLOOP_F2_W
  // consumes at Role A expansion, so a guard on it tests the count the
  // hardware decrements — the iteration-count correctness invariant.
  Register ZOLTripReg;

public:
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *CmpMI,
                         Register TripCountReg,
                         MachineInstr *InvertMI = nullptr)
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(CmpMI), InvertMI(InvertMI),
        TripCountReg(TripCountReg) {}

  // ZOL constructor — for loops already in hardware-loop form.
  HaydnPipelinerLoopInfo(MachineFunction *MF, const HaydnInstrInfo *HII,
                         MachineInstr *EndLoop, MachineInstr *LoopStart,
                         int64_t MinTripCount,
                         Register ZOLTripReg = Register())
      : MF(MF), HII(HII), EndLoop(EndLoop), CmpMI(nullptr), InvertMI(nullptr),
        TripCountReg(), IsZOL(true), LoopStart(LoopStart),
        MinTripCount(MinTripCount), ZOLTripReg(ZOLTripReg) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override;

  // Pre-RA SMS containment: reject every StageCount > 1 (no issue-cycle
  // identity crosses RA; ZOL and soft counted alike — closes inverted ZOL
  // multi-stage gate). Also reject ZOL StageCount <= 1 (no overlap), PPS-3
  // stage-count / reg-pressure gates, and AIE ZeroOverheadLoop
  // MaxStageCount >= MinTripCount. Accepted StageCount==1 schedules stay
  // bare logical MIs (proven counted residual only). Metrics live on the
  // MachinePipeliner success remark — no generic post-expand virtual.
  // Costs hwloop geometry on final parcels (kernel II + MinBodyBundles pad);
  // SetupIssueDistance is preheader→BEGIN, never an II>=Setup floor.
  // F41: -haydn-sms-containment-max may lift the bound for SOFT loops only
  // (test/bisect through the classic expander); ZOL multi-stage stays
  // accepted on the generic path (W68.1 form-uniform lift).
  bool shouldUseSchedule(SwingSchedulerDAG &SSD, SMSchedule &SMS) override;

  /// Advisory format-union cycle estimate for the pre-RA body \p Body (the
  /// contract's only pre-RA format API). Returns nullopt when any packable
  /// body instruction has no coverage in any available Format E row (fail
  /// closed — an RA-legal tuple with no alternate is a schema gap, surfaced,
  /// not silently estimated). Coverage is proven from the generated
  /// golden-admitted format table (findAltSpan: a non-NOP alternate span
  /// means the logical name is encodable in at least one row), never from a
  /// selected row/entry/unit. The cycle bound packs the packable instructions
  /// at the admitted widest (E3) row entry capacity — one cycle per pack —
  /// and is a conservative floor for proposal ranking, not emitted-II truth.
  /// Loop-control, PHIs, and metadata (debug/CFI/kill/position) instructions
  /// are not packable Format E entries and are excluded from the bound.
  ///
  /// \param Body body instructions to estimate (typically the loop's real
  /// instructions in program order).
  std::optional<CycleEstimate> estimateCyclesAcrossAvailableFormats(
      ArrayRef<MachineInstr *> Body) const;

  std::optional<bool>
  createTripCountGreaterCondition(int TC, MachineBasicBlock &MBB,
                                  SmallVectorImpl<MachineOperand> &Cond) override;

  // F41 law: ZOL edits the LoopStart $adj operand (hw counter is not
  // expander-cloned). Soft counted loops are a STRUCTURAL no-op: the
  // expander clones the stage-0 control chain into every prolog (delta
  // realized structurally) and any inserted def either dangles in the
  // erased original MBB or corrupts the already-inserted original-count
  // guards via replaceRegWith. Peer law: ARM soft adjustTripCount == {};
  // AIE soft base == log-only. Never insert MIR here for soft loops.
  void adjustTripCount(int TripCountAdjust) override;

  void setPreheader(MachineBasicBlock *NewPreheader) override;
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNPIPELINERLOOPINFO_H
