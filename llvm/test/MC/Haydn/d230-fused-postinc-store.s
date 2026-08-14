# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o /dev/null
#
# Smoke: fused post-inc store assembles (public mnemonic st32_post).

.text
  { s_sw_post_imm r3, r1, 1 }
  { d_sdw_post_imm d0, r1, 1 }
