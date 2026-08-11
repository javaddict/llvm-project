//===-- HaydnSchedule.td - Haydn Scheduling -----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pipeline scheduling model for the Haydn 3-issue VLIW DSP.
// Defines DFA functional units (slot datapaths) and itineraries for both
// packetization and the MachinePipeliner (SMS).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Functional Units (DFA-based resource tracking for VLIW packetization)
//
// Slot datapaths — each slot has ONE encoding space, so only one instruction
// per slot per bundle:
// Slot 0: ALU32 or Load/Store (shared encoding space, mutually exclusive).
// Slot 1: Full ALU (32/64/SIMD/MAC) or Load.
// Slot 2: Full ALU (32/64/SIMD/MAC).
//
// NOTE — GPR 4R2W register-file ports are NOT modeled as FuncUnits here.
// attempted to add `GPR_R0..GPR_R3` + `GPR_W0, GPR_W1` as same-cycle
// InstrStage choice-sets so the DFA/scheduler/SMS would see port pressure.
// That change triggered a SIGSEGV in the post-RA VLIW scheduler
// (HaydnConvergingVLIWScheduler::SchedulingCost -> base
// ConvergingVLIWScheduler::SchedulingCost) at -O1+ on 6 IIR/long-branch
// tests: the base scheduler dereferenced a dangling/invalid SUnit inside
// getWeakLeft while scoring ready candidates. Root cause: the multi-stage
// itineraries (slot stage + port choice stages with NextCycles_=0) changed
// DFA reservation behavior such that the scheduler walked a ReadyQueue
// containing an SUnit whose memory was no longer valid (use-after-free in
// upstream llvm/lib/CodeGen/VLIWMachineScheduler.cpp, which we cannot patch
// under hard constraint #0). See and decision.
//
// G-MC-6 / AIE-#6 design (AIE-first):
// AIE models *named exclusive* register ports as itinerary FuncUnits
// (R_WX_PORT, R_RV_PORT, …) that conflict on bit-overlap.
// Haydn GPR/DR ports are a *shared pool* (4R2W / 7R3W) with R0-free and
// per-MI source dedup — pool occupancy is not exclusive-bit conflict.
// Therefore the single port authority is HaydnPortModel + HaydnFuncUnitWrapper
// counters in HaydnHazardRecognizer (post-RA pack path). Slot FuncUnits
// below remain the DFA/SMS slot authority only.
// Re-introducing GPR/DR ports as InstrStage FuncUnits needs either a pool
// aware scoreboard (not exclusive OR) or an upstream ReadyQueue UAF fix
// do not re-open exclusive named-port FUs for Haydn's pool ISA.
//
// The GPR 4R2W / DR 7R3W budgets are AUTHORITATIVELY enforced in C++ by
// HaydnPortModel::countGPRPorts / countDRPorts (operand inspect, R0-free
// source dedup). Packetizer retired; HR is the sole consumer.
//===----------------------------------------------------------------------===//

def SLOT0 : FuncUnit;   // Slot 0: ALU or LS (one encoding space)
def SLOT1 : FuncUnit;   // Slot 1: Full ALU or Load
def SLOT2 : FuncUnit;   // Slot 2: Full ALU

//===----------------------------------------------------------------------===//
// Itinerary Classes (map instructions to functional units)
//===----------------------------------------------------------------------===//

def PSEUDO       : InstrItinClass;  // Pseudo instructions (no resources)
def Slot0_ALU    : InstrItinClass;  // Slot 0 only (branches, calls, CSR)
def Slot0_LS     : InstrItinClass;  // Slot 0 only (Load/Store — same FU as Slot0_ALU)
def Slot12_ALU   : InstrItinClass;  // Slot 1 or 2 (ALU64/SIMD, multiply-form MAC)
def Slot12_ALU_AccLat : InstrItinClass;  // SIMD accumulator-MAC with
                                        // 1-cycle acc-feedback latency (Slot12_ALU
                                        // with [2,1,1,2] OperandCycles). The
                                        // InstrStage<1> avoids ResMII inflation
                                        // (unlike Slot12_MAC's InstrStage<2> which
                                        // inflated ResMII 5->9 in live testing).
                                        // OperandCycles give RecMII the correct
                                        // recurrence depth so SMS produces multi
                                        // stage schedules.
def Slot012_ALU  : InstrItinClass;  // Any slot (ALU32 — full ALU in all 3 slots)
def Slot1_LD     : InstrItinClass;  // Slot 1 only (LD32_S1, LD64_S1)
def Slot2_LS     : InstrItinClass;  // Slot 2 only, MEMORY op — Data_Latency 2.
                                    // The S2 forms of the LS formats are still
                                    // loads/stores: they must NOT borrow
                                    // Slot2_ALU, whose latency is 1. Haydn has
                                    // no interlock, so a latency-1 claim on a
                                    // load makes the scheduler (and the
                                    // HaydnLatencyStalls net) allow a read in
                                    // the very next bundle.
