# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj -o %t %s 2>&1 && \
# RUN:   llvm-objdump -d %t 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — fail-closed: reserved low-nibble 0x7 raw 8-byte pattern must
# NOT decode as a legacy D-class / real FU instruction. Product is Format E
# 12-byte only; this is a negative decode residual, not a product-NOP invent.

# REGRESSION: low nibble 0x7 was historically aliased through a legacy DC_D2
# path. Product path must not silently accept that as a real instruction
# mnemonic. Do not claim all-zero or reserved patterns are product idle/NOP.

        .quad 0x0000000000000007

# CHECK-LABEL: <.text>:
# CHECK-NOT: d_sw_l_pre_imm
# CHECK-NOT: add32
# CHECK-NOT: mul
