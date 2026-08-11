# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
# XFAIL: *
#
# KNOWN DEFECT — the assertion is right and the encoder is wrong. Do not
# "fix" this by relaxing it; see FORMAT-E-SWITCH-PLAN.md § 5.14. This file is
# the SILENT form of that defect and hwloop-fixup-reserved-bit.s is the loud
# one; both should go green together.
#
# REGRESSION TEST (RISK-6): the hardware-loop offset fixups must land in the
# fields the instruction declares. That was the original question here and it
# is still the question — only the layout it is asked against has changed.
#
# The Bundle128 version: HWLoopOff1 (FieldLsb=26) and HWLoopOff2 (FieldLsb=14)
# had been transcribed from the legacy 48-bit parcel and never updated, so
# applyFixup wrote offset1 into bits[31:26] of the LoWord — corrupting the
# rs/reserved bits and leaving the real field zero. The range was right and
# the POSITION was wrong, which is why a range-only fix did not close it.
#
# The format E version, measured: the fields are
#
#     e1{17-12} = uimm6_offset1     e1{29-18} = uimm12_offset2
#
# and with .Lbody 12 bytes ahead and .Lend 24 ahead the encoder produces
#
#     off1 = 0        off2 = 3 (prints as 12)
#
# off1 is zero for any choice of units, and off2 is carrying the distance to
# .Lbody — the value that belongs to off1. So the two label fixups are landing
# in the wrong field, and this time NO reserved bit is set and nothing
# complains. The disassembler prints `set_hwloop_f2 0, 0, 12, r1` for a loop
# whose body starts at +12 and ends at +24.
#
# The check below is what a correct encoder produces. Note it needs no byte
# pattern: asserting the printed operands says the same thing and survives a
# layout change, which the old byte0 == 0x08 assertion did not.

.text
.globl test_d486_hwloop_fieldlsb
# .balign 4, not 16 — see § 5.9: a 12-byte parcel cannot align to 16.
.balign 4
test_d486_hwloop_fieldlsb:
    # SET_HWLOOP_F2 in the parcel at 0.
    set_hwloop_f2 0, .Lbody, .Lend, r1
.Lbody:
    # one parcel ahead -> 12 bytes
    { add32 r1, r2, r3 }
.Lend:
    # two parcels ahead -> 24 bytes
    { add32 r4, r5, r6 }

# CHECK-LABEL: <test_d486_hwloop_fieldlsb>:
# CHECK: set_hwloop_f2{{.*}}0, 12, 24, r1
