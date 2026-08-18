; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/GISel/HaydnLegalizerInfo.cpp --check-prefix=SRC
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s -o - | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s -o - | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: semantic — G_ANYEXT s96→s128 must terminate. Pre-fix, unmerge /
; lshr / store of the non-pow2 source re-entered the custom arm and hung.
; COPY of a load/trunc is walked; AND/OR/XOR/SELECT rebuild in s128.
; Peer: AIE legalizer widen-then-op (AIELegalizerHelper); Haydn overlays
; the s96 gcc-layout bitfield path.
;
; SRC: G_ANYEXT of a COPY of a G_LOAD/G_TRUNC s96
; SRC: Walk COPY so the load/trunc arms below still fire
; SRC: Extending both
; SRC: operands then doing the op in s128 avoids unmerging s96
; SRC: Do not emit G_INSERT/G_ANYEXT of sN

; LEG-LABEL: name: s96_zext_load
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; CHECK-LABEL: s96_zext_load:
; CHECK: jalr
define void @s96_zext_load(ptr %p, ptr %q) nounwind {
  %v = load i96, ptr %p, align 4
  %e = zext i96 %v to i128
  %lo = trunc i128 %e to i64
  store i64 %lo, ptr %q, align 8
  ret void
}

; LEG-LABEL: name: s96_and_rmw
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; CHECK-LABEL: s96_and_rmw:
; CHECK: jalr
define void @s96_and_rmw(ptr %p) nounwind {
  %v = load i96, ptr %p, align 4
  %c = and i96 %v, -4294967296
  store i96 %c, ptr %p, align 4
  ret void
}

; COPY-of-load insert: gcc-layout bitfield. Without the COPY walk this
; re-entered ANYEXT.
; LEG-LABEL: name: s96_copy_of_load_insert
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; CHECK-LABEL: s96_copy_of_load_insert:
; CHECK: jalr
define void @s96_copy_of_load_insert(ptr %p, i32 %v) nounwind {
  %s = load i96, ptr %p, align 4
  %z = zext i32 %v to i96
  %cleared = and i96 %s, -4294967296
  %ins = or i96 %cleared, %z
  store i96 %ins, ptr %p, align 4
  ret void
}

; SELECT rebuild: both arms anyext to s128, then select, then unmerge.
; LEG-LABEL: name: s96_select
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; CHECK-LABEL: s96_select:
; CHECK: jalr
define void @s96_select(ptr %p, ptr %q, i1 %c) nounwind {
  %a = load i96, ptr %p, align 4
  %b = load i96, ptr %q, align 4
  %s = select i1 %c, i96 %a, i96 %b
  store i96 %s, ptr %p, align 4
  ret void
}
