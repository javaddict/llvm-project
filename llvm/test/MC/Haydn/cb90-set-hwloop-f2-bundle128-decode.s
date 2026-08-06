# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# Bundle128 SET_HWLOOP_F2_S0 must decode (not <unknown>).
# isCodeGenOnly=1 on SET_HWLOOP_*_W_S0 excluded them from DecoderTableS048
# so llvm-objdump lost memset/memcpy SET parcels on direct-ELF and ISS never
# armed HWLR. Confirm round-trip prints set_hwloop_f2.

.text
.globl test_cb90_hwloop_decode
.balign 16
test_cb90_hwloop_decode:
    set_hwloop_f2 1, .Lbody, .Lend, r3
.Lbody:
    { nop }
    { nop }
.Lend:
    { nop }

# CHECK-LABEL: <test_cb90_hwloop_decode>:
# CHECK-NOT: <unknown>
# CHECK: set_hwloop_f2
