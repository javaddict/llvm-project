# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — fused post-inc store public mnemonics assemble and disassemble.

# Public asm aliases st32_post / st64_post (FieldSlot / assembler spelling)
# must encode as Format E parcels and round-trip through objdump to the
# architectural post-inc store forms. Assemble success alone is not the contract.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 87 83 3b 11 00 00 00 00 00 00 00 00 { nop; s_sw_post_imm r3, r1, 1 }
# CHECK: c: 87 83 0a 11 00 00 00 00 00 00 00 00 { nop; d_sdw_post_imm d0, r1, 1 }
# CHECK-NOT: <?>
# CHECK-NOT: <unknown>

.text
  { st32_post r3, r1, 1 }
  { st64_post d0, r1, 1 }
