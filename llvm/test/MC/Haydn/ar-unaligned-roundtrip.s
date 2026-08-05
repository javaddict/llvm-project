# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s --check-prefix=ENC
# REQUIRES: haydn-registered-target
#
# MC + disassembly coverage for golden LS AR unaligned ops (#103-#109):
# PLDWWUA, D_LQHWUA_POST, D_LTWUA_POST, FLAR, WBARWUA
# D_SQHWUA_POST, D_STWUA_POST
#
# Guards:
# 1) AsmParser accepts the mnemonics inside Bundle128
# 2) Encoder produces stable 16-byte parcels
# 3) Disassembler round-trips mnemonics + operands (ar_sel / dir_sel / regs)
# 4) Stream sequences (load path + store path) survive encode/decode
# 5) AR can dual-issue with ALU32 in the same bundle

#===----------------------------------------------------------------------===#
# Per-op single-slot bundles (operand field stress)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_pldwwua>:
# CHECK: { nop; nop; pldwwua{{ *}}3, r12 }
# ENC-LABEL: f_pldwwua:
# B3.7: brace asm emits Format->Opcode BUNDLE128_FULL (S0-S1-S2 + NOP fill).
# ENC: { nop; nop; pldwwua{{ *}}3, r12 } // encoding: [0x3c,0x00,0x00,0x00,0xc0,0x39,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_pldwwua:
  { pldwwua 3, r12 }

# CHECK-LABEL: <f_flar>:
# CHECK: { nop; nop; flar{{ *}}2 }
# ENC-LABEL: f_flar:
# ENC: { nop; nop; flar{{ *}}2 } // encoding: [0x02,0x00,0x00,0x00,0x80,0x3a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_flar:
  { flar 2 }

# CHECK-LABEL: <f_wbarwua>:
# CHECK: { nop; nop; wbarwua{{ *}}1, r3, 1 }
# ENC-LABEL: f_wbarwua:
# ENC: { nop; nop; wbarwua{{ *}}1, r3, 1 } // encoding: [0x27,0x00,0x00,0x00,0xc0,0x3a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_wbarwua:
  { wbarwua 1, r3, 1 }

# CHECK-LABEL: <f_d_lqhwua_post>:
# CHECK: { nop; nop; d_lqhwua_post{{ *}}d7, 2, r4, r5, 1 }
# ENC-LABEL: f_d_lqhwua_post:
# ENC: { nop; nop; d_lqhwua_post{{ *}}d7, 2, r4, r5, 1 } // encoding: [0x48,0xe5,0x01,0x00,0x00,0x3a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_d_lqhwua_post:
  { d_lqhwua_post d7, 2, r4, r5, 1 }

# CHECK-LABEL: <f_d_ltwua_post>:
# CHECK: { nop; nop; d_ltwua_post{{ *}}d3, 0, r6, r7, 0 }
# ENC-LABEL: f_d_ltwua_post:
# ENC: { nop; nop; d_ltwua_post{{ *}}d3, 0, r6, r7, 0 } // encoding: [0x60,0xc7,0x00,0x00,0x40,0x3a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_d_ltwua_post:
  { d_ltwua_post d3, 0, r6, r7, 0 }

# CHECK-LABEL: <f_d_sqhwua_post>:
# CHECK: { nop; nop; d_sqhwua_post{{ *}}d1, 3, r8, r9, 1 }
# ENC-LABEL: f_d_sqhwua_post:
# ENC: { nop; nop; d_sqhwua_post{{ *}}d1, 3, r8, r9, 1 } // encoding: [0x88,0x79,0x00,0x00,0x00,0x3b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_d_sqhwua_post:
  { d_sqhwua_post d1, 3, r8, r9, 1 }

# CHECK-LABEL: <f_d_stwua_post>:
# CHECK: { nop; nop; d_stwua_post{{ *}}d2, 0, r10, r11, 0 }
# ENC-LABEL: f_d_stwua_post:
# ENC: { nop; nop; d_stwua_post{{ *}}d2, 0, r10, r11, 0 } // encoding: [0xa0,0x8b,0x00,0x00,0x40,0x3b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
f_d_stwua_post:
  { d_stwua_post d2, 0, r10, r11, 0 }

#===----------------------------------------------------------------------===#
# Canonical operand corners (ar_sel=0, dir=0, d0/r1/r2)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_corners>:
# CHECK: { nop; nop; pldwwua{{ *}}0, r1 }
# CHECK: { nop; nop; d_lqhwua_post{{ *}}d0, 0, r1, r2, 0 }
# CHECK: { nop; nop; d_ltwua_post{{ *}}d0, 0, r1, r2, 0 }
# CHECK: { nop; nop; d_sqhwua_post{{ *}}d0, 0, r1, r2, 0 }
# CHECK: { nop; nop; d_stwua_post{{ *}}d0, 0, r1, r2, 0 }
# CHECK: { nop; nop; flar{{ *}}0 }
# CHECK: { nop; nop; wbarwua{{ *}}0, r1, 0 }
f_corners:
  { pldwwua 0, r1 }
  { d_lqhwua_post d0, 0, r1, r2, 0 }
  { d_ltwua_post d0, 0, r1, r2, 0 }
  { d_sqhwua_post d0, 0, r1, r2, 0 }
  { d_stwua_post d0, 0, r1, r2, 0 }
  { flar 0 }
  { wbarwua 0, r1, 0 }

#===----------------------------------------------------------------------===#
# Stream sequences (matches intrinsic e2e usage order)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_load_stream>:
# CHECK: { nop; nop; pldwwua{{ *}}0, r1 }
# CHECK: { nop; nop; d_lqhwua_post{{ *}}d0, 0, r2, r3, 0 }
# CHECK: { nop; nop; d_ltwua_post{{ *}}d1, 0, r2, r3, 0 }
# CHECK: { nop; nop; flar{{ *}}0 }
f_load_stream:
  { pldwwua 0, r1 }
  { d_lqhwua_post d0, 0, r2, r3, 0 }
  { d_ltwua_post d1, 0, r2, r3, 0 }
  { flar 0 }

# CHECK-LABEL: <f_store_stream>:
# CHECK: { nop; nop; pldwwua{{ *}}0, r1 }
# CHECK: { nop; nop; d_sqhwua_post{{ *}}d0, 0, r2, r3, 0 }
# CHECK: { nop; nop; d_stwua_post{{ *}}d1, 0, r2, r3, 0 }
# CHECK: { nop; nop; wbarwua{{ *}}0, r2, 0 }
f_store_stream:
  { pldwwua 0, r1 }
  { d_sqhwua_post d0, 0, r2, r3, 0 }
  { d_stwua_post d1, 0, r2, r3, 0 }
  { wbarwua 0, r2, 0 }

#===----------------------------------------------------------------------===#
# Dual-issue: AR + ALU32 in one Bundle128
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_dual_pldw_add>:
# B3.7: Bundle.canAdd/add + emit BUNDLE128_FULL S0-S1-S2.
# Two-op right-aligned text: pldwwua names s1 but is S0-only, so it falls
# back to s0; add32 keeps the s0 position it was written in -> spreads to s2.
# CHECK: { nop; pldwwua{{ *}}0, r1; add32{{ *}}r2, r3, r4 }
# ENC-LABEL: f_dual_pldw_add:
# show-encoding prints preformed BUNDLE128_FULL slot order (same as objdump).
# ENC: { nop; pldwwua{{ *}}0, r1; add32{{ *}}r2, r3, r4 }
f_dual_pldw_add:
  { pldwwua 0, r1 ; add32 r2, r3, r4 }
