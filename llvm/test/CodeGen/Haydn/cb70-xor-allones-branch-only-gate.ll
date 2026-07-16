; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; REGRESSION TEST : `~(comparison)` consumed as a VALUE (returned/stored
; arith) must lower to a BITWISE NOT (XOR with -1), NOT a logical NOT (XOR with
; +1 after AND 1). The matchXorAllOnesBoolean combine in HaydnPostLegalizerCombiner
; used to rewrite `xor %bool, -1` -> `xor (and %bool, 1), 1` unconditionally
; which is sound ONLY when %bool is G_ANYEXT of an i1 (: the i1
; `xor %x, true` widened with undef upper bits; masked logical NOT required so
; the non-zero branch test / zext-of-i1 value use gets clean 0/1). For a CLEAN
; boolean %bool — G_ZEXT of an icmp, the C `~(a>b)` lowering — the bitwise NOT
; is well-defined (-1 / -2) and value uses REQUIRE it.
;
; Canonical repro: `int f(int a,int b){ return ~(a>b); }` -> host 254 for a>b
; (bitwise NOT of 1 = -2 = 254 as unsigned char); the buggy combine yielded 0
; (logical NOT of 1).

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: f_value_not:
; CHECK: {{.}}

define i32 @f_value_not(i32 %a, i32 %b) {
entry:
  %c = icmp sgt i32 %a, %b
  %z = zext i1 %c to i32
  %r = xor i32 %z, -1
  ret i32 %r
}
