; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; REGRESSION TEST: — HaydnConditionOptimizer::reuseInverseComparisons
; must NOT reuse an inverse SLT32 when one of its source operands has been
; redefined between the previous and current comparison.
;
; Bug: reuseInverseComparisons keys recorded comparisons on physical-register
; NAMES (S1, S2) and verifies only that PrevDst survives between the two
; compares. After regalloc, a fully-unrolled reduction reloads the SAME
; physical registers (r1, r2,...) with DIFFERENT values every iteration, so a
; name-only CSE reuses a comparison between completely different operands.
; The dropped SLT32s were replaced with `xori32 r3, r8, 1` referencing a stale
; r8 from an earlier iteration, so every index-update MOVT32 after i=2 fired
; with the wrong polarity. argmax's max VALUE was tracked correctly (MAX32)
; but its index got stuck at the i=1 value.
;
; Test design: a fully-unrolled argmax where each unrolled iteration loads
; `data[i]` into the same reload slot and the running max into another, then
; does `select i1 (data[i] > mx), i32 idx_i, i32 prev_idx`. The O2 pipeline
; runs reuseInverseComparisons; before the fix, the second and later inverse
; SLT32s were rewritten to XORI32 against the stale i=0 condition.
;
; PASS criterion: each MOVT32 guarding an index update is predicated on a
; condition register defined by an SLT32 in the SAME iteration. We enforce
; this by forbidding ANY `xori32..., 1` in this function — the only way
; reuseInverseComparisons can emit XORI32 here. If even one XORI32-1 appears
; the stale reuse has returned. (A truly-identical pair could in principle be
; CSE'd another way, but in this reduced loop every pair has distinct values
; so any XORI32-1 is necessarily stale.)

@data = external global [16 x i32]

define i32 @cb44_argmax_stale_inverse() {
; CHECK-LABEL: cb44_argmax_stale_inverse:
; CHECK-NOT: xori32 {{[a-z0-9]+}}, {{[a-z0-9]+}}, 1
entry:
  %d0  = load i32, ptr @data
  %p1  = getelementptr [16 x i32], ptr @data, i32 0, i32 1
  %d1  = load i32, ptr %p1
  %p2  = getelementptr [16 x i32], ptr @data, i32 0, i32 2
  %d2  = load i32, ptr %p2
  %p3  = getelementptr [16 x i32], ptr @data, i32 0, i32 3
  %d3  = load i32, ptr %p3
  %p4  = getelementptr [16 x i32], ptr @data, i32 0, i32 4
  %d4  = load i32, ptr %p4
  %p5  = getelementptr [16 x i32], ptr @data, i32 0, i32 5
  %d5  = load i32, ptr %p5
  %p6  = getelementptr [16 x i32], ptr @data, i32 0, i32 6
  %d6  = load i32, ptr %p6
  %p7  = getelementptr [16 x i32], ptr @data, i32 0, i32 7
  %d7  = load i32, ptr %p7
  ; Running max chain (mirrors llvm.smax fold from the original bug).
  %m1  = call i32 @llvm.smax.i32(i32 %d1, i32 %d0)
  %m2  = call i32 @llvm.smax.i32(i32 %d2, i32 %m1)
  %m3  = call i32 @llvm.smax.i32(i32 %d3, i32 %m2)
  %m4  = call i32 @llvm.smax.i32(i32 %d4, i32 %m3)
  %m5  = call i32 @llvm.smax.i32(i32 %d5, i32 %m4)
  %m6  = call i32 @llvm.smax.i32(i32 %d6, i32 %m5)
  %m7  = call i32 @llvm.smax.i32(i32 %d7, i32 %m6)
  ; Per-iteration index-update selects: each predicate is a DISTINCT compare
  ; (d_i > m_{i-1}) and must NOT be replaced by an XORI32 of any earlier one.
  %c1  = icmp sgt i32 %d1, %d0
  %c2  = icmp sgt i32 %d2, %m1
  %c3  = icmp sgt i32 %d3, %m2
  %c4  = icmp sgt i32 %d4, %m3
  %c5  = icmp sgt i32 %d5, %m4
  %c6  = icmp sgt i32 %d6, %m5
  %c7  = icmp sgt i32 %d7, %m6
  %i1  = select i1 %c1, i32 1, i32 0
  %i2  = select i1 %c2, i32 2, i32 %i1
  %i3  = select i1 %c3, i32 3, i32 %i2
  %i4  = select i1 %c4, i32 4, i32 %i3
  %i5  = select i1 %c5, i32 5, i32 %i4
  %i6  = select i1 %c6, i32 6, i32 %i5
  %i7  = select i1 %c7, i32 7, i32 %i6
  ret i32 %i7
}

declare i32 @llvm.smax.i32(i32, i32)
