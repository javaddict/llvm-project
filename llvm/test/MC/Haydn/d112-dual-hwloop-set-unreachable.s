# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_SET=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-SET
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_MIX=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-MIX
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_F2_E2=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-F2-E2
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_F2_E3=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-F2-E3
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_REG=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-REG
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=DUAL_PAD=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUAL-PAD
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=UNIQUE=1 -o %t.uniq.o
# RUN: llvm-readelf -r %t.uniq.o | FileCheck %s --check-prefix=UNIQUE

# D1.12 evidence pin: dual symbolic SET_HWLOOP (the HWLoopOff1/Off2
# same-r_offset ambiguity class) is UNREACHABLE at the MC emitter because
# every dual-SET parcel text shape is rejected at PARSE time, before any
# fixup exists. Unreachability chain (golden v2_2):
#   * All four generated SET members occupy fixed placement — E2 rows
#     SET_HWLOOP_E2_E0_ALU0_HWLRIII / SET_HWLOOP_F2_E2_E0_ALU0_HWLRIIR
#     are entry0-only; E3 rows SET_HWLOOP_F2_E3_E0/E1_ALU0_HWLRIIR are
#     ALU0-only — and assignFormatEMemberEntries admits one member per
#     entry with unit injectivity, so a second SET member has no seat.
#   * No other producer mints R_HAYDN_HWLoopOff1/Off2 fixups, and
#     .reloc cannot name R_HAYDN_* kinds ("unknown relocation name").
#   * The compiler side is covered by the D1.11 one-body trip law
#     (haydnCycleMembersHaveHwloopTripConflict; pinned by
#     d111-freeze-trip-conflict.mir) and the AltOccupancy 0x1 mask.
# The per-kind walls in emitFormatEParcel / qualifyFixupKindForEntry
# (HaydnMCCodeEmitter.cpp) therefore guard a FUTURE golden admission of
# SET at another entry/unit; they cannot fire today. This test is their
# evidence, not a firing pin.
#
# The UNIQUE arm is the load-bearing positive control: ONE symbolic
# set_hwloop_w legitimately emits the R_HAYDN_HWLoopOff1 +
# R_HAYDN_HWLoopOff2 PAIR at the SAME r_offset 0 (distinct RelocKinds,
# distinct windows via the Type==0x10 sniff). Any wall shaped as a
# PAIR count refuses this object and breaks every symbolic SET; the
# walls must compare per RelocKind (duplicated Off1, or duplicated Off2).

.ifdef DUAL_SET
	{ set_hwloop_w 0, xbody, xend, 5; set_hwloop_w 0, xbody, xend, 5 }
# DUAL-SET: error: incorrect bundle: E2-only logical cannot occupy a three-entry row
.endif

.ifdef DUAL_MIX
	{ set_hwloop_w 0, xbody, xend, 5; set_hwloop_f2_w 0, xbody, xend, r1 }
# DUAL-MIX: error: incorrect bundle: E2-only logical cannot occupy a three-entry row
.endif

.ifdef DUAL_F2_E2
	{ set_hwloop_f2_w 0, xbody, xend, r1; set_hwloop_f2_w 0, xbody, xend, r1 }
# DUAL-F2-E2: error: incorrect bundle
.endif

# F2 E3 shapes fail identically (ALU0-only members, unit injectivity).
.ifdef DUAL_F2_E3
	{ set_hwloop_f2_w 0, xbody, xend, r1; set_hwloop_f2_w 0, xbody, xend, r1; nop }
# DUAL-F2-E3: error: incorrect bundle
.endif

.ifdef DUAL_REG
	{ set_hwloop_w 0, xbody, xend, 5; set_hwloop_reg_w 0, xbody, xend, r1 }
# DUAL-REG: error: failed to match instruction in bundle
.endif

# NOP padding does not open a second entry seat for an E2-only SET.
.ifdef DUAL_PAD
	{ set_hwloop_w 0, xbody, xend, 5; nop; set_hwloop_w 0, xbody, xend, 5 }
# DUAL-PAD: error: incorrect bundle: E2-only logical cannot occupy a three-entry row
.endif

.ifdef UNIQUE
.text
.globl _start
_start:
	set_hwloop_w 0, xbody, xend, 5
# UNIQUE: R_HAYDN_HWLoopOff1{{.*}} xbody
# UNIQUE: R_HAYDN_HWLoopOff2{{.*}} xend
# UNIQUE-NOT: R_HAYDN_HWLoopOff1{{.*}} xbody
.endif
