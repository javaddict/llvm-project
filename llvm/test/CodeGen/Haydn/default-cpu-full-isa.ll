; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=generic -mattr=+agu \
; RUN:     -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: run-through — empty -mcpu and a partial +agu mattr list keep the
; product full ISA. x2add32 is FeatureSIMD; a silent demotion would
; scalarize or abort GlobalISel.

declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)

define <2 x i32> @default_cpu_x2add(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: default_cpu_x2add:
; CHECK: x2add32
  %r = call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
