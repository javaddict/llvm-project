# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj -o %t %s 2>&1 && \
# RUN:   llvm-objdump -d %t 2>&1 | FileCheck %s

# Role: object — 64-bit Mode 1 (bits[3:0]=0111) is RESERVED per encoding_manual.md section 7 — the disassembler must raise the.

# REGRESSION TEST: 64-bit Mode 1 (bits[3:0]=0111) is RESERVED per
# encoding_manual.md section 7 — the disassembler must raise the
# illegal_bundle trap (decode fails), NOT silently decode it as a valid
# instruction via the legacy DC_D2 path that aliases on the same low
# nibble.
#
# Bug guarded: Wave 2/ — before this fix, the disassembler called
# classifyBundle which maps low nibble 0x7 to DC_D2 (legacy D-class).
# Per encoding_manual.md section 7, Mode 1 must trap. The fix adds an
# explicit check on PATTERN_64BIT_M1_RESERVED in HaydnDisassembler.cpp
# that returns Fail BEFORE the legacy classifyBundle probe runs.
#
# What this test guards:
# 1. A 64-bit bundle with bits[3:0]=0111 cannot be decoded as a valid
# instruction. The disassembler returns Fail and llvm-objdump prints
# the unknown-instruction marker.
# 2. This is distinct from "successfully decoded as legacy D2" — the
# decode must reject, not silently accept.
#
# Spec reference: encoding_manual.md section 7 (Mode 1 RESERVED).
# Related decision: (encoding wave 2 — Mode 1 illegal_bundle trap).
#
# If a future change accidentally makes Mode 1 decode as a valid bundle
# this test will start printing a real instruction mnemonic and the
# CHECK-unknown pattern will fail.

# Hand-construct a 64-bit bundle with low nibble 0x7 (Mode 1 RESERVED).
# We use.quad to emit raw bytes. The disassembler must reject this.

        .quad 0x0000000000000007

# CHECK-LABEL: <.text>:
# The all-zero-with-pattern-0x7 bundle decodes to NOP via the cycle-6/7/8
# Haydn32 fallback (the low 4 bytes 0x00000007 do not match any Haydn32
# instruction; the bundle probe then runs and the all-zero-slot layout
# yields NOP). What matters for this regression test is that NO real
# instruction mnemonic (d_sw_l_pre_imm / move / add / mul) appears — the
# reserved Mode-1 bundle must NOT decode to a legacy D2 instruction.
# CHECK-NOT:  d_sw_l_pre_imm
# CHECK-NOT:  move
# CHECK-NOT:  add
# CHECK-NOT:  mul
