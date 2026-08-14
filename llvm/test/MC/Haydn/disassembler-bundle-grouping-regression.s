# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:   llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf - | \
# RUN:   FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — llvm-objdump -d must show real VLIW multi-issue bundles as ONE grouped { slot0; slot1; slot2 } line, NOT as N split single-issue lines.

# REGRESSION TEST: llvm-objdump -d must show real VLIW multi-issue bundles as
# ONE grouped { slot0; slot1; slot2 } line, NOT as N split single-issue lines.
#
# HISTORY (/): the legacy 32-bit dispatch in HaydnDisassembler ran
# BEFORE bundle decode and split multi-issue Mode-0 parcels. That Mode-0
# 8-byte path is retired (/ Format-E-only). The load-bearing
# behavior today is the same contract on Format E (16-byte) parcels:
# multi-op encode must disassemble as ONE grouped line per parcel.
#
# Coverage (hand-pinned multi-op asm → encode → disasm):
# 2-issue: add32 + add64 (+ nop in s0)
# 3-issue pack-friendly: two add64 then add32
# 3-issue rematch source: add32 then two add64 (exact rematch, same encode)
# 1-issue control: bare add32 still one line (no false multi-issue)
#
# What breaks if the bug returns:
# CHECK-NEXT between consecutive 16-byte offsets fails (split lines).
# Do NOT update CHECKs to match split output — fix the disassembler.
# Rematch vs pack-friendly source must share one S0-S1-S2 member print order.

.text

# 2-issue Format E (add32 prefers S2, add64→S1 when free)
{ add32 r0, r1, r2 ; add64 d0, d1, d2 }

# 3-issue Format E — pack-friendly source order (two add64 then add32)
{ add64 d0, d1, d2 ; add64 d3, d4, d5 ; add32 r0, r1, r2 }

# 3-issue rematch source — first-fit freezes add32 high; exact rematch keeps
# both add64. Must disassemble as the same S0-S1-S2 member order as above.
{ add32 r0, r1, r2 ; add64 d0, d1, d2 ; add64 d3, d4, d5 }

# Single-issue control (solitary residual prefers S0)
add32 r1, r2, r3

# FileCheck: each parcel is exactly one objdump line at +0x10.
# Printer spacing between mnemonic and operands may vary; use {{.*}}.
# High-entry-first print (CB-142 / #10). One grouped line per parcel.
# CHECK: {{.*}}0: { add32 r0, r1, r2; add64 d0, d1, d2 }
# CHECK-NEXT: c: { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
# CHECK-NEXT: {{.*}}18: { add32 r0, r1, r2; add64 d0, d1, d2; add64 d3, d4, d5 }
# CHECK-NEXT: {{.*}}24: { nop; add32 r1, r2, r3 }
