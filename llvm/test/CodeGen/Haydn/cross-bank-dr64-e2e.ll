; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -enable-misched=false -enable-post-misched=false \
; RUN:     -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — R-CROSS: prefer end-to-end DR64 over pack/unpack ping-pong.

; G-CROSS / R-CROSS: prefer end-to-end DR64 over pack/unpack ping-pong.
; Pure i64 arithmetic must stay on DR64 (add64/xor64/...) without the
; post-RA MOV_GPR_TO_DR64 SP pack chain (st32 lo/hi to sp + ld64).
; Frame prologue may still subi32 sp — that is not a bank pack.
; No GPR-pair aliasing of DR64 — banks remain separate files.
;
; Also pins product elideCrossBank ON O1+: identity extract+repack of the
; same DR64 must not pay the SP pack round-trip.


define i64 @add64_e2e(i64 %a, i64 %b) nounwind {
  %r = add i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: chain64_e2e:
; CHECK: add64
; CHECK: xor64
; CHECK-NOT: move32_dr
; CHECK-NOT: st32{{.*}}sp
; CHECK-NOT: ld64{{.*}}sp
define i64 @chain64_e2e(i64 %a, i64 %b, i64 %c) nounwind {
  %t = add i64 %a, %b
  %r = xor i64 %t, %c
  ret i64 %r
}

; Identity recombine: extract both lanes then pack back. PostSelect elides
; MOV_GPR_TO_DR64(MOVE32_DR_L, MOVE32_DR_H) → OR64; CopyElim may drop the
; identity entirely so the value stays in d0 with no pack memory traffic.
; CHECK-LABEL: identity_repack:
; CHECK-NOT: st32{{.*}}sp
; CHECK-NOT: ld64{{.*}}sp
; CHECK-NOT: move32_dr
define i64 @identity_repack(i64 %a) nounwind {
  %v = bitcast i64 %a to <2 x i32>
  %lo = extractelement <2 x i32> %v, i32 0
  %hi = extractelement <2 x i32> %v, i32 1
  %v2 = insertelement <2 x i32> poison, i32 %lo, i32 0
  %v3 = insertelement <2 x i32> %v2, i32 %hi, i32 1
  %r = bitcast <2 x i32> %v3 to i64
  ret i64 %r
}

; Lane-store fusion remains product-on under elideCrossBank.
; CHECK-LABEL: lane_store_e2e:
; CHECK: d_sw_l_with_imm
; CHECK-NOT: move32_dr_l
define void @lane_store_e2e(i64 %v, ptr %p) nounwind {
  %lo = trunc i64 %v to i32
  store i32 %lo, ptr %p, align 4
  ret void
}
