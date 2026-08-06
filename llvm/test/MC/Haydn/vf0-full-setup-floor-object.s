# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -s --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *
# Owner: GE96-01/03 residual (Format E EncodedBytes=12 setup-floor object pin).

# Role: object — MinSetupBytes as immutable MC/object setup-floor under Format E.

# Product contract (HaydnHWLoopContracts.h):
#   InterveningCycles = 2, productParcelBytes() = 12
#   MinSetupBytes = InterveningCycles × productParcelBytes() = 2 × 12 = 24
#   HWLR_BEGIN = PC_SET + (off1 << 2)
#   Earliest floor: BEGIN at SET+24 → off1 = 24/4 = 6
#
# Geometry (SET + InterveningCycles intervening parcels + body):
#   @0x00  SET_HWLOOP_F2_W Format E parcel (12 B)
#   @0x0c  intervening Format E parcel 1
#   @0x18  BEGIN body (MinSetupBytes = 24)
#   @0x24  END parcel
#
# Prior seals:
#   cb90-set-hwloop-f2-bundle128-decode.s — SET decodes (not <unknown>)
#   d486-hwloop-fieldlsb-bundle128.s      — FieldLsb / reloc geometry
#
# Timing law remains the issue-cycle pair (Following >= InterveningCycles);
# MinSetupBytes is the EncodedBytes coincidence of that floor only.

.text
.globl test_vf0_full_setup_floor
.balign 4
test_vf0_full_setup_floor:
    # SET at PC=0. Symbolic off1/off2 patched by applyFixup.
    set_hwloop_f2_w 0, .Lbody, .Lend, r1
    # InterveningCycles (=2) deficit pads after SET.
    { add32 r0, r0, r0 }
    { add32 r0, r0, r0 }
.Lbody:
    # BEGIN at SET+24 = MinSetupBytes.
    { add32 r1, r2, r3 }
.Lend:
    # Inclusive END after one body parcel (END > BEGIN).
    { add32 r4, r5, r6 }

# CHECK: Hex dump of section '.text':
# First 12 bytes = SET Format E parcel; remaining parcels are 12 B each.
# Exact off1/off2 byte pin still residual under GE96 (XFAIL above).
