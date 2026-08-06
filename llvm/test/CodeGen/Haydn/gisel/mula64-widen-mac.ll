; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Wave T5.1 / T7.3: PreLegalizer G_MULA64 / G_MULA64U formation + select.
;
;   acc += sext(a)*sext(b)  -> mula64.ll   (signed x signed low lane)
;   acc += zext(a)*zext(b)  -> mula64.ulul (unsigned x unsigned low lane)
;
; ISA trap: mula64.ull is unsigned×SIGNED, not fully unsigned. Do not emit it
; for the zext×zext C pattern. High-lane / mixed-sign stay intrinsic-only.
; formMACs is FATED — fusion is PreLegalizer-only (TD form_mula64).
;
; Matrix covered here:
;   - both add orders (mul+acc and acc+mul)
;   - mixed-sign no-fuse
;   - multi-use mul no-fuse
; COPY-chain peel is covered by gisel/mula64-copy-chain.mir

; CHECK-LABEL: widen_mac_ss:
; CHECK: mula64.ll
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__muldi3
define i64 @widen_mac_ss(i32 %a, i32 %b, i64 %acc) {
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}

; CHECK-LABEL: widen_mac_uu:
; CHECK: mula64.ulul
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__muldi3
define i64 @widen_mac_uu(i32 %a, i32 %b, i64 %acc) {
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %acc, %m
  ret i64 %r
}

; Mixed extension: no G_MULA64* fuse; still no libcall (schoolbook / ULUL).
; CHECK-LABEL: widen_mac_mixed:
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__muldi3
; CHECK-NOT: mula64.ll
; CHECK-NOT: mula64.ulul
define i64 @widen_mac_mixed(i32 %a, i32 %b, i64 %acc) {
  %aa = sext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}

; Multi-use mul must not fuse (would leave G_MUL live while apply erases it).
; CHECK-LABEL: widen_mac_ss_multiuse:
; CHECK-NOT: mula64.ll
; CHECK-NOT: mula64.ulul
define i64 @widen_mac_ss_multiuse(i32 %a, i32 %b, i64 %acc) {
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  %r2 = add i64 %r, %m
  ret i64 %r2
}

; Second add order for unsigned (mul + acc) — both orders must fuse.
; CHECK-LABEL: widen_mac_uu_mul_first:
; CHECK: mula64.ulul
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__muldi3
define i64 @widen_mac_uu_mul_first(i32 %a, i32 %b, i64 %acc) {
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}
