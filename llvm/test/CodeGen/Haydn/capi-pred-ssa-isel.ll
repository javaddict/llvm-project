; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -enable-post-misched=false -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -enable-post-misched=false -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — ISel exit: pure SSA pred ops expand only to X2/X4SLT + MOVT + MOVESFR2GPR/MOVEGPR2SFR.
; Post-RA pack of two-epoch reverse is format-pipeline owned; this pin is ISel emit.

; C1.1 / G-PRED-SSA ISel exit: pure SSA pred ops expand only to
;   X2/X4SLT + MOVT + MOVESFR2GPR/MOVEGPR2SFR
; (no formMACs, no FormatID, no phantom MI).
;
; Packetizer may co-issue SLT+MOVESFR2GPR or MOVEGPR2SFR+MOVT in one bundle;


define i32 @test_x2cmplt32(<2 x i32> %a, <2 x i32> %b) {
  %p = call i32 @llvm.haydn.x2cmplt32(<2 x i32> %a, <2 x i32> %b)
  ret i32 %p
}

; CHECK-LABEL: test_x4cmplt16:
; CHECK-DAG: x4slt16
; CHECK-DAG: movesfr2gpr
define i32 @test_x4cmplt16(<4 x i16> %a, <4 x i16> %b) {
  %p = call i32 @llvm.haydn.x4cmplt16(<4 x i16> %a, <4 x i16> %b)
  ret i32 %p
}

; CHECK-LABEL: test_x2mux32:
; CHECK-DAG: movegpr2sfr
; CHECK-DAG: x2movt32
define <2 x i32> @test_x2mux32(i32 %p, <2 x i32> %t, <2 x i32> %f) {
  %r = call <2 x i32> @llvm.haydn.x2mux32(i32 %p, <2 x i32> %t, <2 x i32> %f)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x4mux16:
; CHECK-DAG: movegpr2sfr
; CHECK-DAG: x4movt16
define <4 x i16> @test_x4mux16(i32 %p, <4 x i16> %t, <4 x i16> %f) {
  %r = call <4 x i16> @llvm.haydn.x4mux16(i32 %p, <4 x i16> %t, <4 x i16> %f)
  ret <4 x i16> %r
}

; Fused path: SLT + MOVT only (no SFR capture between epochs).
; CHECK-LABEL: test_x2cmpsel32:
; CHECK-DAG: x2slt32
; CHECK-DAG: x2movt32
; CHECK-NOT: movesfr2gpr
; CHECK-NOT: movegpr2sfr
; CHECK-LABEL: test_x4cmpsel16:
define <2 x i32> @test_x2cmpsel32(<2 x i32> %a, <2 x i32> %b,
                                  <2 x i32> %t, <2 x i32> %f) {
  %r = call <2 x i32> @llvm.haydn.x2cmpsel32(
      <2 x i32> %a, <2 x i32> %b, <2 x i32> %t, <2 x i32> %f)
  ret <2 x i32> %r
}

; CHECK-DAG: x4slt16
; CHECK-DAG: x4movt16
define <4 x i16> @test_x4cmpsel16(<4 x i16> %a, <4 x i16> %b,
                                  <4 x i16> %t, <4 x i16> %f) {
  %r = call <4 x i16> @llvm.haydn.x4cmpsel16(
      <4 x i16> %a, <4 x i16> %b, <4 x i16> %t, <4 x i16> %f)
  ret <4 x i16> %r
}

; Two epochs reverse order: two captures + two restores + two movts.
; CHECK-LABEL: two_epoch_reverse:
; CHECK-DAG: x2slt32
; CHECK-DAG: movesfr2gpr
; CHECK-DAG: x2slt32
; CHECK-DAG: movesfr2gpr
; CHECK-DAG: movegpr2sfr
; CHECK-DAG: x2movt32
; CHECK-DAG: movegpr2sfr
; CHECK-DAG: x2movt32
define <2 x i32> @two_epoch_reverse(<2 x i32> %a1, <2 x i32> %b1,
                                    <2 x i32> %a2, <2 x i32> %b2,
                                    <2 x i32> %t1, <2 x i32> %f1,
                                    <2 x i32> %t2, <2 x i32> %f2) {
  %p1 = call i32 @llvm.haydn.x2cmplt32(<2 x i32> %a1, <2 x i32> %b1)
  %p2 = call i32 @llvm.haydn.x2cmplt32(<2 x i32> %a2, <2 x i32> %b2)
  %r2 = call <2 x i32> @llvm.haydn.x2mux32(i32 %p2, <2 x i32> %t2, <2 x i32> %f2)
  %r1 = call <2 x i32> @llvm.haydn.x2mux32(i32 %p1, <2 x i32> %t1, <2 x i32> %f1)
  %sum = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %r1, <2 x i32> %r2)
  ret <2 x i32> %sum
}

declare i32 @llvm.haydn.x2cmplt32(<2 x i32>, <2 x i32>)
declare i32 @llvm.haydn.x4cmplt16(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2mux32(i32, <2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4mux16(i32, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2cmpsel32(<2 x i32>, <2 x i32>, <2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4cmpsel16(<4 x i16>, <4 x i16>, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
