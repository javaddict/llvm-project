; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: llvm.haydn.fmuls32s.lh must be ternary (acc, a, b).
;
; Bug: int_haydn_fmuls32s_lh was declared haydn_binary_intrinsic (2 params) in
; IntrinsicsHaydn.td, while its Clang builtin __builtin_haydn_fmuls32s_lh and the
; sibling intrinsics (_ll, _hh) are 3-param. The spec defines FMULS32S_LH as a
; read-modify-write accumulator instruction:
; rtd = SATQ1.63(rtd - SATQ1.63(rsd1[31:00] * rsd2[63:32]))
; i.e. rtd is BOTH an input (accumulator) and the output, identical in shape to
; FMULA32S_LH. haydn_dsp.h's AE_MULSF32S_LH(acc,a,b) calls the intrinsic with
; 3 args. With the intrinsic a binary (2-param) FunctionType, Clang CodeGen's
; getParamType(2) hit the assertion
; `i < getNumParams && "getParamType out of range!"` (DerivedTypes.h:138)
; a hard SIGABRT, not a diagnostic, because -global-isel-abort=1 has no
; fallback. This blocked the NatureDSP lattice kernels latr32x32 and latr24x24.
;
; Fix: int_haydn_fmuls32s_lh is now haydn_ternary_intrinsic (3 params)
; matching _ll/_hh, the builtin, and the compat header. The selector now uses
; selectAccMAC (tied-def accumulator form), the target Pat takes 3 operands
; and the FMULS32S_LH instruction def uses FmtALU64Acc with $rd=$rd_in.
;
; Test design: invokes the intrinsic with (acc, a, b). Before the fix, this IR
; would not even type-check (intrinsic declared 2 params) and the Clang-driven
; call path crashed with getParamType out of range. After the fix, selection
; produces the FMULS32S_LH mnemonic. If the intrinsic reverts to binary, the
; declare here becomes a verify error ("intrinsic arg count mismatch") and the
; CHECK for fmuls32s_lh fails.
;
; References:
; ~/haydn-plans/decisions/-fmuls32s-lh-ternary-arity-fix.md
; ~/haydn-plans/lessons/fmuls32s-lh-getparamtype-oob.md
; sibling test: test/CodeGen/Haydn/acc-mac-3arg-regression.ll 
; spec: Database/haydn_instruction_db.json FMULS32S_LH/LL/HH (all 3-operand)

declare i64 @llvm.haydn.fmuls32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.hh(i64, <2 x i32>, <2 x i32>)
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK: fmuls32s_lh
define i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
 %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
 ret i64 %r
}

; CHECK-LABEL: test_fmuls32s_ll:
; CHECK: fmuls32s_ll
define i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
 %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
 ret i64 %r
}

; CHECK-LABEL: test_fmuls32s_hh:
; CHECK: fmuls32s_hh
define i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
 %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
 ret i64 %r
}
