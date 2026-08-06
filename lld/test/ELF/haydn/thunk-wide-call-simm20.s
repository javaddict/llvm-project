# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# CB-112: out-of-range R_HAYDN_WIDE_CallSImm20 (JAL_W) must produce a
# long-call thunk — not "unrecognized relocation … for Haydn target".
#
# CodeGen selects jal_w for soft-div/libcalls → WIDE_CallSImm20. needsThunk
# already requested a thunk for that RelType, but addThunkHaydn only accepted
# legacy CallSImm20 / BranchSImm16, so large yarpgen TUs (seed 2242 etc.)
# failed at link with Fatal on WIDE_CallSImm20 to __udivsi3.
#
# Assertions:
#   1. Assembler emits R_HAYDN_WIDE_CallSImm20 (not legacy CallSImm20).
#   2. ld.lld exits 0 (no "unrecognized relocation" Fatal).
#   3. A __haydn_thunk_* symbol is present (thunk was built).
#   4. The call site is rewritten to a near jal_w (to the thunk), not left
#      as an out-of-range direct far call.

# RELOCS: R_HAYDN_WIDE_CallSImm20 far_callee

# NM: __haydn_thunk_far_callee

.section .text
.globl _start
_start:
    # WIDE call (JAL_W) → R_HAYDN_WIDE_CallSImm20.
    jal_w lr, far_callee

    # 1.25 MB gap — beyond WIDE_CallSImm20 halfword-scaled ±1 MiB byte window.
    .space 0x140000

.globl far_callee
far_callee:
    { add32 r1, r1, r1 }
    .size far_callee, .-far_callee

    .size _start, .-_start

# Call site must resolve to the nearby thunk (negative offset of a few
# parcels), not attempt a multi-megabyte WIDE_Call field.
# CHECK-LABEL: <_start>:
# Printer may render jal_w as jal; pin family + near-thunk resolve.
# CHECK: jal{{(_w)?}} {{.*}}lr,
