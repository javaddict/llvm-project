; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o - %t/static16.ll | FileCheck %s --check-prefix=STATIC
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o /dev/null %t/dyn16.ll 2>&1 | FileCheck %s --check-prefix=DYN
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -o - %t/vla8.ll | FileCheck %s --check-prefix=VLA

; Role: semantic — T-ABI2 alignment closure. Static MaxAlign > 8 realigns
; SP (AND32 + MatInt(-MaxAlign)). Dyn-alloca still rounds/masks to
; StackAlign(8); requested align > 8 stays unable-to-legalize (no BP).
;
; REGRESSION TEST: alloca align 16 used to get a 16-aligned *offset* on a
; runtime-8-aligned SP (silent misalignment / MEMORY_FAULT). FrameLowering
; then fail-closed MaxAlign > 8 because ANDI32 cannot encode -MaxAlign.
;
; Fix: overlay RISCVFrameLowering.cpp:1142-1153 ANDI realign onto
; AND32 + MatInt(-MaxAlign) in a scavenged GPR. Static overalign compiles.
; G_DYN_STACKALLOC align > 8 remains fail-closed in the legalizer.
;
; Test design: static align 16 must realign (fp + and32 with -16 mask);
; VLA align 16 must still diagnose; a default VLA must still compile and
; adjust SP with a masked subtract. If realign is dropped, STATIC loses
; and32; if the dyn reject is dropped, DYN compiles.

;--- static16.ll
define void @static_align16() {
; STATIC-LABEL: static_align16:
; STATIC: addi32{{(_w)?}} fp, sp,
; STATIC: addi32{{(_w)?}} {{r[0-9]+}}, r0, -16
; STATIC: and32 sp, sp,
; STATIC-NOT: stack object alignment
  %p = alloca i32, align 16
  store i32 1, ptr %p, align 16
  ret void
}

;--- dyn16.ll
define ptr @dyn_align16(i32 %n) {
; DYN: {{unable to legalize instruction|stack object alignment}}
; DYN: {{G_DYN_STACKALLOC|StackAlign\(8\)|base pointer}}
  %p = alloca i32, i32 %n, align 16
  ret ptr %p
}

;--- vla8.ll
define ptr @vla_align8(i32 %n) {
; VLA-LABEL: vla_align8:
; VLA: sub32
; VLA: and32
  %p = alloca i32, i32 %n
  ret ptr %p
}
