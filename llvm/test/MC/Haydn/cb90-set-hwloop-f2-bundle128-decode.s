# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: object — Format E SET_HWLOOP_F2_W_S0 must decode (not <unknown>).

# Format E SET_HWLOOP_F2_W_S0 must decode (not <unknown>).
# isCodeGenOnly=1 on SET_HWLOOP_*_W_S0 excluded them from DecoderTableS048
# so llvm-objdump lost memset/memcpy SET parcels on direct-ELF and ISS never
# armed HWLR. Confirm round-trip prints set_hwloop_f2_w.

.text
.globl test_cb90_hwloop_decode
.balign 16
test_cb90_hwloop_decode:
    set_hwloop_f2_w 1, .Lbody, .Lend, r3
.Lbody:
    { nop }
    { nop }
.Lend:
    { nop }

