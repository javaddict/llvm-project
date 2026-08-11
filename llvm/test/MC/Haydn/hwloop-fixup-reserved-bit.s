# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
# XFAIL: *
#
# KNOWN DEFECT — the assertion is right and the encoder is wrong. Do not
# "fix" this by relaxing it; see FORMAT-E-SWITCH-PLAN.md § 5.14.
#
# SET_HWLOOP_F2's two label operands are applied by fixups that write outside
# their fields. SET_HWLOOP_F2_P31_ALU0 lays entry1 out as
#
#     e1{29-18} = uimm12_offset2      e1{17-12} = uimm6_offset1
#     e1{11}    = hwlr_sel            e1{30}    = reserved, must be 0
#
# and the fixups spill across all three. Holding the loop body at 69 bundles
# and varying only N, the number of filler bundles between the instruction
# and the START label:
#
#     N = 0    offset1 = 0    offset2 =  105   reserved = 0
#     N = 4    offset1 = 0    offset2 =  111   reserved = 0
#     N = 5    offset1 = 32   offset2 = 2160   reserved = 0
#     N = 11   offset1 = 32   offset2 =  121   reserved = 1
#
# The body never changes size, so offset2 must not move; offset1 must track
# the start distance and does not. Below the threshold this is a SILENT wrong
# value. From N = 5 the reserved bit eventually sets and the disassembler
# refuses the bundle, which is the only reason this was noticed at all —
# through bqriir32x32_df1-e2e.ll, whose object held one `<unknown>`.
#
# The numeric-operand path DOES range-check (`set_hwloop_f2 1, 0, 4096, r4`
# is rejected); only the label path is unguarded. Same class as § 5.8.
#
# N = 10 below is what llc produces for bqriir32x32_df1_process, and
# reproduces its bytes exactly:  8f 00 00 00 40 4e 01 3c 28 00 00 00
#
# CHECK: set_hwloop_f2
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
