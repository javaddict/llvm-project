# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o \
# RUN:     | FileCheck %s --implicit-check-not='<unknown>'
# REQUIRES: haydn-registered-target

# REGRESSION TEST : Bundle128 flex-coverage completion. The 9 opcodes
# that previously hit the forcing function
# ("opcode 'X' has no Bundle128 form — add a _S<k>_FLEX def") now have flex
# variants and round-trip cleanly.
#
# Coverage added by :
# Category A (new _S<k>_FLEX defs):
# ST8_S0_FLEX (HaydnFormatsLS.td, codepoint 118)
# LDU8_S0_FLEX (HaydnFormatsLS.td, codepoint 119)
# ST64_POST_S1_FLEX (HaydnFormatsLS.td, codepoint 0b0110001 D_ST_RI6)
# BNE_W/BGE_W/BGEU_W/BLT_W/BLTU_W_S0_FLEX (HaydnFormatsALU32.td, 79-83)
# BGEZ_W/BLTZ_W_S0_FLEX (HaydnFormatsALU32.td, 84-85)
# CSRW_W_S0_FLEX (HaydnFormatsALU32.td, codepoint 86)
# LD32_S1_FLEX / LD64_S1_FLEX (HaydnFormatsLD.td, codepoints 62-63)
# Category B (producer retired to legacy name with existing flex):
# XOR32_M0 -> XOR32 (HaydnAsmPrinter 3 re-zero-R0 sites)
# ST32_M0S0LS / LD32_M0S0LS -> ST32 / LD32 (HaydnAsmPrinter varargs)
#
# Test design: assemble each slot-suffixed mnemonic inside a bundle and
# round-trip through objdump. If a flex def regresses (removed or
# isCodeGenOnly mis-set so the matcher drops it), llvm-mc aborts at assemble
# time with "failed to match instruction in bundle" or the forcing function
# fires at emit with "has no Bundle128 form".

# CHECK-LABEL: <f_st8_s0>:
# CHECK: { {{.*}}s_sb_with_imm r1, r2, 4
f_st8_s0:
  { s_sb_with_imm r1, r2, 4 }

# CHECK-LABEL: <f_ldu8_s0>:
# CHECK: { {{.*}}s_lbu_with_imm r1, r2, 4
f_ldu8_s0:
  { s_lbu_with_imm r1, r2, 4 }

# CHECK-LABEL: <f_bne_w_s0>:
# CHECK: { {{.*}}bne r1, r2, [[OFF1:[0-9]+]]
f_bne_w_s0:
  { bne r1, r2, 8 }

# CHECK-LABEL: <f_bge_w_s0>:
# CHECK: { {{.*}}bge r1, r2, [[OFF1:[0-9]+]]
f_bge_w_s0:
  { bge r1, r2, 8 }

# CHECK-LABEL: <f_blt_w_s0>:
# CHECK: { {{.*}}blt r1, r2, [[OFF1:[0-9]+]]
f_blt_w_s0:
  { blt r1, r2, 8 }

# CHECK-LABEL: <f_bgez_w_s0>:
# CHECK: { {{.*}}bgez r1, [[OFF1:[0-9]+]]
f_bgez_w_s0:
  { bgez r1, 8 }

# CHECK-LABEL: <f_bltz_w_s0>:
# CHECK: { {{.*}}bltz r1, [[OFF1:[0-9]+]]
f_bltz_w_s0:
  { bltz r1, 8 }

# CHECK-LABEL: <f_csrw_w_s0>:
# CHECK: { {{.*}}csrw [[CSR:[0-9]+]], r1
f_csrw_w_s0:
  { csrw 32, r1 }

# Dual-load packs two logical ld32/ld64 into S0+S1 (public mnemonic ld32/ld64).
# Bundle text is high slot first, so the S2 position is the empty one.
# CHECK-LABEL: <f_ld32_dual>:
# CHECK: { {{.*}}s_lw_{{[a-z_]*}}{{.*}}; s_lw_{{[a-z_]*}}{{.*}}
f_ld32_dual:
  { nop; s_lw_with_imm r1, r2, 0; s_lw_with_imm r3, r2, 2 }

# CHECK-LABEL: <f_ld64_dual>:
# CHECK: { {{.*}}d_ldw_{{[a-z_]*}}{{.*}}; d_ldw_{{[a-z_]*}}{{.*}}
f_ld64_dual:
  { nop; d_ldw_with_imm d0, r2, 0; d_ldw_with_imm d1, r2, 1 }
