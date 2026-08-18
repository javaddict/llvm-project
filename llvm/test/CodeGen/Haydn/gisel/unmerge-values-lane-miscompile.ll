; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s
;
; REGRESSION TEST: G_UNMERGE_VALUES narrow-lane selection must EXTRACT each
; lane (shift+mask), not COPY the whole source register into every def.
;
; Bug (fixed by commit 0528a8d4c315, 2026-08-07, which shipped with no test —
; GOALS W40 / peer-compare audit N8): the G_UNMERGE_VALUES selector handled
; only s64->2xs32 (MOVE32_DR_L/H) and s64->4xs16, then ended in a fail-OPEN
; fallback that emitted `buildCopy(Dst, Src)` for EVERY def. Any shape without
; an explicit case silently became "every lane = lane 0". For s64->8xs8 it
; also emitted an illegal cross-bank MOVE32 <GPR>, $d. Producers reaching the
; bad shape: extractelement of residual-pack vectors and vector icmp
; scalarization — a memcmp-style byte compare reported "not equal" for two
; IDENTICAL buffers (silent miscompile; gcc-c-torture was 0 FAIL before and
; after, so only a targeted lane-value test can catch it).
;
; Fix under test: the selector gained a GPR32 residual-pack arm (s32 ->
; 2 x s16 / 4 x s8 = SRLI32 lane-shift + ANDI32 lane-mask per def) and the
; fallback now fails CLOSED for width-changing unmerge (selection error
; instead of wrong code).
;
; Test design: -O0 so no IR combining folds the extracts; extractelement of
; <2 x i16> / <4 x i8> (32-bit residual packs living in ONE GPR32 per
; HaydnLegalizerInfo G_BUILD_VECTOR customFor) reaches the selector as
; G_UNMERGE_VALUES s32 -> N x sN. Lane >= 1 is the discriminator: the
; pre-fix fallback COPY'd the whole GPR32 into every def, so lane 1+ reused
; lane-0 bits and NO SRLI32 was emitted for the extract. The zext(iN)->i32
; widening elsewhere in these functions legalizes to AND-only (mask <= 20
; bits, HaydnLegalizerInfo Phase A), so an `srli32 <lane-shift>` here can
; ONLY come from the selector's lane-extract arm. If the fail-open fallback
; returns, the srli32 CHECKs fail; if the arm is deleted and the fail-closed
; guard fires instead, "cannot select" appears and the CHECK-NOT fails;
; -verify-machineinstrs additionally rejects any cross-bank COPY that
; survives to copyPhysReg.
;
; Sibling coverage: v8i8-lane-extract-cross-bank.ll pins the s64->8xs8 arm
; (MOVE32_DR_L/H); this file pins the GPR32 residual arms the same commit
; added.

; CHECK-NOT: cannot select

; Lane 1 of an argument-passed <2 x i16> pack (CC_Haydn passes v2i16 in one
; GPR — the exact shape legalize-v2i16-scalarize.ll exercised while silently
; relying on the bad fallback: lane 0 accidentally right, lane 1 wrong).
define i32 @v2i16_lane1_arg(<2 x i16> %x) nounwind {
; CHECK-LABEL: v2i16_lane1_arg:
entry:
  %e1 = extractelement <2 x i16> %x, i32 1
  %z1 = zext i16 %e1 to i32
  ret i32 %z1
; Lane 1 of a 2-lane s16 pack = (pack >> 16) & 0xFFFF. The pre-fix fallback
; emitted no shift at all (whole-GPR copy).
; CHECK: srli32 {{r[0-9]+}}, {{r[0-9]+}}, 16
; CHECK: andi32 {{r[0-9]+}}, {{r[0-9]+}}, 65535
}

; Vector-icmp scalarization producer (the memcmp-style silent-miscompile
; path named in the fix commit): icmp of <2 x i16> unmerges BOTH operand
; packs. Lane 1 of each pack must be shift+mask extracted, not COPY'd.
define i32 @icmp_v2i16_lane_sum(<2 x i16> %x, <2 x i16> %y) nounwind {
; CHECK-LABEL: icmp_v2i16_lane_sum:
entry:
  %c = icmp eq <2 x i16> %x, %y
  %z = zext <2 x i1> %c to <2 x i32>
  %e0 = extractelement <2 x i32> %z, i32 0
  %e1 = extractelement <2 x i32> %z, i32 1
  %r = add i32 %e0, %e1
  ret i32 %r
; Both GPR32 packs (x in one GPR, y in one GPR) are unmerged with the lane
; arm: two lane-1 shifts by 16 (one per operand pack), masks 0xFFFF.
; CHECK-COUNT-2: srli32 {{r[0-9]+}}, {{r[0-9]+}}, 16
; CHECK: andi32 {{r[0-9]+}}, {{r[0-9]+}}, 65535
}

; Lanes 1..3 of a <4 x i8> pack (shifts 8/16/24, mask 0xFF). Lane 0 is
; deliberately NOT extracted: it is the "accidentally right" lane under the
; old fallback and would weaken the discriminator. Uses the argument-passed
; pack (CC_Haydn passes v4i8 in one GPR), the shape that reaches the
; selector as G_UNMERGE_VALUES s32 -> 4 x s8.
define i32 @v4i8_upper_lanes_arg(<4 x i8> %x) nounwind {
; CHECK-LABEL: v4i8_upper_lanes_arg:
entry:
  %e1 = extractelement <4 x i8> %x, i32 1
  %e2 = extractelement <4 x i8> %x, i32 2
  %e3 = extractelement <4 x i8> %x, i32 3
  %z1 = zext i8 %e1 to i32
  %z2 = zext i8 %e2 to i32
  %z3 = zext i8 %e3 to i32
  %s12 = add i32 %z1, %z2
  %s = add i32 %s12, %z3
  ret i32 %s
; CHECK: srli32 {{r[0-9]+}}, {{r[0-9]+}}, 8
; CHECK: srli32 {{r[0-9]+}}, {{r[0-9]+}}, 16
; CHECK: srli32 {{r[0-9]+}}, {{r[0-9]+}}, 24
; CHECK: andi32 {{r[0-9]+}}, {{r[0-9]+}}, 255
}
