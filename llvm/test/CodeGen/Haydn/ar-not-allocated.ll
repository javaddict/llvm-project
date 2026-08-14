; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -o - %s | FileCheck %s
;
; Role: MIR — AR stream ops pin AR0/AR1 physregs; RA never sees an AR vreg.
;
; REGRESSION TEST: AR64 is not allocatable (no copyPhysReg / no spill).
; ISel must pin architectural AR0/AR1 as implicit physregs. If AR becomes
; allocatable again, a `%{{[0-9]+}}:ar` vreg appears here and RA can assign
; a class that cannot be copied or spilled.
;
; Test design: one PLDWWUA (ar_sel=0) and one FLAR (ar_sel=1). CHECK phys
; implicit-defs and CHECK-NOT an AR-class virtual register.

declare void @llvm.haydn.pldwwua(i32, ptr)
declare void @llvm.haydn.flar(i32)

define void @pin_ar0(ptr %ptr) {
; CHECK-LABEL: name: pin_ar0
; CHECK: PLDWWUA
; CHECK-SAME: implicit-def {{(dead )?}}$ar0
; CHECK-NOT: {{%[0-9]+}}:ar{{[^a-zA-Z0-9_]}}
  call void @llvm.haydn.pldwwua(i32 0, ptr %ptr)
  ret void
}

define void @pin_ar1() {
; CHECK-LABEL: name: pin_ar1
; CHECK: FLAR
; CHECK-SAME: implicit-def {{(dead )?}}$ar1
; CHECK-NOT: {{%[0-9]+}}:ar{{[^a-zA-Z0-9_]}}
  call void @llvm.haydn.flar(i32 1)
  ret void
}
