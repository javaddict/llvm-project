# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: not ld.lld %t.o -o %t --section-start=.text=0x10000 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp (was CB-112): out-of-range R_HAYDN_WIDE_CallSImm20
# (JAL_W) used to produce a long-call thunk. CodeGen selects jal_w for
# soft-div/libcalls → WIDE_CallSImm20, so large TUs hit this seat. The
# R0-borrowing veneer is retired (wrong-code); the far call is now an
# explicit link error naming the veneer ABI gap — still NOT the old
# "unrecognized relocation … for Haydn target" Fatal, and no R0-writing
# thunk bytes are emitted. Any veneer restoration is owned by ISA-70/D1.58.

# RELOCS: R_HAYDN_WIDE_CallSImm20 far_callee

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

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_CallSImm20 to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70){{.*}}{{[Rr]}}efusing to emit R0-writing thunk bytes
# CHECK-NOT: __haydn_thunk
