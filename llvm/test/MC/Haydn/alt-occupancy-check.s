# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_alt_occupancy.py --check --family e96 | FileCheck %s
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/FormatE/generate_alt_occupancy.py --check-prefix=MIN
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnGenAltOccupancy.inc --check-prefix=INC
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# Occupancy generated-vs-checked ratchet in the default owner gate.
# AlternateInsts is MultiSlot_Pseudo only (one ADD32_MSP row after
# LogicalMaterialize retirement). --check requires HaydnGenFormats.inc
# via HAYDN_GEN_FORMATS from llvm_obj_root so a 1-row universe cannot
# satisfy MIN_OCCUPANCY_ROWS 715.
#
# CHECK: OK occupancy not AlternateInsts-only
# CHECK: OK stale-flip detector
# CHECK: OK occupancy rows=
# CHECK: OK product occupancy LD32=0x3 LD64=0x3 ST32=0x1 ST64=0x1 SEXT32T64=0x7
#
# D1.22: masks come from the golden-cited exception table or verified
# class bases; the name-substring arm and X2/X4 overlay are gone. The
# NOT pins sit before the first positive match so FileCheck scans the
# whole file from position 0 for the deleted heuristics.
# MIN-NOT: "64" in name
# MIN-NOT: apply_x2x4_available_overlay
# MIN-NOT: SLOT0_CONTROL
# MIN-NOT: DR_ALU_EXTRA
# MIN: RESIDUAL_OCCUPANCY_EXCEPTIONS
# MIN: ITIN_CLASS_BASE
# MIN: MIN_OCCUPANCY_ROWS = 715
# MIN: residual occupancy is not name-derivable
# MIN: occupancy --check requires --formats HaydnGenFormats.inc
# MIN: HAYDN_GEN_FORMATS
#
# INC-DAG: { Haydn::LD32, 0x3,
# INC-DAG: { Haydn::LD64, 0x3,
# INC-DAG: { Haydn::ST32, 0x1,
# INC-DAG: { Haydn::ST64, 0x1,
# INC-DAG: { Haydn::SEXT32T64, 0x7,
# INC-DAG: { Haydn::NOP, 0x1,
# INC-DAG: { Haydn::WFI, 0x1,
# INC-DAG: { Haydn::ADD64, 0x6,
