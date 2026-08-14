# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o \
# RUN:     | FileCheck %s --implicit-check-not='<unknown>'
# REQUIRES: haydn-registered-target

# REGRESSION TEST : `{ s_sw_with_imm lr, sp, 3 }` and the LD32/ST32/LD64/ST64
# `_S0_FLEX` family MUST parse inside a bundle.
#
# Bug: the LS `_S0_FLEX` defs (S_LW_{{[A-Z_]*}}/S_SW_{{[A-Z_]*}}/D_LDW_{{[A-Z_]*}}/ST64_S0_FLEX, codepoints
# 110-113) and the reg-offset siblings (codepoints 114-117) carried
# `isCodeGenOnly = 1`. That bit excludes an instruction from BOTH the decoder
# trie AND the asm matcher
# (llvm/utils/TableGen/AsmMatcherEmitter.cpp:1587 and
# llvm/utils/TableGen/DecoderEmitter.cpp:1378 both gate on it). As a result
# `s_sw_with_imm` was absent from HaydnGenAsmMatcher.inc while the DR64 ALU64
# `_S{0,1,2}_FLEX` siblings (no isCodeGenOnly) matched fine, so:
# `{ s_sw_with_imm lr, sp, 3 }` -> "failed to match instruction in bundle"
# `{ s_sw_with_imm lr, sp, 3 }` -> OK (legacy ST32 mnemonic, never isCodeGenOnly)
#
# Fix : replace `isCodeGenOnly = 1` with `DecoderNamespace =
# "FlexEmitterOnly"` on the 8 affected defs. The matcher ignores
# DecoderNamespace, so the mnemonics re-enter the matcher; the decoder emitter
# groups by namespace, so these stay out of every live decode trie (preserving
# the / decoder-exclusion intent via the correct mechanism).
#
# Test design: assemble each slot-suffixed LS mnemonic inside a bundle and
# round-trip through objdump. If the matcher entry regresses, llvm-mc aborts
# at assemble time with "failed to match instruction in bundle". The legacy
# positional forms (`{ s_sw_with_imm... }` etc.) are checked alongside to catch any
# accidental breakage of the non-suffixed path.

# CHECK-LABEL: <f_st32_s0>:
# CHECK: { {{.*}}s_sw_with_imm lr, sp, 3
f_st32_s0:
  { s_sw_with_imm lr, sp, 3 }

# CHECK-LABEL: <f_st32_legacy>:
# CHECK: { {{.*}}s_sw_with_imm lr, sp, 3
f_st32_legacy:
  { s_sw_with_imm lr, sp, 3 }

# CHECK-LABEL: <f_ld32_s0>:
# CHECK: { {{.*}}s_lw_with_imm r1, sp, 8
f_ld32_s0:
  { s_lw_with_imm r1, sp, 8 }

# CHECK-LABEL: <f_ld32_legacy>:
# CHECK: { {{.*}}s_lw_with_imm r1, sp, 8
f_ld32_legacy:
  { s_lw_with_imm r1, sp, 8 }

# CHECK-LABEL: <f_ld64_s0>:
# CHECK: { {{.*}}d_ldw_with_imm d0, sp, 2
f_ld64_s0:
  { d_ldw_with_imm d0, sp, 2 }

# CHECK-LABEL: <f_st64_s0>:
# CHECK: { {{.*}}d_sdw_with_imm d0, sp, 2
f_st64_s0:
  { d_sdw_with_imm d0, sp, 2 }
