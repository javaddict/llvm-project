; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; CB-130: a <2 x i1> vector predicate must legalize.
;
; The vector G_ICMP is scalarized, which builds `<2 x s1> = G_BUILD_VECTOR` out
; of the two scalar results. Haydn has no vector-of-i1, so there is nothing for
; that build to become — it is an artifact, and the matching unmerge arrives
; when the zext and select users are scalarized in turn.
;
; HaydnLegalizerInfo asked for it to be narrowed instead, with
; `.clampMaxNumElements(0, S1, 1)`. One element is not a smaller vector:
; clampMaxNumElements builds its target with LLT::scalarOrVector(), which
; returns a SCALAR for a count of one, so fewerElementsVectorMerge asserted
; "Expected vector types" and clang aborted. The rule is gone; the build now
; falls to .lower(), reports UnableToLegalize, and the artifact combiner
; cancels it against the unmerge.
;
; The assertion is what this test is for, so the CHECKs pin the shape rather
; than the schedule: both lanes compared, both lanes conditionally moved.
; -verify-machineinstrs on the RUN line is the other half.

define <2 x i32> @cb130_v2i1_predicate(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: cb130_v2i1_predicate:
entry:
  %cmp = icmp ult <2 x i32> %a, %b
  %ext = zext <2 x i1> %cmp to <2 x i32>
  %sel = select <2 x i1> %cmp, <2 x i32> <i32 7, i32 9>, <2 x i32> zeroinitializer
  %sum = add <2 x i32> %ext, %sel
  ret <2 x i32> %sum
}

; Two lanes: two scalar unsigned compares, and two conditional moves for the
; select. A vector-of-i1 would show neither.
; CHECK: sltu32
; CHECK-SAME: sltu32
; CHECK: movt32
; CHECK: movt32
; CHECK: jalr
