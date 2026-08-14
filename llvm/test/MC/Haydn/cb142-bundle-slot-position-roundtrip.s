# RUN: llvm-mc -triple=haydn-unknown-elf -mcpu=haydn -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --mcpu=haydn --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST (CB-142): a bundle's TEXTUAL POSITION names its slot, so the
# AsmPrinter's layout re-assembles to the bytes a direct `clang -c` produced.
# Before this, `clang -S x.c && clang -c x.s` did not reproduce `clang -c x.c`:
# it failed outright at -O2/-O3/-Os on dhry_1.c, and silently re-slotted 112
# bundles at -O1.
#
# Bundle text is written HIGH SLOT FIRST and right-aligned on s0, per the ISA
# (VLIW_Engine_Compiler_Constraints.md "Bundle format: G:{`slot2`, `slot1`,
# `slot0`}"). For N entries, entry i names slot N-1-i:
#   `{ a; b; c }` = s2,s1,s0     `{ a; b }` = s1,s0     `{ a }` = s0
#
# Bug: the `_S0/_S1/_S2` members of a multi-slot logical all share one
# AsmString, so MatchInstructionImpl always resolved a bundle mnemonic to the
# FIRST member, and that member's fixed getSlotKind pinned it to slot 0 no
# matter where it was written. Fix: the parser passes the textual position as
# the Bundle::add slot hint and de-materializes the matched member to its
# logical base, so encodeSlotSubInst materializes Alts[SlotIdx] for the slot
# the text names. An illegal hint still falls back to the solver (cb54/cb88).

.text

#===----------------------------------------------------------------------===#
# The exact dhry_1.c -O2 bundle that used to abort with "incorrect bundle":
# The immediate-ALU op here used to be `addi32`, and it cannot be: ADDI32's
# imm20 fits only the wide 2-entry windows, so it has NO 3-entry placement and
# a bundle asking for it in a 3-entry shape is unbuildable. SLLI32 takes a
# uimm5 and does reach all three entries — the narrow-immediate ops are the
# ones that fit the 3-entry form. In ISA order the store sits in the high entry
# and the shift in the low one.
#===----------------------------------------------------------------------===#
# CHECK-LABEL: <f_cb142_dhry_bundle>:
# The store lands in entry 0 because LOADSTORE0 serves only that entry, so
# it prints last however the source ordered the bundle.
# CHECK: { slli32 r4, r0, 6; nop; s_sb_post_imm r4, r1, 1 }
f_cb142_dhry_bundle:
  { s_sb_post_imm r4, r1, 1; nop; slli32 r4, r0, 6 }

#===----------------------------------------------------------------------===#
# `nop` fillers are slot placeholders, not padding: the same store must land in
# a different slot in each of these three bundles, and must not migrate.
#===----------------------------------------------------------------------===#
# CHECK-LABEL: <f_cb142_store_in_s2>:
# CHECK: { nop; nop; s_sb_post_imm r4, r1, 1 }
f_cb142_store_in_s2:
  { s_sb_post_imm r4, r1, 1; nop; nop }

# CHECK-LABEL: <f_cb142_store_in_s1>:
# CHECK: { nop; nop; s_sb_post_imm r4, r1, 1 }
f_cb142_store_in_s1:
  { nop; s_sb_post_imm r4, r1, 1; nop }

# CHECK-LABEL: <f_cb142_store_in_s0>:
# CHECK: { nop; nop; s_sb_post_imm r4, r1, 1 }
f_cb142_store_in_s0:
  { nop; nop; s_sb_post_imm r4, r1, 1 }

#===----------------------------------------------------------------------===#
# Right-aligned short forms: the last entry is always s0.
#===----------------------------------------------------------------------===#
# CHECK-LABEL: <f_cb142_right_aligned>:
# CHECK: { lui r1, 1; nop; nop }
# CHECK: { nop; lui r1, 1 }
# CHECK: { nop; nop; lui r1, 1 }
f_cb142_right_aligned:
  { lui r1, 1 }
  { nop; lui r1, 1 }
  { nop; nop; lui r1, 1 }

#===----------------------------------------------------------------------===#
# `s_sw_post_imm` / `d_sdw_post_imm` are the legacy codegen names the AsmPrinter emits
# for the fused post-increment stores. Their `_S1` members carried
# isCodeGenOnly, which drops a mnemonic from the asm matcher as well as the
# decoder trie (same bug class as d463), so `clang -S` printed text that no
# assembler could read back: "failed to match instruction in bundle". They
# share a codepoint with S_SW_POST_IMM / D_SDW_POST_IMM, which stay the
# canonical decoder entries, so the alias must ASSEMBLE and come back under the
# canonical name.
#===----------------------------------------------------------------------===#
# CHECK-LABEL: <f_cb142_legacy_post_names>:
# CHECK: { nop; nop; s_sw_post_imm r6, r7, 1 }
# CHECK: { nop; nop; d_sdw_post_imm d0, r7, 1 }
f_cb142_legacy_post_names:
  { nop; s_sw_post_imm r6, r7, 1; nop }
  { nop; d_sdw_post_imm d0, r7, 1; nop }

#===----------------------------------------------------------------------===#
# A fully packed bundle must come back verbatim — this is the property the
# `-S` round trip depends on. move32 names s2, addi32 s1, st32 s0.
#===----------------------------------------------------------------------===#
# CHECK-LABEL: <f_cb142_full_layout>:
# CHECK: { move32 r6, r3; slli32 r4, r3, 4; s_sw_{{[a-z_]*}} r3, r1, 0 }
f_cb142_full_layout:
  { move32 r6, r3; slli32 r4, r3, 4; s_sw_with_imm r3, r1, 0 }

# CHECK-NOT: <?>
# CHECK-NOT: <unknown>
