# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
#
# Smoke: fused post-inc store assembles (public mnemonic st32_post).

.text
  { st32.post r3, r1, 1 }
  { st64.post d0, r1, 1 }
