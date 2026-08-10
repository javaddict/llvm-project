# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Format E SET_HWLOOP_F2 must decode (not <unknown>) with
# typed HWLoopOff1/Off2 (not R_HAYDN_32). Body pads are product-legal ALU
# reals (no idle/all-nop invent). Same-section Off1/Off2 resolve at assemble.

# Format E SET_HWLOOP_F2 must decode (not <unknown>) after FieldLsb patch.
# Confirm round-trip prints set_hwloop_f2 (ISS arms HWLR from real SET wire).

.text
.globl test_cb90_hwloop_decode
.balign 16
test_cb90_hwloop_decode:
    set_hwloop_f2_w 1, .Lbody, .Lend, r3
.Lbody:
    { add32 r0, r0, r0 }
    { add32 r0, r0, r0 }
.Lend:
    { add32 r0, r0, r0 }

# CHECK-LABEL: <test_cb90_hwloop_decode>:
# CHECK: set_hwloop_f2
# CHECK-NOT: <unknown>
# CHECK: add32
