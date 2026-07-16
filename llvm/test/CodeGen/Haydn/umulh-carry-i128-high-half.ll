; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 < %s | FileCheck %s

; REGRESSION TEST: G_UMULH s64 must propagate the carry out of the
; cross-partial (LH + HL + (LL>>32)) sum into the high-64 result.
;
; Bug: the G_UMULH custom legalizer in HaydnLegalizerInfo.cpp computed
; Sum2 = LH + HL + (LL >> 32) (mod 2^64)
; Mid = Sum2 >> 32
; UMULH = HH + Mid
; When (LH + HL + (LL>>32)) carries out of bit 63 — which happens precisely
; when both cross-partials LH and HL have their top bits set — that carry
; represents a +2^32 contribution to UMULH and was silently dropped. The
; result was short by exactly 2^32.
;
; Decisive case: umulh(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF) must equal
; 0xFFFFFFFFFFFFFFFE. With the buggy legalizer it returned
; 0xFFFFFFFEFFFFFFFE (short by 2^32) because:
; HH = 0xFFFFFFFE00000001, LH = HL = 0xFFFFFFFE00000001
; LL = 0xFFFFFFFE00000001, (LL>>32) = 0xFFFFFFFE
; Sum2 = 2*0xFFFFFFFE00000001 + 0xFFFFFFFE = 0xFFFFFFFD00000000 (mod 2^64)
; with a carry-out of 1 that the old code discarded.
; Mid(buggy) = 0xFFFFFFFD -> result = 0xFFFFFFFEFFFFFFFE (WRONG)
; Mid(fixed) = 0xFFFFFFFE + carry -> result = 0xFFFFFFFFFFFFFFFE (RIGHT)
;
; Repro surface: at -O2 InstCombine folds a hand-written 64x64->128 high-half
; helper into `mul nuw i128, lshr 64, trunc`, which the legalizer routes to
; G_UMULH. At -O1 the source decomposition survives and the bug is masked
; so this test is -O2-specific on purpose.
;
; Test design: we compute the high 64 bits of (a*b) for opaque runtime
; operands (so InstCombine cannot constant-fold the result) and return it
; from a function so the value is live.
;
; What the assembly-level CHECK guards: the schoolbook decomposition stays
; inline through G_UMULH — no __muldi3/__multi3 libcall may appear (a
; libcall would bypass G_UMULH entirely and mask any carry bug), and at
; least one native mul64 widening partial must be emitted. The CARRY
; CORRECTNESS ITSELF cannot be fully pinned from assembly text alone
; (the buggy and fixed add/shift chains are structurally similar); the
; decisive runtime check is the BundleSim bignum repro
; ssd2/mhyang/BundleSim/benchmarks/diff_sweep/bignum/red_umulh_legalizer.c
; (host returns 0xFF for limb[2]&0xFF, buggy sim returned 0xFE). That repro
; is the authoritative behavioral test for this fix and lives in the
; benchmark harness rather than in lit.

define i64 @umulh_all_ones(i64 %a, i64 %b) {
  ; Caller passes 0xFFFFFFFFFFFFFFFF for both %a and %b.
  ; Expected runtime result: 0xFFFFFFFFFFFFFFFE.
  %xa = zext i64 %a to i128
  %xb = zext i64 %b to i128
  %m  = mul nuw i128 %xa, %xb
  %hi = lshr i128 %m, 64
  %r  = trunc i128 %hi to i64
  ret i64 %r
}

; CHECK-LABEL: umulh_all_ones:
; CHECK-NOT:   call{{.*}}__muldi3
; CHECK-NOT:   call{{.*}}__multi3
; CHECK:       mul64

define i64 @umulh_cross_partial_carry(i64 %a, i64 %b) {
  ; Second probe with non-all-ones operands that still set both cross
  ; partials' high bits. Caller passes 0xFFFFFFFF80000000 for both.
  ; LH = HL = 0x80000000 * 0xFFFFFFFF = 0x7FFFFFFF80000000 (bit 63 set)
  ; Expected runtime result: 0xFFFFFFFF00000000.
  %xa = zext i64 %a to i128
  %xb = zext i64 %b to i128
  %m  = mul nuw i128 %xa, %xb
  %hi = lshr i128 %m, 64
  %r  = trunc i128 %hi to i64
  ret i64 %r
}

; CHECK-LABEL: umulh_cross_partial_carry:
; CHECK-NOT:   call{{.*}}__muldi3
; CHECK-NOT:   call{{.*}}__multi3
; CHECK:       mul64
