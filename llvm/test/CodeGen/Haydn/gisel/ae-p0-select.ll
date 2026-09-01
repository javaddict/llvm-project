; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false -stop-after=instruction-select \
; RUN:     -o - %s | FileCheck %s
;
; Role: semantic — public AE residual surface that reaches GISel.
;   * one-arg CVT16X4 is zero-pad + X4SAT32T16
;   * SA64 POS/NEG is WBARWUA ar_sel 0 vs 1 with distinct dir ImmArgs —
;     after the CB-151 member-shape reshape, dir_sel is NOT encoded
;     (golden direction is rs[2:1]); it folds at selection, so the two
;     calls must still lower to distinguished WBARWUA ar_sel forms, never
;     alias into one another
; TRUNCA pack, CVTQ56 <<16, and saturating left-shift are header C
; (unsigned pack / unsigned << / unsigned-shift round-trip). They do not
; invent a selector opcode.

declare <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32>, <2 x i32>)
declare void @llvm.haydn.wbarwua(i32, ptr, i32)

define i64 @cvt16x4_1arg_zero_pad(i64 %a) nounwind {
; CHECK-LABEL: name: cvt16x4_1arg_zero_pad
; CHECK: X4SAT32T16
  %va = bitcast i64 %a to <2 x i32>
  %z = bitcast i64 0 to <2 x i32>
  %r = call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> %va, <2 x i32> %z)
  %out = bitcast <4 x i16> %r to i64
  ret i64 %out
}

define void @sa64neg_dir1(ptr %p) nounwind {
; CHECK-LABEL: name: sa64neg_dir1
; CHECK: WBARWUA 0, %{{[0-9]+}}
; CHECK-NOT: WBARWUA %{{[0-9]+}}, 0, 1
  call void @llvm.haydn.wbarwua(i32 0, ptr %p, i32 1)
  ret void
}

define void @sa64pos_dir0(ptr %p) nounwind {
; CHECK-LABEL: name: sa64pos_dir0
; CHECK: WBARWUA 0, %{{[0-9]+}}
  call void @llvm.haydn.wbarwua(i32 0, ptr %p, i32 0)
  ret void
}