def Slot01_LD    : InstrItinClass;  // Slot 0 or 1 (plain LD64 — : the spec
                                   // D_LDW_WITH_IMM allows a 64-bit load in slot 0
                                   // (Load/Store unit) OR slot 1 (Load unit). A
                                   // choice-set itinerary lets the DFA/SMS pack two
                                   // such loads (slot0+slot1) in one cycle. Post-RA
                                   // HR tryAddProduct → setDesc materializes
                                   // LD64_S0/S1 (B3.exit.3; no promoteLoads).)
def Slot1_ALU    : InstrItinClass;  // Slot 1 only (SIMD-multiply the DB restricts
                                   // to slot 1 — capstone-schedule-port-audit
                                   // §(b)-1, : 16 X2/X4 SIMD-MAC ops
                                   // over-promised slot 2 under Slot12_ALU)

// single-slot-2 mirrors of the slot-1 single-slot itineraries. The
// CodeGenFormat cutover made GISel InstructionSelect emit `_S<k>` opcodes
// where `k` is the encoder's COMMITTED slot. The FLEX itinerary must therefore
// model that SINGLE slot (NOT a choice-set): a choice-set on a FLEX op would
// let SMS "schedule" a slot the encoder will not emit, producing an un-encodable
// kernel. See + reviews/haydn-sms-gap-rootcause-.md.
def Slot2_ALU    : InstrItinClass;  // Slot 2 only — single-slot-2 ALU64/SIMM
                                   // (mirrors Slot1_ALU shape with [SLOT2]).
def Slot2_MAC    : InstrItinClass;  // Slot 2 only — accumulator-feedback MAC
                                   // slot-2-committed (mirrors Slot1_MAC's
                                   // [InstrStage<1,[SLOT2]>],[2,1,1,2] shape).
def Slot2_ALU_AccLat : InstrItinClass;  // Slot 2 only — SIMD accumulator-MAC with
                                        // 1-cycle acc-feedback latency (mirrors
                                        // Slot12_ALU_AccLat's OperandCycles on a
                                        // single-slot-2 InstrStage<1>).

// Accumulator-feedback MAC classes. The MAC units (one standalone unit per
// slot, in slot 1 and slot 2) hold an internal accumulator register and feed
// it back to the next MAC's `ra` input in 1 cycle. The `rd` result is written
// back through the GPR/DR register file in 2 cycles ("data can back"). Per the
// spec DB `_meta.latency_info.mac_lat2` = "All MAC operations, 2-cycle
// pipelined." These two distinct latencies are encoded in ONE OperandCycles
// list with NO Bypass: the LLVM formula
// latency = DefCycle - UseCycle + 1 (MCInstrItineraries.h:222)
// yields acc->acc = 2-2+1 = 1 (feedback) and result->any-other-use = 2-1+1 = 2
// (regfile writeback). See D2XX for the full micro-arch rationale.
def Slot12_MAC   : InstrItinClass;  // Scalar acc-MAC — slot 1/2 (MAC32/MACQ31 phantoms removed)
// Acc-input-first MAC (FmtALU64Acc: outs rd, ins rd_in, rs1, rs2). Same
// resources/latencies as Slot12_MAC but OperandCycles ordered for rd_in at
// operand index 1: [rd=2, ra=2, rs1=1, rs2=1] → acc→acc latency 2-2+1=1.
// Using Slot12_MAC's [2,1,1,2] mapped ra to rs2 and inflated RecMII (bkfir
// 4→7) — P3.
def Slot12_MAC_AccFirst : InstrItinClass;
// Unit-model peer of Slot12_MAC_AccFirst, and the reason a hand-written
// schedule file still exists: HaydnFormatESchedule.td is generated from
// instruction_type_index.json, which gives ONE Data_Latency per instruction
// and cannot say which operand is read when. The [2, 2, 1, 1] below is the
// FmtALU64Acc operand order (rd, rd_in, rs1, rs2) and it is load-bearing —
// mapping the accumulator to rs2 instead inflated bkfir's RecMII from 4 to 7.
// Generated classes carry what the database states; this one carries what it
// does not.
def Unit_MAC0MAC1_L2_AccFirst : InstrItinClass;
def Slot1_MAC    : InstrItinClass;  // SIMD acc-MAC (X*MULA*/X*MULS*) — slot 1 only

