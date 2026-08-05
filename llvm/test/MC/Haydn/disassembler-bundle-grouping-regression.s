# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:   llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf - | \
# RUN:   FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: llvm-objdump -d must show real VLIW multi-issue bundles as
# ONE grouped { slot0; slot1; slot2 } line, NOT as N split single-issue lines.
#
# HISTORY (/): the legacy 32-bit dispatch in HaydnDisassembler ran
# BEFORE bundle decode and split multi-issue Mode-0 parcels. That Mode-0
# 8-byte path is retired (/ Bundle128-only). The load-bearing
# behavior today is the same contract on Bundle128 (16-byte) parcels:
# multi-op encode must disassemble as ONE grouped line per parcel.
#
# Coverage (hand-pinned multi-op asm → encode → disasm):
# 2-issue: add32 + add64 (+ nop in s2)
# 3-issue: add32 + add64 + add64
# 1-issue control: bare add32 still one line (no false multi-issue)
#
# What breaks if the bug returns:
# CHECK-NEXT between consecutive 16-byte offsets fails (split lines).
# Do NOT update CHECKs to match split output — fix the disassembler.

.text

# 2-issue Bundle128: right-aligned text names s1,s0; add64 has no s0 field
# so it spreads to s2 (add64->S2, add32->S1, unchanged from before).
{ add32 r0, r1, r2 ; add64 d0, d1, d2 }

# 3-issue Bundle128 — pack-friendly source order (two add64 then add32)
{ add64 d0, d1, d2 ; add64 d3, d4, d5 ; add32 r0, r1, r2 }

# Single-issue control (solitary residual prefers S0)
add32 r1, r2, r3

# FileCheck: each parcel is exactly one objdump line at +0x10.
# Printer spacing between mnemonic and operands may vary; use {{.*}}.
# CHECK:        0: {{.*}}{ add64{{.*}}d0, d1, d2; add32{{.*}}r0, r1, r2; nop }
# CHECK-NEXT:  10: {{.*}}{ add64{{.*}}d0, d1, d2; add64{{.*}}d3, d4, d5; add32{{.*}}r0, r1, r2 }
# CHECK-NEXT:  20: {{.*}}{ nop; nop; add32{{.*}}r1, r2, r3 }
