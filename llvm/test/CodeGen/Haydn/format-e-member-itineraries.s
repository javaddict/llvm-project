# The importer --check gate lives in format-e-records-check.s (gated on the
# haydn-golden-canonical feature — the canonical ledger exists only on
# the plans machine). This test keeps the environment-free content
# checks against the committed generated files.
# RUN: FileCheck %s --check-prefix=LOAD --input-file=%S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# RUN: FileCheck %s --check-prefix=STORE --input-file=%S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# RUN: FileCheck %s --check-prefix=BEQ --input-file=%S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# RUN: FileCheck %s --check-prefix=SINCOS --input-file=%S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# RUN: FileCheck %s --check-prefix=FLAGS --input-file=%S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# RUN: not grep -F 'let isCodeGenOnly = 0, isAsmParserOnly = 0, hasSideEffects = 1 in' %S/../../../lib/Target/Haydn/HaydnFormatsE96Members.td.inc
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: member itinerary attachment and load/store/branch flags.
#
# Bug: generated E2/E3 members were emitted under a file-wide
# `hasSideEffects = 1` let with no Itinerary=, so every member opcode had
# SchedClass 0 and no mayLoad/mayStore/isBranch. Activating members in place
# of residual `_S*` opcodes would zero post-commit scheduling, branch
# analysis, and LatencyStalls at once.
# Fix: generate_format_e_records.py wraps each member in a per-def let that
# maps the generated unit onto a published itinerary class (ALU0->Slot0_ALU,
# LOADSTORE0->Slot0_LS, LOAD1->Slot1_LD, MAC0->Slot1_MAC, MAC1->Slot2_MAC,
# SIN_COS/ARCTAN->Slot*_ALU_SinCosLat) and classifies mayLoad/mayStore/
# isBranch/isTerminator/hasSideEffects from golden type/unit/logical.
# If this regresses: members lose Itinerary= (SchedClass 0), a load loses
# mayLoad, a store loses mayStore, BEQ loses isBranch, or the blanket
# hasSideEffects wrapper returns.
#
# Role: generator-check — member itinerary+flag contract (NOT CompleteModel=1,
# NOT per-op admission, NOT `_S*` retirement).
#
# Pairing of these defs with the preceding let (mayLoad/mayStore/isBranch/
# Itinerary) is checked by generate_format_e_records.py --check.
#
# LOAD: def D_LW_POST_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<(outs DR64:$dest1_0, GPR32:$dest2_wb), (ins GPR32:$dest2_1, simm6:$imm_2)
# STORE: def S_SW_POST_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<(outs GPR32:$dest2_wb), (ins GPR32:$dest1_0, GPR32:$dest2_1, simm6:$imm_2)
# BEQ: def BEQ_E2_E0_ALU0_RI12 : HaydnEntryE2E0
# SINCOS: Itinerary = Slot1_ALU_SinCosLat
# SINCOS: def SIN_COS_E3_E1_ALU1_RI4 : HaydnEntryE3E1
# FLAGS: mayLoad = 1
# FLAGS: mayStore = 1
# FLAGS: isBranch = 1
# FLAGS-NOT: let isCodeGenOnly = 0, isAsmParserOnly = 0, hasSideEffects = 1 in
