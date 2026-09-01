# RUN: rm -rf %t && mkdir -p %t/pos && cp %S/Inputs/d122-occupancy-fail-closed/HaydnInstrFormats.td %S/Inputs/d122-occupancy-fail-closed/HaydnInstrFormatsC.td %S/Inputs/d122-occupancy-fail-closed/HaydnInstrInfo.td %S/Inputs/d122-occupancy-fail-closed/HaydnInstrInfoGolden.td.inc %S/Inputs/d122-occupancy-fail-closed/HaydnMultiSlotPseudo.td %t/
# RUN: not %python %S/../../../lib/Target/Haydn/FormatE/generate_alt_occupancy.py --haydn-dir %t --out %t/out.inc 2>&1 | FileCheck --check-prefix=GHOST %s
# Positive control: same fixture with the ghost row removed; the
# golden-backed exception row (ADD64) resolves and the run stops only at
# the fixture row floor, proving the arm is not blanket-rejecting.
# RUN: cp %t/HaydnInstrFormats.td %t/HaydnInstrFormatsC.td %t/HaydnInstrInfoGolden.td.inc %t/HaydnMultiSlotPseudo.td %t/pos/ && sed -e '/D122GHOST64/d' %t/HaydnInstrInfo.td > %t/pos/HaydnInstrInfo.td && not %python %S/../../../lib/Target/Haydn/FormatE/generate_alt_occupancy.py --haydn-dir %t/pos --out %t/pos/out.inc 2>&1 | FileCheck --check-prefix=POS %s
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# D1.22: residual occupancy is never derived from an instruction-name
# substring. A unit-bearing itinerary row (Slot012_ALU) with no golden
# instruction_type_index Available row and no explicit
# RESIDUAL_OCCUPANCY_EXCEPTIONS entry must fail closed naming the row,
# its itinerary, and the expected unit set. Golden index comes from
# HAYDN_GOLDEN_DIR/BUNDLESIM_GOLDEN_DIR (lit.local.cfg propagates it).
# Fixture: Inputs/d122-occupancy-fail-closed/.
#
# GHOST: error: D122GHOST64 itinerary Slot012_ALU has no golden Available ['ALU0', 'ALU1', 'ALU2'] row (residual occupancy is not name-derivable)
#
# POS: error: occupancy parse drift: 2 rows
