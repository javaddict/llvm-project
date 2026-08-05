; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O1 -o - %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -o - %s | FileCheck %s --check-prefix=O2
;
; REGRESSION TEST: `xor i1 %x, true` on a boolean, widened to s32, must lower to
; a logical NOT (XOR with 1 after masking to 0/1), NOT a bitwise NOT (XOR with
; 1) when the result is consumed by a non-zero branch test.
;
; Bug : the Legalizer widened `xor i1 %x, true` (an i1 logical NOT) to
; G_XOR (anyext i1->s32 %x), G_CONSTANT i32 -1
; a 32-bit bitwise NOT. For a boolean operand (0 or 1 with undef/anyext upper
; bits) the bitwise NOT yields -1 / -2 (or undef upper bits) — NEVER zero. The
; Haydn G_BRCOND lowering tests the s32 register non-zero (BNEZ/BEQZ) and the
; G_TRUNC s32->s1 is a no-op alias, so a loop-exit condition built from
; `xor i1 %x, true` never tested as false and the loop spun forever (:
; BST insert `while (placed == 0)` was NOEXIT).
;
; Fix: the PostLegalizerCombiner rewrites `G_XOR %bool, -1` (with a known-0/1
; %bool) to `G_XOR (G_AND %bool, 1), 1` — a sound logical NOT under the non-zero
; test (the AND masks any undef upper bits from anyext). The mask is redundant
; but harmless for already-clean booleans (icmp/zext/select).
;
; Test design: a minimal `icmp -> xor i1,true -> zext` forces the combine. The
; CHECKs pin the masked logical-NOT shape: an `xori32` with constant `1`, not a
; bitwise `not32` (or an `xor32` fed by a materialized `-1`), plus the `andi32 1`
; that cleans the anyext upper bits when the result is zero-extended.

define i32 @xor_i1_bool_not(i32 %x) {
; O1-LABEL: xor_i1_bool_not:
; O2-LABEL: xor_i1_bool_not:
; A bitwise NOT of the 0/1 predicate is the regression this test guards.
; O1-NOT: not32
; O2-NOT: not32
; The logical NOT: XOR with the constant 1 (buggy form used -1).
; O1: xori32 {{r[0-9]+}}, {{r[0-9]+}}, 1
; O2: xori32 {{r[0-9]+}}, {{r[0-9]+}}, 1
; The mask that cleans anyext upper bits for the zext of the result.
; O1: andi32 {{r[0-9]+}}, {{r[0-9]+}}, 1
; O2: andi32 {{r[0-9]+}}, {{r[0-9]+}}, 1
entry:
  %c = icmp eq i32 %x, 0
  %n = xor i1 %c, true
  %z = zext i1 %n to i32
  ret i32 %z
}
