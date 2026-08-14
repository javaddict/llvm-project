# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — `{ st32 lr, sp, 12 }` and the LD32/ST32/LD64/ST64 `_S0_FLEX` family MUST parse inside a bundle.

# REGRESSION TEST : `{ st32 lr, sp, 12 }` and the LD32/ST32/LD64/ST64
# `_S0_FLEX` family MUST parse inside a bundle.
#
# Bug: the LS `_S0_FLEX` defs (LD32/ST32/LD64/ST64_S0_FLEX, codepoints
# 110-113) and the reg-offset siblings (codepoints 114-117) carried
# `isCodeGenOnly = 1`. That bit excludes an instruction from BOTH the decoder
# trie AND the asm matcher
# (llvm/utils/TableGen/AsmMatcherEmitter.cpp:1587 and
# llvm/utils/TableGen/DecoderEmitter.cpp:1378 both gate on it). As a result
# `st32` was absent from HaydnGenAsmMatcher.inc while the DR64 ALU64
# `_S{0,1,2}_FLEX` siblings (no isCodeGenOnly) matched fine, so:
# `{ st32 lr, sp, 12 }` -> "failed to match instruction in bundle"
# `{ st32 lr, sp, 12 }` -> OK (legacy ST32 mnemonic, never isCodeGenOnly)
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
# positional forms (`{ st32... }` etc.) are checked alongside to catch any
# accidental breakage of the non-suffixed path.

# CHECK-LABEL: <f_st32_s0>:
# CHECK: {{.*}}0: 87 43 fb cd 00 00 00 00 00 00 00 00 { nop; st32 lr, sp, 12 }

f_st32_s0:
  { st32 lr, sp, 12 }

# CHECK-LABEL: <f_st32_legacy>:
# CHECK: c: 87 43 fb cd 00 00 00 00 00 00 00 00 { nop; st32 lr, sp, 12 }
f_st32_legacy:
  { st32 lr, sp, 12 }

# CHECK-LABEL: <f_ld32_s0>:
# CHECK: {{.*}}18: 87 43 13 8d 00 00 00 00 00 00 00 00 { nop; ld32 r1, sp, 8 }
f_ld32_s0:
  { ld32 r1, sp, 8 }

# CHECK-LABEL: <f_ld32_legacy>:
# CHECK: {{.*}}24: 87 43 13 8d 00 00 00 00 00 00 00 00 { nop; ld32 r1, sp, 8 }
f_ld32_legacy:
  { ld32 r1, sp, 8 }

# CHECK-LABEL: <f_ld64_s0>:
# CHECK: {{.*}}30: 87 43 02 0d 01 00 00 00 00 00 00 00 { nop; ld64 d0, sp, 16 }
f_ld64_s0:
  { ld64 d0, sp, 16 }

# CHECK-LABEL: <f_st64_s0>:
# CHECK: 3c: 87 43 0a 0d 01 00 00 00 00 00 00 00 { nop; st64 d0, sp, 16 }
f_st64_s0:
  { st64 d0, sp, 16 }
