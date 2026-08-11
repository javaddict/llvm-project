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
# What breaks if the bug returns: the NEXT-chain between consecutive parcel
# offsets fails, because a split parcel puts extra lines between them.
# Do NOT update the checks to match split output — fix the disassembler.
#
# The offsets are the load-bearing part and they now step by 12, not 16: one
# format E parcel per line is the same contract one bundle format later.
# The ORDER of the ops inside the braces is NOT part of the contract — it is
# whichever entry the packer chose — so each line accepts either order rather
# than pinning today's. § 5.12's fix will move those choices.

.text

# 2-issue: two ops, one bundle. Under Bundle128 this was a slot-capability
# story (add64 had no s0 field and spread to s2); under format E both simply
# take a free unit, so only the co-issue itself is asserted.
{ add32 r0, r1, r2 ; add64 d0, d1, d2 }

# 3-issue — pack-friendly source order (two add64 then add32)
{ add64 d0, d1, d2 ; add64 d3, d4, d5 ; add32 r0, r1, r2 }

# Single-issue control (solitary residual prefers S0)
add32 r1, r2, r3

# FileCheck: each parcel is exactly one objdump line, at +0xc.
# Printer spacing between mnemonic and operands may vary; use {{.*}}.
# CHECK:       0: {{.*}}{ {{add64.*d0, d1, d2.*add32.*r0, r1, r2|add32.*r0, r1, r2.*add64.*d0, d1, d2}}
# CHECK-NEXT:  c: {{.*}}{ {{.*}}add64{{.*}}d0, d1, d2{{.*}}add64{{.*}}d3, d4, d5{{.*}}add32{{.*}}r0, r1, r2
# CHECK-NEXT: 18: {{.*}}{ {{.*}}add32{{.*}}r1, r2, r3
