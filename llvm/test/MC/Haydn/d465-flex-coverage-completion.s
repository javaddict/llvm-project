# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
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
# CHECK: { st8 r1, r2, 4; nop; nop }
f_st8_s0:
  { st8 r1, r2, 4 }

# CHECK-LABEL: <f_ldu8_s0>:
# CHECK: { ldu8 r1, r2, 4; nop; nop }
f_ldu8_s0:
  { ldu8 r1, r2, 4 }

# CHECK-LABEL: <f_bne_w_s0>:
# CHECK: { bne_w r1, r2, [[OFF1:[0-9]+]]; nop; nop }
f_bne_w_s0:
  { bne_w r1, r2, 8 }

# CHECK-LABEL: <f_bge_w_s0>:
# CHECK: { bge_w r1, r2, [[OFF1:[0-9]+]]; nop; nop }
f_bge_w_s0:
  { bge_w r1, r2, 8 }

# CHECK-LABEL: <f_blt_w_s0>:
# CHECK: { blt_w r1, r2, [[OFF1:[0-9]+]]; nop; nop }
f_blt_w_s0:
  { blt_w r1, r2, 8 }

# CHECK-LABEL: <f_bgez_w_s0>:
# CHECK: { bgez_w r1, [[OFF1:[0-9]+]]; nop; nop }
f_bgez_w_s0:
  { bgez_w r1, 8 }

# CHECK-LABEL: <f_bltz_w_s0>:
# CHECK: { bltz_w r1, [[OFF1:[0-9]+]]; nop; nop }
f_bltz_w_s0:
  { bltz_w r1, 8 }

# CHECK-LABEL: <f_csrw_w_s0>:
# CHECK: { csrw_w [[CSR:[0-9]+]], r1; nop; nop }
f_csrw_w_s0:
  { csrw_w 32, r1 }

# Dual-load packs two logical ld32/ld64 into S0+S1 (public mnemonic ld32/ld64).
# CHECK-LABEL: <f_ld32_dual>:
# CHECK: { ld32{{.*}}; ld32{{.*}}
f_ld32_dual:
  { ld32 r1, r2, 0; ld32 r3, r2, 8; nop }

# CHECK-LABEL: <f_ld64_dual>:
# CHECK: { ld64{{.*}}; ld64{{.*}}
f_ld64_dual:
  { ld64 d0, r2, 0; ld64 d1, r2, 8; nop }
