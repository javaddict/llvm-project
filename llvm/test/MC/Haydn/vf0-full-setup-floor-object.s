# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-objdump -s --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=HEX
# REQUIRES: haydn-registered-target

# Role: object — MinSetupBytes as immutable MC/object setup-floor under Format E.
#
# Product contract (HaydnHWLoopContracts.h):
#   InterveningCycles = 2, productParcelBytes() = 12
#   MinSetupBytes = InterveningCycles × productParcelBytes() = 2 × 12 = 24
#
# Geometry pinned here (SET + two intervening real ALU pads + body + end):
#   @0x00  SET_HWLOOP_F2_W Format E parcel (12 B)
#   @0x0c  intervening Format E parcel 1
#   @0x18  intervening Format E parcel 2
#   @0x24  BEGIN body
#   @0x30  END parcel
#
# Pads are product-legal ALU reals (no idle/all-nop invent). Exact off1/off2
# wire scale is not golden-defined and is not pinned (reloc-unresolved in .o).
#
# Prior seals:
#   cb90-set-hwloop-f2-bundle128-decode.s — SET decodes (not <unknown>)
#   d486-hwloop-fieldlsb-bundle128.s      — FieldLsb / parcel stride

.text
.globl test_vf0_full_setup_floor
.balign 4
test_vf0_full_setup_floor:
    # SET at PC=0. Symbolic off1/off2 patched by applyFixup / linker.
    set_hwloop_f2_w 0, .Lbody, .Lend, r1
    # InterveningCycles (=2) real ALU pads after SET.
    { add32 r0, r0, r0 }
    { add32 r0, r0, r0 }
.Lbody:
    { add32 r1, r2, r3 }
.Lend:
    { add32 r4, r5, r6 }

# CHECK-LABEL: <test_vf0_full_setup_floor>:
# CHECK: set_hwloop_f2
# CHECK-NOT: <unknown>
# CHECK: {{[[:space:]]+c:}}
# CHECK: {{[[:space:]]+18:}}
# CHECK: {{[[:space:]]+24:}}
# CHECK: {{[[:space:]]+30:}}
# CHECK: add32

# HEX: Contents of section .text:
# First parcel is SET (lo-nibble/header 07 0c ...); remaining 12 B parcels.
# HEX: 070c
