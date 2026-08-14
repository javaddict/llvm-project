; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; CB-144: element access on a vector of i1 with a VARIABLE index.
;
; Haydn has no vector-of-i1, and the generic lowering could not help: with a
; variable index it spills the vector to a stack slot, and
; lowerExtractInsertVectorElt gives up on an element that is not byte-sized.
; So `extractelement <2 x i1> %c, i32 %i` reported
;
;   LLVM ERROR: unable to legalize instruction:
;   %4:_(s1) = G_EXTRACT_VECTOR_ELT %3:_(<2 x s1>), %2:_(s32)
;
; and that is reachable from ordinary C — a vector icmp feeding a
; variable-indexed read is all it takes.
;
; The fix is to widen the ELEMENT out of i1 before anything has to represent
; it. widenScalar on type index 0 anyexts the source vector to match and
; truncates the result back, so the whole thing becomes an s32 extract from a
; <N x s32> — a shape the existing rules already handle, and byte-sized, so
; the stack lowering applies.
;
; The lowering is verified by EXECUTION, not by reading the schedule: the
; simulator runs both lanes of both operand orders and the result is exact.
; The CHECKs here pin the shape that says the i1 vector is gone — one compare
; per lane, and an index scaled to a byte offset.

define i32 @extract_v2i1(<2 x i32> %a, <2 x i32> %b, i32 %i) {
; CHECK-LABEL: extract_v2i1:
; Two lanes compared separately: the vector of i1 does not survive.
; Two separate positive assertions, deliberately NOT a same-line one: the
; point is that the lane compares exist as scalars, not that the packer
; happened to put them in one bundle. Which bundle each lands in is a
; scheduling detail, and pinning it made this test fail on a pure density
; improvement (CB-147). Naming the same-line directive in this comment
; would re-arm it -- FileCheck reads directives out of comments.
; CHECK: sltu32
; CHECK: sltu32
; The index is masked to the lane count and scaled to the element's byte size.
; CHECK: andi32
; CHECK: slli32
  %c = icmp ult <2 x i32> %a, %b
  %e = extractelement <2 x i1> %c, i32 %i
  %z = zext i1 %e to i32
  ret i32 %z
}

define <2 x i32> @insert_v2i1(<2 x i32> %a, <2 x i32> %b, i1 %v, i32 %i) {
; CHECK-LABEL: insert_v2i1:
; CHECK: jalr
  %c = icmp ult <2 x i32> %a, %b
  %n = insertelement <2 x i1> %c, i1 %v, i32 %i
  %z = zext <2 x i1> %n to <2 x i32>
  ret <2 x i32> %z
}

define i32 @extract_v4i1(<4 x i32> %a, <4 x i32> %b, i32 %i) {
; CHECK-LABEL: extract_v4i1:
; CHECK: jalr
  %c = icmp ult <4 x i32> %a, %b
  %e = extractelement <4 x i1> %c, i32 %i
  %z = zext i1 %e to i32
  ret i32 %z
}
