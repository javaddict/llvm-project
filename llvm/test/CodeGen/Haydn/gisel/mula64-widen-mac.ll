; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Wave T5.1 / T7.3: PreLegalizer G_MULA64 / G_MULA64U formation + select.

; Wave T5.1 / T7.3: PreLegalizer G_MULA64 / G_MULA64U formation + select.
;
;   acc += sext(a)*sext(b)  -> mula64.ll   (signed x signed low lane)
;   acc += zext(a)*zext(b)  -> mula64.ulul (unsigned x unsigned low lane)
;
; ISA trap: mula64.ull is unsigned×SIGNED, not fully unsigned. Do not emit it
; for the zext×zext C pattern. High-lane / mixed-sign stay intrinsic-only.
; formMACs is FATED — fusion is PreLegalizer-only (TD form_mula64).
;
; Fused add orders live here (full llc). Mixed-sign / multi-use no-fuse
; schoolbook is gisel/mul-s64-widen-legalize.mir (legalizer emits
; G_HAYDN_MUL64_WIDEN*; instruction-select emits MUL64_LL / MUL64_ULUL).
; COPY-chain peel: gisel/mula64-copy-chain.mir
;
; Unfused 32x32->64 (no accumulator) must select the widening generic, not
; a libcall. Fused cases above stay mula64.*.

; CHECK-LABEL: widen_mac_ss:
; CHECK: mula64.ll
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3

define i64 @widen_mac_ss(i32 %a, i32 %b, i64 %acc) {
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}

; CHECK-LABEL: widen_mac_uu:
; CHECK: mula64.ulul
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
define i64 @widen_mac_uu(i32 %a, i32 %b, i64 %acc) {
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %acc, %m
  ret i64 %r
}

; Second add order for unsigned (mul + acc) — both orders must fuse.
; CHECK-LABEL: widen_mac_uu_mul_first:
; CHECK: mula64.ulul
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
define i64 @widen_mac_uu_mul_first(i32 %a, i32 %b, i64 %acc) {
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}

; CHECK-LABEL: widen_mul_ss:
; CHECK: mul64.ll
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
define i64 @widen_mul_ss(i32 %a, i32 %b) {
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}

; CHECK-LABEL: widen_mul_uu:
; CHECK: mul64.ulul
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
define i64 @widen_mul_uu(i32 %a, i32 %b) {
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}
