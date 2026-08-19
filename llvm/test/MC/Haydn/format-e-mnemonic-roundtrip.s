# RUN: %python %S/../../../utils/haydn/check_mc_mnemonic_coverage.py
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target
#
# DO NOT EDIT.
# Generator: llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py
# Schema: format-e-records/1
# Family: e96 (id=0)
# Check: python3 llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py --check --family e96
# Umbrella: llvm/lib/Target/Haydn/FormatE/check_generated.sh
# Overlay: authored_catalog_overlay.json
# Overlay-SHA256: 276dea569f8761e81bb3d17e041a4f74911d34ba6f30711939a293519ac531fc
# One Format E packet per product non-NOP logical from the golden
# member table. Packet form follows the first generated member's
# Mode and EntryIdx (high-entry-first TEXT). Unused entries are
# architectural NOP: E2 e0 is `{ nop; insn }`, E3 e0 is
# `{ nop; nop; insn }`. Never a singleton/underfill packet.
# Bare nop is covered in nop-format-e-not-all-zero.s. Hypothesized
# Auto.td encodings are
# isCodeGenOnly and live in auto-hypothesized-unencodable.s, not here.
#
# Role: object — assemble each product mnemonic, require a 12-byte
# non-all-zero parcel, and require objdump to print the logical name.
# Peer: llvm/test/MC/Hexagon/v67_all.s (mnemonic × assemble+objdump).
# ENC-COUNT-806: encoding: [
# ENC-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]

.text

# MNEM: abs32
rt_abs32:
{ nop; abs32 r1, r2 }
# DIS-LABEL: <rt_abs32>:
# DIS: {{[ \t]}}abs32{{[ \t,;]}}

# MNEM: abs32s
rt_abs32s:
{ nop; abs32s r1, r2 }
# DIS-LABEL: <rt_abs32s>:
# DIS: {{[ \t]}}abs32s{{[ \t,;]}}

# MNEM: abs64
rt_abs64:
{ nop; abs64 d0, d1 }
# DIS-LABEL: <rt_abs64>:
# DIS: {{[ \t]}}abs64{{[ \t,;]}}

# MNEM: abs64s
rt_abs64s:
{ nop; abs64s d0, d1 }
# DIS-LABEL: <rt_abs64s>:
# DIS: {{[ \t]}}abs64s{{[ \t,;]}}

# MNEM: add32
rt_add32:
{ nop; add32 r1, r2, r3 }
# DIS-LABEL: <rt_add32>:
# DIS: {{[ \t]}}add32{{[ \t,;]}}

# MNEM: add32s
rt_add32s:
{ nop; add32s r1, r2, r3 }
# DIS-LABEL: <rt_add32s>:
# DIS: {{[ \t]}}add32s{{[ \t,;]}}

# MNEM: add64
rt_add64:
{ nop; add64 d0, d1, d2 }
# DIS-LABEL: <rt_add64>:
# DIS: {{[ \t]}}add64{{[ \t,;]}}

# MNEM: add64s
rt_add64s:
{ nop; add64s d0, d1, d2 }
# DIS-LABEL: <rt_add64s>:
# DIS: {{[ \t]}}add64s{{[ \t,;]}}

# MNEM: add64s_h
rt_add64s_h:
{ nop; add64s_h d0, d1, d2 }
# DIS-LABEL: <rt_add64s_h>:
# DIS: {{[ \t]}}add64s_h{{[ \t,;]}}

# MNEM: add64s_l
rt_add64s_l:
{ nop; add64s_l d0, d1, d2 }
# DIS-LABEL: <rt_add64s_l>:
# DIS: {{[ \t]}}add64s_l{{[ \t,;]}}

# MNEM: add64_h
rt_add64_h:
{ nop; add64_h d0, d1, d2 }
# DIS-LABEL: <rt_add64_h>:
# DIS: {{[ \t]}}add64_h{{[ \t,;]}}

# MNEM: add64_l
rt_add64_l:
{ nop; add64_l d0, d1, d2 }
# DIS-LABEL: <rt_add64_l>:
# DIS: {{[ \t]}}add64_l{{[ \t,;]}}

# MNEM: addi32
rt_addi32:
{ nop; addi32 r1, r2, 1 }
# DIS-LABEL: <rt_addi32>:
# DIS: {{[ \t]}}addi32{{[ \t,;]}}

# MNEM: addi32s
rt_addi32s:
{ nop; addi32s r1, r2, 1 }
# DIS-LABEL: <rt_addi32s>:
# DIS: {{[ \t]}}addi32s{{[ \t,;]}}

# MNEM: and32
rt_and32:
{ nop; and32 r1, r2, r3 }
# DIS-LABEL: <rt_and32>:
# DIS: {{[ \t]}}and32{{[ \t,;]}}

# MNEM: and64
rt_and64:
{ nop; and64 d0, d1, d2 }
# DIS-LABEL: <rt_and64>:
# DIS: {{[ \t]}}and64{{[ \t,;]}}

# MNEM: andi32
rt_andi32:
{ nop; andi32 r1, r2, 1 }
# DIS-LABEL: <rt_andi32>:
# DIS: {{[ \t]}}andi32{{[ \t,;]}}

# MNEM: arctan
rt_arctan:
{ nop; nop; arctan r1, d0, 1 }
# DIS-LABEL: <rt_arctan>:
# DIS: {{[ \t]}}arctan{{[ \t,;]}}

# MNEM: beq
rt_beq:
{ nop; beq r1, r2, 0 }
# DIS-LABEL: <rt_beq>:
# DIS: {{[ \t]}}beq{{[ \t,;]}}

# MNEM: beqz
rt_beqz:
{ nop; beqz r1, 0 }
# DIS-LABEL: <rt_beqz>:
# DIS: {{[ \t]}}beqz{{[ \t,;]}}

# MNEM: bge
rt_bge:
{ nop; bge r1, r2, 0 }
# DIS-LABEL: <rt_bge>:
# DIS: {{[ \t]}}bge{{[ \t,;]}}

# MNEM: bgeu
rt_bgeu:
{ nop; bgeu r1, r2, 0 }
# DIS-LABEL: <rt_bgeu>:
# DIS: {{[ \t]}}bgeu{{[ \t,;]}}

# MNEM: bgez
rt_bgez:
{ nop; bgez r1, 0 }
# DIS-LABEL: <rt_bgez>:
# DIS: {{[ \t]}}bgez{{[ \t,;]}}

# MNEM: blt
rt_blt:
{ nop; blt r1, r2, 0 }
# DIS-LABEL: <rt_blt>:
# DIS: {{[ \t]}}blt{{[ \t,;]}}

# MNEM: bltu
rt_bltu:
{ nop; bltu r1, r2, 0 }
# DIS-LABEL: <rt_bltu>:
# DIS: {{[ \t]}}bltu{{[ \t,;]}}

# MNEM: bltz
rt_bltz:
{ nop; bltz r1, 0 }
# DIS-LABEL: <rt_bltz>:
# DIS: {{[ \t]}}bltz{{[ \t,;]}}

# MNEM: bne
rt_bne:
{ nop; bne r1, r2, 0 }
# DIS-LABEL: <rt_bne>:
# DIS: {{[ \t]}}bne{{[ \t,;]}}

# MNEM: bnez
rt_bnez:
{ nop; bnez r1, 0 }
# DIS-LABEL: <rt_bnez>:
# DIS: {{[ \t]}}bnez{{[ \t,;]}}

# MNEM: brev32
rt_brev32:
{ nop; brev32 r1, r2, r3 }
# DIS-LABEL: <rt_brev32>:
# DIS: {{[ \t]}}brev32{{[ \t,;]}}

# MNEM: csrr
rt_csrr:
{ nop; csrr r1, 1 }
# DIS-LABEL: <rt_csrr>:
# DIS: {{[ \t]}}csrr{{[ \t,;]}}

# MNEM: csrw
rt_csrw:
{ nop; csrw 1, r1 }
# DIS-LABEL: <rt_csrw>:
# DIS: {{[ \t]}}csrw{{[ \t,;]}}

