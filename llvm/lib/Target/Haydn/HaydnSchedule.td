//===-- HaydnSchedule.td - Haydn Scheduling -----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pipeline scheduling model for the Haydn Format E VLIW DSP.
// Resource identity is the seven execution units (not positional slots).
// Entry selection is format legality, not a processor resource bit.
//
// Pooled register-file ports (GPR/DR/AR/SFR) are NOT FuncUnits here — they
// are counting budgets enforced by HaydnPortModel + HaydnFuncUnitWrapper.
//
// Per-instruction port vectors / full Data_Latency tables are not product-
// qualified until golden admits them into generated records. Existing
// itinerary class names remain as scaffolding attachment points; InstrStage
// resources below are the seven units only. InstrItinData rows are generated
// by FormatE/generate_sched_records.py (HaydnGenSchedRecords.inc).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Functional Units — seven Format E execution units
//
// Order is load-bearing for InstrStage unit bit indices and
// HAYDN_NUM_FU_BITS / HaydnExecUnit in HaydnHazardRecognizer.h:
//   0 LOADSTORE0, 1 LOAD1, 2 ALU0, 3 ALU1, 4 ALU2, 5 MAC0, 6 MAC1
//
// Injectivity: multiple entries in one bundle must not map to the same unit.
// ALU0 and LOADSTORE0 are independent units; co-issue is legal when mapped
// to different entries (E3 product geometry).
//===----------------------------------------------------------------------===//

def LOADSTORE0 : FuncUnit;
def LOAD1      : FuncUnit;
def ALU0       : FuncUnit;
def ALU1       : FuncUnit;
def ALU2       : FuncUnit;
def MAC0       : FuncUnit;
def MAC1       : FuncUnit;

//===----------------------------------------------------------------------===//
// Itinerary Classes (scaffold class names; unit stages are product resources)
//
// Class identifiers retain historical Slot* names because instruction .td
// files attach by those names. Stages reserve execution units, not slots.
// InstrItinData for these names is generated (HaydnGenSchedRecords.inc).
// The two unreferenced 5-cycle accumulator-latency classes are deleted.
//===----------------------------------------------------------------------===//

def PSEUDO       : InstrItinClass;
def Slot0_ALU    : InstrItinClass;  // control / scalar ALU scaffold → ALU0
def Slot0_LS     : InstrItinClass;  // load/store → LOADSTORE0
def Slot12_ALU   : InstrItinClass;  // wide ALU → ALU1|ALU2
// SIN_COS / ARCTAN: Constraints Data_Latency = uimm4+2. uimm4 is 4-bit, so
// the conservative bound is 15+2 = 17. Exact operand-dependent windows wait
// for a generated per-op import. InstrStage stays 1 cycle:
// ProductMaxInstrStageCycles=1; the uimm4+2 unit-occupancy lock is class-3
// residual (ProductDraftArctanMultiCycleLockEnabled=false), not this class.
def Slot12_ALU_SinCosLat : InstrItinClass; // logical SIN_COS/ARCTAN → ALU1|ALU2
def Slot012_ALU  : InstrItinClass;  // any ALU → ALU0|ALU1|ALU2
def Slot1_LD     : InstrItinClass;  // load unit → LOAD1
def Slot01_LD    : InstrItinClass;  // dual-load menu → LOADSTORE0|LOAD1
def Slot1_ALU    : InstrItinClass;  // → ALU1
def Slot1_ALU_SinCosLat : InstrItinClass; // SIN_COS_S1 / ARCTAN_S1 → ALU1
def Slot2_ALU    : InstrItinClass;  // → ALU2
// Slot 2 memory forms: Data_Latency 2. Must NOT borrow Slot2_ALU (latency 1).
// Haydn has no interlock — wrong latency here is a silent silicon hazard.
def Slot2_LS     : InstrItinClass;  // → ALU2 (S2 pack seat), mem Data_Latency 2
def Slot2_MAC    : InstrItinClass;  // → MAC1
def Slot2_ALU_SinCosLat : InstrItinClass; // SIN_COS_S2 / ARCTAN_S2 → ALU2
def Slot12_MAC   : InstrItinClass;  // → MAC0|MAC1
def Slot1_MAC    : InstrItinClass;  // → MAC0

// Generated ProcessorItineraries (AIE peer: AIE2PGenSchedule.td
// ProcessorItineraries + AIETarget.td MemInstrItinData).
include "HaydnGenSchedRecords.inc"

//===----------------------------------------------------------------------===//
// Scheduling Machine Model
//===----------------------------------------------------------------------===//

def HaydnSchedModel : SchedMachineModel {
  // Max issue per cycle == max Format E entries (E3).
  let IssueWidth = 3;
  let Itineraries = HaydnItineraries;
  // Common load Data_Latency scaffold (OperandCycles [2] on load classes).
  let LoadLatency = 2;
  // EarlyIfConversion CritLimit = MispredictPenalty/2. Product MOVT/MOVF
  // diamonds need CritLimit >= condition latency (compare is 1–2 cycles).
  // Architectural taken-branch flush is Branch_Penalty=2 in golden/constraints
  // — that is *not* this field; starving if-conversion with MispredictPenalty=2
  // (CritLimit=1) rejects every diamond (condition adds 2). Keep 8 as product
  // if-conversion budget until a dedicated branch-penalty field is plumbed.
  let MispredictPenalty = 8;
  // Incomplete until golden admits complete per-op port/latency/pipeline
  // tables into generated records (PortModel admission helper stays false).
  // Aggregate unit itineraries + pooled PortModel ceilings remain product-safe
  // and are carried by the shared availability-aware record (IssueWidth /
  // NumExecUnits / LoadLatency scaffold + bank port budgets).
  let CompleteModel = 0;
  // Open the post-RA MachineScheduler gate (Haydn owns the strategy).
  let PostRAScheduler = 1;
}
