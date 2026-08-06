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
// qualified until the restored golden instruction-type index lands. Existing
// itinerary class names remain as scaffolding attachment points; InstrStage
// resources below are the seven units only.
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
//===----------------------------------------------------------------------===//

def PSEUDO       : InstrItinClass;
def Slot0_ALU    : InstrItinClass;  // control / scalar ALU scaffold → ALU0
def Slot0_LS     : InstrItinClass;  // load/store → LOADSTORE0
def Slot12_ALU   : InstrItinClass;  // wide ALU → ALU1|ALU2
def Slot12_ALU_AccLat : InstrItinClass;
def Slot012_ALU  : InstrItinClass;  // any ALU → ALU0|ALU1|ALU2
def Slot1_LD     : InstrItinClass;  // load unit → LOAD1
def Slot01_LD    : InstrItinClass;  // dual-load menu → LOADSTORE0|LOAD1
def Slot1_ALU    : InstrItinClass;  // → ALU1
def Slot2_ALU    : InstrItinClass;  // → ALU2
def Slot2_MAC    : InstrItinClass;  // → MAC1
def Slot2_ALU_AccLat : InstrItinClass;
def Slot12_MAC   : InstrItinClass;  // → MAC0|MAC1
def Slot12_MAC_AccFirst : InstrItinClass;
def Slot1_MAC    : InstrItinClass;  // → MAC0

//===----------------------------------------------------------------------===//
// Processor Itineraries (DFA automaton over the seven units)
//
// Single-stage occupancy for one issue cycle. OperandCycles retain existing
// latency scaffolding (loads 2; common ALU 1; MAC acc-feedback shape). Full
// per-op qualification waits on restored golden latency tables.
//===----------------------------------------------------------------------===//

def HaydnItineraries : ProcessorItineraries<
  [LOADSTORE0, LOAD1, ALU0, ALU1, ALU2, MAC0, MAC1],
  [],
  [
    InstrItinData<PSEUDO, []>,

    // Scalar/control ALU scaffold on ALU0 (independent of LOADSTORE0).
    InstrItinData<Slot0_ALU, [InstrStage<1, [ALU0]>], [1]>,
    // Load/Store unit; load-use latency 2 (common Data_Latency scaffold).
    InstrItinData<Slot0_LS,  [InstrStage<1, [LOADSTORE0]>], [2]>,

    InstrItinData<Slot12_ALU, [InstrStage<1, [ALU1, ALU2]>], [1]>,
    InstrItinData<Slot12_ALU_AccLat, [InstrStage<1, [ALU1, ALU2]>],
                  [5, 1, 1, 5]>,

    // Any of the three ALU units.
    InstrItinData<Slot012_ALU, [InstrStage<1, [ALU0, ALU1, ALU2]>], [1]>,

    InstrItinData<Slot1_LD, [InstrStage<1, [LOAD1]>], [2]>,
    InstrItinData<Slot01_LD, [InstrStage<1, [LOADSTORE0, LOAD1]>], [2]>,

    InstrItinData<Slot1_ALU, [InstrStage<1, [ALU1]>], [1]>,

    // MAC units: internal acc feedback vs regfile writeback in OperandCycles.
    InstrItinData<Slot12_MAC, [InstrStage<1, [MAC0, MAC1]>], [2, 1, 1, 2]>,
    InstrItinData<Slot12_MAC_AccFirst, [InstrStage<1, [MAC0, MAC1]>],
                  [2, 2, 1, 1]>,
    InstrItinData<Slot1_MAC, [InstrStage<1, [MAC0]>], [2, 1, 1, 2]>,

    InstrItinData<Slot2_ALU, [InstrStage<1, [ALU2]>], [1]>,
    InstrItinData<Slot2_ALU_AccLat, [InstrStage<1, [ALU2]>], [5, 1, 1, 5]>,
    InstrItinData<Slot2_MAC, [InstrStage<1, [MAC1]>], [2, 1, 1, 2]>
  ]>;

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
  // Incomplete until golden per-op port/latency tables are restored.
  let CompleteModel = 0;
  // Open the post-RA MachineScheduler gate (Haydn owns the strategy).
  let PostRAScheduler = 1;
}
