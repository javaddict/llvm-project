# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_sched_records.py --check
# RUN: FileCheck %s --check-prefix=PORT --input-file=%S/../../../lib/Target/Haydn/HaydnPortModel.h
# RUN: FileCheck %s --check-prefix=RC --input-file=%S/../../../lib/Target/Haydn/HaydnResourceCycle.h
# RUN: FileCheck %s --check-prefix=MEM --input-file=%S/../../../lib/Target/Haydn/HaydnGenMemoryCycles.inc
# RUN: FileCheck %s --check-prefix=GEN --input-file=%S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: FileCheck %s --check-prefix=II --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: FileCheck %s --check-prefix=SCHED --input-file=%S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'def Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'def Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenMemoryCycles.inc
# RUN: not grep -F '[5, 1, 1, 5]' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS' %S/../../../lib/Target/Haydn/HaydnResourceCycle.h
# RUN: not grep -F 'case Slot0_LS:' %S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: not grep -F 'HAYDN_LOAD_LATENCY_SCAFFOLD) - 1' %S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: %python -c "from pathlib import Path; r=Path(r'%S/../../../lib/Target/Haydn').resolve(); n='return 0; // Slot0_LS'; hits=[p.name for p in r.glob('HaydnGen*.inc') if n in p.read_text(encoding='utf-8')]; assert hits==['HaydnGenMemoryCycles.inc'], hits"
# RUN: %python -c "from pathlib import Path; r=Path(r'%S/../../../lib/Target/Haydn').resolve(); n='Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS'; hits=sorted(p.name for p in r.glob('*') if p.suffix in ('.h','.cpp','.inc') and n in p.read_text(encoding='utf-8', errors='ignore')); assert hits==['HaydnPortModel.h'], hits"
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: each resource-truth literal has one home.
#
# Bug: PortModel ceilings/alone list, ResourceCycle isHaydnSMSAloneOpcode,
# and HaydnInstrInfo getFirst/LastMemoryCycle each copied the same numbers.
# AccLat dead classes were a third itinerary home of unknown provenance.
# Fix: alone list stays only in haydnOpcodeIssuesAloneInCycle (PortModel);
# ResourceCycle wraps it. MemoryCycle first/last live only in generated
# HaydnGenMemoryCycles.inc (AIE MemInstrItinData / AIEMemoryCyclesEmitter
# peer). AccLat class names stay absent. CompleteModel stays 0.
# If this regresses: a second opcode list or hand memory-cycle switch
# returns, or MemoryCycle literals leak into a second generated file.
#
# Role: generator-check — single resource-truth home (NOT per-op import,
# NOT CompleteModel=1, NOT LatencyStalls merge).
#
# PORT: inline bool haydnOpcodeIssuesAloneInCycle
# PORT: Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS
#
# RC: inline bool isHaydnSMSAloneOpcode
# RC: return haydnOpcodeIssuesAloneInCycle(Opcode)
# RC-NOT: Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS
#
# MEM: Auto-generated
# MEM: HaydnInstrInfo::getFirstMemoryCycle
# MEM: case Haydn::Sched::Slot0_LS: return 0; // Slot0_LS
# MEM: case Haydn::Sched::Slot1_LD: return 0; // Slot1_LD
# MEM: case Haydn::Sched::Slot01_LD: return 0; // Slot01_LD
# MEM: HaydnInstrInfo::getLastMemoryCycle
# MEM: case Haydn::Sched::Slot0_LS: return 1; // Slot0_LS
# MEM: case Haydn::Sched::Slot1_LD: return 1; // Slot1_LD
# MEM: case Haydn::Sched::Slot01_LD: return 1; // Slot01_LD
# MEM-NOT: Slot12_ALU_AccLat
# MEM-NOT: Slot2_ALU_AccLat
#
# GEN: Auto-generated
# GEN: MemoryCycle first/last literals live in HaydnGenMemoryCycles.inc
# GEN-NOT: return 0; // Slot0_LS
# GEN-NOT: Slot12_ALU_AccLat
# GEN-NOT: Slot2_ALU_AccLat
# GEN-NOT: [5, 1, 1, 5]
#
# II: HaydnGenMemoryCycles.inc
# II-NOT: case Slot0_LS:
# II-NOT: HAYDN_LOAD_LATENCY_SCAFFOLD) - 1
#
# SCHED: include "HaydnGenSchedRecords.inc"
# SCHED: let CompleteModel = 0
# SCHED-NOT: let CompleteModel = 1
# SCHED-NOT: def Slot12_ALU_AccLat
# SCHED-NOT: def Slot2_ALU_AccLat