# MNEM: d_ldw_brev_imm
rt_d_ldw_brev_imm:
{ nop; d_ldw_brev_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_ldw_brev_imm>:
# DIS: {{[ \t]}}d_ldw_brev_imm{{[ \t,;]}}

# MNEM: d_ldw_brev_reg
rt_d_ldw_brev_reg:
{ nop; d_ldw_brev_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_ldw_brev_reg>:
# DIS: {{[ \t]}}d_ldw_brev_reg{{[ \t,;]}}

# MNEM: d_ldw_cb_imm
rt_d_ldw_cb_imm:
{ nop; d_ldw_cb_imm 0, d0, r1, 1 }
# DIS-LABEL: <rt_d_ldw_cb_imm>:
# DIS: {{[ \t]}}d_ldw_cb_imm{{[ \t,;]}}

# MNEM: d_ldw_cb_reg
rt_d_ldw_cb_reg:
{ nop; d_ldw_cb_reg 0, d0, r1, r2 }
# DIS-LABEL: <rt_d_ldw_cb_reg>:
# DIS: {{[ \t]}}d_ldw_cb_reg{{[ \t,;]}}

# MNEM: d_ldw_post_imm
rt_d_ldw_post_imm:
{ nop; d_ldw_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_ldw_post_imm>:
# DIS: {{[ \t]}}d_ldw_post_imm{{[ \t,;]}}

# MNEM: d_ldw_post_reg
rt_d_ldw_post_reg:
{ nop; d_ldw_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_ldw_post_reg>:
# DIS: {{[ \t]}}d_ldw_post_reg{{[ \t,;]}}

# MNEM: d_ldw_pre_imm
rt_d_ldw_pre_imm:
{ nop; d_ldw_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_ldw_pre_imm>:
# DIS: {{[ \t]}}d_ldw_pre_imm{{[ \t,;]}}

# MNEM: d_ldw_pre_reg
rt_d_ldw_pre_reg:
{ nop; d_ldw_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_ldw_pre_reg>:
# DIS: {{[ \t]}}d_ldw_pre_reg{{[ \t,;]}}

# MNEM: ld64
# LOGICAL: d_ldw_with_imm
rt_ld64:
{ nop; ld64 d0, r1, 1 }
# DIS-LABEL: <rt_ld64>:
# DIS: {{[ \t]}}ld64{{[ \t,;]}}

# MNEM: ld64_reg
# LOGICAL: d_ldw_with_reg
rt_ld64_reg:
{ nop; ld64_reg d0, r1, r2 }
# DIS-LABEL: <rt_ld64_reg>:
# DIS: {{[ \t]}}ld64_reg{{[ \t,;]}}

# MNEM: d_lhw_post_imm
rt_d_lhw_post_imm:
{ nop; d_lhw_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lhw_post_imm>:
# DIS: {{[ \t]}}d_lhw_post_imm{{[ \t,;]}}

# MNEM: d_lhw_post_reg
rt_d_lhw_post_reg:
{ nop; d_lhw_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lhw_post_reg>:
# DIS: {{[ \t]}}d_lhw_post_reg{{[ \t,;]}}

# MNEM: d_lhw_pre_imm
rt_d_lhw_pre_imm:
{ nop; d_lhw_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lhw_pre_imm>:
# DIS: {{[ \t]}}d_lhw_pre_imm{{[ \t,;]}}

# MNEM: d_lhw_pre_reg
rt_d_lhw_pre_reg:
{ nop; d_lhw_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lhw_pre_reg>:
# DIS: {{[ \t]}}d_lhw_pre_reg{{[ \t,;]}}

# MNEM: d_lhw_with_imm
rt_d_lhw_with_imm:
{ nop; d_lhw_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lhw_with_imm>:
# DIS: {{[ \t]}}d_lhw_with_imm{{[ \t,;]}}

# MNEM: d_lhw_with_reg
rt_d_lhw_with_reg:
{ nop; d_lhw_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lhw_with_reg>:
# DIS: {{[ \t]}}d_lhw_with_reg{{[ \t,;]}}

# MNEM: d_lqhwua_post
rt_d_lqhwua_post:
{ nop; d_lqhwua_post d0, 0, r1, r2, 0 }
# DIS-LABEL: <rt_d_lqhwua_post>:
# DIS: {{[ \t]}}d_lqhwua_post{{[ \t,;]}}

# MNEM: d_ltwua_post
rt_d_ltwua_post:
{ nop; d_ltwua_post d0, 0, r1, r2, 0 }
# DIS-LABEL: <rt_d_ltwua_post>:
# DIS: {{[ \t]}}d_ltwua_post{{[ \t,;]}}

# MNEM: d_lw_post_imm
rt_d_lw_post_imm:
{ nop; d_lw_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lw_post_imm>:
# DIS: {{[ \t]}}d_lw_post_imm{{[ \t,;]}}

# MNEM: d_lw_post_reg
rt_d_lw_post_reg:
{ nop; d_lw_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lw_post_reg>:
# DIS: {{[ \t]}}d_lw_post_reg{{[ \t,;]}}

# MNEM: d_lw_pre_imm
rt_d_lw_pre_imm:
{ nop; d_lw_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lw_pre_imm>:
# DIS: {{[ \t]}}d_lw_pre_imm{{[ \t,;]}}

# MNEM: d_lw_pre_reg
rt_d_lw_pre_reg:
{ nop; d_lw_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lw_pre_reg>:
# DIS: {{[ \t]}}d_lw_pre_reg{{[ \t,;]}}

# MNEM: d_lw_with_imm
rt_d_lw_with_imm:
{ nop; d_lw_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_lw_with_imm>:
# DIS: {{[ \t]}}d_lw_with_imm{{[ \t,;]}}

# MNEM: d_lw_with_reg
rt_d_lw_with_reg:
{ nop; d_lw_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_lw_with_reg>:
# DIS: {{[ \t]}}d_lw_with_reg{{[ \t,;]}}

# MNEM: d_sdw_brev_imm
rt_d_sdw_brev_imm:
{ nop; d_sdw_brev_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sdw_brev_imm>:
# DIS: {{[ \t]}}d_sdw_brev_imm{{[ \t,;]}}

# MNEM: d_sdw_brev_reg
rt_d_sdw_brev_reg:
{ nop; d_sdw_brev_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sdw_brev_reg>:
# DIS: {{[ \t]}}d_sdw_brev_reg{{[ \t,;]}}

# MNEM: d_sdw_cb_imm
rt_d_sdw_cb_imm:
{ nop; d_sdw_cb_imm 0, d0, r1, 1 }
# DIS-LABEL: <rt_d_sdw_cb_imm>:
# DIS: {{[ \t]}}d_sdw_cb_imm{{[ \t,;]}}

# MNEM: d_sdw_cb_reg
rt_d_sdw_cb_reg:
{ nop; d_sdw_cb_reg 0, d0, r1, r2 }
# DIS-LABEL: <rt_d_sdw_cb_reg>:
# DIS: {{[ \t]}}d_sdw_cb_reg{{[ \t,;]}}

# MNEM: d_sdw_post_imm
rt_d_sdw_post_imm:
{ nop; d_sdw_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sdw_post_imm>:
# DIS: {{[ \t]}}d_sdw_post_imm{{[ \t,;]}}

# MNEM: d_sdw_post_reg
rt_d_sdw_post_reg:
{ nop; d_sdw_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sdw_post_reg>:
# DIS: {{[ \t]}}d_sdw_post_reg{{[ \t,;]}}

# MNEM: d_sdw_pre_imm
rt_d_sdw_pre_imm:
{ nop; d_sdw_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sdw_pre_imm>:
# DIS: {{[ \t]}}d_sdw_pre_imm{{[ \t,;]}}

# MNEM: d_sdw_pre_reg
rt_d_sdw_pre_reg:
{ nop; d_sdw_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sdw_pre_reg>:
# DIS: {{[ \t]}}d_sdw_pre_reg{{[ \t,;]}}

# MNEM: st64
# LOGICAL: d_sdw_with_imm
rt_st64:
{ nop; st64 d0, r1, 1 }
# DIS-LABEL: <rt_st64>:
# DIS: {{[ \t]}}st64{{[ \t,;]}}

# MNEM: st64_reg
# LOGICAL: d_sdw_with_reg
rt_st64_reg:
{ nop; st64_reg d0, r1, r2 }
# DIS-LABEL: <rt_st64_reg>:
# DIS: {{[ \t]}}st64_reg{{[ \t,;]}}

# MNEM: d_shw_post_imm
rt_d_shw_post_imm:
{ nop; d_shw_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_shw_post_imm>:
# DIS: {{[ \t]}}d_shw_post_imm{{[ \t,;]}}

# MNEM: d_shw_post_reg
rt_d_shw_post_reg:
{ nop; d_shw_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_shw_post_reg>:
# DIS: {{[ \t]}}d_shw_post_reg{{[ \t,;]}}

# MNEM: d_shw_pre_imm
rt_d_shw_pre_imm:
{ nop; d_shw_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_shw_pre_imm>:
# DIS: {{[ \t]}}d_shw_pre_imm{{[ \t,;]}}

# MNEM: d_shw_pre_reg
rt_d_shw_pre_reg:
{ nop; d_shw_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_shw_pre_reg>:
# DIS: {{[ \t]}}d_shw_pre_reg{{[ \t,;]}}

# MNEM: d_shw_with_imm
rt_d_shw_with_imm:
{ nop; d_shw_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_shw_with_imm>:
# DIS: {{[ \t]}}d_shw_with_imm{{[ \t,;]}}

# MNEM: d_shw_with_reg
rt_d_shw_with_reg:
{ nop; d_shw_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_shw_with_reg>:
# DIS: {{[ \t]}}d_shw_with_reg{{[ \t,;]}}

# MNEM: d_sqhwua_post
rt_d_sqhwua_post:
{ nop; d_sqhwua_post d0, 0, r1, r2, 0 }
# DIS-LABEL: <rt_d_sqhwua_post>:
# DIS: {{[ \t]}}d_sqhwua_post{{[ \t,;]}}

# MNEM: d_stwua_post
rt_d_stwua_post:
{ nop; d_stwua_post d0, 0, r1, r2, 0 }
# DIS-LABEL: <rt_d_stwua_post>:
# DIS: {{[ \t]}}d_stwua_post{{[ \t,;]}}

# MNEM: d_sw_f64rs_post_imm
rt_d_sw_f64rs_post_imm:
{ nop; d_sw_f64rs_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_f64rs_post_imm>:
# DIS: {{[ \t]}}d_sw_f64rs_post_imm{{[ \t,;]}}

# MNEM: d_sw_f64rs_post_reg
rt_d_sw_f64rs_post_reg:
{ nop; d_sw_f64rs_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_f64rs_post_reg>:
# DIS: {{[ \t]}}d_sw_f64rs_post_reg{{[ \t,;]}}

# MNEM: d_sw_f64rs_with_imm
rt_d_sw_f64rs_with_imm:
{ nop; d_sw_f64rs_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_f64rs_with_imm>:
# DIS: {{[ \t]}}d_sw_f64rs_with_imm{{[ \t,;]}}

# MNEM: d_sw_f64rs_with_reg
rt_d_sw_f64rs_with_reg:
{ nop; d_sw_f64rs_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_f64rs_with_reg>:
# DIS: {{[ \t]}}d_sw_f64rs_with_reg{{[ \t,;]}}

# MNEM: d_sw_h_post_imm
rt_d_sw_h_post_imm:
{ nop; d_sw_h_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_h_post_imm>:
# DIS: {{[ \t]}}d_sw_h_post_imm{{[ \t,;]}}

# MNEM: d_sw_h_post_reg
rt_d_sw_h_post_reg:
{ nop; d_sw_h_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_h_post_reg>:
# DIS: {{[ \t]}}d_sw_h_post_reg{{[ \t,;]}}

# MNEM: d_sw_h_pre_imm
rt_d_sw_h_pre_imm:
{ nop; d_sw_h_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_h_pre_imm>:
# DIS: {{[ \t]}}d_sw_h_pre_imm{{[ \t,;]}}

# MNEM: d_sw_h_pre_reg
rt_d_sw_h_pre_reg:
{ nop; d_sw_h_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_h_pre_reg>:
# DIS: {{[ \t]}}d_sw_h_pre_reg{{[ \t,;]}}

# MNEM: d_sw_h_with_imm
rt_d_sw_h_with_imm:
{ nop; d_sw_h_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_h_with_imm>:
# DIS: {{[ \t]}}d_sw_h_with_imm{{[ \t,;]}}

# MNEM: d_sw_h_with_reg
rt_d_sw_h_with_reg:
{ nop; d_sw_h_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_h_with_reg>:
# DIS: {{[ \t]}}d_sw_h_with_reg{{[ \t,;]}}

# MNEM: d_sw_l_post_imm
rt_d_sw_l_post_imm:
{ nop; d_sw_l_post_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_l_post_imm>:
# DIS: {{[ \t]}}d_sw_l_post_imm{{[ \t,;]}}

# MNEM: d_sw_l_post_reg
rt_d_sw_l_post_reg:
{ nop; d_sw_l_post_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_l_post_reg>:
# DIS: {{[ \t]}}d_sw_l_post_reg{{[ \t,;]}}

# MNEM: d_sw_l_pre_imm
rt_d_sw_l_pre_imm:
{ nop; d_sw_l_pre_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_l_pre_imm>:
# DIS: {{[ \t]}}d_sw_l_pre_imm{{[ \t,;]}}

# MNEM: d_sw_l_pre_reg
rt_d_sw_l_pre_reg:
{ nop; d_sw_l_pre_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_l_pre_reg>:
# DIS: {{[ \t]}}d_sw_l_pre_reg{{[ \t,;]}}

# MNEM: d_sw_l_with_imm
rt_d_sw_l_with_imm:
{ nop; d_sw_l_with_imm d0, r1, 1 }
# DIS-LABEL: <rt_d_sw_l_with_imm>:
# DIS: {{[ \t]}}d_sw_l_with_imm{{[ \t,;]}}

# MNEM: d_sw_l_with_reg
rt_d_sw_l_with_reg:
{ nop; d_sw_l_with_reg d0, r1, r2 }
# DIS-LABEL: <rt_d_sw_l_with_reg>:
# DIS: {{[ \t]}}d_sw_l_with_reg{{[ \t,;]}}

# MNEM: exp2
rt_exp2:
{ nop; nop; exp2 r1, r2 }
# DIS-LABEL: <rt_exp2>:
# DIS: {{[ \t]}}exp2{{[ \t,;]}}

# MNEM: f2mulaa32rs.hhll
# LOGICAL: f2mulaa32rs_hhll
rt_f2mulaa32rs_hhll:
{ nop; f2mulaa32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulaa32rs_hhll>:
# DIS: {{[ \t]}}f2mulaa32rs.hhll{{[ \t,;]}}

# MNEM: f2mulaa32rs.hllh
# LOGICAL: f2mulaa32rs_hllh
rt_f2mulaa32rs_hllh:
{ nop; f2mulaa32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulaa32rs_hllh>:
# DIS: {{[ \t]}}f2mulaa32rs.hllh{{[ \t,;]}}

# MNEM: f2mulaa32r.hhll
# LOGICAL: f2mulaa32r_hhll
rt_f2mulaa32r_hhll:
{ nop; f2mulaa32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulaa32r_hhll>:
# DIS: {{[ \t]}}f2mulaa32r.hhll{{[ \t,;]}}

# MNEM: f2mulaa32r.hllh
# LOGICAL: f2mulaa32r_hllh
rt_f2mulaa32r_hllh:
{ nop; f2mulaa32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulaa32r_hllh>:
# DIS: {{[ \t]}}f2mulaa32r.hllh{{[ \t,;]}}

# MNEM: f2mulas32rs.hhll
# LOGICAL: f2mulas32rs_hhll
rt_f2mulas32rs_hhll:
{ nop; f2mulas32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulas32rs_hhll>:
# DIS: {{[ \t]}}f2mulas32rs.hhll{{[ \t,;]}}

# MNEM: f2mulas32rs.hllh
# LOGICAL: f2mulas32rs_hllh
rt_f2mulas32rs_hllh:
{ nop; f2mulas32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulas32rs_hllh>:
# DIS: {{[ \t]}}f2mulas32rs.hllh{{[ \t,;]}}

# MNEM: f2mulas32r.hhll
# LOGICAL: f2mulas32r_hhll
rt_f2mulas32r_hhll:
{ nop; f2mulas32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulas32r_hhll>:
# DIS: {{[ \t]}}f2mulas32r.hhll{{[ \t,;]}}

# MNEM: f2mulas32r.hllh
# LOGICAL: f2mulas32r_hllh
rt_f2mulas32r_hllh:
{ nop; f2mulas32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulas32r_hllh>:
# DIS: {{[ \t]}}f2mulas32r.hllh{{[ \t,;]}}

# MNEM: f2mulsa32rs.hhll
# LOGICAL: f2mulsa32rs_hhll
rt_f2mulsa32rs_hhll:
{ nop; f2mulsa32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulsa32rs_hhll>:
# DIS: {{[ \t]}}f2mulsa32rs.hhll{{[ \t,;]}}

# MNEM: f2mulsa32rs.hllh
# LOGICAL: f2mulsa32rs_hllh
rt_f2mulsa32rs_hllh:
{ nop; f2mulsa32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulsa32rs_hllh>:
# DIS: {{[ \t]}}f2mulsa32rs.hllh{{[ \t,;]}}

# MNEM: f2mulsa32r.hhll
# LOGICAL: f2mulsa32r_hhll
rt_f2mulsa32r_hhll:
{ nop; f2mulsa32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulsa32r_hhll>:
# DIS: {{[ \t]}}f2mulsa32r.hhll{{[ \t,;]}}

# MNEM: f2mulsa32r.hllh
# LOGICAL: f2mulsa32r_hllh
rt_f2mulsa32r_hllh:
{ nop; f2mulsa32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulsa32r_hllh>:
# DIS: {{[ \t]}}f2mulsa32r.hllh{{[ \t,;]}}

# MNEM: f2mulss32rs.hhll
# LOGICAL: f2mulss32rs_hhll
rt_f2mulss32rs_hhll:
{ nop; f2mulss32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulss32rs_hhll>:
# DIS: {{[ \t]}}f2mulss32rs.hhll{{[ \t,;]}}

# MNEM: f2mulss32rs.hllh
# LOGICAL: f2mulss32rs_hllh
rt_f2mulss32rs_hllh:
{ nop; f2mulss32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulss32rs_hllh>:
# DIS: {{[ \t]}}f2mulss32rs.hllh{{[ \t,;]}}

# MNEM: f2mulss32r.hhll
# LOGICAL: f2mulss32r_hhll
rt_f2mulss32r_hhll:
{ nop; f2mulss32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulss32r_hhll>:
# DIS: {{[ \t]}}f2mulss32r.hhll{{[ \t,;]}}

# MNEM: f2mulss32r.hllh
# LOGICAL: f2mulss32r_hllh
rt_f2mulss32r_hllh:
{ nop; f2mulss32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulss32r_hllh>:
# DIS: {{[ \t]}}f2mulss32r.hllh{{[ \t,;]}}

# MNEM: f2mulzaa32rs.hhll
# LOGICAL: f2mulzaa32rs_hhll
rt_f2mulzaa32rs_hhll:
{ nop; f2mulzaa32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzaa32rs_hhll>:
# DIS: {{[ \t]}}f2mulzaa32rs.hhll{{[ \t,;]}}

# MNEM: f2mulzaa32rs.hllh
# LOGICAL: f2mulzaa32rs_hllh
rt_f2mulzaa32rs_hllh:
{ nop; f2mulzaa32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzaa32rs_hllh>:
# DIS: {{[ \t]}}f2mulzaa32rs.hllh{{[ \t,;]}}

# MNEM: f2mulzaa32r.hhll
# LOGICAL: f2mulzaa32r_hhll
rt_f2mulzaa32r_hhll:
{ nop; f2mulzaa32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzaa32r_hhll>:
# DIS: {{[ \t]}}f2mulzaa32r.hhll{{[ \t,;]}}

# MNEM: f2mulzaa32r.hllh
# LOGICAL: f2mulzaa32r_hllh
rt_f2mulzaa32r_hllh:
{ nop; f2mulzaa32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzaa32r_hllh>:
# DIS: {{[ \t]}}f2mulzaa32r.hllh{{[ \t,;]}}

# MNEM: f2mulzas32rs.hhll
# LOGICAL: f2mulzas32rs_hhll
rt_f2mulzas32rs_hhll:
{ nop; f2mulzas32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzas32rs_hhll>:
# DIS: {{[ \t]}}f2mulzas32rs.hhll{{[ \t,;]}}

# MNEM: f2mulzas32rs.hllh
# LOGICAL: f2mulzas32rs_hllh
rt_f2mulzas32rs_hllh:
{ nop; f2mulzas32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzas32rs_hllh>:
# DIS: {{[ \t]}}f2mulzas32rs.hllh{{[ \t,;]}}

# MNEM: f2mulzas32r.hhll
# LOGICAL: f2mulzas32r_hhll
rt_f2mulzas32r_hhll:
{ nop; f2mulzas32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzas32r_hhll>:
# DIS: {{[ \t]}}f2mulzas32r.hhll{{[ \t,;]}}

# MNEM: f2mulzas32r.hllh
# LOGICAL: f2mulzas32r_hllh
rt_f2mulzas32r_hllh:
{ nop; f2mulzas32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzas32r_hllh>:
# DIS: {{[ \t]}}f2mulzas32r.hllh{{[ \t,;]}}

# MNEM: f2mulzsa32rs.hhll
# LOGICAL: f2mulzsa32rs_hhll
rt_f2mulzsa32rs_hhll:
{ nop; f2mulzsa32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzsa32rs_hhll>:
# DIS: {{[ \t]}}f2mulzsa32rs.hhll{{[ \t,;]}}

# MNEM: f2mulzsa32rs.hllh
# LOGICAL: f2mulzsa32rs_hllh
rt_f2mulzsa32rs_hllh:
{ nop; f2mulzsa32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzsa32rs_hllh>:
# DIS: {{[ \t]}}f2mulzsa32rs.hllh{{[ \t,;]}}

# MNEM: f2mulzsa32r.hhll
# LOGICAL: f2mulzsa32r_hhll
rt_f2mulzsa32r_hhll:
{ nop; f2mulzsa32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzsa32r_hhll>:
# DIS: {{[ \t]}}f2mulzsa32r.hhll{{[ \t,;]}}

# MNEM: f2mulzsa32r.hllh
# LOGICAL: f2mulzsa32r_hllh
rt_f2mulzsa32r_hllh:
{ nop; f2mulzsa32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzsa32r_hllh>:
# DIS: {{[ \t]}}f2mulzsa32r.hllh{{[ \t,;]}}

# MNEM: f2mulzss32rs.hhll
# LOGICAL: f2mulzss32rs_hhll
rt_f2mulzss32rs_hhll:
{ nop; f2mulzss32rs.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzss32rs_hhll>:
# DIS: {{[ \t]}}f2mulzss32rs.hhll{{[ \t,;]}}

# MNEM: f2mulzss32rs.hllh
# LOGICAL: f2mulzss32rs_hllh
rt_f2mulzss32rs_hllh:
{ nop; f2mulzss32rs.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzss32rs_hllh>:
# DIS: {{[ \t]}}f2mulzss32rs.hllh{{[ \t,;]}}

# MNEM: f2mulzss32r.hhll
# LOGICAL: f2mulzss32r_hhll
rt_f2mulzss32r_hhll:
{ nop; f2mulzss32r.hhll d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzss32r_hhll>:
# DIS: {{[ \t]}}f2mulzss32r.hhll{{[ \t,;]}}

# MNEM: f2mulzss32r.hllh
# LOGICAL: f2mulzss32r_hllh
rt_f2mulzss32r_hllh:
{ nop; f2mulzss32r.hllh d0, d1, d2 }
# DIS-LABEL: <rt_f2mulzss32r_hllh>:
# DIS: {{[ \t]}}f2mulzss32r.hllh{{[ \t,;]}}

# MNEM: ff2mul32rs.hh
# LOGICAL: ff2mul32rs_hh
rt_ff2mul32rs_hh:
{ nop; ff2mul32rs.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32rs_hh>:
# DIS: {{[ \t]}}ff2mul32rs.hh{{[ \t,;]}}

# MNEM: ff2mul32rs.lh
# LOGICAL: ff2mul32rs_lh
rt_ff2mul32rs_lh:
{ nop; ff2mul32rs.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32rs_lh>:
# DIS: {{[ \t]}}ff2mul32rs.lh{{[ \t,;]}}

# MNEM: ff2mul32rs.ll
# LOGICAL: ff2mul32rs_ll
rt_ff2mul32rs_ll:
{ nop; ff2mul32rs.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32rs_ll>:
# DIS: {{[ \t]}}ff2mul32rs.ll{{[ \t,;]}}

# MNEM: ff2mul32r.hh
# LOGICAL: ff2mul32r_hh
rt_ff2mul32r_hh:
{ nop; ff2mul32r.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32r_hh>:
# DIS: {{[ \t]}}ff2mul32r.hh{{[ \t,;]}}

# MNEM: ff2mul32r.lh
# LOGICAL: ff2mul32r_lh
rt_ff2mul32r_lh:
{ nop; ff2mul32r.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32r_lh>:
# DIS: {{[ \t]}}ff2mul32r.lh{{[ \t,;]}}

# MNEM: ff2mul32r.ll
# LOGICAL: ff2mul32r_ll
rt_ff2mul32r_ll:
{ nop; ff2mul32r.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2mul32r_ll>:
# DIS: {{[ \t]}}ff2mul32r.ll{{[ \t,;]}}

# MNEM: ff2mula32rs.hh
# LOGICAL: ff2mula32rs_hh
rt_ff2mula32rs_hh:
{ nop; ff2mula32rs.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32rs_hh>:
# DIS: {{[ \t]}}ff2mula32rs.hh{{[ \t,;]}}

# MNEM: ff2mula32rs.lh
# LOGICAL: ff2mula32rs_lh
rt_ff2mula32rs_lh:
{ nop; ff2mula32rs.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32rs_lh>:
# DIS: {{[ \t]}}ff2mula32rs.lh{{[ \t,;]}}

# MNEM: ff2mula32rs.ll
# LOGICAL: ff2mula32rs_ll
rt_ff2mula32rs_ll:
{ nop; ff2mula32rs.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32rs_ll>:
# DIS: {{[ \t]}}ff2mula32rs.ll{{[ \t,;]}}

# MNEM: ff2mula32r.hh
# LOGICAL: ff2mula32r_hh
rt_ff2mula32r_hh:
{ nop; ff2mula32r.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32r_hh>:
# DIS: {{[ \t]}}ff2mula32r.hh{{[ \t,;]}}

# MNEM: ff2mula32r.lh
# LOGICAL: ff2mula32r_lh
rt_ff2mula32r_lh:
{ nop; ff2mula32r.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32r_lh>:
# DIS: {{[ \t]}}ff2mula32r.lh{{[ \t,;]}}

# MNEM: ff2mula32r.ll
# LOGICAL: ff2mula32r_ll
rt_ff2mula32r_ll:
{ nop; ff2mula32r.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2mula32r_ll>:
# DIS: {{[ \t]}}ff2mula32r.ll{{[ \t,;]}}

# MNEM: ff2muls32rs.hh
# LOGICAL: ff2muls32rs_hh
rt_ff2muls32rs_hh:
{ nop; ff2muls32rs.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32rs_hh>:
# DIS: {{[ \t]}}ff2muls32rs.hh{{[ \t,;]}}

# MNEM: ff2muls32rs.lh
# LOGICAL: ff2muls32rs_lh
rt_ff2muls32rs_lh:
{ nop; ff2muls32rs.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32rs_lh>:
# DIS: {{[ \t]}}ff2muls32rs.lh{{[ \t,;]}}

# MNEM: ff2muls32rs.ll
# LOGICAL: ff2muls32rs_ll
rt_ff2muls32rs_ll:
{ nop; ff2muls32rs.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32rs_ll>:
# DIS: {{[ \t]}}ff2muls32rs.ll{{[ \t,;]}}

# MNEM: ff2muls32r.hh
# LOGICAL: ff2muls32r_hh
rt_ff2muls32r_hh:
{ nop; ff2muls32r.hh d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32r_hh>:
# DIS: {{[ \t]}}ff2muls32r.hh{{[ \t,;]}}

# MNEM: ff2muls32r.lh
# LOGICAL: ff2muls32r_lh
rt_ff2muls32r_lh:
{ nop; ff2muls32r.lh d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32r_lh>:
# DIS: {{[ \t]}}ff2muls32r.lh{{[ \t,;]}}

# MNEM: ff2muls32r.ll
# LOGICAL: ff2muls32r_ll
rt_ff2muls32r_ll:
{ nop; ff2muls32r.ll d0, d1, d2 }
# DIS-LABEL: <rt_ff2muls32r_ll>:
# DIS: {{[ \t]}}ff2muls32r.ll{{[ \t,;]}}

# MNEM: flar
rt_flar:
{ nop; flar 0 }
# DIS-LABEL: <rt_flar>:
# DIS: {{[ \t]}}flar{{[ \t,;]}}

# MNEM: fmul16.hs00
# LOGICAL: fmul16_hs00
rt_fmul16_hs00:
{ nop; fmul16.hs00 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs00>:
# DIS: {{[ \t]}}fmul16.hs00{{[ \t,;]}}

# MNEM: fmul16.hs01
# LOGICAL: fmul16_hs01
rt_fmul16_hs01:
{ nop; fmul16.hs01 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs01>:
# DIS: {{[ \t]}}fmul16.hs01{{[ \t,;]}}

# MNEM: fmul16.hs02
# LOGICAL: fmul16_hs02
rt_fmul16_hs02:
{ nop; fmul16.hs02 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs02>:
# DIS: {{[ \t]}}fmul16.hs02{{[ \t,;]}}

# MNEM: fmul16.hs03
# LOGICAL: fmul16_hs03
rt_fmul16_hs03:
{ nop; fmul16.hs03 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs03>:
# DIS: {{[ \t]}}fmul16.hs03{{[ \t,;]}}

# MNEM: fmul16.hs11
# LOGICAL: fmul16_hs11
rt_fmul16_hs11:
{ nop; fmul16.hs11 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs11>:
# DIS: {{[ \t]}}fmul16.hs11{{[ \t,;]}}

# MNEM: fmul16.hs12
# LOGICAL: fmul16_hs12
rt_fmul16_hs12:
{ nop; fmul16.hs12 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs12>:
# DIS: {{[ \t]}}fmul16.hs12{{[ \t,;]}}

# MNEM: fmul16.hs13
# LOGICAL: fmul16_hs13
rt_fmul16_hs13:
{ nop; fmul16.hs13 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs13>:
# DIS: {{[ \t]}}fmul16.hs13{{[ \t,;]}}

# MNEM: fmul16.hs22
# LOGICAL: fmul16_hs22
rt_fmul16_hs22:
{ nop; fmul16.hs22 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs22>:
# DIS: {{[ \t]}}fmul16.hs22{{[ \t,;]}}

# MNEM: fmul16.hs23
# LOGICAL: fmul16_hs23
rt_fmul16_hs23:
{ nop; fmul16.hs23 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs23>:
# DIS: {{[ \t]}}fmul16.hs23{{[ \t,;]}}

# MNEM: fmul16.hs33
# LOGICAL: fmul16_hs33
rt_fmul16_hs33:
{ nop; fmul16.hs33 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_hs33>:
# DIS: {{[ \t]}}fmul16.hs33{{[ \t,;]}}

# MNEM: fmul16.ls00
# LOGICAL: fmul16_ls00
rt_fmul16_ls00:
{ nop; fmul16.ls00 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls00>:
# DIS: {{[ \t]}}fmul16.ls00{{[ \t,;]}}

# MNEM: fmul16.ls01
# LOGICAL: fmul16_ls01
rt_fmul16_ls01:
{ nop; fmul16.ls01 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls01>:
# DIS: {{[ \t]}}fmul16.ls01{{[ \t,;]}}

# MNEM: fmul16.ls02
# LOGICAL: fmul16_ls02
rt_fmul16_ls02:
{ nop; fmul16.ls02 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls02>:
# DIS: {{[ \t]}}fmul16.ls02{{[ \t,;]}}

# MNEM: fmul16.ls03
# LOGICAL: fmul16_ls03
rt_fmul16_ls03:
{ nop; fmul16.ls03 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls03>:
# DIS: {{[ \t]}}fmul16.ls03{{[ \t,;]}}

# MNEM: fmul16.ls11
# LOGICAL: fmul16_ls11
rt_fmul16_ls11:
{ nop; fmul16.ls11 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls11>:
# DIS: {{[ \t]}}fmul16.ls11{{[ \t,;]}}

# MNEM: fmul16.ls12
# LOGICAL: fmul16_ls12
rt_fmul16_ls12:
{ nop; fmul16.ls12 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls12>:
# DIS: {{[ \t]}}fmul16.ls12{{[ \t,;]}}

# MNEM: fmul16.ls13
# LOGICAL: fmul16_ls13
rt_fmul16_ls13:
{ nop; fmul16.ls13 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls13>:
# DIS: {{[ \t]}}fmul16.ls13{{[ \t,;]}}

# MNEM: fmul16.ls22
# LOGICAL: fmul16_ls22
rt_fmul16_ls22:
{ nop; fmul16.ls22 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls22>:
# DIS: {{[ \t]}}fmul16.ls22{{[ \t,;]}}

# MNEM: fmul16.ls23
# LOGICAL: fmul16_ls23
rt_fmul16_ls23:
{ nop; fmul16.ls23 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls23>:
# DIS: {{[ \t]}}fmul16.ls23{{[ \t,;]}}

# MNEM: fmul16.ls33
# LOGICAL: fmul16_ls33
rt_fmul16_ls33:
{ nop; fmul16.ls33 d0, d1, d2 }
# DIS-LABEL: <rt_fmul16_ls33>:
# DIS: {{[ \t]}}fmul16.ls33{{[ \t,;]}}

# MNEM: fmul32s.hh
# LOGICAL: fmul32s_hh
rt_fmul32s_hh:
{ nop; fmul32s.hh d0, d1, d2 }
# DIS-LABEL: <rt_fmul32s_hh>:
# DIS: {{[ \t]}}fmul32s.hh{{[ \t,;]}}

# MNEM: fmul32s.lh
# LOGICAL: fmul32s_lh
rt_fmul32s_lh:
{ nop; fmul32s.lh d0, d1, d2 }
# DIS-LABEL: <rt_fmul32s_lh>:
# DIS: {{[ \t]}}fmul32s.lh{{[ \t,;]}}

# MNEM: fmul32s.ll
# LOGICAL: fmul32s_ll
rt_fmul32s_ll:
{ nop; fmul32s.ll d0, d1, d2 }
# DIS-LABEL: <rt_fmul32s_ll>:
# DIS: {{[ \t]}}fmul32s.ll{{[ \t,;]}}

# MNEM: fmul32x16.h0
# LOGICAL: fmul32x16_h0
rt_fmul32x16_h0:
{ nop; fmul32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_h0>:
# DIS: {{[ \t]}}fmul32x16.h0{{[ \t,;]}}

# MNEM: fmul32x16.h1
# LOGICAL: fmul32x16_h1
rt_fmul32x16_h1:
{ nop; fmul32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_h1>:
# DIS: {{[ \t]}}fmul32x16.h1{{[ \t,;]}}

# MNEM: fmul32x16.h2
# LOGICAL: fmul32x16_h2
rt_fmul32x16_h2:
{ nop; fmul32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_h2>:
# DIS: {{[ \t]}}fmul32x16.h2{{[ \t,;]}}

# MNEM: fmul32x16.h3
# LOGICAL: fmul32x16_h3
rt_fmul32x16_h3:
{ nop; fmul32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_h3>:
# DIS: {{[ \t]}}fmul32x16.h3{{[ \t,;]}}

# MNEM: fmul32x16.l0
# LOGICAL: fmul32x16_l0
rt_fmul32x16_l0:
{ nop; fmul32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_l0>:
# DIS: {{[ \t]}}fmul32x16.l0{{[ \t,;]}}

# MNEM: fmul32x16.l1
# LOGICAL: fmul32x16_l1
rt_fmul32x16_l1:
{ nop; fmul32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_l1>:
# DIS: {{[ \t]}}fmul32x16.l1{{[ \t,;]}}

# MNEM: fmul32x16.l2
# LOGICAL: fmul32x16_l2
rt_fmul32x16_l2:
{ nop; fmul32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_l2>:
# DIS: {{[ \t]}}fmul32x16.l2{{[ \t,;]}}

# MNEM: fmul32x16.l3
# LOGICAL: fmul32x16_l3
rt_fmul32x16_l3:
{ nop; fmul32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_fmul32x16_l3>:
# DIS: {{[ \t]}}fmul32x16.l3{{[ \t,;]}}

# MNEM: fmula16.hs00
# LOGICAL: fmula16_hs00
rt_fmula16_hs00:
{ nop; fmula16.hs00 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs00>:
# DIS: {{[ \t]}}fmula16.hs00{{[ \t,;]}}

# MNEM: fmula16.hs01
# LOGICAL: fmula16_hs01
rt_fmula16_hs01:
{ nop; fmula16.hs01 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs01>:
# DIS: {{[ \t]}}fmula16.hs01{{[ \t,;]}}

# MNEM: fmula16.hs02
# LOGICAL: fmula16_hs02
rt_fmula16_hs02:
{ nop; fmula16.hs02 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs02>:
# DIS: {{[ \t]}}fmula16.hs02{{[ \t,;]}}

# MNEM: fmula16.hs03
# LOGICAL: fmula16_hs03
rt_fmula16_hs03:
{ nop; fmula16.hs03 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs03>:
# DIS: {{[ \t]}}fmula16.hs03{{[ \t,;]}}

# MNEM: fmula16.hs11
# LOGICAL: fmula16_hs11
rt_fmula16_hs11:
{ nop; fmula16.hs11 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs11>:
# DIS: {{[ \t]}}fmula16.hs11{{[ \t,;]}}

# MNEM: fmula16.hs12
# LOGICAL: fmula16_hs12
rt_fmula16_hs12:
{ nop; fmula16.hs12 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs12>:
# DIS: {{[ \t]}}fmula16.hs12{{[ \t,;]}}

# MNEM: fmula16.hs13
# LOGICAL: fmula16_hs13
rt_fmula16_hs13:
{ nop; fmula16.hs13 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs13>:
# DIS: {{[ \t]}}fmula16.hs13{{[ \t,;]}}

# MNEM: fmula16.hs22
# LOGICAL: fmula16_hs22
rt_fmula16_hs22:
{ nop; fmula16.hs22 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs22>:
# DIS: {{[ \t]}}fmula16.hs22{{[ \t,;]}}

# MNEM: fmula16.hs23
# LOGICAL: fmula16_hs23
rt_fmula16_hs23:
{ nop; fmula16.hs23 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs23>:
# DIS: {{[ \t]}}fmula16.hs23{{[ \t,;]}}

# MNEM: fmula16.hs33
# LOGICAL: fmula16_hs33
rt_fmula16_hs33:
{ nop; fmula16.hs33 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_hs33>:
# DIS: {{[ \t]}}fmula16.hs33{{[ \t,;]}}

# MNEM: fmula16.ls00
# LOGICAL: fmula16_ls00
rt_fmula16_ls00:
{ nop; fmula16.ls00 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls00>:
# DIS: {{[ \t]}}fmula16.ls00{{[ \t,;]}}

# MNEM: fmula16.ls01
# LOGICAL: fmula16_ls01
rt_fmula16_ls01:
{ nop; fmula16.ls01 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls01>:
# DIS: {{[ \t]}}fmula16.ls01{{[ \t,;]}}

# MNEM: fmula16.ls02
# LOGICAL: fmula16_ls02
rt_fmula16_ls02:
{ nop; fmula16.ls02 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls02>:
# DIS: {{[ \t]}}fmula16.ls02{{[ \t,;]}}

# MNEM: fmula16.ls03
# LOGICAL: fmula16_ls03
rt_fmula16_ls03:
{ nop; fmula16.ls03 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls03>:
# DIS: {{[ \t]}}fmula16.ls03{{[ \t,;]}}

# MNEM: fmula16.ls11
# LOGICAL: fmula16_ls11
rt_fmula16_ls11:
{ nop; fmula16.ls11 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls11>:
# DIS: {{[ \t]}}fmula16.ls11{{[ \t,;]}}

# MNEM: fmula16.ls12
# LOGICAL: fmula16_ls12
rt_fmula16_ls12:
{ nop; fmula16.ls12 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls12>:
# DIS: {{[ \t]}}fmula16.ls12{{[ \t,;]}}

# MNEM: fmula16.ls13
# LOGICAL: fmula16_ls13
rt_fmula16_ls13:
{ nop; fmula16.ls13 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls13>:
# DIS: {{[ \t]}}fmula16.ls13{{[ \t,;]}}

# MNEM: fmula16.ls22
# LOGICAL: fmula16_ls22
rt_fmula16_ls22:
{ nop; fmula16.ls22 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls22>:
# DIS: {{[ \t]}}fmula16.ls22{{[ \t,;]}}

# MNEM: fmula16.ls23
# LOGICAL: fmula16_ls23
rt_fmula16_ls23:
{ nop; fmula16.ls23 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls23>:
# DIS: {{[ \t]}}fmula16.ls23{{[ \t,;]}}

# MNEM: fmula16.ls33
# LOGICAL: fmula16_ls33
rt_fmula16_ls33:
{ nop; fmula16.ls33 d0, d1, d2 }
# DIS-LABEL: <rt_fmula16_ls33>:
# DIS: {{[ \t]}}fmula16.ls33{{[ \t,;]}}

# MNEM: fmula32s.hh
# LOGICAL: fmula32s_hh
rt_fmula32s_hh:
{ nop; fmula32s.hh d0, d1, d2 }
# DIS-LABEL: <rt_fmula32s_hh>:
# DIS: {{[ \t]}}fmula32s.hh{{[ \t,;]}}

# MNEM: fmula32s.lh
# LOGICAL: fmula32s_lh
rt_fmula32s_lh:
{ nop; fmula32s.lh d0, d1, d2 }
# DIS-LABEL: <rt_fmula32s_lh>:
# DIS: {{[ \t]}}fmula32s.lh{{[ \t,;]}}

# MNEM: fmula32s.ll
# LOGICAL: fmula32s_ll
rt_fmula32s_ll:
{ nop; fmula32s.ll d0, d1, d2 }
# DIS-LABEL: <rt_fmula32s_ll>:
# DIS: {{[ \t]}}fmula32s.ll{{[ \t,;]}}

# MNEM: fmula32x16.h0
# LOGICAL: fmula32x16_h0
rt_fmula32x16_h0:
{ nop; fmula32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_h0>:
# DIS: {{[ \t]}}fmula32x16.h0{{[ \t,;]}}

# MNEM: fmula32x16.h1
# LOGICAL: fmula32x16_h1
rt_fmula32x16_h1:
{ nop; fmula32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_h1>:
# DIS: {{[ \t]}}fmula32x16.h1{{[ \t,;]}}

# MNEM: fmula32x16.h2
# LOGICAL: fmula32x16_h2
rt_fmula32x16_h2:
{ nop; fmula32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_h2>:
# DIS: {{[ \t]}}fmula32x16.h2{{[ \t,;]}}

# MNEM: fmula32x16.h3
# LOGICAL: fmula32x16_h3
rt_fmula32x16_h3:
{ nop; fmula32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_h3>:
# DIS: {{[ \t]}}fmula32x16.h3{{[ \t,;]}}

# MNEM: fmula32x16.l0
# LOGICAL: fmula32x16_l0
rt_fmula32x16_l0:
{ nop; fmula32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_l0>:
# DIS: {{[ \t]}}fmula32x16.l0{{[ \t,;]}}

# MNEM: fmula32x16.l1
# LOGICAL: fmula32x16_l1
rt_fmula32x16_l1:
{ nop; fmula32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_l1>:
# DIS: {{[ \t]}}fmula32x16.l1{{[ \t,;]}}

# MNEM: fmula32x16.l2
# LOGICAL: fmula32x16_l2
rt_fmula32x16_l2:
{ nop; fmula32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_l2>:
# DIS: {{[ \t]}}fmula32x16.l2{{[ \t,;]}}

# MNEM: fmula32x16.l3
# LOGICAL: fmula32x16_l3
rt_fmula32x16_l3:
{ nop; fmula32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_fmula32x16_l3>:
# DIS: {{[ \t]}}fmula32x16.l3{{[ \t,;]}}

# MNEM: fmulaa16.hs.11.00
# LOGICAL: fmulaa16_hs_11_00
rt_fmulaa16_hs_11_00:
{ nop; fmulaa16.hs.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_hs_11_00>:
# DIS: {{[ \t]}}fmulaa16.hs.11.00{{[ \t,;]}}

# MNEM: fmulaa16.hs.13.02
# LOGICAL: fmulaa16_hs_13_02
rt_fmulaa16_hs_13_02:
{ nop; fmulaa16.hs.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_hs_13_02>:
# DIS: {{[ \t]}}fmulaa16.hs.13.02{{[ \t,;]}}

# MNEM: fmulaa16.hs.33.22
# LOGICAL: fmulaa16_hs_33_22
rt_fmulaa16_hs_33_22:
{ nop; fmulaa16.hs.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_hs_33_22>:
# DIS: {{[ \t]}}fmulaa16.hs.33.22{{[ \t,;]}}

# MNEM: fmulaa16.ls.11.00
# LOGICAL: fmulaa16_ls_11_00
rt_fmulaa16_ls_11_00:
{ nop; fmulaa16.ls.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_ls_11_00>:
# DIS: {{[ \t]}}fmulaa16.ls.11.00{{[ \t,;]}}

# MNEM: fmulaa16.ls.13.02
# LOGICAL: fmulaa16_ls_13_02
rt_fmulaa16_ls_13_02:
{ nop; fmulaa16.ls.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_ls_13_02>:
# DIS: {{[ \t]}}fmulaa16.ls.13.02{{[ \t,;]}}

# MNEM: fmulaa16.ls.33.22
# LOGICAL: fmulaa16_ls_33_22
rt_fmulaa16_ls_33_22:
{ nop; fmulaa16.ls.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa16_ls_33_22>:
# DIS: {{[ \t]}}fmulaa16.ls.33.22{{[ \t,;]}}

# MNEM: fmulaa32s.hhll
# LOGICAL: fmulaa32s_hhll
rt_fmulaa32s_hhll:
{ nop; fmulaa32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32s_hhll>:
# DIS: {{[ \t]}}fmulaa32s.hhll{{[ \t,;]}}

# MNEM: fmulaa32s.hllh
# LOGICAL: fmulaa32s_hllh
rt_fmulaa32s_hllh:
{ nop; fmulaa32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32s_hllh>:
# DIS: {{[ \t]}}fmulaa32s.hllh{{[ \t,;]}}

# MNEM: fmulaa32x16.h0.l1
# LOGICAL: fmulaa32x16_h0_l1
rt_fmulaa32x16_h0_l1:
{ nop; fmulaa32x16.h0.l1 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32x16_h0_l1>:
# DIS: {{[ \t]}}fmulaa32x16.h0.l1{{[ \t,;]}}

# MNEM: fmulaa32x16.h1.l0
# LOGICAL: fmulaa32x16_h1_l0
rt_fmulaa32x16_h1_l0:
{ nop; fmulaa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32x16_h1_l0>:
# DIS: {{[ \t]}}fmulaa32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulaa32x16.h2.l3
# LOGICAL: fmulaa32x16_h2_l3
rt_fmulaa32x16_h2_l3:
{ nop; fmulaa32x16.h2.l3 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32x16_h2_l3>:
# DIS: {{[ \t]}}fmulaa32x16.h2.l3{{[ \t,;]}}

# MNEM: fmulaa32x16.h3.l2
# LOGICAL: fmulaa32x16_h3_l2
rt_fmulaa32x16_h3_l2:
{ nop; fmulaa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulaa32x16_h3_l2>:
# DIS: {{[ \t]}}fmulaa32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulas32s.hhll
# LOGICAL: fmulas32s_hhll
rt_fmulas32s_hhll:
{ nop; fmulas32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulas32s_hhll>:
# DIS: {{[ \t]}}fmulas32s.hhll{{[ \t,;]}}

# MNEM: fmulas32s.hllh
# LOGICAL: fmulas32s_hllh
rt_fmulas32s_hllh:
{ nop; fmulas32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulas32s_hllh>:
# DIS: {{[ \t]}}fmulas32s.hllh{{[ \t,;]}}

# MNEM: fmulas32x16.h1.l0
# LOGICAL: fmulas32x16_h1_l0
rt_fmulas32x16_h1_l0:
{ nop; fmulas32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulas32x16_h1_l0>:
# DIS: {{[ \t]}}fmulas32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulas32x16.h3.l2
# LOGICAL: fmulas32x16_h3_l2
rt_fmulas32x16_h3_l2:
{ nop; fmulas32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulas32x16_h3_l2>:
# DIS: {{[ \t]}}fmulas32x16.h3.l2{{[ \t,;]}}

# MNEM: fmuls16.hs00
# LOGICAL: fmuls16_hs00
rt_fmuls16_hs00:
{ nop; fmuls16.hs00 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs00>:
# DIS: {{[ \t]}}fmuls16.hs00{{[ \t,;]}}

# MNEM: fmuls16.hs01
# LOGICAL: fmuls16_hs01
rt_fmuls16_hs01:
{ nop; fmuls16.hs01 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs01>:
# DIS: {{[ \t]}}fmuls16.hs01{{[ \t,;]}}

# MNEM: fmuls16.hs02
# LOGICAL: fmuls16_hs02
rt_fmuls16_hs02:
{ nop; fmuls16.hs02 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs02>:
# DIS: {{[ \t]}}fmuls16.hs02{{[ \t,;]}}

# MNEM: fmuls16.hs03
# LOGICAL: fmuls16_hs03
rt_fmuls16_hs03:
{ nop; fmuls16.hs03 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs03>:
# DIS: {{[ \t]}}fmuls16.hs03{{[ \t,;]}}

# MNEM: fmuls16.hs11
# LOGICAL: fmuls16_hs11
rt_fmuls16_hs11:
{ nop; fmuls16.hs11 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs11>:
# DIS: {{[ \t]}}fmuls16.hs11{{[ \t,;]}}

# MNEM: fmuls16.hs12
# LOGICAL: fmuls16_hs12
rt_fmuls16_hs12:
{ nop; fmuls16.hs12 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs12>:
# DIS: {{[ \t]}}fmuls16.hs12{{[ \t,;]}}

# MNEM: fmuls16.hs13
# LOGICAL: fmuls16_hs13
rt_fmuls16_hs13:
{ nop; fmuls16.hs13 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs13>:
# DIS: {{[ \t]}}fmuls16.hs13{{[ \t,;]}}

# MNEM: fmuls16.hs22
# LOGICAL: fmuls16_hs22
rt_fmuls16_hs22:
{ nop; fmuls16.hs22 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs22>:
# DIS: {{[ \t]}}fmuls16.hs22{{[ \t,;]}}

# MNEM: fmuls16.hs23
# LOGICAL: fmuls16_hs23
rt_fmuls16_hs23:
{ nop; fmuls16.hs23 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs23>:
# DIS: {{[ \t]}}fmuls16.hs23{{[ \t,;]}}

# MNEM: fmuls16.hs33
# LOGICAL: fmuls16_hs33
rt_fmuls16_hs33:
{ nop; fmuls16.hs33 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_hs33>:
# DIS: {{[ \t]}}fmuls16.hs33{{[ \t,;]}}

# MNEM: fmuls16.ls00
# LOGICAL: fmuls16_ls00
rt_fmuls16_ls00:
{ nop; fmuls16.ls00 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls00>:
# DIS: {{[ \t]}}fmuls16.ls00{{[ \t,;]}}

# MNEM: fmuls16.ls01
# LOGICAL: fmuls16_ls01
rt_fmuls16_ls01:
{ nop; fmuls16.ls01 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls01>:
# DIS: {{[ \t]}}fmuls16.ls01{{[ \t,;]}}

# MNEM: fmuls16.ls02
# LOGICAL: fmuls16_ls02
rt_fmuls16_ls02:
{ nop; fmuls16.ls02 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls02>:
# DIS: {{[ \t]}}fmuls16.ls02{{[ \t,;]}}

# MNEM: fmuls16.ls03
# LOGICAL: fmuls16_ls03
rt_fmuls16_ls03:
{ nop; fmuls16.ls03 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls03>:
# DIS: {{[ \t]}}fmuls16.ls03{{[ \t,;]}}

# MNEM: fmuls16.ls11
# LOGICAL: fmuls16_ls11
rt_fmuls16_ls11:
{ nop; fmuls16.ls11 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls11>:
# DIS: {{[ \t]}}fmuls16.ls11{{[ \t,;]}}

# MNEM: fmuls16.ls12
# LOGICAL: fmuls16_ls12
rt_fmuls16_ls12:
{ nop; fmuls16.ls12 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls12>:
# DIS: {{[ \t]}}fmuls16.ls12{{[ \t,;]}}

# MNEM: fmuls16.ls13
# LOGICAL: fmuls16_ls13
rt_fmuls16_ls13:
{ nop; fmuls16.ls13 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls13>:
# DIS: {{[ \t]}}fmuls16.ls13{{[ \t,;]}}

# MNEM: fmuls16.ls22
# LOGICAL: fmuls16_ls22
rt_fmuls16_ls22:
{ nop; fmuls16.ls22 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls22>:
# DIS: {{[ \t]}}fmuls16.ls22{{[ \t,;]}}

# MNEM: fmuls16.ls23
# LOGICAL: fmuls16_ls23
rt_fmuls16_ls23:
{ nop; fmuls16.ls23 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls23>:
# DIS: {{[ \t]}}fmuls16.ls23{{[ \t,;]}}

# MNEM: fmuls16.ls33
# LOGICAL: fmuls16_ls33
rt_fmuls16_ls33:
{ nop; fmuls16.ls33 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls16_ls33>:
# DIS: {{[ \t]}}fmuls16.ls33{{[ \t,;]}}

# MNEM: fmuls32s.hh
# LOGICAL: fmuls32s_hh
rt_fmuls32s_hh:
{ nop; fmuls32s.hh d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32s_hh>:
# DIS: {{[ \t]}}fmuls32s.hh{{[ \t,;]}}

# MNEM: fmuls32s.lh
# LOGICAL: fmuls32s_lh
rt_fmuls32s_lh:
{ nop; fmuls32s.lh d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32s_lh>:
# DIS: {{[ \t]}}fmuls32s.lh{{[ \t,;]}}

# MNEM: fmuls32s.ll
# LOGICAL: fmuls32s_ll
rt_fmuls32s_ll:
{ nop; fmuls32s.ll d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32s_ll>:
# DIS: {{[ \t]}}fmuls32s.ll{{[ \t,;]}}

# MNEM: fmuls32x16.h0
# LOGICAL: fmuls32x16_h0
rt_fmuls32x16_h0:
{ nop; fmuls32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_h0>:
# DIS: {{[ \t]}}fmuls32x16.h0{{[ \t,;]}}

# MNEM: fmuls32x16.h1
# LOGICAL: fmuls32x16_h1
rt_fmuls32x16_h1:
{ nop; fmuls32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_h1>:
# DIS: {{[ \t]}}fmuls32x16.h1{{[ \t,;]}}

# MNEM: fmuls32x16.h2
# LOGICAL: fmuls32x16_h2
rt_fmuls32x16_h2:
{ nop; fmuls32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_h2>:
# DIS: {{[ \t]}}fmuls32x16.h2{{[ \t,;]}}

# MNEM: fmuls32x16.h3
# LOGICAL: fmuls32x16_h3
rt_fmuls32x16_h3:
{ nop; fmuls32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_h3>:
# DIS: {{[ \t]}}fmuls32x16.h3{{[ \t,;]}}

# MNEM: fmuls32x16.l0
# LOGICAL: fmuls32x16_l0
rt_fmuls32x16_l0:
{ nop; fmuls32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_l0>:
# DIS: {{[ \t]}}fmuls32x16.l0{{[ \t,;]}}

# MNEM: fmuls32x16.l1
# LOGICAL: fmuls32x16_l1
rt_fmuls32x16_l1:
{ nop; fmuls32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_l1>:
# DIS: {{[ \t]}}fmuls32x16.l1{{[ \t,;]}}

# MNEM: fmuls32x16.l2
# LOGICAL: fmuls32x16_l2
rt_fmuls32x16_l2:
{ nop; fmuls32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_l2>:
# DIS: {{[ \t]}}fmuls32x16.l2{{[ \t,;]}}

# MNEM: fmuls32x16.l3
# LOGICAL: fmuls32x16_l3
rt_fmuls32x16_l3:
{ nop; fmuls32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_fmuls32x16_l3>:
# DIS: {{[ \t]}}fmuls32x16.l3{{[ \t,;]}}

# MNEM: fmulsa32s.hhll
# LOGICAL: fmulsa32s_hhll
rt_fmulsa32s_hhll:
{ nop; fmulsa32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulsa32s_hhll>:
# DIS: {{[ \t]}}fmulsa32s.hhll{{[ \t,;]}}

# MNEM: fmulsa32s.hllh
# LOGICAL: fmulsa32s_hllh
rt_fmulsa32s_hllh:
{ nop; fmulsa32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulsa32s_hllh>:
# DIS: {{[ \t]}}fmulsa32s.hllh{{[ \t,;]}}

# MNEM: fmulsa32x16.h1.l0
# LOGICAL: fmulsa32x16_h1_l0
rt_fmulsa32x16_h1_l0:
{ nop; fmulsa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulsa32x16_h1_l0>:
# DIS: {{[ \t]}}fmulsa32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulsa32x16.h3.l2
# LOGICAL: fmulsa32x16_h3_l2
rt_fmulsa32x16_h3_l2:
{ nop; fmulsa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulsa32x16_h3_l2>:
# DIS: {{[ \t]}}fmulsa32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulss16.hs.11.00
# LOGICAL: fmulss16_hs_11_00
rt_fmulss16_hs_11_00:
{ nop; fmulss16.hs.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_hs_11_00>:
# DIS: {{[ \t]}}fmulss16.hs.11.00{{[ \t,;]}}

# MNEM: fmulss16.hs.13.02
# LOGICAL: fmulss16_hs_13_02
rt_fmulss16_hs_13_02:
{ nop; fmulss16.hs.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_hs_13_02>:
# DIS: {{[ \t]}}fmulss16.hs.13.02{{[ \t,;]}}

# MNEM: fmulss16.hs.33.22
# LOGICAL: fmulss16_hs_33_22
rt_fmulss16_hs_33_22:
{ nop; fmulss16.hs.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_hs_33_22>:
# DIS: {{[ \t]}}fmulss16.hs.33.22{{[ \t,;]}}

# MNEM: fmulss16.ls.11.00
# LOGICAL: fmulss16_ls_11_00
rt_fmulss16_ls_11_00:
{ nop; fmulss16.ls.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_ls_11_00>:
# DIS: {{[ \t]}}fmulss16.ls.11.00{{[ \t,;]}}

# MNEM: fmulss16.ls.13.02
# LOGICAL: fmulss16_ls_13_02
rt_fmulss16_ls_13_02:
{ nop; fmulss16.ls.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_ls_13_02>:
# DIS: {{[ \t]}}fmulss16.ls.13.02{{[ \t,;]}}

# MNEM: fmulss16.ls.33.22
# LOGICAL: fmulss16_ls_33_22
rt_fmulss16_ls_33_22:
{ nop; fmulss16.ls.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss16_ls_33_22>:
# DIS: {{[ \t]}}fmulss16.ls.33.22{{[ \t,;]}}

# MNEM: fmulss32s.hhll
# LOGICAL: fmulss32s_hhll
rt_fmulss32s_hhll:
{ nop; fmulss32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulss32s_hhll>:
# DIS: {{[ \t]}}fmulss32s.hhll{{[ \t,;]}}

# MNEM: fmulss32s.hllh
# LOGICAL: fmulss32s_hllh
rt_fmulss32s_hllh:
{ nop; fmulss32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulss32s_hllh>:
# DIS: {{[ \t]}}fmulss32s.hllh{{[ \t,;]}}

# MNEM: fmulss32x16.h1.l0
# LOGICAL: fmulss32x16_h1_l0
rt_fmulss32x16_h1_l0:
{ nop; fmulss32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss32x16_h1_l0>:
# DIS: {{[ \t]}}fmulss32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulss32x16.h3.l2
# LOGICAL: fmulss32x16_h3_l2
rt_fmulss32x16_h3_l2:
{ nop; fmulss32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulss32x16_h3_l2>:
# DIS: {{[ \t]}}fmulss32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulzaa16.hs.11.00
# LOGICAL: fmulzaa16_hs_11_00
rt_fmulzaa16_hs_11_00:
{ nop; fmulzaa16.hs.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_hs_11_00>:
# DIS: {{[ \t]}}fmulzaa16.hs.11.00{{[ \t,;]}}

# MNEM: fmulzaa16.hs.13.02
# LOGICAL: fmulzaa16_hs_13_02
rt_fmulzaa16_hs_13_02:
{ nop; fmulzaa16.hs.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_hs_13_02>:
# DIS: {{[ \t]}}fmulzaa16.hs.13.02{{[ \t,;]}}

# MNEM: fmulzaa16.hs.33.22
# LOGICAL: fmulzaa16_hs_33_22
rt_fmulzaa16_hs_33_22:
{ nop; fmulzaa16.hs.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_hs_33_22>:
# DIS: {{[ \t]}}fmulzaa16.hs.33.22{{[ \t,;]}}

# MNEM: fmulzaa16.ls.11.00
# LOGICAL: fmulzaa16_ls_11_00
rt_fmulzaa16_ls_11_00:
{ nop; fmulzaa16.ls.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_ls_11_00>:
# DIS: {{[ \t]}}fmulzaa16.ls.11.00{{[ \t,;]}}

# MNEM: fmulzaa16.ls.13.02
# LOGICAL: fmulzaa16_ls_13_02
rt_fmulzaa16_ls_13_02:
{ nop; fmulzaa16.ls.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_ls_13_02>:
# DIS: {{[ \t]}}fmulzaa16.ls.13.02{{[ \t,;]}}

# MNEM: fmulzaa16.ls.33.22
# LOGICAL: fmulzaa16_ls_33_22
rt_fmulzaa16_ls_33_22:
{ nop; fmulzaa16.ls.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa16_ls_33_22>:
# DIS: {{[ \t]}}fmulzaa16.ls.33.22{{[ \t,;]}}

# MNEM: fmulzaa32s.hhll
# LOGICAL: fmulzaa32s_hhll
rt_fmulzaa32s_hhll:
{ nop; fmulzaa32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32s_hhll>:
# DIS: {{[ \t]}}fmulzaa32s.hhll{{[ \t,;]}}

# MNEM: fmulzaa32s.hllh
# LOGICAL: fmulzaa32s_hllh
rt_fmulzaa32s_hllh:
{ nop; fmulzaa32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32s_hllh>:
# DIS: {{[ \t]}}fmulzaa32s.hllh{{[ \t,;]}}

# MNEM: fmulzaa32x16.h0.l1
# LOGICAL: fmulzaa32x16_h0_l1
rt_fmulzaa32x16_h0_l1:
{ nop; fmulzaa32x16.h0.l1 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32x16_h0_l1>:
# DIS: {{[ \t]}}fmulzaa32x16.h0.l1{{[ \t,;]}}

# MNEM: fmulzaa32x16.h1.l0
# LOGICAL: fmulzaa32x16_h1_l0
rt_fmulzaa32x16_h1_l0:
{ nop; fmulzaa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32x16_h1_l0>:
# DIS: {{[ \t]}}fmulzaa32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulzaa32x16.h2.l3
# LOGICAL: fmulzaa32x16_h2_l3
rt_fmulzaa32x16_h2_l3:
{ nop; fmulzaa32x16.h2.l3 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32x16_h2_l3>:
# DIS: {{[ \t]}}fmulzaa32x16.h2.l3{{[ \t,;]}}

# MNEM: fmulzaa32x16.h3.l2
# LOGICAL: fmulzaa32x16_h3_l2
rt_fmulzaa32x16_h3_l2:
{ nop; fmulzaa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzaa32x16_h3_l2>:
# DIS: {{[ \t]}}fmulzaa32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulzas32s.hhll
# LOGICAL: fmulzas32s_hhll
rt_fmulzas32s_hhll:
{ nop; fmulzas32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulzas32s_hhll>:
# DIS: {{[ \t]}}fmulzas32s.hhll{{[ \t,;]}}

# MNEM: fmulzas32s.hllh
# LOGICAL: fmulzas32s_hllh
rt_fmulzas32s_hllh:
{ nop; fmulzas32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulzas32s_hllh>:
# DIS: {{[ \t]}}fmulzas32s.hllh{{[ \t,;]}}

# MNEM: fmulzas32x16.h1.l0
# LOGICAL: fmulzas32x16_h1_l0
rt_fmulzas32x16_h1_l0:
{ nop; fmulzas32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzas32x16_h1_l0>:
# DIS: {{[ \t]}}fmulzas32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulzas32x16.h3.l2
# LOGICAL: fmulzas32x16_h3_l2
rt_fmulzas32x16_h3_l2:
{ nop; fmulzas32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzas32x16_h3_l2>:
# DIS: {{[ \t]}}fmulzas32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulzsa32s.hhll
# LOGICAL: fmulzsa32s_hhll
rt_fmulzsa32s_hhll:
{ nop; fmulzsa32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulzsa32s_hhll>:
# DIS: {{[ \t]}}fmulzsa32s.hhll{{[ \t,;]}}

# MNEM: fmulzsa32s.hllh
# LOGICAL: fmulzsa32s_hllh
rt_fmulzsa32s_hllh:
{ nop; fmulzsa32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulzsa32s_hllh>:
# DIS: {{[ \t]}}fmulzsa32s.hllh{{[ \t,;]}}

# MNEM: fmulzsa32x16.h1.l0
# LOGICAL: fmulzsa32x16_h1_l0
rt_fmulzsa32x16_h1_l0:
{ nop; fmulzsa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzsa32x16_h1_l0>:
# DIS: {{[ \t]}}fmulzsa32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulzsa32x16.h3.l2
# LOGICAL: fmulzsa32x16_h3_l2
rt_fmulzsa32x16_h3_l2:
{ nop; fmulzsa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzsa32x16_h3_l2>:
# DIS: {{[ \t]}}fmulzsa32x16.h3.l2{{[ \t,;]}}

# MNEM: fmulzss16.hs.11.00
# LOGICAL: fmulzss16_hs_11_00
rt_fmulzss16_hs_11_00:
{ nop; fmulzss16.hs.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_hs_11_00>:
# DIS: {{[ \t]}}fmulzss16.hs.11.00{{[ \t,;]}}

# MNEM: fmulzss16.hs.13.02
# LOGICAL: fmulzss16_hs_13_02
rt_fmulzss16_hs_13_02:
{ nop; fmulzss16.hs.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_hs_13_02>:
# DIS: {{[ \t]}}fmulzss16.hs.13.02{{[ \t,;]}}

# MNEM: fmulzss16.hs.33.22
# LOGICAL: fmulzss16_hs_33_22
rt_fmulzss16_hs_33_22:
{ nop; fmulzss16.hs.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_hs_33_22>:
# DIS: {{[ \t]}}fmulzss16.hs.33.22{{[ \t,;]}}

# MNEM: fmulzss16.ls.11.00
# LOGICAL: fmulzss16_ls_11_00
rt_fmulzss16_ls_11_00:
{ nop; fmulzss16.ls.11.00 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_ls_11_00>:
# DIS: {{[ \t]}}fmulzss16.ls.11.00{{[ \t,;]}}

# MNEM: fmulzss16.ls.13.02
# LOGICAL: fmulzss16_ls_13_02
rt_fmulzss16_ls_13_02:
{ nop; fmulzss16.ls.13.02 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_ls_13_02>:
# DIS: {{[ \t]}}fmulzss16.ls.13.02{{[ \t,;]}}

# MNEM: fmulzss16.ls.33.22
# LOGICAL: fmulzss16_ls_33_22
rt_fmulzss16_ls_33_22:
{ nop; fmulzss16.ls.33.22 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss16_ls_33_22>:
# DIS: {{[ \t]}}fmulzss16.ls.33.22{{[ \t,;]}}

# MNEM: fmulzss32s.hhll
# LOGICAL: fmulzss32s_hhll
rt_fmulzss32s_hhll:
{ nop; fmulzss32s.hhll d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss32s_hhll>:
# DIS: {{[ \t]}}fmulzss32s.hhll{{[ \t,;]}}

# MNEM: fmulzss32s.hllh
# LOGICAL: fmulzss32s_hllh
rt_fmulzss32s_hllh:
{ nop; fmulzss32s.hllh d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss32s_hllh>:
# DIS: {{[ \t]}}fmulzss32s.hllh{{[ \t,;]}}

# MNEM: fmulzss32x16.h1.l0
# LOGICAL: fmulzss32x16_h1_l0
rt_fmulzss32x16_h1_l0:
{ nop; fmulzss32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss32x16_h1_l0>:
# DIS: {{[ \t]}}fmulzss32x16.h1.l0{{[ \t,;]}}

# MNEM: fmulzss32x16.h3.l2
# LOGICAL: fmulzss32x16_h3_l2
rt_fmulzss32x16_h3_l2:
{ nop; fmulzss32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_fmulzss32x16_h3_l2>:
# DIS: {{[ \t]}}fmulzss32x16.h3.l2{{[ \t,;]}}

# MNEM: jal
rt_jal:
{ nop; jal r1, 0 }
# DIS-LABEL: <rt_jal>:
# DIS: {{[ \t]}}jal{{[ \t,;]}}

# MNEM: jalr
rt_jalr:
{ nop; jalr r1, r2, 0 }
# DIS-LABEL: <rt_jalr>:
# DIS: {{[ \t]}}jalr{{[ \t,;]}}

# MNEM: log2
rt_log2:
{ nop; nop; log2 r1, r2 }
# DIS-LABEL: <rt_log2>:
# DIS: {{[ \t]}}log2{{[ \t,;]}}

# MNEM: lui
rt_lui:
{ nop; lui r1, 1 }
# DIS-LABEL: <rt_lui>:
# DIS: {{[ \t]}}lui{{[ \t,;]}}

# MNEM: max32
rt_max32:
{ nop; max32 r1, r2, r3 }
# DIS-LABEL: <rt_max32>:
# DIS: {{[ \t]}}max32{{[ \t,;]}}

# MNEM: max64
rt_max64:
{ nop; max64 d0, d1, d2 }
# DIS-LABEL: <rt_max64>:
# DIS: {{[ \t]}}max64{{[ \t,;]}}

# MNEM: maxu32
rt_maxu32:
{ nop; maxu32 r1, r2, r3 }
# DIS-LABEL: <rt_maxu32>:
# DIS: {{[ \t]}}maxu32{{[ \t,;]}}

# MNEM: min32
rt_min32:
{ nop; min32 r1, r2, r3 }
# DIS-LABEL: <rt_min32>:
# DIS: {{[ \t]}}min32{{[ \t,;]}}

# MNEM: min64
rt_min64:
{ nop; min64 d0, d1, d2 }
# DIS-LABEL: <rt_min64>:
# DIS: {{[ \t]}}min64{{[ \t,;]}}

# MNEM: minu32
rt_minu32:
{ nop; minu32 r1, r2, r3 }
# DIS-LABEL: <rt_minu32>:
# DIS: {{[ \t]}}minu32{{[ \t,;]}}

# MNEM: move32
rt_move32:
{ nop; move32 r1, r2 }
# DIS-LABEL: <rt_move32>:
# DIS: {{[ \t]}}move32{{[ \t,;]}}

# MNEM: move32_dr_h
rt_move32_dr_h:
{ nop; move32_dr_h r1, d0 }
# DIS-LABEL: <rt_move32_dr_h>:
# DIS: {{[ \t]}}move32_dr_h{{[ \t,;]}}

# MNEM: move32_dr_l
rt_move32_dr_l:
{ nop; move32_dr_l r1, d0 }
# DIS-LABEL: <rt_move32_dr_l>:
# DIS: {{[ \t]}}move32_dr_l{{[ \t,;]}}

# MNEM: move64
rt_move64:
{ nop; move64 d0, d1 }
# DIS-LABEL: <rt_move64>:
# DIS: {{[ \t]}}move64{{[ \t,;]}}

# MNEM: movegpr2sfr
rt_movegpr2sfr:
{ nop; movegpr2sfr r1 }
# DIS-LABEL: <rt_movegpr2sfr>:
# DIS: {{[ \t]}}movegpr2sfr{{[ \t,;]}}

# MNEM: movei_h
rt_movei_h:
{ nop; movei_h d0, 1 }
# DIS-LABEL: <rt_movei_h>:
# DIS: {{[ \t]}}movei_h{{[ \t,;]}}

# MNEM: movei_l
rt_movei_l:
{ nop; movei_l d0, 1 }
# DIS-LABEL: <rt_movei_l>:
# DIS: {{[ \t]}}movei_l{{[ \t,;]}}

# MNEM: movesfr2gpr
rt_movesfr2gpr:
{ nop; movesfr2gpr r1 }
# DIS-LABEL: <rt_movesfr2gpr>:
# DIS: {{[ \t]}}movesfr2gpr{{[ \t,;]}}

# MNEM: movf32
rt_movf32:
{ nop; movf32 r1, r2, r3 }
# DIS-LABEL: <rt_movf32>:
# DIS: {{[ \t]}}movf32{{[ \t,;]}}

# MNEM: movf64
rt_movf64:
{ nop; movf64 d0, d1 }
# DIS-LABEL: <rt_movf64>:
# DIS: {{[ \t]}}movf64{{[ \t,;]}}

# MNEM: movt32
rt_movt32:
{ nop; movt32 r1, r2, r3 }
# DIS-LABEL: <rt_movt32>:
# DIS: {{[ \t]}}movt32{{[ \t,;]}}

# MNEM: movt64
rt_movt64:
{ nop; movt64 d0, d1 }
# DIS-LABEL: <rt_movt64>:
# DIS: {{[ \t]}}movt64{{[ \t,;]}}

# MNEM: mul16aq
rt_mul16aq:
{ nop; mul16aq d0, d1, d2 }
# DIS-LABEL: <rt_mul16aq>:
# DIS: {{[ \t]}}mul16aq{{[ \t,;]}}

# MNEM: mul16zaq
rt_mul16zaq:
{ nop; mul16zaq d0, d1, d2 }
# DIS-LABEL: <rt_mul16zaq>:
# DIS: {{[ \t]}}mul16zaq{{[ \t,;]}}

# MNEM: mul32x16.h0
# LOGICAL: mul32x16_h0
rt_mul32x16_h0:
{ nop; mul32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_h0>:
# DIS: {{[ \t]}}mul32x16.h0{{[ \t,;]}}

# MNEM: mul32x16.h1
# LOGICAL: mul32x16_h1
rt_mul32x16_h1:
{ nop; mul32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_h1>:
# DIS: {{[ \t]}}mul32x16.h1{{[ \t,;]}}

# MNEM: mul32x16.h2
# LOGICAL: mul32x16_h2
rt_mul32x16_h2:
{ nop; mul32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_h2>:
# DIS: {{[ \t]}}mul32x16.h2{{[ \t,;]}}

# MNEM: mul32x16.h3
# LOGICAL: mul32x16_h3
rt_mul32x16_h3:
{ nop; mul32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_h3>:
# DIS: {{[ \t]}}mul32x16.h3{{[ \t,;]}}

# MNEM: mul32x16.l0
# LOGICAL: mul32x16_l0
rt_mul32x16_l0:
{ nop; mul32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_l0>:
# DIS: {{[ \t]}}mul32x16.l0{{[ \t,;]}}

# MNEM: mul32x16.l1
# LOGICAL: mul32x16_l1
rt_mul32x16_l1:
{ nop; mul32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_l1>:
# DIS: {{[ \t]}}mul32x16.l1{{[ \t,;]}}

# MNEM: mul32x16.l2
# LOGICAL: mul32x16_l2
rt_mul32x16_l2:
{ nop; mul32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_l2>:
# DIS: {{[ \t]}}mul32x16.l2{{[ \t,;]}}

# MNEM: mul32x16.l3
# LOGICAL: mul32x16_l3
rt_mul32x16_l3:
{ nop; mul32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_mul32x16_l3>:
# DIS: {{[ \t]}}mul32x16.l3{{[ \t,;]}}

# MNEM: mul64.hh
# LOGICAL: mul64_hh
rt_mul64_hh:
{ nop; mul64.hh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_hh>:
# DIS: {{[ \t]}}mul64.hh{{[ \t,;]}}

# MNEM: mul64.hl
# LOGICAL: mul64_hl
rt_mul64_hl:
{ nop; mul64.hl d0, d1, d2 }
# DIS-LABEL: <rt_mul64_hl>:
# DIS: {{[ \t]}}mul64.hl{{[ \t,;]}}

# MNEM: mul64.huh
# LOGICAL: mul64_huh
rt_mul64_huh:
{ nop; mul64.huh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_huh>:
# DIS: {{[ \t]}}mul64.huh{{[ \t,;]}}

# MNEM: mul64.hul
# LOGICAL: mul64_hul
rt_mul64_hul:
{ nop; mul64.hul d0, d1, d2 }
# DIS-LABEL: <rt_mul64_hul>:
# DIS: {{[ \t]}}mul64.hul{{[ \t,;]}}

# MNEM: mul64.lh
# LOGICAL: mul64_lh
rt_mul64_lh:
{ nop; mul64.lh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_lh>:
# DIS: {{[ \t]}}mul64.lh{{[ \t,;]}}

# MNEM: mul64.ll
# LOGICAL: mul64_ll
rt_mul64_ll:
{ nop; mul64.ll d0, d1, d2 }
# DIS-LABEL: <rt_mul64_ll>:
# DIS: {{[ \t]}}mul64.ll{{[ \t,;]}}

# MNEM: mul64.luh
# LOGICAL: mul64_luh
rt_mul64_luh:
{ nop; mul64.luh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_luh>:
# DIS: {{[ \t]}}mul64.luh{{[ \t,;]}}

# MNEM: mul64.lul
# LOGICAL: mul64_lul
rt_mul64_lul:
{ nop; mul64.lul d0, d1, d2 }
# DIS-LABEL: <rt_mul64_lul>:
# DIS: {{[ \t]}}mul64.lul{{[ \t,;]}}

# MNEM: mul64.uhh
# LOGICAL: mul64_uhh
rt_mul64_uhh:
{ nop; mul64.uhh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_uhh>:
# DIS: {{[ \t]}}mul64.uhh{{[ \t,;]}}

# MNEM: mul64.uhl
# LOGICAL: mul64_uhl
rt_mul64_uhl:
{ nop; mul64.uhl d0, d1, d2 }
# DIS-LABEL: <rt_mul64_uhl>:
# DIS: {{[ \t]}}mul64.uhl{{[ \t,;]}}

# MNEM: mul64.uhuh
# LOGICAL: mul64_uhuh
rt_mul64_uhuh:
{ nop; mul64.uhuh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_uhuh>:
# DIS: {{[ \t]}}mul64.uhuh{{[ \t,;]}}

# MNEM: mul64.uhul
# LOGICAL: mul64_uhul
rt_mul64_uhul:
{ nop; mul64.uhul d0, d1, d2 }
# DIS-LABEL: <rt_mul64_uhul>:
# DIS: {{[ \t]}}mul64.uhul{{[ \t,;]}}

# MNEM: mul64.ulh
# LOGICAL: mul64_ulh
rt_mul64_ulh:
{ nop; mul64.ulh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_ulh>:
# DIS: {{[ \t]}}mul64.ulh{{[ \t,;]}}

# MNEM: mul64.ull
# LOGICAL: mul64_ull
rt_mul64_ull:
{ nop; mul64.ull d0, d1, d2 }
# DIS-LABEL: <rt_mul64_ull>:
# DIS: {{[ \t]}}mul64.ull{{[ \t,;]}}

# MNEM: mul64.uluh
# LOGICAL: mul64_uluh
rt_mul64_uluh:
{ nop; mul64.uluh d0, d1, d2 }
# DIS-LABEL: <rt_mul64_uluh>:
# DIS: {{[ \t]}}mul64.uluh{{[ \t,;]}}

# MNEM: mul64.ulul
# LOGICAL: mul64_ulul
rt_mul64_ulul:
{ nop; mul64.ulul d0, d1, d2 }
# DIS-LABEL: <rt_mul64_ulul>:
# DIS: {{[ \t]}}mul64.ulul{{[ \t,;]}}

# MNEM: mula32x16.h0
# LOGICAL: mula32x16_h0
rt_mula32x16_h0:
{ nop; mula32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_h0>:
# DIS: {{[ \t]}}mula32x16.h0{{[ \t,;]}}

# MNEM: mula32x16.h1
# LOGICAL: mula32x16_h1
rt_mula32x16_h1:
{ nop; mula32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_h1>:
# DIS: {{[ \t]}}mula32x16.h1{{[ \t,;]}}

# MNEM: mula32x16.h2
# LOGICAL: mula32x16_h2
rt_mula32x16_h2:
{ nop; mula32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_h2>:
# DIS: {{[ \t]}}mula32x16.h2{{[ \t,;]}}

# MNEM: mula32x16.h3
# LOGICAL: mula32x16_h3
rt_mula32x16_h3:
{ nop; mula32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_h3>:
# DIS: {{[ \t]}}mula32x16.h3{{[ \t,;]}}

# MNEM: mula32x16.l0
# LOGICAL: mula32x16_l0
rt_mula32x16_l0:
{ nop; mula32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_l0>:
# DIS: {{[ \t]}}mula32x16.l0{{[ \t,;]}}

# MNEM: mula32x16.l1
# LOGICAL: mula32x16_l1
rt_mula32x16_l1:
{ nop; mula32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_l1>:
# DIS: {{[ \t]}}mula32x16.l1{{[ \t,;]}}

# MNEM: mula32x16.l2
# LOGICAL: mula32x16_l2
rt_mula32x16_l2:
{ nop; mula32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_l2>:
# DIS: {{[ \t]}}mula32x16.l2{{[ \t,;]}}

# MNEM: mula32x16.l3
# LOGICAL: mula32x16_l3
rt_mula32x16_l3:
{ nop; mula32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_mula32x16_l3>:
# DIS: {{[ \t]}}mula32x16.l3{{[ \t,;]}}

# MNEM: mula64.hh
# LOGICAL: mula64_hh
rt_mula64_hh:
{ nop; mula64.hh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_hh>:
# DIS: {{[ \t]}}mula64.hh{{[ \t,;]}}

# MNEM: mula64.hl
# LOGICAL: mula64_hl
rt_mula64_hl:
{ nop; mula64.hl d0, d1, d2 }
# DIS-LABEL: <rt_mula64_hl>:
# DIS: {{[ \t]}}mula64.hl{{[ \t,;]}}

# MNEM: mula64.huh
# LOGICAL: mula64_huh
rt_mula64_huh:
{ nop; mula64.huh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_huh>:
# DIS: {{[ \t]}}mula64.huh{{[ \t,;]}}

# MNEM: mula64.hul
# LOGICAL: mula64_hul
rt_mula64_hul:
{ nop; mula64.hul d0, d1, d2 }
# DIS-LABEL: <rt_mula64_hul>:
# DIS: {{[ \t]}}mula64.hul{{[ \t,;]}}

# MNEM: mula64.lh
# LOGICAL: mula64_lh
rt_mula64_lh:
{ nop; mula64.lh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_lh>:
# DIS: {{[ \t]}}mula64.lh{{[ \t,;]}}

# MNEM: mula64.ll
# LOGICAL: mula64_ll
rt_mula64_ll:
{ nop; mula64.ll d0, d1, d2 }
# DIS-LABEL: <rt_mula64_ll>:
# DIS: {{[ \t]}}mula64.ll{{[ \t,;]}}

# MNEM: mula64.luh
# LOGICAL: mula64_luh
rt_mula64_luh:
{ nop; mula64.luh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_luh>:
# DIS: {{[ \t]}}mula64.luh{{[ \t,;]}}

# MNEM: mula64.lul
# LOGICAL: mula64_lul
rt_mula64_lul:
{ nop; mula64.lul d0, d1, d2 }
# DIS-LABEL: <rt_mula64_lul>:
# DIS: {{[ \t]}}mula64.lul{{[ \t,;]}}

# MNEM: mula64.uhh
# LOGICAL: mula64_uhh
rt_mula64_uhh:
{ nop; mula64.uhh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_uhh>:
# DIS: {{[ \t]}}mula64.uhh{{[ \t,;]}}

# MNEM: mula64.uhl
# LOGICAL: mula64_uhl
rt_mula64_uhl:
{ nop; mula64.uhl d0, d1, d2 }
# DIS-LABEL: <rt_mula64_uhl>:
# DIS: {{[ \t]}}mula64.uhl{{[ \t,;]}}

# MNEM: mula64.uhuh
# LOGICAL: mula64_uhuh
rt_mula64_uhuh:
{ nop; mula64.uhuh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_uhuh>:
# DIS: {{[ \t]}}mula64.uhuh{{[ \t,;]}}

# MNEM: mula64.uhul
# LOGICAL: mula64_uhul
rt_mula64_uhul:
{ nop; mula64.uhul d0, d1, d2 }
# DIS-LABEL: <rt_mula64_uhul>:
# DIS: {{[ \t]}}mula64.uhul{{[ \t,;]}}

# MNEM: mula64.ulh
# LOGICAL: mula64_ulh
rt_mula64_ulh:
{ nop; mula64.ulh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_ulh>:
# DIS: {{[ \t]}}mula64.ulh{{[ \t,;]}}

# MNEM: mula64.ull
# LOGICAL: mula64_ull
rt_mula64_ull:
{ nop; mula64.ull d0, d1, d2 }
# DIS-LABEL: <rt_mula64_ull>:
# DIS: {{[ \t]}}mula64.ull{{[ \t,;]}}

# MNEM: mula64.uluh
# LOGICAL: mula64_uluh
rt_mula64_uluh:
{ nop; mula64.uluh d0, d1, d2 }
# DIS-LABEL: <rt_mula64_uluh>:
# DIS: {{[ \t]}}mula64.uluh{{[ \t,;]}}

# MNEM: mula64.ulul
# LOGICAL: mula64_ulul
rt_mula64_ulul:
{ nop; mula64.ulul d0, d1, d2 }
# DIS-LABEL: <rt_mula64_ulul>:
# DIS: {{[ \t]}}mula64.ulul{{[ \t,;]}}

# MNEM: mulaa32x16.h0.l1
# LOGICAL: mulaa32x16_h0_l1
rt_mulaa32x16_h0_l1:
{ nop; mulaa32x16.h0.l1 d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32x16_h0_l1>:
# DIS: {{[ \t]}}mulaa32x16.h0.l1{{[ \t,;]}}

# MNEM: mulaa32x16.h1.l0
# LOGICAL: mulaa32x16_h1_l0
rt_mulaa32x16_h1_l0:
{ nop; mulaa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32x16_h1_l0>:
# DIS: {{[ \t]}}mulaa32x16.h1.l0{{[ \t,;]}}

# MNEM: mulaa32x16.h2.l3
# LOGICAL: mulaa32x16_h2_l3
rt_mulaa32x16_h2_l3:
{ nop; mulaa32x16.h2.l3 d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32x16_h2_l3>:
# DIS: {{[ \t]}}mulaa32x16.h2.l3{{[ \t,;]}}

# MNEM: mulaa32x16.h3.l2
# LOGICAL: mulaa32x16_h3_l2
rt_mulaa32x16_h3_l2:
{ nop; mulaa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32x16_h3_l2>:
# DIS: {{[ \t]}}mulaa32x16.h3.l2{{[ \t,;]}}

# MNEM: mulaa32.hhll
# LOGICAL: mulaa32_hhll
rt_mulaa32_hhll:
{ nop; mulaa32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32_hhll>:
# DIS: {{[ \t]}}mulaa32.hhll{{[ \t,;]}}

# MNEM: mulaa32.hllh
# LOGICAL: mulaa32_hllh
rt_mulaa32_hllh:
{ nop; mulaa32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulaa32_hllh>:
# DIS: {{[ \t]}}mulaa32.hllh{{[ \t,;]}}

# MNEM: mulas32x16.h1.l0
# LOGICAL: mulas32x16_h1_l0
rt_mulas32x16_h1_l0:
{ nop; mulas32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulas32x16_h1_l0>:
# DIS: {{[ \t]}}mulas32x16.h1.l0{{[ \t,;]}}

# MNEM: mulas32x16.h3.l2
# LOGICAL: mulas32x16_h3_l2
rt_mulas32x16_h3_l2:
{ nop; mulas32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulas32x16_h3_l2>:
# DIS: {{[ \t]}}mulas32x16.h3.l2{{[ \t,;]}}

# MNEM: mulas32.hhll
# LOGICAL: mulas32_hhll
rt_mulas32_hhll:
{ nop; mulas32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulas32_hhll>:
# DIS: {{[ \t]}}mulas32.hhll{{[ \t,;]}}

# MNEM: mulas32.hllh
# LOGICAL: mulas32_hllh
rt_mulas32_hllh:
{ nop; mulas32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulas32_hllh>:
# DIS: {{[ \t]}}mulas32.hllh{{[ \t,;]}}

# MNEM: mulas64.hh
# LOGICAL: mulas64_hh
rt_mulas64_hh:
{ nop; mulas64.hh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_hh>:
# DIS: {{[ \t]}}mulas64.hh{{[ \t,;]}}

# MNEM: mulas64.hl
# LOGICAL: mulas64_hl
rt_mulas64_hl:
{ nop; mulas64.hl d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_hl>:
# DIS: {{[ \t]}}mulas64.hl{{[ \t,;]}}

# MNEM: mulas64.huh
# LOGICAL: mulas64_huh
rt_mulas64_huh:
{ nop; mulas64.huh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_huh>:
# DIS: {{[ \t]}}mulas64.huh{{[ \t,;]}}

# MNEM: mulas64.hul
# LOGICAL: mulas64_hul
rt_mulas64_hul:
{ nop; mulas64.hul d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_hul>:
# DIS: {{[ \t]}}mulas64.hul{{[ \t,;]}}

# MNEM: mulas64.lh
# LOGICAL: mulas64_lh
rt_mulas64_lh:
{ nop; mulas64.lh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_lh>:
# DIS: {{[ \t]}}mulas64.lh{{[ \t,;]}}

# MNEM: mulas64.ll
# LOGICAL: mulas64_ll
rt_mulas64_ll:
{ nop; mulas64.ll d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_ll>:
# DIS: {{[ \t]}}mulas64.ll{{[ \t,;]}}

# MNEM: mulas64.luh
# LOGICAL: mulas64_luh
rt_mulas64_luh:
{ nop; mulas64.luh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_luh>:
# DIS: {{[ \t]}}mulas64.luh{{[ \t,;]}}

# MNEM: mulas64.lul
# LOGICAL: mulas64_lul
rt_mulas64_lul:
{ nop; mulas64.lul d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_lul>:
# DIS: {{[ \t]}}mulas64.lul{{[ \t,;]}}

# MNEM: mulas64.uhh
# LOGICAL: mulas64_uhh
rt_mulas64_uhh:
{ nop; mulas64.uhh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_uhh>:
# DIS: {{[ \t]}}mulas64.uhh{{[ \t,;]}}

# MNEM: mulas64.uhl
# LOGICAL: mulas64_uhl
rt_mulas64_uhl:
{ nop; mulas64.uhl d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_uhl>:
# DIS: {{[ \t]}}mulas64.uhl{{[ \t,;]}}

# MNEM: mulas64.uhuh
# LOGICAL: mulas64_uhuh
rt_mulas64_uhuh:
{ nop; mulas64.uhuh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_uhuh>:
# DIS: {{[ \t]}}mulas64.uhuh{{[ \t,;]}}

# MNEM: mulas64.uhul
# LOGICAL: mulas64_uhul
rt_mulas64_uhul:
{ nop; mulas64.uhul d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_uhul>:
# DIS: {{[ \t]}}mulas64.uhul{{[ \t,;]}}

# MNEM: mulas64.ulh
# LOGICAL: mulas64_ulh
rt_mulas64_ulh:
{ nop; mulas64.ulh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_ulh>:
# DIS: {{[ \t]}}mulas64.ulh{{[ \t,;]}}

# MNEM: mulas64.ull
# LOGICAL: mulas64_ull
rt_mulas64_ull:
{ nop; mulas64.ull d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_ull>:
# DIS: {{[ \t]}}mulas64.ull{{[ \t,;]}}

# MNEM: mulas64.uluh
# LOGICAL: mulas64_uluh
rt_mulas64_uluh:
{ nop; mulas64.uluh d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_uluh>:
# DIS: {{[ \t]}}mulas64.uluh{{[ \t,;]}}

# MNEM: mulas64.ulul
# LOGICAL: mulas64_ulul
rt_mulas64_ulul:
{ nop; mulas64.ulul d0, d1, d2 }
# DIS-LABEL: <rt_mulas64_ulul>:
# DIS: {{[ \t]}}mulas64.ulul{{[ \t,;]}}

# MNEM: mull
rt_mull:
{ nop; mull r1, r2, r1 }
# DIS-LABEL: <rt_mull>:
# DIS: {{[ \t]}}mull{{[ \t,;]}}

# MNEM: muls32x16.h0
# LOGICAL: muls32x16_h0
rt_muls32x16_h0:
{ nop; muls32x16.h0 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_h0>:
# DIS: {{[ \t]}}muls32x16.h0{{[ \t,;]}}

# MNEM: muls32x16.h1
# LOGICAL: muls32x16_h1
rt_muls32x16_h1:
{ nop; muls32x16.h1 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_h1>:
# DIS: {{[ \t]}}muls32x16.h1{{[ \t,;]}}

# MNEM: muls32x16.h2
# LOGICAL: muls32x16_h2
rt_muls32x16_h2:
{ nop; muls32x16.h2 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_h2>:
# DIS: {{[ \t]}}muls32x16.h2{{[ \t,;]}}

# MNEM: muls32x16.h3
# LOGICAL: muls32x16_h3
rt_muls32x16_h3:
{ nop; muls32x16.h3 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_h3>:
# DIS: {{[ \t]}}muls32x16.h3{{[ \t,;]}}

# MNEM: muls32x16.l0
# LOGICAL: muls32x16_l0
rt_muls32x16_l0:
{ nop; muls32x16.l0 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_l0>:
# DIS: {{[ \t]}}muls32x16.l0{{[ \t,;]}}

# MNEM: muls32x16.l1
# LOGICAL: muls32x16_l1
rt_muls32x16_l1:
{ nop; muls32x16.l1 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_l1>:
# DIS: {{[ \t]}}muls32x16.l1{{[ \t,;]}}

# MNEM: muls32x16.l2
# LOGICAL: muls32x16_l2
rt_muls32x16_l2:
{ nop; muls32x16.l2 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_l2>:
# DIS: {{[ \t]}}muls32x16.l2{{[ \t,;]}}

# MNEM: muls32x16.l3
# LOGICAL: muls32x16_l3
rt_muls32x16_l3:
{ nop; muls32x16.l3 d0, d1, d2 }
# DIS-LABEL: <rt_muls32x16_l3>:
# DIS: {{[ \t]}}muls32x16.l3{{[ \t,;]}}

# MNEM: muls64.hh
# LOGICAL: muls64_hh
rt_muls64_hh:
{ nop; muls64.hh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_hh>:
# DIS: {{[ \t]}}muls64.hh{{[ \t,;]}}

# MNEM: muls64.hl
# LOGICAL: muls64_hl
rt_muls64_hl:
{ nop; muls64.hl d0, d1, d2 }
# DIS-LABEL: <rt_muls64_hl>:
# DIS: {{[ \t]}}muls64.hl{{[ \t,;]}}

# MNEM: muls64.huh
# LOGICAL: muls64_huh
rt_muls64_huh:
{ nop; muls64.huh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_huh>:
# DIS: {{[ \t]}}muls64.huh{{[ \t,;]}}

# MNEM: muls64.hul
# LOGICAL: muls64_hul
rt_muls64_hul:
{ nop; muls64.hul d0, d1, d2 }
# DIS-LABEL: <rt_muls64_hul>:
# DIS: {{[ \t]}}muls64.hul{{[ \t,;]}}

# MNEM: muls64.lh
# LOGICAL: muls64_lh
rt_muls64_lh:
{ nop; muls64.lh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_lh>:
# DIS: {{[ \t]}}muls64.lh{{[ \t,;]}}

# MNEM: muls64.ll
# LOGICAL: muls64_ll
rt_muls64_ll:
{ nop; muls64.ll d0, d1, d2 }
# DIS-LABEL: <rt_muls64_ll>:
# DIS: {{[ \t]}}muls64.ll{{[ \t,;]}}

# MNEM: muls64.luh
# LOGICAL: muls64_luh
rt_muls64_luh:
{ nop; muls64.luh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_luh>:
# DIS: {{[ \t]}}muls64.luh{{[ \t,;]}}

# MNEM: muls64.lul
# LOGICAL: muls64_lul
rt_muls64_lul:
{ nop; muls64.lul d0, d1, d2 }
# DIS-LABEL: <rt_muls64_lul>:
# DIS: {{[ \t]}}muls64.lul{{[ \t,;]}}

# MNEM: muls64.uhh
# LOGICAL: muls64_uhh
rt_muls64_uhh:
{ nop; muls64.uhh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_uhh>:
# DIS: {{[ \t]}}muls64.uhh{{[ \t,;]}}

# MNEM: muls64.uhl
# LOGICAL: muls64_uhl
rt_muls64_uhl:
{ nop; muls64.uhl d0, d1, d2 }
# DIS-LABEL: <rt_muls64_uhl>:
# DIS: {{[ \t]}}muls64.uhl{{[ \t,;]}}

# MNEM: muls64.uhuh
# LOGICAL: muls64_uhuh
rt_muls64_uhuh:
{ nop; muls64.uhuh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_uhuh>:
# DIS: {{[ \t]}}muls64.uhuh{{[ \t,;]}}

# MNEM: muls64.uhul
# LOGICAL: muls64_uhul
rt_muls64_uhul:
{ nop; muls64.uhul d0, d1, d2 }
# DIS-LABEL: <rt_muls64_uhul>:
# DIS: {{[ \t]}}muls64.uhul{{[ \t,;]}}

# MNEM: muls64.ulh
# LOGICAL: muls64_ulh
rt_muls64_ulh:
{ nop; muls64.ulh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_ulh>:
# DIS: {{[ \t]}}muls64.ulh{{[ \t,;]}}

# MNEM: muls64.ull
# LOGICAL: muls64_ull
rt_muls64_ull:
{ nop; muls64.ull d0, d1, d2 }
# DIS-LABEL: <rt_muls64_ull>:
# DIS: {{[ \t]}}muls64.ull{{[ \t,;]}}

# MNEM: muls64.uluh
# LOGICAL: muls64_uluh
rt_muls64_uluh:
{ nop; muls64.uluh d0, d1, d2 }
# DIS-LABEL: <rt_muls64_uluh>:
# DIS: {{[ \t]}}muls64.uluh{{[ \t,;]}}

# MNEM: muls64.ulul
# LOGICAL: muls64_ulul
rt_muls64_ulul:
{ nop; muls64.ulul d0, d1, d2 }
# DIS-LABEL: <rt_muls64_ulul>:
# DIS: {{[ \t]}}muls64.ulul{{[ \t,;]}}

# MNEM: mulsa32x16.h1.l0
# LOGICAL: mulsa32x16_h1_l0
rt_mulsa32x16_h1_l0:
{ nop; mulsa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulsa32x16_h1_l0>:
# DIS: {{[ \t]}}mulsa32x16.h1.l0{{[ \t,;]}}

# MNEM: mulsa32x16.h3.l2
# LOGICAL: mulsa32x16_h3_l2
rt_mulsa32x16_h3_l2:
{ nop; mulsa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulsa32x16_h3_l2>:
# DIS: {{[ \t]}}mulsa32x16.h3.l2{{[ \t,;]}}

# MNEM: mulsa32.hhll
# LOGICAL: mulsa32_hhll
rt_mulsa32_hhll:
{ nop; mulsa32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulsa32_hhll>:
# DIS: {{[ \t]}}mulsa32.hhll{{[ \t,;]}}

# MNEM: mulsa32.hllh
# LOGICAL: mulsa32_hllh
rt_mulsa32_hllh:
{ nop; mulsa32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulsa32_hllh>:
# DIS: {{[ \t]}}mulsa32.hllh{{[ \t,;]}}

# MNEM: mulss32x16.h1.l0
# LOGICAL: mulss32x16_h1_l0
rt_mulss32x16_h1_l0:
{ nop; mulss32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulss32x16_h1_l0>:
# DIS: {{[ \t]}}mulss32x16.h1.l0{{[ \t,;]}}

# MNEM: mulss32x16.h3.l2
# LOGICAL: mulss32x16_h3_l2
rt_mulss32x16_h3_l2:
{ nop; mulss32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulss32x16_h3_l2>:
# DIS: {{[ \t]}}mulss32x16.h3.l2{{[ \t,;]}}

# MNEM: mulss32.hhll
# LOGICAL: mulss32_hhll
rt_mulss32_hhll:
{ nop; mulss32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulss32_hhll>:
# DIS: {{[ \t]}}mulss32.hhll{{[ \t,;]}}

# MNEM: mulss32.hllh
# LOGICAL: mulss32_hllh
rt_mulss32_hllh:
{ nop; mulss32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulss32_hllh>:
# DIS: {{[ \t]}}mulss32.hllh{{[ \t,;]}}

# MNEM: mulss64.hh
# LOGICAL: mulss64_hh
rt_mulss64_hh:
{ nop; mulss64.hh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_hh>:
# DIS: {{[ \t]}}mulss64.hh{{[ \t,;]}}

# MNEM: mulss64.hl
# LOGICAL: mulss64_hl
rt_mulss64_hl:
{ nop; mulss64.hl d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_hl>:
# DIS: {{[ \t]}}mulss64.hl{{[ \t,;]}}

# MNEM: mulss64.huh
# LOGICAL: mulss64_huh
rt_mulss64_huh:
{ nop; mulss64.huh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_huh>:
# DIS: {{[ \t]}}mulss64.huh{{[ \t,;]}}

# MNEM: mulss64.hul
# LOGICAL: mulss64_hul
rt_mulss64_hul:
{ nop; mulss64.hul d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_hul>:
# DIS: {{[ \t]}}mulss64.hul{{[ \t,;]}}

# MNEM: mulss64.lh
# LOGICAL: mulss64_lh
rt_mulss64_lh:
{ nop; mulss64.lh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_lh>:
# DIS: {{[ \t]}}mulss64.lh{{[ \t,;]}}

# MNEM: mulss64.ll
# LOGICAL: mulss64_ll
rt_mulss64_ll:
{ nop; mulss64.ll d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_ll>:
# DIS: {{[ \t]}}mulss64.ll{{[ \t,;]}}

# MNEM: mulss64.luh
# LOGICAL: mulss64_luh
rt_mulss64_luh:
{ nop; mulss64.luh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_luh>:
# DIS: {{[ \t]}}mulss64.luh{{[ \t,;]}}

# MNEM: mulss64.lul
# LOGICAL: mulss64_lul
rt_mulss64_lul:
{ nop; mulss64.lul d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_lul>:
# DIS: {{[ \t]}}mulss64.lul{{[ \t,;]}}

# MNEM: mulss64.uhh
# LOGICAL: mulss64_uhh
rt_mulss64_uhh:
{ nop; mulss64.uhh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_uhh>:
# DIS: {{[ \t]}}mulss64.uhh{{[ \t,;]}}

# MNEM: mulss64.uhl
# LOGICAL: mulss64_uhl
rt_mulss64_uhl:
{ nop; mulss64.uhl d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_uhl>:
# DIS: {{[ \t]}}mulss64.uhl{{[ \t,;]}}

# MNEM: mulss64.uhuh
# LOGICAL: mulss64_uhuh
rt_mulss64_uhuh:
{ nop; mulss64.uhuh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_uhuh>:
# DIS: {{[ \t]}}mulss64.uhuh{{[ \t,;]}}

# MNEM: mulss64.uhul
# LOGICAL: mulss64_uhul
rt_mulss64_uhul:
{ nop; mulss64.uhul d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_uhul>:
# DIS: {{[ \t]}}mulss64.uhul{{[ \t,;]}}

# MNEM: mulss64.ulh
# LOGICAL: mulss64_ulh
rt_mulss64_ulh:
{ nop; mulss64.ulh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_ulh>:
# DIS: {{[ \t]}}mulss64.ulh{{[ \t,;]}}

# MNEM: mulss64.ull
# LOGICAL: mulss64_ull
rt_mulss64_ull:
{ nop; mulss64.ull d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_ull>:
# DIS: {{[ \t]}}mulss64.ull{{[ \t,;]}}

# MNEM: mulss64.uluh
# LOGICAL: mulss64_uluh
rt_mulss64_uluh:
{ nop; mulss64.uluh d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_uluh>:
# DIS: {{[ \t]}}mulss64.uluh{{[ \t,;]}}

# MNEM: mulss64.ulul
# LOGICAL: mulss64_ulul
rt_mulss64_ulul:
{ nop; mulss64.ulul d0, d1, d2 }
# DIS-LABEL: <rt_mulss64_ulul>:
# DIS: {{[ \t]}}mulss64.ulul{{[ \t,;]}}

# MNEM: mulssh
rt_mulssh:
{ nop; mulssh r1, r2, r3 }
# DIS-LABEL: <rt_mulssh>:
# DIS: {{[ \t]}}mulssh{{[ \t,;]}}

# MNEM: mulsuh
rt_mulsuh:
{ nop; mulsuh r1, r2, r3 }
# DIS-LABEL: <rt_mulsuh>:
# DIS: {{[ \t]}}mulsuh{{[ \t,;]}}

# MNEM: muluuh
rt_muluuh:
{ nop; muluuh r1, r2, r3 }
# DIS-LABEL: <rt_muluuh>:
# DIS: {{[ \t]}}muluuh{{[ \t,;]}}

# MNEM: mulzaa32x16.h0.l1
# LOGICAL: mulzaa32x16_h0_l1
rt_mulzaa32x16_h0_l1:
{ nop; mulzaa32x16.h0.l1 d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32x16_h0_l1>:
# DIS: {{[ \t]}}mulzaa32x16.h0.l1{{[ \t,;]}}

# MNEM: mulzaa32x16.h1.l0
# LOGICAL: mulzaa32x16_h1_l0
rt_mulzaa32x16_h1_l0:
{ nop; mulzaa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32x16_h1_l0>:
# DIS: {{[ \t]}}mulzaa32x16.h1.l0{{[ \t,;]}}

# MNEM: mulzaa32x16.h2.l3
# LOGICAL: mulzaa32x16_h2_l3
rt_mulzaa32x16_h2_l3:
{ nop; mulzaa32x16.h2.l3 d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32x16_h2_l3>:
# DIS: {{[ \t]}}mulzaa32x16.h2.l3{{[ \t,;]}}

# MNEM: mulzaa32x16.h3.l2
# LOGICAL: mulzaa32x16_h3_l2
rt_mulzaa32x16_h3_l2:
{ nop; mulzaa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32x16_h3_l2>:
# DIS: {{[ \t]}}mulzaa32x16.h3.l2{{[ \t,;]}}

# MNEM: mulzaa32.hhll
# LOGICAL: mulzaa32_hhll
rt_mulzaa32_hhll:
{ nop; mulzaa32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32_hhll>:
# DIS: {{[ \t]}}mulzaa32.hhll{{[ \t,;]}}

# MNEM: mulzaa32.hllh
# LOGICAL: mulzaa32_hllh
rt_mulzaa32_hllh:
{ nop; mulzaa32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulzaa32_hllh>:
# DIS: {{[ \t]}}mulzaa32.hllh{{[ \t,;]}}

# MNEM: mulzas32x16.h1.l0
# LOGICAL: mulzas32x16_h1_l0
rt_mulzas32x16_h1_l0:
{ nop; mulzas32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulzas32x16_h1_l0>:
# DIS: {{[ \t]}}mulzas32x16.h1.l0{{[ \t,;]}}

# MNEM: mulzas32x16.h3.l2
# LOGICAL: mulzas32x16_h3_l2
rt_mulzas32x16_h3_l2:
{ nop; mulzas32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulzas32x16_h3_l2>:
# DIS: {{[ \t]}}mulzas32x16.h3.l2{{[ \t,;]}}

# MNEM: mulzas32.hhll
# LOGICAL: mulzas32_hhll
rt_mulzas32_hhll:
{ nop; mulzas32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulzas32_hhll>:
# DIS: {{[ \t]}}mulzas32.hhll{{[ \t,;]}}

# MNEM: mulzas32.hllh
# LOGICAL: mulzas32_hllh
rt_mulzas32_hllh:
{ nop; mulzas32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulzas32_hllh>:
# DIS: {{[ \t]}}mulzas32.hllh{{[ \t,;]}}

# MNEM: mulzsa32x16.h1.l0
# LOGICAL: mulzsa32x16_h1_l0
rt_mulzsa32x16_h1_l0:
{ nop; mulzsa32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulzsa32x16_h1_l0>:
# DIS: {{[ \t]}}mulzsa32x16.h1.l0{{[ \t,;]}}

# MNEM: mulzsa32x16.h3.l2
# LOGICAL: mulzsa32x16_h3_l2
rt_mulzsa32x16_h3_l2:
{ nop; mulzsa32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulzsa32x16_h3_l2>:
# DIS: {{[ \t]}}mulzsa32x16.h3.l2{{[ \t,;]}}

# MNEM: mulzsa32.hhll
# LOGICAL: mulzsa32_hhll
rt_mulzsa32_hhll:
{ nop; mulzsa32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulzsa32_hhll>:
# DIS: {{[ \t]}}mulzsa32.hhll{{[ \t,;]}}

# MNEM: mulzsa32.hllh
# LOGICAL: mulzsa32_hllh
rt_mulzsa32_hllh:
{ nop; mulzsa32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulzsa32_hllh>:
# DIS: {{[ \t]}}mulzsa32.hllh{{[ \t,;]}}

# MNEM: mulzss32x16.h1.l0
# LOGICAL: mulzss32x16_h1_l0
rt_mulzss32x16_h1_l0:
{ nop; mulzss32x16.h1.l0 d0, d1, d2 }
# DIS-LABEL: <rt_mulzss32x16_h1_l0>:
# DIS: {{[ \t]}}mulzss32x16.h1.l0{{[ \t,;]}}

# MNEM: mulzss32x16.h3.l2
# LOGICAL: mulzss32x16_h3_l2
rt_mulzss32x16_h3_l2:
{ nop; mulzss32x16.h3.l2 d0, d1, d2 }
# DIS-LABEL: <rt_mulzss32x16_h3_l2>:
# DIS: {{[ \t]}}mulzss32x16.h3.l2{{[ \t,;]}}

# MNEM: mulzss32.hhll
# LOGICAL: mulzss32_hhll
rt_mulzss32_hhll:
{ nop; mulzss32.hhll d0, d1, d2 }
# DIS-LABEL: <rt_mulzss32_hhll>:
# DIS: {{[ \t]}}mulzss32.hhll{{[ \t,;]}}

# MNEM: mulzss32.hllh
# LOGICAL: mulzss32_hllh
rt_mulzss32_hllh:
{ nop; mulzss32.hllh d0, d1, d2 }
# DIS-LABEL: <rt_mulzss32_hllh>:
# DIS: {{[ \t]}}mulzss32.hllh{{[ \t,;]}}

# MNEM: neg32
rt_neg32:
{ nop; neg32 r1, r2 }
# DIS-LABEL: <rt_neg32>:
# DIS: {{[ \t]}}neg32{{[ \t,;]}}

# MNEM: neg32s
rt_neg32s:
{ nop; neg32s r1, r2 }
# DIS-LABEL: <rt_neg32s>:
# DIS: {{[ \t]}}neg32s{{[ \t,;]}}

# MNEM: neg64
rt_neg64:
{ nop; neg64 d0, d1 }
# DIS-LABEL: <rt_neg64>:
# DIS: {{[ \t]}}neg64{{[ \t,;]}}

# MNEM: neg64s
rt_neg64s:
{ nop; neg64s d0, d1 }
# DIS-LABEL: <rt_neg64s>:
# DIS: {{[ \t]}}neg64s{{[ \t,;]}}

# MNEM: not32
rt_not32:
{ nop; not32 r1, r2 }
# DIS-LABEL: <rt_not32>:
# DIS: {{[ \t]}}not32{{[ \t,;]}}

# MNEM: not64
rt_not64:
{ nop; not64 d0, d1 }
# DIS-LABEL: <rt_not64>:
# DIS: {{[ \t]}}not64{{[ \t,;]}}

# MNEM: nsa16_l
rt_nsa16_l:
{ nop; nsa16_l r1, d0 }
# DIS-LABEL: <rt_nsa16_l>:
# DIS: {{[ \t]}}nsa16_l{{[ \t,;]}}

# MNEM: nsa32
rt_nsa32:
{ nop; nsa32 r1, r2 }
# DIS-LABEL: <rt_nsa32>:
# DIS: {{[ \t]}}nsa32{{[ \t,;]}}

# MNEM: nsa32_l
rt_nsa32_l:
{ nop; nsa32_l r1, d0 }
# DIS-LABEL: <rt_nsa32_l>:
# DIS: {{[ \t]}}nsa32_l{{[ \t,;]}}

# MNEM: nsa64
rt_nsa64:
{ nop; nsa64 r1, d0 }
# DIS-LABEL: <rt_nsa64>:
# DIS: {{[ \t]}}nsa64{{[ \t,;]}}

# MNEM: nsau32
rt_nsau32:
{ nop; nsau32 r1, r2 }
# DIS-LABEL: <rt_nsau32>:
# DIS: {{[ \t]}}nsau32{{[ \t,;]}}

# MNEM: nsaz16_l
rt_nsaz16_l:
{ nop; nsaz16_l r1, d0 }
# DIS-LABEL: <rt_nsaz16_l>:
# DIS: {{[ \t]}}nsaz16_l{{[ \t,;]}}

# MNEM: nsaz32_l
rt_nsaz32_l:
{ nop; nsaz32_l r1, d0 }
# DIS-LABEL: <rt_nsaz32_l>:
# DIS: {{[ \t]}}nsaz32_l{{[ \t,;]}}

# MNEM: nsaz64
rt_nsaz64:
{ nop; nsaz64 r1, d0 }
# DIS-LABEL: <rt_nsaz64>:
# DIS: {{[ \t]}}nsaz64{{[ \t,;]}}

# MNEM: or32
rt_or32:
{ nop; or32 r1, r2, r3 }
# DIS-LABEL: <rt_or32>:
# DIS: {{[ \t]}}or32{{[ \t,;]}}

# MNEM: or64
rt_or64:
{ nop; or64 d0, d1, d2 }
# DIS-LABEL: <rt_or64>:
# DIS: {{[ \t]}}or64{{[ \t,;]}}

# MNEM: ori32
rt_ori32:
{ nop; ori32 r1, r2, 1 }
# DIS-LABEL: <rt_ori32>:
# DIS: {{[ \t]}}ori32{{[ \t,;]}}

# MNEM: pldwwua
# LOGICAL: pldwwua_post
rt_pldwwua:
{ nop; pldwwua 0, r1 }
# DIS-LABEL: <rt_pldwwua>:
# DIS: {{[ \t]}}pldwwua{{[ \t,;]}}

# MNEM: popcount32
rt_popcount32:
{ nop; popcount32 r1, r2 }
# DIS-LABEL: <rt_popcount32>:
# DIS: {{[ \t]}}popcount32{{[ \t,;]}}

# MNEM: popcount64
rt_popcount64:
{ nop; popcount64 r1, d0 }
# DIS-LABEL: <rt_popcount64>:
# DIS: {{[ \t]}}popcount64{{[ \t,;]}}

# MNEM: recip
rt_recip:
{ nop; nop; recip r1, r2 }
# DIS-LABEL: <rt_recip>:
# DIS: {{[ \t]}}recip{{[ \t,;]}}

# MNEM: seq32
rt_seq32:
{ nop; seq32 r1, r2, r3 }
# DIS-LABEL: <rt_seq32>:
# DIS: {{[ \t]}}seq32{{[ \t,;]}}

# MNEM: seq64
rt_seq64:
{ nop; seq64 d0, d1 }
# DIS-LABEL: <rt_seq64>:
# DIS: {{[ \t]}}seq64{{[ \t,;]}}

# MNEM: set_hwloop
rt_set_hwloop:
{ nop; set_hwloop_w 0, 16, 32, 4 }
# DIS-LABEL: <rt_set_hwloop>:
# DIS: {{[ \t]}}set_hwloop{{[ \t,;]}}

# MNEM: set_hwloop_f2
rt_set_hwloop_f2:
{ nop; set_hwloop_f2_w 0, 16, 32, r1 }
# DIS-LABEL: <rt_set_hwloop_f2>:
# DIS: {{[ \t]}}set_hwloop_f2{{[ \t,;]}}

# MNEM: set_hwloop_reg
rt_set_hwloop_reg:
{ nop; set_hwloop_reg_w 0, r1, r2, r3 }
# DIS-LABEL: <rt_set_hwloop_reg>:
# DIS: {{[ \t]}}set_hwloop_reg{{[ \t,;]}}

# MNEM: sext32t64
rt_sext32t64:
{ nop; sext32t64 d0, r1 }
# DIS-LABEL: <rt_sext32t64>:
# DIS: {{[ \t]}}sext32t64{{[ \t,;]}}

# MNEM: sin_cos
rt_sin_cos:
{ nop; nop; sin_cos d0, r1, 1 }
# DIS-LABEL: <rt_sin_cos>:
# DIS: {{[ \t]}}sin_cos{{[ \t,;]}}

# MNEM: sle32
rt_sle32:
{ nop; sle32 r1, r2, r3 }
# DIS-LABEL: <rt_sle32>:
# DIS: {{[ \t]}}sle32{{[ \t,;]}}

# MNEM: sle64
rt_sle64:
{ nop; sle64 d0, d1 }
# DIS-LABEL: <rt_sle64>:
# DIS: {{[ \t]}}sle64{{[ \t,;]}}

# MNEM: sll32
rt_sll32:
{ nop; sll32 r1, r2, r3 }
# DIS-LABEL: <rt_sll32>:
# DIS: {{[ \t]}}sll32{{[ \t,;]}}

# MNEM: sll64
rt_sll64:
{ nop; sll64 d0, d1, r1 }
# DIS-LABEL: <rt_sll64>:
# DIS: {{[ \t]}}sll64{{[ \t,;]}}

# MNEM: slli32
rt_slli32:
{ nop; slli32 r1, r2, 1 }
# DIS-LABEL: <rt_slli32>:
# DIS: {{[ \t]}}slli32{{[ \t,;]}}

# MNEM: slli64
rt_slli64:
{ nop; slli64 d0, d0, 1 }
# DIS-LABEL: <rt_slli64>:
# DIS: {{[ \t]}}slli64{{[ \t,;]}}

# MNEM: slt32
rt_slt32:
{ nop; slt32 r1, r2, r3 }
# DIS-LABEL: <rt_slt32>:
# DIS: {{[ \t]}}slt32{{[ \t,;]}}

# MNEM: slt64
rt_slt64:
{ nop; slt64 d0, d1 }
# DIS-LABEL: <rt_slt64>:
# DIS: {{[ \t]}}slt64{{[ \t,;]}}

# MNEM: sltu32
rt_sltu32:
{ nop; sltu32 r1, r2, r3 }
# DIS-LABEL: <rt_sltu32>:
# DIS: {{[ \t]}}sltu32{{[ \t,;]}}

# MNEM: smula16s.00
# LOGICAL: smula16s_00
rt_smula16s_00:
{ nop; smula16s.00 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_00>:
# DIS: {{[ \t]}}smula16s.00{{[ \t,;]}}

# MNEM: smula16s.10
# LOGICAL: smula16s_10
rt_smula16s_10:
{ nop; smula16s.10 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_10>:
# DIS: {{[ \t]}}smula16s.10{{[ \t,;]}}

# MNEM: smula16s.11
# LOGICAL: smula16s_11
rt_smula16s_11:
{ nop; smula16s.11 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_11>:
# DIS: {{[ \t]}}smula16s.11{{[ \t,;]}}

# MNEM: smula16s.20
# LOGICAL: smula16s_20
rt_smula16s_20:
{ nop; smula16s.20 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_20>:
# DIS: {{[ \t]}}smula16s.20{{[ \t,;]}}

# MNEM: smula16s.21
# LOGICAL: smula16s_21
rt_smula16s_21:
{ nop; smula16s.21 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_21>:
# DIS: {{[ \t]}}smula16s.21{{[ \t,;]}}

# MNEM: smula16s.22
# LOGICAL: smula16s_22
rt_smula16s_22:
{ nop; smula16s.22 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_22>:
# DIS: {{[ \t]}}smula16s.22{{[ \t,;]}}

# MNEM: smula16s.30
# LOGICAL: smula16s_30
rt_smula16s_30:
{ nop; smula16s.30 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_30>:
# DIS: {{[ \t]}}smula16s.30{{[ \t,;]}}

# MNEM: smula16s.31
# LOGICAL: smula16s_31
rt_smula16s_31:
{ nop; smula16s.31 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_31>:
# DIS: {{[ \t]}}smula16s.31{{[ \t,;]}}

# MNEM: smula16s.32
# LOGICAL: smula16s_32
rt_smula16s_32:
{ nop; smula16s.32 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_32>:
# DIS: {{[ \t]}}smula16s.32{{[ \t,;]}}

# MNEM: smula16s.33
# LOGICAL: smula16s_33
rt_smula16s_33:
{ nop; smula16s.33 d0, d1, d2 }
# DIS-LABEL: <rt_smula16s_33>:
# DIS: {{[ \t]}}smula16s.33{{[ \t,;]}}

# MNEM: smula16.00
# LOGICAL: smula16_00
rt_smula16_00:
{ nop; smula16.00 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_00>:
# DIS: {{[ \t]}}smula16.00{{[ \t,;]}}

# MNEM: smula16.10
# LOGICAL: smula16_10
rt_smula16_10:
{ nop; smula16.10 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_10>:
# DIS: {{[ \t]}}smula16.10{{[ \t,;]}}

# MNEM: smula16.11
# LOGICAL: smula16_11
rt_smula16_11:
{ nop; smula16.11 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_11>:
# DIS: {{[ \t]}}smula16.11{{[ \t,;]}}

# MNEM: smula16.20
# LOGICAL: smula16_20
rt_smula16_20:
{ nop; smula16.20 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_20>:
# DIS: {{[ \t]}}smula16.20{{[ \t,;]}}

# MNEM: smula16.21
# LOGICAL: smula16_21
rt_smula16_21:
{ nop; smula16.21 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_21>:
# DIS: {{[ \t]}}smula16.21{{[ \t,;]}}

# MNEM: smula16.22
# LOGICAL: smula16_22
rt_smula16_22:
{ nop; smula16.22 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_22>:
# DIS: {{[ \t]}}smula16.22{{[ \t,;]}}

# MNEM: smula16.30
# LOGICAL: smula16_30
rt_smula16_30:
{ nop; smula16.30 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_30>:
# DIS: {{[ \t]}}smula16.30{{[ \t,;]}}

# MNEM: smula16.31
# LOGICAL: smula16_31
rt_smula16_31:
{ nop; smula16.31 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_31>:
# DIS: {{[ \t]}}smula16.31{{[ \t,;]}}

# MNEM: smula16.32
# LOGICAL: smula16_32
rt_smula16_32:
{ nop; smula16.32 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_32>:
# DIS: {{[ \t]}}smula16.32{{[ \t,;]}}

# MNEM: smula16.33
# LOGICAL: smula16_33
rt_smula16_33:
{ nop; smula16.33 d0, d1, d2 }
# DIS-LABEL: <rt_smula16_33>:
# DIS: {{[ \t]}}smula16.33{{[ \t,;]}}

# MNEM: smuls16s.00
# LOGICAL: smuls16s_00
rt_smuls16s_00:
{ nop; smuls16s.00 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_00>:
# DIS: {{[ \t]}}smuls16s.00{{[ \t,;]}}

# MNEM: smuls16s.10
# LOGICAL: smuls16s_10
rt_smuls16s_10:
{ nop; smuls16s.10 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_10>:
# DIS: {{[ \t]}}smuls16s.10{{[ \t,;]}}

# MNEM: smuls16s.11
# LOGICAL: smuls16s_11
rt_smuls16s_11:
{ nop; smuls16s.11 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_11>:
# DIS: {{[ \t]}}smuls16s.11{{[ \t,;]}}

# MNEM: smuls16s.20
# LOGICAL: smuls16s_20
rt_smuls16s_20:
{ nop; smuls16s.20 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_20>:
# DIS: {{[ \t]}}smuls16s.20{{[ \t,;]}}

# MNEM: smuls16s.21
# LOGICAL: smuls16s_21
rt_smuls16s_21:
{ nop; smuls16s.21 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_21>:
# DIS: {{[ \t]}}smuls16s.21{{[ \t,;]}}

# MNEM: smuls16s.22
# LOGICAL: smuls16s_22
rt_smuls16s_22:
{ nop; smuls16s.22 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_22>:
# DIS: {{[ \t]}}smuls16s.22{{[ \t,;]}}

# MNEM: smuls16s.30
# LOGICAL: smuls16s_30
rt_smuls16s_30:
{ nop; smuls16s.30 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_30>:
# DIS: {{[ \t]}}smuls16s.30{{[ \t,;]}}

# MNEM: smuls16s.31
# LOGICAL: smuls16s_31
rt_smuls16s_31:
{ nop; smuls16s.31 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_31>:
# DIS: {{[ \t]}}smuls16s.31{{[ \t,;]}}

# MNEM: smuls16s.32
# LOGICAL: smuls16s_32
rt_smuls16s_32:
{ nop; smuls16s.32 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_32>:
# DIS: {{[ \t]}}smuls16s.32{{[ \t,;]}}

# MNEM: smuls16s.33
# LOGICAL: smuls16s_33
rt_smuls16s_33:
{ nop; smuls16s.33 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16s_33>:
# DIS: {{[ \t]}}smuls16s.33{{[ \t,;]}}

# MNEM: smuls16.00
# LOGICAL: smuls16_00
rt_smuls16_00:
{ nop; smuls16.00 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_00>:
# DIS: {{[ \t]}}smuls16.00{{[ \t,;]}}

# MNEM: smuls16.10
# LOGICAL: smuls16_10
rt_smuls16_10:
{ nop; smuls16.10 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_10>:
# DIS: {{[ \t]}}smuls16.10{{[ \t,;]}}

# MNEM: smuls16.11
# LOGICAL: smuls16_11
rt_smuls16_11:
{ nop; smuls16.11 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_11>:
# DIS: {{[ \t]}}smuls16.11{{[ \t,;]}}

# MNEM: smuls16.20
# LOGICAL: smuls16_20
rt_smuls16_20:
{ nop; smuls16.20 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_20>:
# DIS: {{[ \t]}}smuls16.20{{[ \t,;]}}

# MNEM: smuls16.21
# LOGICAL: smuls16_21
rt_smuls16_21:
{ nop; smuls16.21 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_21>:
# DIS: {{[ \t]}}smuls16.21{{[ \t,;]}}

# MNEM: smuls16.22
# LOGICAL: smuls16_22
rt_smuls16_22:
{ nop; smuls16.22 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_22>:
# DIS: {{[ \t]}}smuls16.22{{[ \t,;]}}

# MNEM: smuls16.30
# LOGICAL: smuls16_30
rt_smuls16_30:
{ nop; smuls16.30 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_30>:
# DIS: {{[ \t]}}smuls16.30{{[ \t,;]}}

# MNEM: smuls16.31
# LOGICAL: smuls16_31
rt_smuls16_31:
{ nop; smuls16.31 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_31>:
# DIS: {{[ \t]}}smuls16.31{{[ \t,;]}}

# MNEM: smuls16.32
# LOGICAL: smuls16_32
rt_smuls16_32:
{ nop; smuls16.32 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_32>:
# DIS: {{[ \t]}}smuls16.32{{[ \t,;]}}

# MNEM: smuls16.33
# LOGICAL: smuls16_33
rt_smuls16_33:
{ nop; smuls16.33 d0, d1, d2 }
# DIS-LABEL: <rt_smuls16_33>:
# DIS: {{[ \t]}}smuls16.33{{[ \t,;]}}

# MNEM: sqrt
rt_sqrt:
{ nop; nop; sqrt r1, r2 }
# DIS-LABEL: <rt_sqrt>:
# DIS: {{[ \t]}}sqrt{{[ \t,;]}}

# MNEM: sra32
rt_sra32:
{ nop; sra32 r1, r2, r3 }
# DIS-LABEL: <rt_sra32>:
# DIS: {{[ \t]}}sra32{{[ \t,;]}}

# MNEM: sra32r
rt_sra32r:
{ nop; sra32r r1, r2, r3 }
# DIS-LABEL: <rt_sra32r>:
# DIS: {{[ \t]}}sra32r{{[ \t,;]}}

# MNEM: sra64
rt_sra64:
{ nop; sra64 d0, d1, r1 }
# DIS-LABEL: <rt_sra64>:
# DIS: {{[ \t]}}sra64{{[ \t,;]}}

# MNEM: sra64r
rt_sra64r:
{ nop; sra64r d0, d1, r1 }
# DIS-LABEL: <rt_sra64r>:
# DIS: {{[ \t]}}sra64r{{[ \t,;]}}

# MNEM: srai32
rt_srai32:
{ nop; srai32 r1, r2, 1 }
# DIS-LABEL: <rt_srai32>:
# DIS: {{[ \t]}}srai32{{[ \t,;]}}

# MNEM: srai32r
rt_srai32r:
{ nop; srai32r r1, r2, 1 }
# DIS-LABEL: <rt_srai32r>:
# DIS: {{[ \t]}}srai32r{{[ \t,;]}}

# MNEM: srai64
rt_srai64:
{ nop; srai64 d0, d0, 1 }
# DIS-LABEL: <rt_srai64>:
# DIS: {{[ \t]}}srai64{{[ \t,;]}}

# MNEM: srai64r
rt_srai64r:
{ nop; srai64r d0, d0, 1 }
# DIS-LABEL: <rt_srai64r>:
# DIS: {{[ \t]}}srai64r{{[ \t,;]}}

# MNEM: srl32
rt_srl32:
{ nop; srl32 r1, r2, r3 }
# DIS-LABEL: <rt_srl32>:
# DIS: {{[ \t]}}srl32{{[ \t,;]}}

# MNEM: srl64
rt_srl64:
{ nop; srl64 d0, d1, r1 }
# DIS-LABEL: <rt_srl64>:
# DIS: {{[ \t]}}srl64{{[ \t,;]}}

# MNEM: srli32
rt_srli32:
{ nop; srli32 r1, r2, 1 }
# DIS-LABEL: <rt_srli32>:
# DIS: {{[ \t]}}srli32{{[ \t,;]}}

# MNEM: srli64
rt_srli64:
{ nop; srli64 d0, d0, 1 }
# DIS-LABEL: <rt_srli64>:
# DIS: {{[ \t]}}srli64{{[ \t,;]}}

# MNEM: sub32
rt_sub32:
{ nop; sub32 r1, r2, r3 }
# DIS-LABEL: <rt_sub32>:
# DIS: {{[ \t]}}sub32{{[ \t,;]}}

# MNEM: sub32s
rt_sub32s:
{ nop; sub32s r1, r2, r3 }
# DIS-LABEL: <rt_sub32s>:
# DIS: {{[ \t]}}sub32s{{[ \t,;]}}

# MNEM: sub64
rt_sub64:
{ nop; sub64 d0, d1, d2 }
# DIS-LABEL: <rt_sub64>:
# DIS: {{[ \t]}}sub64{{[ \t,;]}}

# MNEM: sub64s
rt_sub64s:
{ nop; sub64s d0, d1, d2 }
# DIS-LABEL: <rt_sub64s>:
# DIS: {{[ \t]}}sub64s{{[ \t,;]}}

# MNEM: sub64s_h
rt_sub64s_h:
{ nop; sub64s_h d0, d1, d2 }
# DIS-LABEL: <rt_sub64s_h>:
# DIS: {{[ \t]}}sub64s_h{{[ \t,;]}}

# MNEM: sub64s_l
rt_sub64s_l:
{ nop; sub64s_l d0, d1, d2 }
# DIS-LABEL: <rt_sub64s_l>:
# DIS: {{[ \t]}}sub64s_l{{[ \t,;]}}

# MNEM: sub64_h
rt_sub64_h:
{ nop; sub64_h d0, d1, d2 }
# DIS-LABEL: <rt_sub64_h>:
# DIS: {{[ \t]}}sub64_h{{[ \t,;]}}

# MNEM: sub64_l
rt_sub64_l:
{ nop; sub64_l d0, d1, d2 }
# DIS-LABEL: <rt_sub64_l>:
# DIS: {{[ \t]}}sub64_l{{[ \t,;]}}

# MNEM: subi32
rt_subi32:
{ nop; subi32 r1, r2, 1 }
# DIS-LABEL: <rt_subi32>:
# DIS: {{[ \t]}}subi32{{[ \t,;]}}

# MNEM: subi32s
rt_subi32s:
{ nop; subi32s r1, r2, 1 }
# DIS-LABEL: <rt_subi32s>:
# DIS: {{[ \t]}}subi32s{{[ \t,;]}}

# MNEM: s_lbs_post_imm
rt_s_lbs_post_imm:
{ nop; s_lbs_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lbs_post_imm>:
# DIS: {{[ \t]}}s_lbs_post_imm{{[ \t,;]}}

# MNEM: s_lbs_post_reg
rt_s_lbs_post_reg:
{ nop; s_lbs_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbs_post_reg>:
# DIS: {{[ \t]}}s_lbs_post_reg{{[ \t,;]}}

# MNEM: s_lbs_pre_imm
rt_s_lbs_pre_imm:
{ nop; s_lbs_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lbs_pre_imm>:
# DIS: {{[ \t]}}s_lbs_pre_imm{{[ \t,;]}}

# MNEM: s_lbs_pre_reg
rt_s_lbs_pre_reg:
{ nop; s_lbs_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbs_pre_reg>:
# DIS: {{[ \t]}}s_lbs_pre_reg{{[ \t,;]}}

# MNEM: ld8
# LOGICAL: s_lbs_with_imm
rt_ld8:
{ nop; ld8 r1, r2, 1 }
# DIS-LABEL: <rt_ld8>:
# DIS: {{[ \t]}}ld8{{[ \t,;]}}

# MNEM: s_lbs_with_reg
rt_s_lbs_with_reg:
{ nop; s_lbs_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbs_with_reg>:
# DIS: {{[ \t]}}s_lbs_with_reg{{[ \t,;]}}

# MNEM: s_lbu_post_imm
rt_s_lbu_post_imm:
{ nop; s_lbu_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lbu_post_imm>:
# DIS: {{[ \t]}}s_lbu_post_imm{{[ \t,;]}}

# MNEM: s_lbu_post_reg
rt_s_lbu_post_reg:
{ nop; s_lbu_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbu_post_reg>:
# DIS: {{[ \t]}}s_lbu_post_reg{{[ \t,;]}}

# MNEM: s_lbu_pre_imm
rt_s_lbu_pre_imm:
{ nop; s_lbu_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lbu_pre_imm>:
# DIS: {{[ \t]}}s_lbu_pre_imm{{[ \t,;]}}

# MNEM: s_lbu_pre_reg
rt_s_lbu_pre_reg:
{ nop; s_lbu_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbu_pre_reg>:
# DIS: {{[ \t]}}s_lbu_pre_reg{{[ \t,;]}}

# MNEM: ldu8
# LOGICAL: s_lbu_with_imm
rt_ldu8:
{ nop; ldu8 r1, r2, 1 }
# DIS-LABEL: <rt_ldu8>:
# DIS: {{[ \t]}}ldu8{{[ \t,;]}}

# MNEM: s_lbu_with_reg
rt_s_lbu_with_reg:
{ nop; s_lbu_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lbu_with_reg>:
# DIS: {{[ \t]}}s_lbu_with_reg{{[ \t,;]}}

# MNEM: s_lhws_post_imm
rt_s_lhws_post_imm:
{ nop; s_lhws_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lhws_post_imm>:
# DIS: {{[ \t]}}s_lhws_post_imm{{[ \t,;]}}

# MNEM: s_lhws_post_reg
rt_s_lhws_post_reg:
{ nop; s_lhws_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhws_post_reg>:
# DIS: {{[ \t]}}s_lhws_post_reg{{[ \t,;]}}

# MNEM: s_lhws_pre_imm
rt_s_lhws_pre_imm:
{ nop; s_lhws_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lhws_pre_imm>:
# DIS: {{[ \t]}}s_lhws_pre_imm{{[ \t,;]}}

# MNEM: s_lhws_pre_reg
rt_s_lhws_pre_reg:
{ nop; s_lhws_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhws_pre_reg>:
# DIS: {{[ \t]}}s_lhws_pre_reg{{[ \t,;]}}

# MNEM: ld16
# LOGICAL: s_lhws_with_imm
rt_ld16:
{ nop; ld16 r1, r2, 1 }
# DIS-LABEL: <rt_ld16>:
# DIS: {{[ \t]}}ld16{{[ \t,;]}}

# MNEM: s_lhws_with_reg
rt_s_lhws_with_reg:
{ nop; s_lhws_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhws_with_reg>:
# DIS: {{[ \t]}}s_lhws_with_reg{{[ \t,;]}}

# MNEM: s_lhwu_post_imm
rt_s_lhwu_post_imm:
{ nop; s_lhwu_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lhwu_post_imm>:
# DIS: {{[ \t]}}s_lhwu_post_imm{{[ \t,;]}}

# MNEM: s_lhwu_post_reg
rt_s_lhwu_post_reg:
{ nop; s_lhwu_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhwu_post_reg>:
# DIS: {{[ \t]}}s_lhwu_post_reg{{[ \t,;]}}

# MNEM: s_lhwu_pre_imm
rt_s_lhwu_pre_imm:
{ nop; s_lhwu_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lhwu_pre_imm>:
# DIS: {{[ \t]}}s_lhwu_pre_imm{{[ \t,;]}}

# MNEM: s_lhwu_pre_reg
rt_s_lhwu_pre_reg:
{ nop; s_lhwu_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhwu_pre_reg>:
# DIS: {{[ \t]}}s_lhwu_pre_reg{{[ \t,;]}}

# MNEM: ldu16
# LOGICAL: s_lhwu_with_imm
rt_ldu16:
{ nop; ldu16 r1, r2, 1 }
# DIS-LABEL: <rt_ldu16>:
# DIS: {{[ \t]}}ldu16{{[ \t,;]}}

# MNEM: s_lhwu_with_reg
rt_s_lhwu_with_reg:
{ nop; s_lhwu_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lhwu_with_reg>:
# DIS: {{[ \t]}}s_lhwu_with_reg{{[ \t,;]}}

# MNEM: s_lw_brev_imm
rt_s_lw_brev_imm:
{ nop; s_lw_brev_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lw_brev_imm>:
# DIS: {{[ \t]}}s_lw_brev_imm{{[ \t,;]}}

# MNEM: s_lw_brev_reg
rt_s_lw_brev_reg:
{ nop; s_lw_brev_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lw_brev_reg>:
# DIS: {{[ \t]}}s_lw_brev_reg{{[ \t,;]}}

# MNEM: s_lw_post_imm
rt_s_lw_post_imm:
{ nop; s_lw_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lw_post_imm>:
# DIS: {{[ \t]}}s_lw_post_imm{{[ \t,;]}}

# MNEM: s_lw_post_reg
rt_s_lw_post_reg:
{ nop; s_lw_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lw_post_reg>:
# DIS: {{[ \t]}}s_lw_post_reg{{[ \t,;]}}

# MNEM: s_lw_pre_imm
rt_s_lw_pre_imm:
{ nop; s_lw_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_lw_pre_imm>:
# DIS: {{[ \t]}}s_lw_pre_imm{{[ \t,;]}}

# MNEM: s_lw_pre_reg
rt_s_lw_pre_reg:
{ nop; s_lw_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_lw_pre_reg>:
# DIS: {{[ \t]}}s_lw_pre_reg{{[ \t,;]}}

# MNEM: ld32
# LOGICAL: s_lw_with_imm
rt_ld32:
{ nop; ld32 r1, r2, 1 }
# DIS-LABEL: <rt_ld32>:
# DIS: {{[ \t]}}ld32{{[ \t,;]}}

# MNEM: ld32_reg
# LOGICAL: s_lw_with_reg
rt_ld32_reg:
{ nop; ld32_reg r1, r2, r3 }
# DIS-LABEL: <rt_ld32_reg>:
# DIS: {{[ \t]}}ld32_reg{{[ \t,;]}}

# MNEM: s_sb_post_imm
rt_s_sb_post_imm:
{ nop; s_sb_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_sb_post_imm>:
# DIS: {{[ \t]}}s_sb_post_imm{{[ \t,;]}}

# MNEM: s_sb_post_reg
rt_s_sb_post_reg:
{ nop; s_sb_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sb_post_reg>:
# DIS: {{[ \t]}}s_sb_post_reg{{[ \t,;]}}

# MNEM: s_sb_pre_imm
rt_s_sb_pre_imm:
{ nop; s_sb_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_sb_pre_imm>:
# DIS: {{[ \t]}}s_sb_pre_imm{{[ \t,;]}}

# MNEM: s_sb_pre_reg
rt_s_sb_pre_reg:
{ nop; s_sb_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sb_pre_reg>:
# DIS: {{[ \t]}}s_sb_pre_reg{{[ \t,;]}}

# MNEM: st8
# LOGICAL: s_sb_with_imm
rt_st8:
{ nop; st8 r1, r2, 1 }
# DIS-LABEL: <rt_st8>:
# DIS: {{[ \t]}}st8{{[ \t,;]}}

# MNEM: s_sb_with_reg
rt_s_sb_with_reg:
{ nop; s_sb_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sb_with_reg>:
# DIS: {{[ \t]}}s_sb_with_reg{{[ \t,;]}}

# MNEM: s_shw_post_imm
rt_s_shw_post_imm:
{ nop; s_shw_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_shw_post_imm>:
# DIS: {{[ \t]}}s_shw_post_imm{{[ \t,;]}}

# MNEM: s_shw_post_reg
rt_s_shw_post_reg:
{ nop; s_shw_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_shw_post_reg>:
# DIS: {{[ \t]}}s_shw_post_reg{{[ \t,;]}}

# MNEM: s_shw_pre_imm
rt_s_shw_pre_imm:
{ nop; s_shw_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_shw_pre_imm>:
# DIS: {{[ \t]}}s_shw_pre_imm{{[ \t,;]}}

# MNEM: s_shw_pre_reg
rt_s_shw_pre_reg:
{ nop; s_shw_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_shw_pre_reg>:
# DIS: {{[ \t]}}s_shw_pre_reg{{[ \t,;]}}

# MNEM: st16
# LOGICAL: s_shw_with_imm
rt_st16:
{ nop; st16 r1, r2, 1 }
# DIS-LABEL: <rt_st16>:
# DIS: {{[ \t]}}st16{{[ \t,;]}}

# MNEM: s_shw_with_reg
rt_s_shw_with_reg:
{ nop; s_shw_with_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_shw_with_reg>:
# DIS: {{[ \t]}}s_shw_with_reg{{[ \t,;]}}

# MNEM: s_sw_brev_imm
rt_s_sw_brev_imm:
{ nop; s_sw_brev_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_sw_brev_imm>:
# DIS: {{[ \t]}}s_sw_brev_imm{{[ \t,;]}}

# MNEM: s_sw_brev_reg
rt_s_sw_brev_reg:
{ nop; s_sw_brev_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sw_brev_reg>:
# DIS: {{[ \t]}}s_sw_brev_reg{{[ \t,;]}}

# MNEM: s_sw_post_imm
rt_s_sw_post_imm:
{ nop; s_sw_post_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_sw_post_imm>:
# DIS: {{[ \t]}}s_sw_post_imm{{[ \t,;]}}

# MNEM: s_sw_post_reg
rt_s_sw_post_reg:
{ nop; s_sw_post_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sw_post_reg>:
# DIS: {{[ \t]}}s_sw_post_reg{{[ \t,;]}}

# MNEM: s_sw_pre_imm
rt_s_sw_pre_imm:
{ nop; s_sw_pre_imm r1, r2, 1 }
# DIS-LABEL: <rt_s_sw_pre_imm>:
# DIS: {{[ \t]}}s_sw_pre_imm{{[ \t,;]}}

# MNEM: s_sw_pre_reg
rt_s_sw_pre_reg:
{ nop; s_sw_pre_reg r1, r2, r3 }
# DIS-LABEL: <rt_s_sw_pre_reg>:
# DIS: {{[ \t]}}s_sw_pre_reg{{[ \t,;]}}

# MNEM: st32
# LOGICAL: s_sw_with_imm
rt_st32:
{ nop; st32 r1, r2, 1 }
# DIS-LABEL: <rt_st32>:
# DIS: {{[ \t]}}st32{{[ \t,;]}}

# MNEM: st32_reg
# LOGICAL: s_sw_with_reg
rt_st32_reg:
{ nop; st32_reg r1, r2, r3 }
# DIS-LABEL: <rt_st32_reg>:
# DIS: {{[ \t]}}st32_reg{{[ \t,;]}}

# MNEM: transf64
rt_transf64:
{ nop; transf64 d0, r1 }
# DIS-LABEL: <rt_transf64>:
# DIS: {{[ \t]}}transf64{{[ \t,;]}}

# MNEM: transf64f2
rt_transf64f2:
{ nop; transf64f2 d0, r1 }
# DIS-LABEL: <rt_transf64f2>:
# DIS: {{[ \t]}}transf64f2{{[ \t,;]}}

# MNEM: transf64_h
rt_transf64_h:
{ nop; transf64_h d0, d1 }
# DIS-LABEL: <rt_transf64_h>:
# DIS: {{[ \t]}}transf64_h{{[ \t,;]}}

# MNEM: transf64_l
rt_transf64_l:
{ nop; transf64_l d0, d1 }
# DIS-LABEL: <rt_transf64_l>:
# DIS: {{[ \t]}}transf64_l{{[ \t,;]}}

# MNEM: wbarwua
rt_wbarwua:
{ nop; wbarwua 0, r1, 0 }
# DIS-LABEL: <rt_wbarwua>:
# DIS: {{[ \t]}}wbarwua{{[ \t,;]}}

# MNEM: wfi
# UNENCODABLE: WFI<TBD> (wfi) — no invented encoding

# MNEM: x2abs32
rt_x2abs32:
{ nop; x2abs32 d0, d1 }
# DIS-LABEL: <rt_x2abs32>:
# DIS: {{[ \t]}}x2abs32{{[ \t,;]}}

# MNEM: x2abs32s
rt_x2abs32s:
{ nop; x2abs32s d0, d1 }
# DIS-LABEL: <rt_x2abs32s>:
# DIS: {{[ \t]}}x2abs32s{{[ \t,;]}}

# MNEM: x2add32
rt_x2add32:
{ nop; x2add32 d0, d1, d2 }
# DIS-LABEL: <rt_x2add32>:
# DIS: {{[ \t]}}x2add32{{[ \t,;]}}

# MNEM: x2add32s
rt_x2add32s:
{ nop; x2add32s d0, d1, d2 }
# DIS-LABEL: <rt_x2add32s>:
# DIS: {{[ \t]}}x2add32s{{[ \t,;]}}

# MNEM: x2add32s_hllh
rt_x2add32s_hllh:
{ nop; x2add32s_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2add32s_hllh>:
# DIS: {{[ \t]}}x2add32s_hllh{{[ \t,;]}}

# MNEM: x2add32_hllh
rt_x2add32_hllh:
{ nop; x2add32_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2add32_hllh>:
# DIS: {{[ \t]}}x2add32_hllh{{[ \t,;]}}

# MNEM: x2addsub32
rt_x2addsub32:
{ nop; x2addsub32 d0, d1, d2 }
# DIS-LABEL: <rt_x2addsub32>:
# DIS: {{[ \t]}}x2addsub32{{[ \t,;]}}

# MNEM: x2addsub32s
rt_x2addsub32s:
{ nop; x2addsub32s d0, d1, d2 }
# DIS-LABEL: <rt_x2addsub32s>:
# DIS: {{[ \t]}}x2addsub32s{{[ \t,;]}}

# MNEM: x2addsub32s_hllh
rt_x2addsub32s_hllh:
{ nop; x2addsub32s_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2addsub32s_hllh>:
# DIS: {{[ \t]}}x2addsub32s_hllh{{[ \t,;]}}

# MNEM: x2addsub32_hllh
rt_x2addsub32_hllh:
{ nop; x2addsub32_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2addsub32_hllh>:
# DIS: {{[ \t]}}x2addsub32_hllh{{[ \t,;]}}

# MNEM: x2clamp32
rt_x2clamp32:
{ nop; x2clamp32 d0, d1, d2 }
# DIS-LABEL: <rt_x2clamp32>:
# DIS: {{[ \t]}}x2clamp32{{[ \t,;]}}

# MNEM: x2cmul32
rt_x2cmul32:
{ nop; x2cmul32 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2cmul32>:
# DIS: {{[ \t]}}x2cmul32{{[ \t,;]}}

# MNEM: x2cmul32s
rt_x2cmul32s:
{ nop; x2cmul32s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2cmul32s>:
# DIS: {{[ \t]}}x2cmul32s{{[ \t,;]}}

# MNEM: x2cmul32s.f2
# LOGICAL: x2cmul32s_f2
rt_x2cmul32s_f2:
{ nop; x2cmul32s.f2 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2cmul32s_f2>:
# DIS: {{[ \t]}}x2cmul32s.f2{{[ \t,;]}}

# MNEM: x2cmul32x16.h
# LOGICAL: x2cmul32x16_h
rt_x2cmul32x16_h:
{ nop; x2cmul32x16.h d0, d1, d2 }
# DIS-LABEL: <rt_x2cmul32x16_h>:
# DIS: {{[ \t]}}x2cmul32x16.h{{[ \t,;]}}

# MNEM: x2cmul32x16.l
# LOGICAL: x2cmul32x16_l
rt_x2cmul32x16_l:
{ nop; x2cmul32x16.l d0, d1, d2 }
# DIS-LABEL: <rt_x2cmul32x16_l>:
# DIS: {{[ \t]}}x2cmul32x16.l{{[ \t,;]}}

# MNEM: x2cmul32.f2
# LOGICAL: x2cmul32_f2
rt_x2cmul32_f2:
{ nop; x2cmul32.f2 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2cmul32_f2>:
# DIS: {{[ \t]}}x2cmul32.f2{{[ \t,;]}}

# MNEM: x2cmula32x16.h
# LOGICAL: x2cmula32x16_h
rt_x2cmula32x16_h:
{ nop; x2cmula32x16.h d0, d1, d2 }
# DIS-LABEL: <rt_x2cmula32x16_h>:
# DIS: {{[ \t]}}x2cmula32x16.h{{[ \t,;]}}

# MNEM: x2cmula32x16.l
# LOGICAL: x2cmula32x16_l
rt_x2cmula32x16_l:
{ nop; x2cmula32x16.l d0, d1, d2 }
# DIS-LABEL: <rt_x2cmula32x16_l>:
# DIS: {{[ \t]}}x2cmula32x16.l{{[ \t,;]}}

# MNEM: x2dot32
rt_x2dot32:
{ nop; x2dot32 d0, d1, d2 }
# DIS-LABEL: <rt_x2dot32>:
# DIS: {{[ \t]}}x2dot32{{[ \t,;]}}

# MNEM: x2fcmul32rs
rt_x2fcmul32rs:
{ nop; x2fcmul32rs d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmul32rs>:
# DIS: {{[ \t]}}x2fcmul32rs{{[ \t,;]}}

# MNEM: x2fcmul32rss
rt_x2fcmul32rss:
{ nop; x2fcmul32rss d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmul32rss>:
# DIS: {{[ \t]}}x2fcmul32rss{{[ \t,;]}}

# MNEM: x2fcmul32x16rs.h
# LOGICAL: x2fcmul32x16rs_h
rt_x2fcmul32x16rs_h:
{ nop; x2fcmul32x16rs.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmul32x16rs_h>:
# DIS: {{[ \t]}}x2fcmul32x16rs.h{{[ \t,;]}}

# MNEM: x2fcmul32x16rs.l
# LOGICAL: x2fcmul32x16rs_l
rt_x2fcmul32x16rs_l:
{ nop; x2fcmul32x16rs.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmul32x16rs_l>:
# DIS: {{[ \t]}}x2fcmul32x16rs.l{{[ \t,;]}}

# MNEM: x2fcmula32rs
rt_x2fcmula32rs:
{ nop; x2fcmula32rs d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmula32rs>:
# DIS: {{[ \t]}}x2fcmula32rs{{[ \t,;]}}

# MNEM: x2fcmula32rss
rt_x2fcmula32rss:
{ nop; x2fcmula32rss d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmula32rss>:
# DIS: {{[ \t]}}x2fcmula32rss{{[ \t,;]}}

# MNEM: x2fcmula32x16rs.h
# LOGICAL: x2fcmula32x16rs_h
rt_x2fcmula32x16rs_h:
{ nop; x2fcmula32x16rs.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmula32x16rs_h>:
# DIS: {{[ \t]}}x2fcmula32x16rs.h{{[ \t,;]}}

# MNEM: x2fcmula32x16rs.l
# LOGICAL: x2fcmula32x16rs_l
rt_x2fcmula32x16rs_l:
{ nop; x2fcmula32x16rs.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fcmula32x16rs_l>:
# DIS: {{[ \t]}}x2fcmula32x16rs.l{{[ \t,;]}}

# MNEM: x2ff2rsst32
rt_x2ff2rsst32:
{ nop; x2ff2rsst32 d0, d1, d2 }
# DIS-LABEL: <rt_x2ff2rsst32>:
# DIS: {{[ \t]}}x2ff2rsst32{{[ \t,;]}}

# MNEM: x2ff2rst32
rt_x2ff2rst32:
{ nop; x2ff2rst32 d0, d1, d2 }
# DIS-LABEL: <rt_x2ff2rst32>:
# DIS: {{[ \t]}}x2ff2rst32{{[ \t,;]}}

# MNEM: x2fmul32rs
rt_x2fmul32rs:
{ nop; x2fmul32rs d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32rs>:
# DIS: {{[ \t]}}x2fmul32rs{{[ \t,;]}}

# MNEM: x2fmul32rss
rt_x2fmul32rss:
{ nop; x2fmul32rss d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32rss>:
# DIS: {{[ \t]}}x2fmul32rss{{[ \t,;]}}

# MNEM: x2fmul32ts
rt_x2fmul32ts:
{ nop; x2fmul32ts d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32ts>:
# DIS: {{[ \t]}}x2fmul32ts{{[ \t,;]}}

# MNEM: x2fmul32x16rss.h
# LOGICAL: x2fmul32x16rss_h
rt_x2fmul32x16rss_h:
{ nop; x2fmul32x16rss.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16rss_h>:
# DIS: {{[ \t]}}x2fmul32x16rss.h{{[ \t,;]}}

# MNEM: x2fmul32x16rss.l
# LOGICAL: x2fmul32x16rss_l
rt_x2fmul32x16rss_l:
{ nop; x2fmul32x16rss.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16rss_l>:
# DIS: {{[ \t]}}x2fmul32x16rss.l{{[ \t,;]}}

# MNEM: x2fmul32x16rs.h
# LOGICAL: x2fmul32x16rs_h
rt_x2fmul32x16rs_h:
{ nop; x2fmul32x16rs.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16rs_h>:
# DIS: {{[ \t]}}x2fmul32x16rs.h{{[ \t,;]}}

# MNEM: x2fmul32x16rs.l
# LOGICAL: x2fmul32x16rs_l
rt_x2fmul32x16rs_l:
{ nop; x2fmul32x16rs.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16rs_l>:
# DIS: {{[ \t]}}x2fmul32x16rs.l{{[ \t,;]}}

# MNEM: x2fmul32x16ts.h
# LOGICAL: x2fmul32x16ts_h
rt_x2fmul32x16ts_h:
{ nop; x2fmul32x16ts.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16ts_h>:
# DIS: {{[ \t]}}x2fmul32x16ts.h{{[ \t,;]}}

# MNEM: x2fmul32x16ts.l
# LOGICAL: x2fmul32x16ts_l
rt_x2fmul32x16ts_l:
{ nop; x2fmul32x16ts.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmul32x16ts_l>:
# DIS: {{[ \t]}}x2fmul32x16ts.l{{[ \t,;]}}

# MNEM: x2fmula32rs
rt_x2fmula32rs:
{ nop; x2fmula32rs d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32rs>:
# DIS: {{[ \t]}}x2fmula32rs{{[ \t,;]}}

# MNEM: x2fmula32rss
rt_x2fmula32rss:
{ nop; x2fmula32rss d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32rss>:
# DIS: {{[ \t]}}x2fmula32rss{{[ \t,;]}}

# MNEM: x2fmula32ts
rt_x2fmula32ts:
{ nop; x2fmula32ts d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32ts>:
# DIS: {{[ \t]}}x2fmula32ts{{[ \t,;]}}

# MNEM: x2fmula32x16rss.h
# LOGICAL: x2fmula32x16rss_h
rt_x2fmula32x16rss_h:
{ nop; x2fmula32x16rss.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16rss_h>:
# DIS: {{[ \t]}}x2fmula32x16rss.h{{[ \t,;]}}

# MNEM: x2fmula32x16rss.l
# LOGICAL: x2fmula32x16rss_l
rt_x2fmula32x16rss_l:
{ nop; x2fmula32x16rss.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16rss_l>:
# DIS: {{[ \t]}}x2fmula32x16rss.l{{[ \t,;]}}

# MNEM: x2fmula32x16rs.h
# LOGICAL: x2fmula32x16rs_h
rt_x2fmula32x16rs_h:
{ nop; x2fmula32x16rs.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16rs_h>:
# DIS: {{[ \t]}}x2fmula32x16rs.h{{[ \t,;]}}

# MNEM: x2fmula32x16rs.l
# LOGICAL: x2fmula32x16rs_l
rt_x2fmula32x16rs_l:
{ nop; x2fmula32x16rs.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16rs_l>:
# DIS: {{[ \t]}}x2fmula32x16rs.l{{[ \t,;]}}

# MNEM: x2fmula32x16ts.h
# LOGICAL: x2fmula32x16ts_h
rt_x2fmula32x16ts_h:
{ nop; x2fmula32x16ts.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16ts_h>:
# DIS: {{[ \t]}}x2fmula32x16ts.h{{[ \t,;]}}

# MNEM: x2fmula32x16ts.l
# LOGICAL: x2fmula32x16ts_l
rt_x2fmula32x16ts_l:
{ nop; x2fmula32x16ts.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmula32x16ts_l>:
# DIS: {{[ \t]}}x2fmula32x16ts.l{{[ \t,;]}}

# MNEM: x2fmuls32rs
rt_x2fmuls32rs:
{ nop; x2fmuls32rs d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32rs>:
# DIS: {{[ \t]}}x2fmuls32rs{{[ \t,;]}}

# MNEM: x2fmuls32rss
rt_x2fmuls32rss:
{ nop; x2fmuls32rss d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32rss>:
# DIS: {{[ \t]}}x2fmuls32rss{{[ \t,;]}}

# MNEM: x2fmuls32ts
rt_x2fmuls32ts:
{ nop; x2fmuls32ts d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32ts>:
# DIS: {{[ \t]}}x2fmuls32ts{{[ \t,;]}}

# MNEM: x2fmuls32x16rss.h
# LOGICAL: x2fmuls32x16rss_h
rt_x2fmuls32x16rss_h:
{ nop; x2fmuls32x16rss.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16rss_h>:
# DIS: {{[ \t]}}x2fmuls32x16rss.h{{[ \t,;]}}

# MNEM: x2fmuls32x16rss.l
# LOGICAL: x2fmuls32x16rss_l
rt_x2fmuls32x16rss_l:
{ nop; x2fmuls32x16rss.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16rss_l>:
# DIS: {{[ \t]}}x2fmuls32x16rss.l{{[ \t,;]}}

# MNEM: x2fmuls32x16rs.h
# LOGICAL: x2fmuls32x16rs_h
rt_x2fmuls32x16rs_h:
{ nop; x2fmuls32x16rs.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16rs_h>:
# DIS: {{[ \t]}}x2fmuls32x16rs.h{{[ \t,;]}}

# MNEM: x2fmuls32x16rs.l
# LOGICAL: x2fmuls32x16rs_l
rt_x2fmuls32x16rs_l:
{ nop; x2fmuls32x16rs.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16rs_l>:
# DIS: {{[ \t]}}x2fmuls32x16rs.l{{[ \t,;]}}

# MNEM: x2fmuls32x16ts.h
# LOGICAL: x2fmuls32x16ts_h
rt_x2fmuls32x16ts_h:
{ nop; x2fmuls32x16ts.h d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16ts_h>:
# DIS: {{[ \t]}}x2fmuls32x16ts.h{{[ \t,;]}}

# MNEM: x2fmuls32x16ts.l
# LOGICAL: x2fmuls32x16ts_l
rt_x2fmuls32x16ts_l:
{ nop; x2fmuls32x16ts.l d0, d1, d2 }
# DIS-LABEL: <rt_x2fmuls32x16ts_l>:
# DIS: {{[ \t]}}x2fmuls32x16ts.l{{[ \t,;]}}

# MNEM: x2frsst32
rt_x2frsst32:
{ nop; x2frsst32 d0, d1, d2 }
# DIS-LABEL: <rt_x2frsst32>:
# DIS: {{[ \t]}}x2frsst32{{[ \t,;]}}

# MNEM: x2frst32
rt_x2frst32:
{ nop; x2frst32 d0, d1, d2 }
# DIS-LABEL: <rt_x2frst32>:
# DIS: {{[ \t]}}x2frst32{{[ \t,;]}}

# MNEM: x2hadd32s_h
rt_x2hadd32s_h:
{ nop; x2hadd32s_h d0, d1 }
# DIS-LABEL: <rt_x2hadd32s_h>:
# DIS: {{[ \t]}}x2hadd32s_h{{[ \t,;]}}

# MNEM: x2hadd32s_l
rt_x2hadd32s_l:
{ nop; x2hadd32s_l d0, d1 }
# DIS-LABEL: <rt_x2hadd32s_l>:
# DIS: {{[ \t]}}x2hadd32s_l{{[ \t,;]}}

# MNEM: x2hadd32_h
rt_x2hadd32_h:
{ nop; x2hadd32_h d0, d1 }
# DIS-LABEL: <rt_x2hadd32_h>:
# DIS: {{[ \t]}}x2hadd32_h{{[ \t,;]}}

# MNEM: x2hadd32_l
rt_x2hadd32_l:
{ nop; x2hadd32_l d0, d1 }
# DIS-LABEL: <rt_x2hadd32_l>:
# DIS: {{[ \t]}}x2hadd32_l{{[ \t,;]}}

# MNEM: x2hmax32
rt_x2hmax32:
{ nop; x2hmax32 r1, d0 }
# DIS-LABEL: <rt_x2hmax32>:
# DIS: {{[ \t]}}x2hmax32{{[ \t,;]}}

# MNEM: x2hmin32
rt_x2hmin32:
{ nop; x2hmin32 r1, d0 }
# DIS-LABEL: <rt_x2hmin32>:
# DIS: {{[ \t]}}x2hmin32{{[ \t,;]}}

# MNEM: x2max32
rt_x2max32:
{ nop; x2max32 d0, d1, d2 }
# DIS-LABEL: <rt_x2max32>:
# DIS: {{[ \t]}}x2max32{{[ \t,;]}}

# MNEM: x2min32
rt_x2min32:
{ nop; x2min32 d0, d1, d2 }
# DIS-LABEL: <rt_x2min32>:
# DIS: {{[ \t]}}x2min32{{[ \t,;]}}

# MNEM: x2mjswap32
rt_x2mjswap32:
{ nop; x2mjswap32 d0, d1 }
# DIS-LABEL: <rt_x2mjswap32>:
# DIS: {{[ \t]}}x2mjswap32{{[ \t,;]}}

# MNEM: x2mjswap32s
rt_x2mjswap32s:
{ nop; x2mjswap32s d0, d1 }
# DIS-LABEL: <rt_x2mjswap32s>:
# DIS: {{[ \t]}}x2mjswap32s{{[ \t,;]}}

# MNEM: x2movf32
rt_x2movf32:
{ nop; x2movf32 d0, d1 }
# DIS-LABEL: <rt_x2movf32>:
# DIS: {{[ \t]}}x2movf32{{[ \t,;]}}

# MNEM: x2movt32
rt_x2movt32:
{ nop; x2movt32 d0, d1 }
# DIS-LABEL: <rt_x2movt32>:
# DIS: {{[ \t]}}x2movt32{{[ \t,;]}}

# MNEM: x2mul32
rt_x2mul32:
{ nop; x2mul32 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2mul32>:
# DIS: {{[ \t]}}x2mul32{{[ \t,;]}}

# MNEM: x2mul32x16.h
# LOGICAL: x2mul32x16_h
rt_x2mul32x16_h:
{ nop; x2mul32x16.h d0, d1, d2 }
# DIS-LABEL: <rt_x2mul32x16_h>:
# DIS: {{[ \t]}}x2mul32x16.h{{[ \t,;]}}

# MNEM: x2mul32x16.l
# LOGICAL: x2mul32x16_l
rt_x2mul32x16_l:
{ nop; x2mul32x16.l d0, d1, d2 }
# DIS-LABEL: <rt_x2mul32x16_l>:
# DIS: {{[ \t]}}x2mul32x16.l{{[ \t,;]}}

# MNEM: x2mula32
rt_x2mula32:
{ nop; x2mula32 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2mula32>:
# DIS: {{[ \t]}}x2mula32{{[ \t,;]}}

# MNEM: x2mula32x16.h
# LOGICAL: x2mula32x16_h
rt_x2mula32x16_h:
{ nop; x2mula32x16.h d0, d1, d2 }
# DIS-LABEL: <rt_x2mula32x16_h>:
# DIS: {{[ \t]}}x2mula32x16.h{{[ \t,;]}}

# MNEM: x2mula32x16.l
# LOGICAL: x2mula32x16_l
rt_x2mula32x16_l:
{ nop; x2mula32x16.l d0, d1, d2 }
# DIS-LABEL: <rt_x2mula32x16_l>:
# DIS: {{[ \t]}}x2mula32x16.l{{[ \t,;]}}

# MNEM: x2mulaph32
rt_x2mulaph32:
{ nop; x2mulaph32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulaph32>:
# DIS: {{[ \t]}}x2mulaph32{{[ \t,;]}}

# MNEM: x2mulapl32
rt_x2mulapl32:
{ nop; x2mulapl32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulapl32>:
# DIS: {{[ \t]}}x2mulapl32{{[ \t,;]}}

# MNEM: x2mulph32
rt_x2mulph32:
{ nop; x2mulph32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulph32>:
# DIS: {{[ \t]}}x2mulph32{{[ \t,;]}}

# MNEM: x2mulpl32
rt_x2mulpl32:
{ nop; x2mulpl32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulpl32>:
# DIS: {{[ \t]}}x2mulpl32{{[ \t,;]}}

# MNEM: x2muls32
rt_x2muls32:
{ nop; x2muls32 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x2muls32>:
# DIS: {{[ \t]}}x2muls32{{[ \t,;]}}

# MNEM: x2muls32x16.h
# LOGICAL: x2muls32x16_h
rt_x2muls32x16_h:
{ nop; x2muls32x16.h d0, d1, d2 }
# DIS-LABEL: <rt_x2muls32x16_h>:
# DIS: {{[ \t]}}x2muls32x16.h{{[ \t,;]}}

# MNEM: x2muls32x16.l
# LOGICAL: x2muls32x16_l
rt_x2muls32x16_l:
{ nop; x2muls32x16.l d0, d1, d2 }
# DIS-LABEL: <rt_x2muls32x16_l>:
# DIS: {{[ \t]}}x2muls32x16.l{{[ \t,;]}}

# MNEM: x2mulsph32
rt_x2mulsph32:
{ nop; x2mulsph32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulsph32>:
# DIS: {{[ \t]}}x2mulsph32{{[ \t,;]}}

# MNEM: x2mulspl32
rt_x2mulspl32:
{ nop; x2mulspl32 d0, d1, d2 }
# DIS-LABEL: <rt_x2mulspl32>:
# DIS: {{[ \t]}}x2mulspl32{{[ \t,;]}}

# MNEM: x2neg32
rt_x2neg32:
{ nop; x2neg32 d0, d1 }
# DIS-LABEL: <rt_x2neg32>:
# DIS: {{[ \t]}}x2neg32{{[ \t,;]}}

# MNEM: x2neg32s
rt_x2neg32s:
{ nop; x2neg32s d0, d1 }
# DIS-LABEL: <rt_x2neg32s>:
# DIS: {{[ \t]}}x2neg32s{{[ \t,;]}}

# MNEM: x2neg32s_l
rt_x2neg32s_l:
{ nop; x2neg32s_l d0, d1 }
# DIS-LABEL: <rt_x2neg32s_l>:
# DIS: {{[ \t]}}x2neg32s_l{{[ \t,;]}}

# MNEM: x2neg32_l
rt_x2neg32_l:
{ nop; x2neg32_l d0, d1 }
# DIS-LABEL: <rt_x2neg32_l>:
# DIS: {{[ \t]}}x2neg32_l{{[ \t,;]}}

# MNEM: x2sel32_hh
rt_x2sel32_hh:
{ nop; x2sel32_hh d0, d1, d2 }
# DIS-LABEL: <rt_x2sel32_hh>:
# DIS: {{[ \t]}}x2sel32_hh{{[ \t,;]}}

# MNEM: x2sel32_hl
rt_x2sel32_hl:
{ nop; x2sel32_hl d0, d1, d2 }
# DIS-LABEL: <rt_x2sel32_hl>:
# DIS: {{[ \t]}}x2sel32_hl{{[ \t,;]}}

# MNEM: x2sel32_lh
rt_x2sel32_lh:
{ nop; x2sel32_lh d0, d1, d2 }
# DIS-LABEL: <rt_x2sel32_lh>:
# DIS: {{[ \t]}}x2sel32_lh{{[ \t,;]}}

# MNEM: x2sel32_ll
rt_x2sel32_ll:
{ nop; x2sel32_ll d0, d1, d2 }
# DIS-LABEL: <rt_x2sel32_ll>:
# DIS: {{[ \t]}}x2sel32_ll{{[ \t,;]}}

# MNEM: x2seq32
rt_x2seq32:
{ nop; x2seq32 d0, d1 }
# DIS-LABEL: <rt_x2seq32>:
# DIS: {{[ \t]}}x2seq32{{[ \t,;]}}

# MNEM: x2sle32
rt_x2sle32:
{ nop; x2sle32 d0, d1 }
# DIS-LABEL: <rt_x2sle32>:
# DIS: {{[ \t]}}x2sle32{{[ \t,;]}}

# MNEM: x2sll32
rt_x2sll32:
{ nop; x2sll32 d0, d1, r1 }
# DIS-LABEL: <rt_x2sll32>:
# DIS: {{[ \t]}}x2sll32{{[ \t,;]}}

# MNEM: x2slli32
rt_x2slli32:
{ nop; x2slli32 d0, d0, 1 }
# DIS-LABEL: <rt_x2slli32>:
# DIS: {{[ \t]}}x2slli32{{[ \t,;]}}

# MNEM: x2slt32
rt_x2slt32:
{ nop; x2slt32 d0, d1 }
# DIS-LABEL: <rt_x2slt32>:
# DIS: {{[ \t]}}x2slt32{{[ \t,;]}}

# MNEM: x2sra32
rt_x2sra32:
{ nop; x2sra32 d0, d1, r1 }
# DIS-LABEL: <rt_x2sra32>:
# DIS: {{[ \t]}}x2sra32{{[ \t,;]}}

# MNEM: x2sra32r
rt_x2sra32r:
{ nop; x2sra32r d0, d1, r1 }
# DIS-LABEL: <rt_x2sra32r>:
# DIS: {{[ \t]}}x2sra32r{{[ \t,;]}}

# MNEM: x2srai32
rt_x2srai32:
{ nop; x2srai32 d0, d0, 1 }
# DIS-LABEL: <rt_x2srai32>:
# DIS: {{[ \t]}}x2srai32{{[ \t,;]}}

# MNEM: x2srai32r
rt_x2srai32r:
{ nop; x2srai32r d0, d0, 1 }
# DIS-LABEL: <rt_x2srai32r>:
# DIS: {{[ \t]}}x2srai32r{{[ \t,;]}}

# MNEM: x2srl32
rt_x2srl32:
{ nop; x2srl32 d0, d1, r1 }
# DIS-LABEL: <rt_x2srl32>:
# DIS: {{[ \t]}}x2srl32{{[ \t,;]}}

# MNEM: x2srli32
rt_x2srli32:
{ nop; x2srli32 d0, d0, 1 }
# DIS-LABEL: <rt_x2srli32>:
# DIS: {{[ \t]}}x2srli32{{[ \t,;]}}

# MNEM: x2sub32
rt_x2sub32:
{ nop; x2sub32 d0, d1, d2 }
# DIS-LABEL: <rt_x2sub32>:
# DIS: {{[ \t]}}x2sub32{{[ \t,;]}}

# MNEM: x2sub32s
rt_x2sub32s:
{ nop; x2sub32s d0, d1, d2 }
# DIS-LABEL: <rt_x2sub32s>:
# DIS: {{[ \t]}}x2sub32s{{[ \t,;]}}

# MNEM: x2sub32s_hllh
rt_x2sub32s_hllh:
{ nop; x2sub32s_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2sub32s_hllh>:
# DIS: {{[ \t]}}x2sub32s_hllh{{[ \t,;]}}

# MNEM: x2sub32_hllh
rt_x2sub32_hllh:
{ nop; x2sub32_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2sub32_hllh>:
# DIS: {{[ \t]}}x2sub32_hllh{{[ \t,;]}}

# MNEM: x2subadd32
rt_x2subadd32:
{ nop; x2subadd32 d0, d1, d2 }
# DIS-LABEL: <rt_x2subadd32>:
# DIS: {{[ \t]}}x2subadd32{{[ \t,;]}}

# MNEM: x2subadd32s
rt_x2subadd32s:
{ nop; x2subadd32s d0, d1, d2 }
# DIS-LABEL: <rt_x2subadd32s>:
# DIS: {{[ \t]}}x2subadd32s{{[ \t,;]}}

# MNEM: x2subadd32s_hllh
rt_x2subadd32s_hllh:
{ nop; x2subadd32s_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2subadd32s_hllh>:
# DIS: {{[ \t]}}x2subadd32s_hllh{{[ \t,;]}}

# MNEM: x2subadd32_hllh
rt_x2subadd32_hllh:
{ nop; x2subadd32_hllh d0, d1, d2 }
# DIS-LABEL: <rt_x2subadd32_hllh>:
# DIS: {{[ \t]}}x2subadd32_hllh{{[ \t,;]}}

# MNEM: x2swap32
rt_x2swap32:
{ nop; x2swap32 d0, d1 }
# DIS-LABEL: <rt_x2swap32>:
# DIS: {{[ \t]}}x2swap32{{[ \t,;]}}

# MNEM: x4abs16
rt_x4abs16:
{ nop; x4abs16 d0, d1 }
# DIS-LABEL: <rt_x4abs16>:
# DIS: {{[ \t]}}x4abs16{{[ \t,;]}}

# MNEM: x4abs16s
rt_x4abs16s:
{ nop; x4abs16s d0, d1 }
# DIS-LABEL: <rt_x4abs16s>:
# DIS: {{[ \t]}}x4abs16s{{[ \t,;]}}

# MNEM: x4add16
rt_x4add16:
{ nop; x4add16 d0, d1, d2 }
# DIS-LABEL: <rt_x4add16>:
# DIS: {{[ \t]}}x4add16{{[ \t,;]}}

# MNEM: x4add16s
rt_x4add16s:
{ nop; x4add16s d0, d1, d2 }
# DIS-LABEL: <rt_x4add16s>:
# DIS: {{[ \t]}}x4add16s{{[ \t,;]}}

# MNEM: x4cjmul16s.h
# LOGICAL: x4cjmul16s_h
rt_x4cjmul16s_h:
{ nop; x4cjmul16s.h d0, d1, d2 }
# DIS-LABEL: <rt_x4cjmul16s_h>:
# DIS: {{[ \t]}}x4cjmul16s.h{{[ \t,;]}}

# MNEM: x4cjmul16s.l
# LOGICAL: x4cjmul16s_l
rt_x4cjmul16s_l:
{ nop; x4cjmul16s.l d0, d1, d2 }
# DIS-LABEL: <rt_x4cjmul16s_l>:
# DIS: {{[ \t]}}x4cjmul16s.l{{[ \t,;]}}

# MNEM: x4cjmula16s.h
# LOGICAL: x4cjmula16s_h
rt_x4cjmula16s_h:
{ nop; x4cjmula16s.h d0, d1, d2 }
# DIS-LABEL: <rt_x4cjmula16s_h>:
# DIS: {{[ \t]}}x4cjmula16s.h{{[ \t,;]}}

# MNEM: x4cjmula16s.l
# LOGICAL: x4cjmula16s_l
rt_x4cjmula16s_l:
{ nop; x4cjmula16s.l d0, d1, d2 }
# DIS-LABEL: <rt_x4cjmula16s_l>:
# DIS: {{[ \t]}}x4cjmula16s.l{{[ \t,;]}}

# MNEM: x4clamp16
rt_x4clamp16:
{ nop; x4clamp16 d0, d1, d2 }
# DIS-LABEL: <rt_x4clamp16>:
# DIS: {{[ \t]}}x4clamp16{{[ \t,;]}}

# MNEM: x4cmul16
rt_x4cmul16:
{ nop; x4cmul16 d0, d1 }
# DIS-LABEL: <rt_x4cmul16>:
# DIS: {{[ \t]}}x4cmul16{{[ \t,;]}}

# MNEM: x4cmul16s
rt_x4cmul16s:
{ nop; x4cmul16s d0, d1 }
# DIS-LABEL: <rt_x4cmul16s>:
# DIS: {{[ \t]}}x4cmul16s{{[ \t,;]}}

# MNEM: x4cmul16s.f2
# LOGICAL: x4cmul16s_f2
rt_x4cmul16s_f2:
{ nop; x4cmul16s.f2 d0, d1 }
# DIS-LABEL: <rt_x4cmul16s_f2>:
# DIS: {{[ \t]}}x4cmul16s.f2{{[ \t,;]}}

# MNEM: x4cmul16s.h
# LOGICAL: x4cmul16s_h
rt_x4cmul16s_h:
{ nop; x4cmul16s.h d0, d1, d2 }
# DIS-LABEL: <rt_x4cmul16s_h>:
# DIS: {{[ \t]}}x4cmul16s.h{{[ \t,;]}}

# MNEM: x4cmul16s.l
# LOGICAL: x4cmul16s_l
rt_x4cmul16s_l:
{ nop; x4cmul16s.l d0, d1, d2 }
# DIS-LABEL: <rt_x4cmul16s_l>:
# DIS: {{[ \t]}}x4cmul16s.l{{[ \t,;]}}

# MNEM: x4cmul16.f2
# LOGICAL: x4cmul16_f2
rt_x4cmul16_f2:
{ nop; x4cmul16.f2 d0, d1 }
# DIS-LABEL: <rt_x4cmul16_f2>:
# DIS: {{[ \t]}}x4cmul16.f2{{[ \t,;]}}

# MNEM: x4cmula16s.h
# LOGICAL: x4cmula16s_h
rt_x4cmula16s_h:
{ nop; x4cmula16s.h d0, d1, d2 }
# DIS-LABEL: <rt_x4cmula16s_h>:
# DIS: {{[ \t]}}x4cmula16s.h{{[ \t,;]}}

# MNEM: x4cmula16s.l
# LOGICAL: x4cmula16s_l
rt_x4cmula16s_l:
{ nop; x4cmula16s.l d0, d1, d2 }
# DIS-LABEL: <rt_x4cmula16s_l>:
# DIS: {{[ \t]}}x4cmula16s.l{{[ \t,;]}}

# MNEM: x4conj16
rt_x4conj16:
{ nop; x4conj16 d0, d1 }
# DIS-LABEL: <rt_x4conj16>:
# DIS: {{[ \t]}}x4conj16{{[ \t,;]}}

# MNEM: x4conj16s
rt_x4conj16s:
{ nop; x4conj16s d0, d1 }
# DIS-LABEL: <rt_x4conj16s>:
# DIS: {{[ \t]}}x4conj16s{{[ \t,;]}}

# MNEM: x4dot16
rt_x4dot16:
{ nop; x4dot16 d0, d1, d2 }
# DIS-LABEL: <rt_x4dot16>:
# DIS: {{[ \t]}}x4dot16{{[ \t,;]}}

# MNEM: x4energy16
rt_x4energy16:
{ nop; x4energy16 d0, d1 }
# DIS-LABEL: <rt_x4energy16>:
# DIS: {{[ \t]}}x4energy16{{[ \t,;]}}

# MNEM: x4fcmul16rs
rt_x4fcmul16rs:
{ nop; x4fcmul16rs d0, d1, d2 }
# DIS-LABEL: <rt_x4fcmul16rs>:
# DIS: {{[ \t]}}x4fcmul16rs{{[ \t,;]}}

# MNEM: x4fcmul16rss
rt_x4fcmul16rss:
{ nop; x4fcmul16rss d0, d1, d2 }
# DIS-LABEL: <rt_x4fcmul16rss>:
# DIS: {{[ \t]}}x4fcmul16rss{{[ \t,;]}}

# MNEM: x4fcmula16rs
rt_x4fcmula16rs:
{ nop; x4fcmula16rs d0, d1, d2 }
# DIS-LABEL: <rt_x4fcmula16rs>:
# DIS: {{[ \t]}}x4fcmula16rs{{[ \t,;]}}

# MNEM: x4fcmula16rss
rt_x4fcmula16rss:
{ nop; x4fcmula16rss d0, d1, d2 }
# DIS-LABEL: <rt_x4fcmula16rss>:
# DIS: {{[ \t]}}x4fcmula16rss{{[ \t,;]}}

# MNEM: x4ff2mul16s
rt_x4ff2mul16s:
{ nop; x4ff2mul16s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4ff2mul16s>:
# DIS: {{[ \t]}}x4ff2mul16s{{[ \t,;]}}

# MNEM: x4ff2mula16s
rt_x4ff2mula16s:
{ nop; x4ff2mula16s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4ff2mula16s>:
# DIS: {{[ \t]}}x4ff2mula16s{{[ \t,;]}}

# MNEM: x4ff2muls16s
rt_x4ff2muls16s:
{ nop; x4ff2muls16s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4ff2muls16s>:
# DIS: {{[ \t]}}x4ff2muls16s{{[ \t,;]}}

# MNEM: x4fmul16rs
rt_x4fmul16rs:
{ nop; x4fmul16rs d0, d1, d2 }
# DIS-LABEL: <rt_x4fmul16rs>:
# DIS: {{[ \t]}}x4fmul16rs{{[ \t,;]}}

# MNEM: x4fmul16rss
rt_x4fmul16rss:
{ nop; x4fmul16rss d0, d1, d2 }
# DIS-LABEL: <rt_x4fmul16rss>:
# DIS: {{[ \t]}}x4fmul16rss{{[ \t,;]}}

# MNEM: x4fmul16ts
rt_x4fmul16ts:
{ nop; x4fmul16ts d0, d1, d2 }
# DIS-LABEL: <rt_x4fmul16ts>:
# DIS: {{[ \t]}}x4fmul16ts{{[ \t,;]}}

# MNEM: x4frsst16
rt_x4frsst16:
{ nop; x4frsst16 d0, d1, d2 }
# DIS-LABEL: <rt_x4frsst16>:
# DIS: {{[ \t]}}x4frsst16{{[ \t,;]}}

# MNEM: x4frst16
rt_x4frst16:
{ nop; x4frst16 d0, d1, d2 }
# DIS-LABEL: <rt_x4frst16>:
# DIS: {{[ \t]}}x4frst16{{[ \t,;]}}

# MNEM: x4hadd16_h
rt_x4hadd16_h:
{ nop; x4hadd16_h d0, d1 }
# DIS-LABEL: <rt_x4hadd16_h>:
# DIS: {{[ \t]}}x4hadd16_h{{[ \t,;]}}

# MNEM: x4hadd16_l
rt_x4hadd16_l:
{ nop; x4hadd16_l d0, d1 }
# DIS-LABEL: <rt_x4hadd16_l>:
# DIS: {{[ \t]}}x4hadd16_l{{[ \t,;]}}

# MNEM: x4hmax16
rt_x4hmax16:
{ nop; x4hmax16 r1, d0 }
# DIS-LABEL: <rt_x4hmax16>:
# DIS: {{[ \t]}}x4hmax16{{[ \t,;]}}

# MNEM: x4hmin16
rt_x4hmin16:
{ nop; x4hmin16 r1, d0 }
# DIS-LABEL: <rt_x4hmin16>:
# DIS: {{[ \t]}}x4hmin16{{[ \t,;]}}

# MNEM: x4max16
rt_x4max16:
{ nop; x4max16 d0, d1, d2 }
# DIS-LABEL: <rt_x4max16>:
# DIS: {{[ \t]}}x4max16{{[ \t,;]}}

# MNEM: x4min16
rt_x4min16:
{ nop; x4min16 d0, d1, d2 }
# DIS-LABEL: <rt_x4min16>:
# DIS: {{[ \t]}}x4min16{{[ \t,;]}}

# MNEM: x4mjswap16
rt_x4mjswap16:
{ nop; x4mjswap16 d0, d1 }
# DIS-LABEL: <rt_x4mjswap16>:
# DIS: {{[ \t]}}x4mjswap16{{[ \t,;]}}

# MNEM: x4mjswap16s
rt_x4mjswap16s:
{ nop; x4mjswap16s d0, d1 }
# DIS-LABEL: <rt_x4mjswap16s>:
# DIS: {{[ \t]}}x4mjswap16s{{[ \t,;]}}

# MNEM: x4movf16
rt_x4movf16:
{ nop; x4movf16 d0, d1 }
# DIS-LABEL: <rt_x4movf16>:
# DIS: {{[ \t]}}x4movf16{{[ \t,;]}}

# MNEM: x4movt16
rt_x4movt16:
{ nop; x4movt16 d0, d1 }
# DIS-LABEL: <rt_x4movt16>:
# DIS: {{[ \t]}}x4movt16{{[ \t,;]}}

# MNEM: x4mul16
rt_x4mul16:
{ nop; x4mul16 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4mul16>:
# DIS: {{[ \t]}}x4mul16{{[ \t,;]}}

# MNEM: x4mula16
rt_x4mula16:
{ nop; x4mula16 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4mula16>:
# DIS: {{[ \t]}}x4mula16{{[ \t,;]}}

# MNEM: x4mula16s
rt_x4mula16s:
{ nop; x4mula16s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4mula16s>:
# DIS: {{[ \t]}}x4mula16s{{[ \t,;]}}

# MNEM: x4muls16
rt_x4muls16:
{ nop; x4muls16 d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4muls16>:
# DIS: {{[ \t]}}x4muls16{{[ \t,;]}}

# MNEM: x4muls16s
rt_x4muls16s:
{ nop; x4muls16s d0, d1, d2, d3 }
# DIS-LABEL: <rt_x4muls16s>:
# DIS: {{[ \t]}}x4muls16s{{[ \t,;]}}

# MNEM: x4neg16
rt_x4neg16:
{ nop; x4neg16 d0, d1 }
# DIS-LABEL: <rt_x4neg16>:
# DIS: {{[ \t]}}x4neg16{{[ \t,;]}}

# MNEM: x4neg16s
rt_x4neg16s:
{ nop; x4neg16s d0, d1 }
# DIS-LABEL: <rt_x4neg16s>:
# DIS: {{[ \t]}}x4neg16s{{[ \t,;]}}

# MNEM: x4sat32t16
rt_x4sat32t16:
{ nop; x4sat32t16 d0, d1, d2 }
# DIS-LABEL: <rt_x4sat32t16>:
# DIS: {{[ \t]}}x4sat32t16{{[ \t,;]}}

# MNEM: x4sel16
rt_x4sel16:
{ nop; x4sel16 d0, d1, d2, r1 }
# DIS-LABEL: <rt_x4sel16>:
# DIS: {{[ \t]}}x4sel16{{[ \t,;]}}

# MNEM: x4seli16
rt_x4seli16:
{ nop; x4seli16 d0, d1, d2, 1 }
# DIS-LABEL: <rt_x4seli16>:
# DIS: {{[ \t]}}x4seli16{{[ \t,;]}}

# MNEM: x4seq16
rt_x4seq16:
{ nop; x4seq16 d0, d1 }
# DIS-LABEL: <rt_x4seq16>:
# DIS: {{[ \t]}}x4seq16{{[ \t,;]}}

# MNEM: x4sle16
rt_x4sle16:
{ nop; x4sle16 d0, d1 }
# DIS-LABEL: <rt_x4sle16>:
# DIS: {{[ \t]}}x4sle16{{[ \t,;]}}

# MNEM: x4sll16
rt_x4sll16:
{ nop; x4sll16 d0, d1, r1 }
# DIS-LABEL: <rt_x4sll16>:
# DIS: {{[ \t]}}x4sll16{{[ \t,;]}}

# MNEM: x4slli16
rt_x4slli16:
{ nop; x4slli16 d0, d0, 1 }
# DIS-LABEL: <rt_x4slli16>:
# DIS: {{[ \t]}}x4slli16{{[ \t,;]}}

# MNEM: x4slt16
rt_x4slt16:
{ nop; x4slt16 d0, d1 }
# DIS-LABEL: <rt_x4slt16>:
# DIS: {{[ \t]}}x4slt16{{[ \t,;]}}

# MNEM: x4sra16
rt_x4sra16:
{ nop; x4sra16 d0, d1, r1 }
# DIS-LABEL: <rt_x4sra16>:
# DIS: {{[ \t]}}x4sra16{{[ \t,;]}}

# MNEM: x4sra16r
rt_x4sra16r:
{ nop; x4sra16r d0, d1, r1 }
# DIS-LABEL: <rt_x4sra16r>:
# DIS: {{[ \t]}}x4sra16r{{[ \t,;]}}

# MNEM: x4srai16
rt_x4srai16:
{ nop; x4srai16 d0, d0, 1 }
# DIS-LABEL: <rt_x4srai16>:
# DIS: {{[ \t]}}x4srai16{{[ \t,;]}}

# MNEM: x4srai16r
rt_x4srai16r:
{ nop; x4srai16r d0, d0, 1 }
# DIS-LABEL: <rt_x4srai16r>:
# DIS: {{[ \t]}}x4srai16r{{[ \t,;]}}

# MNEM: x4srl16
rt_x4srl16:
{ nop; x4srl16 d0, d1, r1 }
# DIS-LABEL: <rt_x4srl16>:
# DIS: {{[ \t]}}x4srl16{{[ \t,;]}}

# MNEM: x4srli16
rt_x4srli16:
{ nop; x4srli16 d0, d0, 1 }
# DIS-LABEL: <rt_x4srli16>:
# DIS: {{[ \t]}}x4srli16{{[ \t,;]}}

# MNEM: x4sub16
rt_x4sub16:
{ nop; x4sub16 d0, d1, d2 }
# DIS-LABEL: <rt_x4sub16>:
# DIS: {{[ \t]}}x4sub16{{[ \t,;]}}

# MNEM: x4sub16s
rt_x4sub16s:
{ nop; x4sub16s d0, d1, d2 }
# DIS-LABEL: <rt_x4sub16s>:
# DIS: {{[ \t]}}x4sub16s{{[ \t,;]}}

# MNEM: x4swap16
rt_x4swap16:
{ nop; x4swap16 d0, d1 }
# DIS-LABEL: <rt_x4swap16>:
# DIS: {{[ \t]}}x4swap16{{[ \t,;]}}

# MNEM: xor32
rt_xor32:
{ nop; xor32 r1, r2, r3 }
# DIS-LABEL: <rt_xor32>:
# DIS: {{[ \t]}}xor32{{[ \t,;]}}

# MNEM: xor64
rt_xor64:
{ nop; xor64 d0, d1, d2 }
# DIS-LABEL: <rt_xor64>:
# DIS: {{[ \t]}}xor64{{[ \t,;]}}

# MNEM: xori32
rt_xori32:
{ nop; xori32 r1, r2, 1 }
# DIS-LABEL: <rt_xori32>:
# DIS: {{[ \t]}}xori32{{[ \t,;]}}

# MNEM: zero_dr
rt_zero_dr:
{ nop; zero_dr d0 }
# DIS-LABEL: <rt_zero_dr>:
# DIS: {{[ \t]}}zero_dr{{[ \t,;]}}

# MNEM: zero_gpr
rt_zero_gpr:
{ nop; zero_gpr r1 }
# DIS-LABEL: <rt_zero_gpr>:
# DIS: {{[ \t]}}zero_gpr{{[ \t,;]}}

# MNEM: zero_sfr
rt_zero_sfr:
{ nop; zero_sfr }
# DIS-LABEL: <rt_zero_sfr>:
# DIS: {{[ \t]}}zero_sfr{{[ \t,;]}}

# UNENCODABLE product logicals (real matcher/placement gap):
#   WFI<TBD> -> wfi

