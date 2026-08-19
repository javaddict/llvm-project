//===-- HaydnGenSchedRecords.inc - published itineraries -*- tablegen -*-===//
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
// Published aggregate itineraries only: seven Shared Units, Data_Latency
// 1/2 scaffolds, and SIN_COS/ARCTAN conservative dest bound (uimm4_max+2).
// MemoryCycle first/last literals live in HaydnGenMemoryCycles.inc
// (AIE MemInstrItinData / AIEMemoryCyclesEmitter peer), not here.
// Per-operation port/latency/pipeline tables are not imported.
// CompleteModel stays 0 in HaydnSchedule.td.
// Generated Format E entry capacities (ProductRows EntryCount):
// E2=2 / E3=3. HaydnSchedModel.IssueWidth binds E3. Not a
// competitive invent and not a second 2/3 literal at sched sites.
//
//===----------------------------------------------------------------------===//

// Generated ProductRows EntryCount. One fact for E2=2 / E3=3.
defvar FormatEE2EntryCapacity = 2;
defvar FormatEE3EntryCapacity = 3;

// Itinerary= alias for FmtALU64Acc. OperandCycles use only 1/2.
def Slot12_MAC_AccFirst : InstrItinClass;
// Per-slot AccFirst for committed accumulator members (CB-152c).
def Slot1_MAC_AccFirst : InstrItinClass;
def Slot2_MAC_AccFirst : InstrItinClass;

def HaydnItineraries : ProcessorItineraries<
  [LOADSTORE0, LOAD1, ALU0, ALU1, ALU2, MAC0, MAC1],
  [],
  [
    InstrItinData<Slot0_ALU, [InstrStage<1, [ALU0]>], [1]>,
    InstrItinData<Slot0_LS, [InstrStage<1, [LOADSTORE0]>], [2]>,
    InstrItinData<Slot12_ALU, [InstrStage<1, [ALU1, ALU2]>], [1]>,
    InstrItinData<Slot12_ALU_SinCosLat, [InstrStage<1, [ALU1, ALU2]>], [17]>,
    InstrItinData<Slot012_ALU, [InstrStage<1, [ALU0, ALU1, ALU2]>], [1]>,
    InstrItinData<Slot1_LD, [InstrStage<1, [LOAD1]>], [2]>,
    InstrItinData<Slot01_LD, [InstrStage<1, [LOADSTORE0, LOAD1]>], [2]>,
    InstrItinData<Slot1_ALU, [InstrStage<1, [ALU1]>], [1]>,
    InstrItinData<Slot1_ALU_SinCosLat, [InstrStage<1, [ALU1]>], [17]>,
    InstrItinData<Slot12_MAC, [InstrStage<1, [MAC0, MAC1]>], [2, 1, 1, 2]>,
    InstrItinData<Slot12_MAC_AccFirst, [InstrStage<1, [MAC0, MAC1]>], [2, 2, 1, 1]>,
    InstrItinData<Slot1_MAC, [InstrStage<1, [MAC0]>], [2, 1, 1, 2]>,
    InstrItinData<Slot2_ALU, [InstrStage<1, [ALU2]>], [1]>,
    InstrItinData<Slot2_LS, [InstrStage<1, [ALU2]>], [2]>,
    InstrItinData<Slot2_ALU_SinCosLat, [InstrStage<1, [ALU2]>], [17]>,
    InstrItinData<Slot2_MAC, [InstrStage<1, [MAC1]>], [2, 1, 1, 2]>,
    InstrItinData<Slot1_MAC_AccFirst, [InstrStage<1, [MAC0]>], [2, 2, 1, 1]>,
    InstrItinData<Slot2_MAC_AccFirst, [InstrStage<1, [MAC1]>], [2, 2, 1, 1]>
  ]>;
