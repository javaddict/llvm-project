//===-- HaydnGenSchedMcaBridge.td.inc - llvm-mca ItinRW bridge -*-===//
//
// DO NOT EDIT.
// Generator: llvm/lib/Target/Haydn/FormatE/generate_sched_records.py
// Schema: format-e-records/1
// Family: e96 (id=0)
// Check: python3 llvm/lib/Target/Haydn/FormatE/generate_sched_records.py --check --family e96
// Umbrella: llvm/lib/Target/Haydn/FormatE/check_generated.sh
// Overlay: authored_catalog_overlay.json
// Overlay-SHA256: 276dea569f8761e81bb3d17e041a4f74911d34ba6f30711939a293519ac531fc
//
// ---- M18 llvm-mca bridge (generated; ARM A9 ItinRW peer) ----
// llvm-mca needs a SchedRW model; ItinRW maps each published
// itinerary class to a SchedWrite whose WriteRes carries the same
// golden latency and reserves the same units (groups for menus).
// This is the mca/latency projection of the itineraries in
// HaydnGenSchedRecords.inc — not a second scheduling policy and
// not a completeness claim (the HaydnSchedModel polarity is
// untouched). Separate file because `let SchedModel =` in
// HaydnSchedule.td must follow the HaydnSchedModel def.
//
//===----------------------------------------------------------------------===//

let SchedModel = HaydnSchedModel in {

def HaydnResLOADSTORE0 : ProcResource<1>;
def HaydnResLOAD1 : ProcResource<1>;
def HaydnResALU0 : ProcResource<1>;
def HaydnResALU1 : ProcResource<1>;
def HaydnResALU2 : ProcResource<1>;
def HaydnResMAC0 : ProcResource<1>;
def HaydnResMAC1 : ProcResource<1>;

def HaydnUnitGrpA1A2 : ProcResGroup<[HaydnResALU1, HaydnResALU2]>;
def HaydnUnitGrpA0A1A2 : ProcResGroup<[HaydnResALU0, HaydnResALU1, HaydnResALU2]>;
def HaydnUnitGrpLSL1 : ProcResGroup<[HaydnResLOADSTORE0, HaydnResLOAD1]>;
def HaydnUnitGrpM0M1 : ProcResGroup<[HaydnResMAC0, HaydnResMAC1]>;

}

