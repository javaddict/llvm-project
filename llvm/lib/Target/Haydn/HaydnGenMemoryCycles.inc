//===-- HaydnGenMemoryCycles.inc - MemoryCycle lookup -*- C++ -*-===//
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
// Overlay of AIE MemInstrItinData (AIETarget.td:22-47) and
// AIEMemoryCyclesEmitter.cpp:123-157 / AIE2InstrInfo.cpp:53.
// Published Slot0_LS / Slot1_LD / Slot01_LD / Slot2_LS. first=0
// (issue), last=Data_Latency-1 from the latency-2 scaffold. Not a
// per-op invent.
//
//===----------------------------------------------------------------------===//

#ifndef GET_HAYDN_FAMILY_SCHED
std::optional<int>
HaydnInstrInfo::getFirstMemoryCycle(unsigned SchedClass) const {
  switch (SchedClass) {
  default: return {};
  case Haydn::Sched::Slot0_LS: return 0; // Slot0_LS
  case Haydn::Sched::Slot1_LD: return 0; // Slot1_LD
  case Haydn::Sched::Slot01_LD: return 0; // Slot01_LD
  case Haydn::Sched::Slot2_LS: return 0; // Slot2_LS
  }
}

int HaydnInstrInfo::getMinFirstMemoryCycle() const {
  return 0;
}

int HaydnInstrInfo::getMaxFirstMemoryCycle() const {
  return 0;
}

std::optional<int>
HaydnInstrInfo::getLastMemoryCycle(unsigned SchedClass) const {
  switch (SchedClass) {
  default: return {};
  case Haydn::Sched::Slot0_LS: return 1; // Slot0_LS
  case Haydn::Sched::Slot1_LD: return 1; // Slot1_LD
  case Haydn::Sched::Slot01_LD: return 1; // Slot01_LD
  case Haydn::Sched::Slot2_LS: return 1; // Slot2_LS
  }
}

int HaydnInstrInfo::getMinLastMemoryCycle() const {
  return 1;
}

int HaydnInstrInfo::getMaxLastMemoryCycle() const {
  return 1;
}
#endif // GET_HAYDN_FAMILY_SCHED

#ifdef GET_HAYDN_FAMILY_SCHED
// Family-scoped slot/coissue law (E96). Shared Unit order matches
// Constraints.md and HaydnItineraries FuncUnits. Entry capacities
// are the same FormatEE2/E3 facts as HaydnGenSchedRecords.inc.
// Included from llvm::haydn::format_e in HaydnFormatERecords.h.
inline constexpr unsigned GeneratedFamilyE2EntryCapacity = 2;
inline constexpr unsigned GeneratedFamilyE3EntryCapacity = 3;
inline constexpr const char *GeneratedFamilySharedUnits[] = {
    "LOADSTORE0",
    "LOAD1",
    "ALU0",
    "ALU1",
    "ALU2",
    "MAC0",
    "MAC1"
};
#undef GET_HAYDN_FAMILY_SCHED
#endif
