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
; CHECKs pin the masked logical-NOT shape: an `and32` (the mask) feeding an
; `xor32` whose constant operand is `1`, not `-1`. If the combine regresses
; the `and32` mask disappears and the `xor32` is fed by `addi32.., -1`.

define i32 @xor_i1_bool_not(i32 %x) {
; O1-LABEL: xor_i1_bool_not:
; O2-LABEL: xor_i1_bool_not:
; The constant `1` for the logical NOT (buggy form used -1).
; O1: addi32{{(_w)?}} {{r[0-9]+}}, r0, 1
; O2: addi32{{(_w)?}} {{r[0-9]+}}, r0, 1
; The mask that cleans anyext upper bits, feeding the XOR.
; O1: and32
; O1: xor32
; O2: and32
; O2: xor32
entry:
  %c = icmp eq i32 %x, 0
  %n = xor i1 %c, true
  %z = zext i1 %n to i32
  ret i32 %z
}
