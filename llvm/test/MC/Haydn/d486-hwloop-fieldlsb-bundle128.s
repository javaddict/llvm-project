# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z -s --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — HWLoopOff1/HWLoopOff2 under Format E EncodedBytes=12 geometry
# (not retired 48-bit-parcel / 16-byte Bundle128 FieldLsb).

# REGRESSION TEST (RISK-6 → Format E): emit set_hwloop_f2_w with symbolic
# labels at known Format E parcel offsets, then dump .text.
#
# Product geometry (EncodedBytes=12):
#   Lbody = 12 bytes ahead (one Format E parcel) → off1 = 12/4 = 3
#   Lend  = 24 bytes ahead (two parcels)         → off2 = 24/4 = 6
#
# Filename retains historical d486-*-bundle128-* stem; product is Format E only.
# Pins parcel stride 0xc and product SET_HWLOOP + body decode (not <unknown>).

// CHECK: {{.*}}0: 07 {{.*}} { set_hwloop
// CHECK: {{.*}}c: 07 8b 10 32 00 00 00 00 00 00 00 00  { add32{{.*}}r1, r2, r3
// CHECK: {{.*}}18: 07 8b 40 65 00 00 00 00 00 00 00 00  { add32{{.*}}r4, r5, r6

.text
.globl test_d486_hwloop_fieldlsb
.balign 4
test_d486_hwloop_fieldlsb:
    # SET at offset 0 → 12-byte Format E parcel.
    set_hwloop_f2_w 0, .Lbody, .Lend, r1
.Lbody:
    # offset 12 → off1 = 3
    { add32 r1, r2, r3 }
.Lend:
    # offset 24 → off2 = 6
    { add32 r4, r5, r6 }
