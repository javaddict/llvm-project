; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s

;===----------------------------------------------------------------------===
; SIMD Horizontal Add Optimization Tests
;===----------------------------------------------------------------------===
; Tests for the Haydn SIMD reduction codegen. The llvm.vector.reduce.add
; intrinsic is expanded by the IR-level expand-reductions pass into
; shufflevector + add + extractelement before GISel. The legalizer handles
; the resulting G_SHUFFLE_VECTOR, G_ADD, and G_EXTRACT_VECTOR_ELT.
;
; Current codegen: the reduction produces X2ADD32 (vector add) followed by
; MOV_DR64_TO_GPR (element extraction). The X2HADD32_L / X2DOT32 combines
; are not yet firing because the pattern differs from what PostSelectOptimize
; expects (ADD32 of two MOV_DR64_TO_GPR halves, not X2ADD32 + extract).
;
; TODO: Add a PostSelectOptimize rule that recognizes X2ADD32 where only the
; lower half of MOV_DR64_TO_GPR is used, and replace with X2HADD32_L.
; Similarly, recognize dual-product + X2HADD32_L → X2DOT32 (when formMACs on).
;
; Once those combines are implemented, update these CHECK lines to expect
; x2hadd32_l and x2dot32 instead.
;===----------------------------------------------------------------------===

; TEST 1: Simple vector reduction (sum of <2 x i32>)
; The expand-reductions pass turns this into shuffle + add + extractelement.
; The vector add becomes X2ADD32, and element extraction uses MOV_DR64_TO_GPR.
define i32 @test_vecreduce_add_v2i32(<2 x i32> %v) nounwind {
; CHECK-LABEL: test_vecreduce_add_v2i32:
; CHECK: x2add32
  %r = call i32 @llvm.vector.reduce.add.v2i32(<2 x i32> %v)
  ret i32 %r
}

; TEST 2: Reduction of add result
; add two vectors then reduce -- produces X2ADD32 for the vector add
; then another X2ADD32 for the reduction shuffle+add.
define i32 @test_vecreduce_add_after_simd(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: test_vecreduce_add_after_simd:
; CHECK: x2add32
  %sum = add <2 x i32> %a, %b
  %r = call i32 @llvm.vector.reduce.add.v2i32(<2 x i32> %sum)
  ret i32 %r
}

; TEST 3: Reduction of multiply result
; multiply two vectors then reduce -- G_MUL v2i32 → x2mulpl32 (wrap low).
define i32 @test_vecreduce_mul_then_add(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: test_vecreduce_mul_then_add:
; CHECK: x2mulpl32
  %mul = mul <2 x i32> %a, %b
  %r = call i32 @llvm.vector.reduce.add.v2i32(<2 x i32> %mul)
  ret i32 %r
}

; TEST 4: Reduction with accumulator
; acc += reduce_add(a * b) -- x2mulpl32 (or x2mul32) + add path.
define i32 @test_vecreduce_dot_acc(<2 x i32> %a, <2 x i32> %b, i32 %acc) nounwind {
; CHECK-LABEL: test_vecreduce_dot_acc:
; CHECK: x2mulpl32
; CHECK: add32
  %mul = mul <2 x i32> %a, %b
  %dot = call i32 @llvm.vector.reduce.add.v2i32(<2 x i32> %mul)
  %result = add i32 %acc, %dot
  ret i32 %result
}

declare i32 @llvm.vector.reduce.add.v2i32(<2 x i32>)
