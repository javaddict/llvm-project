# RUN: not llvm-mca -mtriple=haydn-unknown-elf -mcpu=generic -instruction-tables < %s 2>&1 | FileCheck %s
#
# M18: llvm-mca requires an instruction-level SchedRW model
# (MCSchedModel::SchedClassTable); the Haydn product model is
# ProcessorItineraries-only (HaydnSchedule.td, CompleteModel=0). The
# ItinRW bridge that would enable mca (HaydnGenSchedMcaBridge.td.inc)
# also activates generic-scheduler resource arbitration inside llc and
# changed product scheduling, so it is generated+checked but NOT included
# (see HaydnSchedule.td tail comment; AIE peer declines identically).
# This pins the fail-closed mca behavior.

add32 r0, r1, r2

# CHECK: error: unable to find instruction-level scheduling information for target triple 'haydn-unknown-unknown-elf' and cpu 'generic'.
# CHECK-NEXT: note: cpu 'generic' provides itineraries. However, instruction itineraries are currently unsupported.