let SchedModel = HaydnSchedModel in {

def HW_Slot0_ALU : SchedWriteRes<[HaydnResALU0]> { let Latency = 1; }
def HW_Slot0_LS : SchedWriteRes<[HaydnResLOADSTORE0]> { let Latency = 2; }
def HW_Slot0_LS_WbLat : SchedWriteRes<[HaydnResLOADSTORE0]> { let Latency = 1; }
def HW_Slot12_ALU_SinCosLat : SchedWriteRes<[HaydnUnitGrpA1A2]> { let Latency = 17; }
def HW_Slot12_ALU_DspLat : SchedWriteRes<[HaydnUnitGrpA1A2]> { let Latency = 2; }
def HW_Slot1_ALU_DspLat : SchedWriteRes<[HaydnResALU1]> { let Latency = 2; }
def HW_Slot2_ALU_DspLat : SchedWriteRes<[HaydnResALU2]> { let Latency = 2; }
def HW_Slot012_ALU_CsrLat : SchedWriteRes<[HaydnUnitGrpA0A1A2]> { let Latency = 2; }
def HW_Slot0_ALU_CsrLat : SchedWriteRes<[HaydnResALU0]> { let Latency = 2; }
def HW_Slot1_ALU_CsrLat : SchedWriteRes<[HaydnResALU1]> { let Latency = 2; }
def HW_Slot2_ALU_CsrLat : SchedWriteRes<[HaydnResALU2]> { let Latency = 2; }
def HW_Slot012_ALU : SchedWriteRes<[HaydnUnitGrpA0A1A2]> { let Latency = 1; }
def HW_Slot1_LD : SchedWriteRes<[HaydnResLOAD1]> { let Latency = 2; }
def HW_Slot01_LD : SchedWriteRes<[HaydnUnitGrpLSL1]> { let Latency = 2; }
def HW_Slot1_ALU : SchedWriteRes<[HaydnResALU1]> { let Latency = 1; }
def HW_Slot1_ALU_SinCosLat : SchedWriteRes<[HaydnResALU1]> { let Latency = 17; }
def HW_Slot12_MAC : SchedWriteRes<[HaydnUnitGrpM0M1]> { let Latency = 2; }
def HW_Slot12_MAC_AccFirst : SchedWriteRes<[HaydnUnitGrpM0M1]> { let Latency = 2; }
def HW_Slot12_MAC_MulLat : SchedWriteRes<[HaydnUnitGrpM0M1]> { let Latency = 1; }
def HW_Slot1_MAC_MulLat : SchedWriteRes<[HaydnResMAC0]> { let Latency = 1; }
def HW_Slot2_MAC_MulLat : SchedWriteRes<[HaydnResMAC1]> { let Latency = 1; }
def HW_Slot1_MAC : SchedWriteRes<[HaydnResMAC0]> { let Latency = 2; }
def HW_Slot2_ALU : SchedWriteRes<[HaydnResALU2]> { let Latency = 1; }
def HW_Slot2_ALU_SinCosLat : SchedWriteRes<[HaydnResALU2]> { let Latency = 17; }
def HW_Slot2_MAC : SchedWriteRes<[HaydnResMAC1]> { let Latency = 2; }
def HW_Slot1_MAC_AccFirst : SchedWriteRes<[HaydnResMAC0]> { let Latency = 2; }
def HW_Slot2_MAC_AccFirst : SchedWriteRes<[HaydnResMAC1]> { let Latency = 2; }

def : ItinRW<[HW_Slot0_ALU, HW_Slot0_LS, HW_Slot0_LS_WbLat, HW_Slot12_ALU_SinCosLat, HW_Slot12_ALU_DspLat, HW_Slot1_ALU_DspLat],
  [Slot0_ALU,
  Slot0_LS,
  Slot0_LS_WbLat,
  Slot12_ALU_SinCosLat,
  Slot12_ALU_DspLat,
  Slot1_ALU_DspLat]>;
def : ItinRW<[HW_Slot2_ALU_DspLat, HW_Slot012_ALU_CsrLat, HW_Slot0_ALU_CsrLat, HW_Slot1_ALU_CsrLat, HW_Slot2_ALU_CsrLat, HW_Slot012_ALU],
  [Slot2_ALU_DspLat,
  Slot012_ALU_CsrLat,
  Slot0_ALU_CsrLat,
  Slot1_ALU_CsrLat,
  Slot2_ALU_CsrLat,
  Slot012_ALU]>;
def : ItinRW<[HW_Slot1_LD, HW_Slot01_LD, HW_Slot1_ALU, HW_Slot1_ALU_SinCosLat, HW_Slot12_MAC, HW_Slot12_MAC_AccFirst],
  [Slot1_LD,
  Slot01_LD,
  Slot1_ALU,
  Slot1_ALU_SinCosLat,
  Slot12_MAC,
  Slot12_MAC_AccFirst]>;
def : ItinRW<[HW_Slot12_MAC_MulLat, HW_Slot1_MAC_MulLat, HW_Slot2_MAC_MulLat, HW_Slot1_MAC, HW_Slot2_ALU, HW_Slot2_ALU_SinCosLat],
  [Slot12_MAC_MulLat,
  Slot1_MAC_MulLat,
  Slot2_MAC_MulLat,
  Slot1_MAC,
  Slot2_ALU,
  Slot2_ALU_SinCosLat]>;
def : ItinRW<[HW_Slot2_MAC, HW_Slot1_MAC_AccFirst, HW_Slot2_MAC_AccFirst],
  [Slot2_MAC,
  Slot1_MAC_AccFirst,
  Slot2_MAC_AccFirst]>;

} // SchedModel = HaydnSchedModel
