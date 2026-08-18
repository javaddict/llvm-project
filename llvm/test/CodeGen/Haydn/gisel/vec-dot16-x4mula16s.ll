; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false -stop-after=instruction-select \
; RUN:     -o - %s | FileCheck %s
;
; Role: semantic — documented 32-bit sat vec_dot16 path. Default public
; header leaves the quad-16 64-bit acc name undefined, so NatureDSP
; vec_dot16x16_fast uses dest-typed X4MULA16S then ADD32S / SEL32_LH /
; MOVAD32_H. Integer host of
;   x=[10,-10,20,-20,30,40,50,60] y=[1,2,3,4,5,6,7,8]
; is 1190; the two-lane HS_11_00 body is 380. Do not select FMULAA16 here.
; Native X2CMUL stays a separate ISA pin; public AE wrappers stay fail-closed.

declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32>, <2 x i32>)
declare i32 @llvm.haydn.movad32.high(i64)

define i32 @vec_dot16_sat32_reduce(i64 %x0, i64 %y0, i64 %x1, i64 %y1) nounwind {
; CHECK-LABEL: name: vec_dot16_sat32_reduce
; CHECK: X4MULA16S
; CHECK: X4MULA16S
; CHECK: X2ADD32S
; CHECK: X2SEL32_LH
; CHECK: X2ADD32S
; CHECK: MOVE32_DR_H
; CHECK-NOT: FMULAA16
  %vx0 = bitcast i64 %x0 to <4 x i16>
  %vy0 = bitcast i64 %y0 to <4 x i16>
  %vx1 = bitcast i64 %x1 to <4 x i16>
  %vy1 = bitcast i64 %y1 to <4 x i16>
  %m0 = call { i64, i64 } @llvm.haydn.x4mula16s(i64 0, i64 0, <4 x i16> %vx0, <4 x i16> %vy0)
  %h0 = extractvalue { i64, i64 } %m0, 0
  %l0 = extractvalue { i64, i64 } %m0, 1
  %m1 = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %h0, i64 %l0, <4 x i16> %vx1, <4 x i16> %vy1)
  %h1 = extractvalue { i64, i64 } %m1, 0
  %l1 = extractvalue { i64, i64 } %m1, 1
  %vh = bitcast i64 %h1 to <2 x i32>
  %vl = bitcast i64 %l1 to <2 x i32>
  %sum = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %vh, <2 x i32> %vl)
  %swp = call <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32> %sum, <2 x i32> %sum)
  %red = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %sum, <2 x i32> %swp)
  %r64 = bitcast <2 x i32> %red to i64
  %out = call i32 @llvm.haydn.movad32.high(i64 %r64)
  ret i32 %out
}

define i64 @vec_dot16_sat_mac_only(i64 %acc_hi, i64 %acc_lo, i64 %a, i64 %b) nounwind {
; CHECK-LABEL: name: vec_dot16_sat_mac_only
; CHECK: X4MULA16S
; CHECK-NOT: FMULAA16
; CHECK-NOT: X4MULA16{{[^S]}}
  %va = bitcast i64 %a to <4 x i16>
  %vb = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc_hi, i64 %acc_lo, <4 x i16> %va, <4 x i16> %vb)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
