# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_format_e_records.py --check --family e96 | FileCheck %s
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfoManual.td --check-prefix=MANUAL
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmMatcher.td --check-prefix=MATCH
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.td --check-prefix=INFO
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFormatE.td --check-prefix=FORMATE
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFamilies.td --check-prefix=FAMILY
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# Matcher root stays HaydnGeneric.td only. Manual.td stays a 0-def tombstone.
# Dual-dest MAC ties and LS WITH lane stores remain HaydnInst shells in
# HaydnInstrInfo.td (no hypothesized Inst bits). Product encode is Format E
# members. E96 FamilyID=0 only; MF0 stays inert.
#
# CHECK-DAG: OK matcher-root collapse
# CHECK-DAG: OK Manual.td tombstone
# CHECK-DAG: OK residual hand logicals
#
# MANUAL: 0-def tombstone
# MANUAL: Do not include it
# MANUAL: Matcher root stays HaydnGeneric.td only
# MANUAL-NOT: {{^}}def
#
# MATCH: include "HaydnGeneric.td"
# MATCH-NOT: include "HaydnFormatE.td"
# MATCH-NOT: include "HaydnFamilies.td"
# MATCH-NOT: include "HaydnInstrInfoManual.td"
#
# INFO: def D_SW_L_WITH_IMM : HaydnInst
# INFO: def D_SW_H_WITH_IMM : HaydnInst
# INFO: def X2MULA32 : HaydnInst
# INFO-NOT: FmtLaneStore
# INFO-NOT: include "HaydnInstrInfoManual.td"
#
# FORMATE-DAG: include "HaydnFormatsE96Members.td.inc"
# FORMATE-DAG: Matcher root (HaydnAsmMatcher.td) must not include this file
# FORMATE-NOT: HaydnInstrInfoManual
#
# FAMILY-DAG: HaydnFamilyE96
# FAMILY-DAG: Matcher root must not
# FAMILY-NOT: HaydnFamilyMF0
