# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_sched_records.py --check
# RUN: FileCheck %s --check-prefix=SCHED --input-file=%S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: FileCheck %s --check-prefix=GEN --input-file=%S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'def Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'def Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Slot12_MAC_AccFirst' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F '[5, 1, 1, 5]' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: generated itinerary rows replace hand InstrItinData.
#
# Bug: HaydnSchedule.td carried 12 live hand itinerary classes plus three
# unreferenced 5-cycle accumulator-latency classes of unknown provenance.
# There was no golden-hash-pinned generator for the published aggregate
# itineraries. CompleteModel stayed 0 (correct) but the rows were hand-copied.
# Fix: FormatE/generate_sched_records.py emits HaydnGenSchedRecords.inc from
# the same golden hashes as the Format E importer, Constraints.md Shared Unit
# names, Data_Latency 1/2, and SIN_COS/ARCTAN uimm4+2 conservative dest bound
# 17. Hand InstrItinData is gone. The AccLat class defs are deleted.
# CompleteModel stays 0. Per-op admission is not flipped.
# If this regresses: hand itinerary data or AccLat classes return, or --check
# stops catching a stale generated file.
#
# Role: generator-check — published itinerary contract (NOT per-op import,
# NOT CompleteModel=1, NOT member SchedClass attachment).
#
# SCHED-NOT: def Slot12_ALU_AccLat
# SCHED-NOT: def Slot2_ALU_AccLat
# SCHED-NOT: InstrItinData<
# SCHED: include "HaydnGenSchedRecords.inc"
# SCHED: let CompleteModel = 0
# SCHED-NOT: let CompleteModel = 1
#
# GEN: Auto-generated
# GEN: ProcessorItineraries
# GEN: InstrItinData<Slot0_ALU
# GEN-NOT: Slot12_ALU_AccLat
# GEN-NOT: Slot2_ALU_AccLat
# GEN-NOT: [5, 1, 1, 5]
