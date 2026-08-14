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
# 1) AsmParser accepts the mnemonics inside a format E bundle
# 2) Encoder produces stable 12-byte parcels
# 3) Disassembler round-trips mnemonics + operands (ar_sel / regs)
# 4) Stream sequences (load path + store path) survive encode/decode
# 5) AR can dual-issue with ALU32 in the same bundle
#
# Format E dropped both the stride and the direction select from this family,
# so ar_sel is the only immediate each op still carries. Nothing here may
# spell a third operand: an assertion that still did would have been passing
# against a shape the hardware no longer has.

#===----------------------------------------------------------------------===#
# Per-op single-slot bundles (operand field stress)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_pldwwua>:
# CHECK: { pldwwua_post 3, r12; nop; nop }
# ENC-LABEL: f_pldwwua:
# ENC: { pldwwua_post 3, r12; nop; nop } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x30,0x04,0x78,0x00]
f_pldwwua:
  { pldwwua_post 3, r12 }

# CHECK-LABEL: <f_flar>:
# CHECK: { flar 2; nop; nop }
# ENC-LABEL: f_flar:
# ENC: { flar 2; nop; nop } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x30,0x10,0x40,0x00]
f_flar:
  { flar 2 }

# CHECK-LABEL: <f_wbarwua>:
# CHECK: { nop; nop; wbarwua 1, r3 }
# ENC-LABEL: f_wbarwua:
# ENC: { nop; nop; wbarwua 1, r3 } // encoding: [0xcf,0x40,0x61,0x00,0x48,0x00,0x00,0x00,0x20,0x00,0x00,0x00]
f_wbarwua:
  { wbarwua 1, r3 }

# CHECK-LABEL: <f_d_lqhwua_post>:
# CHECK: { d_lqhwua_post d7, 2, r4; nop; nop }
# ENC-LABEL: f_d_lqhwua_post:
# ENC: { d_lqhwua_post d7, 2, r4; nop; nop } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x30,0xec,0x48,0x00]
f_d_lqhwua_post:
  { d_lqhwua_post d7, 2, r4 }

# CHECK-LABEL: <f_d_ltwua_post>:
# CHECK: { d_ltwua_post d3, 0, r6; nop; nop }
# ENC-LABEL: f_d_ltwua_post:
# ENC: { d_ltwua_post d3, 0, r6; nop; nop } // encoding: [0x8f,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x30,0x68,0x0c,0x00]
f_d_ltwua_post:
  { d_ltwua_post d3, 0, r6 }

# CHECK-LABEL: <f_d_sqhwua_post>:
# CHECK: { nop; nop; d_sqhwua_post d1, 3, r8 }
# ENC-LABEL: f_d_sqhwua_post:
# ENC: { nop; nop; d_sqhwua_post d1, 3, r8 } // encoding: [0xcf,0xc0,0x03,0x01,0x58,0x00,0x00,0x00,0x20,0x00,0x00,0x00]
f_d_sqhwua_post:
  { d_sqhwua_post d1, 3, r8 }

# CHECK-LABEL: <f_d_stwua_post>:
# CHECK: { nop; nop; d_stwua_post d2, 0, r10 }
# ENC-LABEL: f_d_stwua_post:
# ENC: { nop; nop; d_stwua_post d2, 0, r10 } // encoding: [0xcf,0x80,0x45,0x01,0x40,0x00,0x00,0x00,0x20,0x00,0x00,0x00]
f_d_stwua_post:
  { d_stwua_post d2, 0, r10 }

#===----------------------------------------------------------------------===#
# Canonical operand corners (ar_sel=0, d0/r1)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_corners>:
# CHECK: { pldwwua_post 0, r1; nop; nop }
# CHECK: { d_lqhwua_post d0, 0, r1; nop; nop }
# CHECK: { d_ltwua_post d0, 0, r1; nop; nop }
# CHECK: { nop; nop; d_sqhwua_post d0, 0, r1 }
# CHECK: { nop; nop; d_stwua_post d0, 0, r1 }
# CHECK: { flar 0; nop; nop }
# CHECK: { nop; nop; wbarwua 0, r1 }
f_corners:
  { pldwwua_post 0, r1 }
  { d_lqhwua_post d0, 0, r1 }
  { d_ltwua_post d0, 0, r1 }
  { d_sqhwua_post d0, 0, r1 }
  { d_stwua_post d0, 0, r1 }
  { flar 0 }
  { wbarwua 0, r1 }

#===----------------------------------------------------------------------===#
# Stream sequences (matches intrinsic e2e usage order)
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_load_stream>:
# CHECK: { pldwwua_post 0, r1; nop; nop }
# CHECK: { d_lqhwua_post d0, 0, r2; nop; nop }
# CHECK: { d_ltwua_post d1, 0, r2; nop; nop }
# CHECK: { flar 0; nop; nop }
f_load_stream:
  { pldwwua_post 0, r1 }
  { d_lqhwua_post d0, 0, r2 }
  { d_ltwua_post d1, 0, r2 }
  { flar 0 }

# CHECK-LABEL: <f_store_stream>:
# CHECK: { pldwwua_post 0, r1; nop; nop }
# CHECK: { nop; nop; d_sqhwua_post d0, 0, r2 }
# CHECK: { nop; nop; d_stwua_post d1, 0, r2 }
# CHECK: { nop; nop; wbarwua 0, r2 }
f_store_stream:
  { pldwwua_post 0, r1 }
  { d_sqhwua_post d0, 0, r2 }
  { d_stwua_post d1, 0, r2 }
  { wbarwua 0, r2 }

#===----------------------------------------------------------------------===#
# Dual-issue: AR + ALU32 in one bundle
#===----------------------------------------------------------------------===#

# CHECK-LABEL: <f_dual_pldw_add>:
# Two ops fit one bundle, so this is the 2-entry composite and NOT the
# NOP-padded 3-entry one every case above produces. That is the difference
# the 12 encoding bytes below are here to hold: 0x07 in byte 0 against the
# 0x8f / 0xcf of the padded forms.
# CHECK: { pldwwua_post 0, r1; add32 r2, r3, r4 }
# ENC-LABEL: f_dual_pldw_add:
# ENC: { pldwwua_post 0, r1; add32 r2, r3, r4 } // encoding: [0x07,0x8b,0x20,0x43,0x00,0x00,0x10,0x20,0x10,0x00,0x00,0x00]
f_dual_pldw_add:
  { pldwwua_post 0, r1 ; add32 r2, r3, r4 }
