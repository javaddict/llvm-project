# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_sched_records.py --check
# RUN: FileCheck %s --check-prefix=PORT --input-file=%S/../../../lib/Target/Haydn/HaydnPortModel.h
# RUN: FileCheck %s --check-prefix=RC --input-file=%S/../../../lib/Target/Haydn/HaydnResourceCycle.h
# RUN: FileCheck %s --check-prefix=MEM --input-file=%S/../../../lib/Target/Haydn/HaydnGenMemoryCycles.inc
# RUN: FileCheck %s --check-prefix=GEN --input-file=%S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: FileCheck %s --check-prefix=II --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: FileCheck %s --check-prefix=SCHED --input-file=%S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: FileCheck %s --check-prefix=BUDGET --input-file=%S/../../../lib/Target/Haydn/HaydnBundlePortBudget.h
# RUN: FileCheck %s --check-prefix=PACK --input-file=%S/../../../lib/Target/Haydn/HaydnPackLegality.h
# RUN: FileCheck %s --check-prefix=MUT --input-file=%S/../../../lib/Target/Haydn/HaydnSchedMutations.cpp
# RUN: FileCheck %s --check-prefix=PORT2 --input-file=%S/../../../lib/Target/Haydn/HaydnPortModel.h
# RUN: not grep -F 'def Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'def Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnSchedule.td
# RUN: not grep -F 'Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# REQUIRES: haydn-golden-canonical
# RUN: not grep -F 'Slot2_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Slot12_ALU_AccLat' %S/../../../lib/Target/Haydn/HaydnGenMemoryCycles.inc
# RUN: not grep -F '[5, 1, 1, 5]' %S/../../../lib/Target/Haydn/HaydnGenSchedRecords.inc
# RUN: not grep -F 'Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS' %S/../../../lib/Target/Haydn/HaydnResourceCycle.h
# RUN: not grep -F 'Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS' %S/../../../lib/Target/Haydn/HaydnPortModel.h
# RUN: not grep -F 'case Slot0_LS:' %S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: not grep -F 'HAYDN_LOAD_LATENCY_SCAFFOLD) - 1' %S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp
# RUN: %python -c "from pathlib import Path; r=Path(r'%S/../../../lib/Target/Haydn').resolve(); n='return 0; // Slot0_LS'; hits=[p.name for p in r.glob('HaydnGen*.inc') if n in p.read_text(encoding='utf-8')]; assert hits==['HaydnGenMemoryCycles.inc'], hits"
# RUN: %python -c "from pathlib import Path; r=Path(r'%S/../../../lib/Target/Haydn').resolve(); n='Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS'; hits=sorted(p.name for p in r.glob('*') if p.suffix in ('.h','.cpp','.inc') and n in p.read_text(encoding='utf-8', errors='ignore')); assert hits==[], hits"
# RUN: %python -c "from pathlib import Path; r=Path(r'%S/../../../lib/Target/Haydn').resolve(); n='Log == Haydn::ARCTAN || Log == Haydn::SIN_COS'; hits=sorted(p.name for p in r.glob('*') if p.suffix in ('.h','.cpp','.inc') and n in p.read_text(encoding='utf-8', errors='ignore')); assert hits==['HaydnPortModel.h'], hits"
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
# PORT: Every explicit operand field
# PORT: MOVE32 rd,rs,rs is 2R1W
# PORT: haydnClassifySoloIssueOpcode
# PORT: logicalOpcodeOrSelf
# PORT: Log == Haydn::ARCTAN || Log == Haydn::SIN_COS
# PORT: inline bool haydnOpcodeIssuesAloneInCycle
# PORT: haydnClassifySoloIssueOpcode
# PORT: HAYDN_SINCOS_OCCUPANCY_BIAS = 2
# PORT: haydnSinCosWindowOccupancyFromImm
# PORT: haydnSinCosArctanWindowLaw
# PORT: haydnMemoryObjectWaitCyclesAdmitted
# PORT: Each explicit operand field reserves one port
# PORT: Per-field: every explicit GPR
# PORT: Logical MOVE32 rd, rs, rs is 2R1W
#
# RC: SFR 2R/1W
# RC: haydnChargeDescNamedSfrPorts
# RC: HaydnMove32ClassMiRepeatedSrcGprReads = 2
# RC: inline bool isHaydnSMSAloneOpcode
# RC: return haydnOpcodeIssuesAloneInCycle(Opcode)
# RC-NOT: Opcode == Haydn::ARCTAN || Opcode == Haydn::SIN_COS
#
# MEM: DO NOT EDIT
# MEM: Generator: llvm/lib/Target/Haydn/FormatE/generate_sched_records.py
# MEM: HaydnInstrInfo::getFirstMemoryCycle
# MEM: case Haydn::Sched::Slot0_LS: return 0; // Slot0_LS
# MEM: case Haydn::Sched::Slot1_LD: return 0; // Slot1_LD
# MEM: case Haydn::Sched::Slot01_LD: return 0; // Slot01_LD
# MEM: case Haydn::Sched::Slot2_LS: return 0; // Slot2_LS
# MEM: HaydnInstrInfo::getLastMemoryCycle
# MEM: case Haydn::Sched::Slot0_LS: return 1; // Slot0_LS
# MEM: case Haydn::Sched::Slot1_LD: return 1; // Slot1_LD
# MEM: case Haydn::Sched::Slot01_LD: return 1; // Slot01_LD
# MEM: case Haydn::Sched::Slot2_LS: return 1; // Slot2_LS
# MEM-NOT: Slot12_ALU_AccLat
# MEM-NOT: Slot2_ALU_AccLat
#
# GEN: DO NOT EDIT
# GEN: Generator: llvm/lib/Target/Haydn/FormatE/generate_sched_records.py
# GEN: MemoryCycle first/last literals live in HaydnGenMemoryCycles.inc
# GEN: defvar FormatEE2EntryCapacity = 2;
# GEN: defvar FormatEE3EntryCapacity = 3;
# GEN-NOT: return 0; // Slot0_LS
# GEN-NOT: Slot12_ALU_AccLat
# GEN-NOT: Slot2_ALU_AccLat
# GEN-NOT: [5, 1, 1, 5]
# GEN-NOT: CompleteModel = 1
#
# II: HaydnGenMemoryCycles.inc
# II: assert(isSoftZeroR0Clean
# II: withDR64PackBase: soft-zero R0 must be clean before pack-base MatInt
# II-NOT: case Slot0_LS:
#
# SCHED: include "HaydnGenSchedRecords.inc"
# SCHED: let IssueWidth = FormatEE3EntryCapacity
# SCHED: let CompleteModel = 0
# SCHED-NOT: let CompleteModel = 1
# SCHED-NOT: def Slot12_ALU_AccLat
# SCHED-NOT: def Slot2_ALU_AccLat
#
# BUDGET: one-commit-site family
# BUDGET: haydnCycleMembersExceedPortBudget
# BUDGET: haydnVerifyCommittedBundlePortBudget
# BUDGET: haydnCycleMembersExceedPortBudget
#
# PACK: FormatEE2EntryCapacity = 2
# PACK: FormatEE3EntryCapacity = 3
# PACK: cycleExceedsSharedPortBudget
# PACK: haydnCycleMembersExceedPortBudget
# PACK: haydnSinCosArctanWindowLaw
# PACK: cycleViolatesNamedSameCycleLaws
# PACK: cycleViolatesSinCosWindow
#
# MUT: haydn-postra-region-end-edges", cl::init(false)
# MUT: haydn-postra-interblock", cl::init(false)
# MUT: haydn-postra-waw-edges", cl::init(false)
# MUT: constexpr bool ExactLatencies = true
# MUT: class MaxLatencyFinder
# MUT: IncludeStages(!EnableHaydnPostRAInterBlock
# MUT: haydnIsSimplifiableReservedReg
# MUT: EnableHaydnPostRAInterBlock
#
# PORT2: HAYDN_SINCOS_OCCUPANCY_MAX
# PORT2: haydnSinCosArctanWindowLaw
# PORT2: HAYDN_NAMED_SAME_CYCLE_LAWS_TAG
# PORT2: haydnIsSimplifiableReservedReg