//===----------------------------------------------------------------------===//
// Processor Itineraries (DFA automaton)
//
// Single-stage itineraries: each instruction occupies exactly one slot
// datapath for one cycle. Most results are latency 1 (available next cycle);
// SMS and the post-RA scheduler read this to find scheduling slack and compute
// the recurrence-bound MII.
//
// MAC is the exception: the MAC units feed the accumulator back internally in
// 1 cycle (enabling II=1 on a serial reduction) but write `rd` through the
// register file in 2 cycles. This is encoded as OperandCycles [2,1,1,2] with
// InstrStage<1> (— stage cycles inflate SMS ResMII; latency is operands
// only) and no Bypass — see Slot12_MAC / Slot1_MAC below.
//===----------------------------------------------------------------------===//

// The unit resources and their itinerary classes are concatenated in so a
// logical can be retargeted from its Slot* class one family at a time. Until
// one is, nothing references them and the model is unchanged.
def HaydnItineraries : ProcessorItineraries<
  !listconcat([SLOT0, SLOT1, SLOT2], HaydnFormatEUnits),
  [],
  !listconcat(HaydnFormatEItinData, [
    InstrItinData<PSEUDO, []>,

    // Slot 0 only — ALU or LS share the same encoding space. Latency 1.
    InstrItinData<Slot0_ALU, [InstrStage<1, [SLOT0]>], [1]>,
    // Slot 0 Load/Store — ISA §55 Data_Latency=2 (result available two
    // bundles after issue). OperandCycles [2] → use latency 2-1+1=2.
    // SMS needs packable address bumps (ADDI32 not ADDI32_W) and S2-first
    // pickSlot so the load's es/ls window is not a single cycle already
    // saturated by ALU; with those, latency 2 yields multi-stage kernels
    // (span > II) instead of the StageCount=0 compact schedules latency 1
    // produced. : keep [2] for post-RA load→consumer gap.
    InstrItinData<Slot0_LS,  [InstrStage<1, [SLOT0]>], [2]>,

    // Slot 1 or 2 — ALU64/SIMD/MAC. Result latency 1.
    InstrItinData<Slot12_ALU, [InstrStage<1, [SLOT1, SLOT2]>], [1]>,

    // SIMD accumulator-MAC with acc-feedback latency. Same resource as
    // Slot12_ALU (InstrStage<1>, no ResMII inflation) but with [2,1,1,2]
    // OperandCycles so SMS sees the 1-cycle acc->acc recurrence (latency =
    // 2-2+1=1) and the 2-cycle result->other latency (2-1+1=2). This gives
    // RecMII the correct depth to produce multi-stage schedules.
    // MAC acc-feedback latency = 4. The acc->acc edge (rd.DefCycle=4
    // rd_in.UseCycle=1) gives latency = 4-1+1 = 4. For firdec32x32_D2's
    // 2-MAC recurrence chain: RecMII = 2×4 = 8 → MII = max(ResMII=5, RecMII=8)
    // = 8 → matches HiFi3z II=8 → schedule span(9) > II(8) → StageCount=2.
    // The InstrStage<1> keeps ResMII unchanged (no DFA inflation).
    InstrItinData<Slot12_ALU_AccLat, [InstrStage<1, [SLOT1, SLOT2]>], [5, 1, 1, 5]>,

    // Any slot — ALU32 can execute in all 3 slots. Result latency 1.
    InstrItinData<Slot012_ALU, [InstrStage<1, [SLOT0, SLOT1, SLOT2]>], [1]>,

    // Slot 1 load — LD32_S1/LD64_S1 (alternative-slot load promoted by
    // so two loads pack into one bundle). Load-use latency 2 (see Slot0_LS).
    InstrItinData<Slot1_LD, [InstrStage<1, [SLOT1]>], [2]>,

    // Slot 0 or 1 load — plain LD64. The spec D_LDW_WITH_IMM allows a
    // 64-bit load in either slot; the choice-set lets the DFA pack two LD64
    // (slot0 + slot1) in one cycle, matching HiFi's dual-load FIR kernels.
    // Load-use latency 2 (see Slot0_LS).
    InstrItinData<Slot01_LD, [InstrStage<1, [SLOT0, SLOT1]>], [2]>,

    // Slot 1 only — SIMD-MAC the DB restricts to slot 1 (X2CMUL32, X4MUL16
    // X4SEL16, X4/X2 *MUL* families). Single-stage, latency 1 (same shape as
    // the other itineraries — capstone-schedule-port-audit §(b)-1).
    InstrItinData<Slot1_ALU, [InstrStage<1, [SLOT1]>], [1]>,

    // Accumulator-feedback MAC — scalar form (MAC32, MACQ31), slot 1 or 2.
    //
    // InstrStage MUST be <1>. SMS ResourceManager counts stage
    // cycles into ResMII; InstrStage<2> doubled MAC resource cost (8 MACs →
    // ResMII 10 instead of 4). Latency lives in OperandCycles only:
    // * OperandCycles = [rd=2, rs1=1, rs2=1, ra=2]. The LLVM formula
    // latency = DefCycle - UseCycle + 1 yields:
    // acc->acc (rd->next-MAC ra): 2 - 2 + 1 = 1 (feedback path)
    // result->any-other-use: 2 - 1 + 1 = 2 (regfile writeback)
    // OperandCycles take priority over the stage fallback when present.
    // Shape matches Slot12_ALU_AccLat (InstrStage<1> + multi-cycle operands).
    InstrItinData<Slot12_MAC, [InstrStage<1, [SLOT1, SLOT2]>], [2, 1, 1, 2]>,

    // FmtALU64Acc operand order (rd, rd_in, rs1, rs2): acc use is index 1.
    InstrItinData<Slot12_MAC_AccFirst, [InstrStage<1, [SLOT1, SLOT2]>],
                  [2, 2, 1, 1]>,
    // Same operand cycles on the units the database gives these: MAC0/MAC1.
    InstrItinData<Unit_MAC0MAC1_L2_AccFirst,
                  [InstrStage<1, [U_MAC0, U_MAC1]>], [2, 2, 1, 1]>,

    // Accumulator-feedback MAC — SIMD form (X2MULA32, X4MULA16,...), slot 1
    // only. Same InstrStage<1> + [2,1,1,2] shape.
    InstrItinData<Slot1_MAC, [InstrStage<1, [SLOT1]>], [2, 1, 1, 2]>,

    // single-slot-2 mirrors. Identical OperandCycles to the slot-1
    // counterparts (same datapath micro-architecture in slot 1 and slot 2 per
    // spec §slot architecture — both slots host a full 32/64-bit ALU + MAC
    // datapath with the same acc-feedback path). Only the reserved FU changes:
    // SLOT2 instead of SLOT1. These are the itineraries the FLEX `_S2`
    // classes attach to.
    InstrItinData<Slot2_ALU, [InstrStage<1, [SLOT2]>], [1]>,
    InstrItinData<Slot2_LS,  [InstrStage<1, [SLOT2]>], [2]>,
    InstrItinData<Slot2_ALU_AccLat, [InstrStage<1, [SLOT2]>], [5, 1, 1, 5]>,
    InstrItinData<Slot2_MAC, [InstrStage<1, [SLOT2]>], [2, 1, 1, 2]>
  ])>;

