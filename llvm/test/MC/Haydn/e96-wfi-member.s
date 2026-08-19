# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=SUFFIX=1 -o /dev/null \
# RUN:   2>&1 | FileCheck %s --check-prefix=SUFFIX
# REQUIRES: haydn-registered-target

# WFI product encode is the generated HINT member (empty dag). The
# FieldSlot peer is not a matcher result.

.ifndef SUFFIX
# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: {{.*}}wfi
# CHECK-NOT: <unknown>
.text
  { wfi }
  { wfi; nop; nop }
.endif

.ifdef SUFFIX
# SUFFIX: error: assembler matched a private placement opcode
wfi_s0
.endif
