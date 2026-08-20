# RUN: %python %S/../../../lib/Target/Haydn/FormatE/family_core.py --check --family e96
# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_sched_records.py --check
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# Family-scoped slot/coissue law is generated (Shared Units + E2/E3
# capacities) and consumed through getFamilyRecords. --check diffs the
# committed HaydnGenMemoryCycles.inc family-sched section. Matcher root
# stays HaydnGeneric.td only. Per-op resource admission stays closed.

# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnGenMemoryCycles.inc \
# RUN:   --check-prefix=GEN
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFormatERecords.h \
# RUN:   --check-prefix=API
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSchedule.td \
# RUN:   --check-prefix=MODEL
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmMatcher.td \
# RUN:   --check-prefix=MATCH

# GEN: GeneratedFamilyE2EntryCapacity = 2
# GEN: GeneratedFamilyE3EntryCapacity = 3
# GEN: "LOADSTORE0"
# GEN: "MAC1"
# API: getFamilyRecords
# API: GeneratedFamilySharedUnits
# MODEL: CompleteModel = 0
# MODEL-NOT: CompleteModel = 1
# MATCH: include "HaydnGeneric.td"
# MATCH-NOT: include "HaydnFormatE.td"
# MATCH-NOT: include "HaydnFamilies.td"
# MATCH-NOT: include "HaydnInstrInfoManual.td"