//===----------------------------------------------------------------------===//
// Scheduling Machine Model
//===----------------------------------------------------------------------===//

def HaydnSchedModel : SchedMachineModel {
  // Max issue per cycle == bundle width (3 slots)
  let IssueWidth = 3;
  let Itineraries = HaydnItineraries;
  // ISA §55 / Slot0_LS OperandCycles [2]: load result available 2 bundles
  // after issue (see Slot0_LS comment).
  let LoadLatency = 2;
  // ISA spec §Branch Penalty: a taken branch flushes the 2 already-fetched
  // bundles (static not-taken prediction). Also consumed by generic
  // EarlyIfConversion : CritLimit = MispredictPenalty/2. With the
  // previous value of 2, CritLimit was 1 and pure PHI triangles
  // (ResLength=2, MinCrit=0) were rejected ("Not enough available ILP")
  // so EarlyIfConv never formed MOVT/MOVF and GenMux Pattern 2 tried to
  // do it post-RA (and broke CFG). Raise to 8 → CritLimit 4 so small
  // diamond/triangle cmovs convert on SSA. Still cost metadata only for
  // the VLIW post-RA scheduler (no hazard-recognizer blocking). /.
  let MispredictPenalty = 8;
  // No complete model yet - itineraries only
  let CompleteModel = 0;
  // Enable the upstream PostMachineScheduler gate so the pass actually runs
  // and delegates to createPostMachineScheduler (-> createHaydnPostRAVLIWScheduler).
  // Without this, TargetSubtargetInfo::enablePostRAMachineScheduler returns
  // false (it ANDs enableMachineScheduler && enablePostRAScheduler, and the
  // latter reads this bit). The pass then short-circuits at
  // MachineScheduler.cpp:714 ("Subtarget disables post-MI-sched.") and
  // HaydnConvergingVLIWScheduler is NEVER instantiated — making the entire
  // post-RA VLIW scheduler, including the ResourceDemand heuristic, dead
  // code. See. ARM M4/M55 and AIE2 all set PostRAScheduler = 1.
  // Haydn's own targetSchedulesPostRAScheduling=true suppresses the
  // DUPLICATE upstream PostMachineScheduler; this bit only opens the gate.
  let PostRAScheduler = 1;
}
