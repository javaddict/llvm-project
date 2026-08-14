# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=OBJ
# RUN: ld.lld -m elf32haydn -e caller %t.o -o %t.elf
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.elf | \
# RUN:   FileCheck %s --check-prefix=ELF
#
# Role: object — T-MC9 / M11: llvm-objdump -d symbolizes a PC-relative call
# via HaydnMCInstrAnalysis::evaluateBranch (walks Format E BUNDLE_E96_*
# isInst children; last imm is a dump-byte displacement).
#
# Evidence: linked disasm resolves the call to `<callee>`. Relocatable .o
# keeps the unpatched 0 field and must NOT pretend the target is `<caller>`.
#
# Residual: cond-branch members still decode as architectural NOP (T-MC4 /
# F13 inverse + GE96-03 scale). Mapping symbols are ABI-absent (see
# objdump-no-mapping-symbols.s). No golden invent.
#
# Peer: RISCV/Hexagon MCInstrAnalysis + llvm-objdump evaluateBranch postfix.

.section .text.caller,"ax",@progbits
.globl caller
.type caller,@function
caller:
  { jal lr, callee; nop; nop }
  { xor32 r0, r0, r0; nop; nop }

.section .text.callee,"ax",@progbits
.globl callee
.type callee,@function
callee:
  { xor32 r0, r0, r0; nop; nop }

# OBJ-LABEL: <caller>:
# OBJ: jal{{.*}}lr
# OBJ-NOT: jal{{.*}}<caller>
# OBJ-LABEL: <callee>:

# ELF-LABEL: <caller>:
# ELF: jal{{.*}}<callee>
# ELF-LABEL: <callee>:
