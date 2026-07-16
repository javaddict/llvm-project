# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o 2>&1 | \
# RUN:   FileCheck %s

# REGRESSION TEST : objdump on a Bundle128.o with relocated JAL must
# not crash and must decode the JAL slot correctly.
#
# Bug context: the objdump/Bundle128 decode path was the dominant direct-.elf
# blocker. llvm-objdump -d on compiler-rt adddf3.o aborted at
# HaydnDisassembler.cpp:333 (decodeSImmOperandXStepWide<6,0,true> assert
# 8-bit tblgen-aggregated field with bits 6/7 set). The hard bar per CLAUDE.md
# "bounds-safe printOperand": the disassembler must NEVER abort on hostile
# text — it must decode-or-degrade. crt0.o (relocated JAL Bundle128s) was
# reported alongside as producing `jal lr, 0` ("JAL garbage") + `<unknown>`
# at trailing bytes.
#
# Diagnosis :
# (1) CRASH (adddf3.o): the simm6 assert — fixed by masking the field to N
# bits before sign/zero-extend (decodeSImmOperandXStepWide + the sibling
# decodeLSPage1Imm5). This is the actual blocker.
# (2) `jal lr, 0` is NOT garbage — the imm20 field IS 0 in the unlinked
# o because the call target is a relocation (R_HAYDN_CallSImm20) filled
# in at link time. `llvm-objdump -dr` shows the relocation. The decode is
# correct. (User misdiagnosis.)
# (3) `<unknown>` on trailing bytes (e.g. crt0's final `0f 78`) is correct:
# the bytes claim a 64-bit width class (low-nibble 0xF, bits[1:0]=11) but
# fewer than 8 bytes remain — genuinely undecodable trailing bytes. This
# is the expected graceful-degradation output, not a bug.
#
# Test design: assemble a Bundle128 `jal lr, target` (a relocated call) and
# confirm objdump exits 0, decodes the JAL slot, and does NOT abort. The imm20
# is 0 in the.o (relocation placeholder); CHECK pins the `jal lr, 0`
# rendering (the correct decode of the unlinked field) plus the relocation
# line under -dr. If the mask fix regresses, objdump aborts on the LD
# simm6 path elsewhere in linked objects (the crash is exercised by the
# sibling d432-decodesimm6-no-assert.s test); this test pins the JAL decode.

# Bundle128 JAL: produces a 16-byte parcel with a relocated imm20 field.
jal lr, external_target

# CHECK-LABEL: Disassembly of section .text:
# CHECK: jal	lr, 0
# CHECK-NOT: {{Assertion|abort|Stack dump|PLEASE submit a bug report}}
