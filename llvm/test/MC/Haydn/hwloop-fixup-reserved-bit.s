# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST for § 5.14, FIXED. This was XFAIL until the reloc geometry
# table gained the type code in its key; it is the LOUD half of that defect and
# d486-hwloop-fieldlsb-bundle128.s is the quiet half. Keep both.
#
# SET_HWLOOP_F2's two label operands are applied by fixups that write outside
# their fields. SET_HWLOOP_F2_P31_ALU0 lays entry1 out as
#
#     e1{29-18} = uimm12_offset2      e1{17-12} = uimm6_offset1
#     e1{11}    = hwlr_sel            e1{30}    = reserved, must be 0
#
# and the fixups used to spill across all three, because the geometry table
# was keyed on (FieldSize, entry count, entry index, mapping) — which does not
# identify a field when one entry holds a 6-bit AND a 12-bit offset. off1 was
# patched at bundle bit 62 instead of 49, i.e. inside off2's field and, for a
# large enough value, past its end into the reserved bit:
#
#     N = 0    offset1 = 0    offset2 =  105   reserved = 0
#     N = 4    offset1 = 0    offset2 =  111   reserved = 0
#     N = 5    offset1 = 32   offset2 = 2160   reserved = 0
#     N = 11   offset1 = 32   offset2 =  121   reserved = 1
#
# The body never changed size, so offset2 must not move; offset1 must track
# the start distance and did not. Below the threshold that is a SILENT wrong
# value; past it the disassembler refuses the bundle, which is the only reason
# any of it was noticed — through bqriir32x32_df1-e2e.ll, whose object held
# one `<unknown>`.
#
# The numeric-operand path always range-checked (`set_hwloop_f2 1, 0, 4096,
# r4` is rejected); only the label path went through the table.
#
# N = 10 below is what llc produces for bqriir32x32_df1_process. The start
# label is 10 filler bundles + the instruction's own = 132 bytes ahead, and
# the end label is 69 bundles past that = 960.
#
# CHECK: set_hwloop_f2 1, 132, 960, r4
# CHECK-NOT: <unknown>

.text
{ set_hwloop_f2 1, L0, L1, r4 }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
L0:
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
{ nop; nop; nop }
L1:
