; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: F2MULZAA32RS_HHLL (binary / zero-accumulator dual-product MAC)
; must be selectable directly, AND the accumulating form F2MULAA32RS_HHLL with a
; literal-zero accumulator must fold to it.
;
; Bug : the DB defines both
; F2MULAA32RS_HHLL rtd = rtd + r_temp1 + r_temp0 (accumulating; reads acc)
; F2MULZAA32RS_HHLL rtd = 0 + r_temp1 + r_temp0 (binary; no acc read)
; The ZAA (zero-accumulator / binary) form IS the binary dual-product MAC. ISA-51
; wrongly claimed "no binary form exists" — it does (the Z prefix). The real gap
; was in the compiler: the selector had no `case haydn_f2mulzaa*`, so calling the
; ZAA intrinsic crashed InstructionSelect (fallthrough return false), and the
; NatureDSP compat header emitted the wasteful `f2mulaa32rs_hhll(acc=0,...)` chain
; (3 callsites in haydn_dsp.h).
;
; Fix :
; 1. Selector: 4 ZAA cases routed to selectBinary (binary, no tied accumulator).
; 2. PostSelectOptimize: fold F2MULAA(acc=0) -> F2MULZAA (drop the tied acc).
; 3. haydn_dsp.h: call ZAA directly; removed the false "no binary form" claim.
;
; Test design:
; Part A — direct ZAA intrinsic selection: each variant must emit exactly the
; ZAA mnemonic with NO preceding zero-init (no G_CONSTANT materialization
; no MOV_GPR_TO_DR64 of a zero). If the selector regresses (loses the case)
; llc crashes in InstructionSelect.
; Part B — combiner fold: f2mulaa32rs_hhll(i64 0,...) must fold to
; f2mulzaa32rs_hhll. CHECK-NOT: f2mulaa32rs_hhll proves the fold fired.
;
; References:
; ~/haydn-plans/decisions/-f2mulzaa-binary-mac-lowering.md
; spec: Database/haydn_instruction_db.json F2MULZAA32RS_HHLL / F2MULAA32RS_HHLL
; HaydnInstrInfoAuto.td:41 (AA, FmtALU64Acc tied-def), :137 (ZAA, FmtALU64)

declare i64 @llvm.haydn.f2mulzaa32rs.hhll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32rs.hllh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32r.hhll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulzaa32r.hllh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
;===--- Part A: direct ZAA intrinsic selection ---=========================

; CHECK: 	.globl	test_f2mulzaa32rs_hhll          // -- Begin function test_f2mulzaa32rs_hhll
; CHECK: 	.type	test_f2mulzaa32rs_hhll,@function
; CHECK-LABEL: test_f2mulzaa32rs_hhll:                 // @test_f2mulzaa32rs_hhll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulzaa32rs_hhll	d0, d0, d1{{.*}} }
; CHECK: 	{ {{.*}}jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}{{.*}}r0, lr, 0{{.*}} }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_f2mulzaa32rs_hhll, .Lfunc_end0-test_f2mulzaa32rs_hhll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_f2mulzaa32rs_hhll(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32rs.hhll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

; CHECK: 	.globl	test_f2mulzaa32rs_hllh          // -- Begin function test_f2mulzaa32rs_hllh
; CHECK: 	.type	test_f2mulzaa32rs_hllh,@function
; CHECK-LABEL: test_f2mulzaa32rs_hllh:                 // @test_f2mulzaa32rs_hllh
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulzaa32rs_hllh	d0, d0, d1{{.*}} }
; CHECK: 	{ {{.*}}jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}{{.*}}r0, lr, 0{{.*}} }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_f2mulzaa32rs_hllh, .Lfunc_end1-test_f2mulzaa32rs_hllh
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_f2mulzaa32rs_hllh(i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32rs.hllh(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

; CHECK: 	.globl	test_f2mulzaa32r_hhll           // -- Begin function test_f2mulzaa32r_hhll
; CHECK: 	.type	test_f2mulzaa32r_hhll,@function
; CHECK-LABEL: test_f2mulzaa32r_hhll:                  // @test_f2mulzaa32r_hhll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulzaa32r_hhll	d0, d0, d1{{.*}} }
; CHECK: 	{ {{.*}}jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}{{.*}}r0, lr, 0{{.*}} }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_f2mulzaa32r_hhll, .Lfunc_end2-test_f2mulzaa32r_hhll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_f2mulzaa32r_hhll(i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32r.hhll(<2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

; CHECK: 	.globl	test_f2mulzaa32r_hllh           // -- Begin function test_f2mulzaa32r_hllh
; CHECK: 	.type	test_f2mulzaa32r_hllh,@function
; CHECK-LABEL: test_f2mulzaa32r_hllh:                  // @test_f2mulzaa32r_hllh
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulzaa32r_hllh	d0, d0, d1{{.*}} }
; CHECK: 	{ {{.*}}jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}{{.*}}r0, lr, 0{{.*}} }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_f2mulzaa32r_hllh, .Lfunc_end3-test_f2mulzaa32r_hllh
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_f2mulzaa32r_hllh(i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulzaa32r.hllh(<2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

;===--- Part B: combiner fold F2MULAA(acc=0) -> F2MULZAA ---===============

; test_fold_zero_acc:
; CHECK-NOT: f2mulaa32rs_hhll
; CHECK: 	.globl	test_fold_zero_acc              // -- Begin function test_fold_zero_acc
; CHECK: 	.type	test_fold_zero_acc,@function
; CHECK-LABEL: test_fold_zero_acc:                     // @test_fold_zero_acc
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulzaa32rs_hhll	d0, d0, d1{{.*}} }
; CHECK: 	{ {{.*}}jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}{{.*}}r0, lr, 0{{.*}} }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_fold_zero_acc, .Lfunc_end4-test_fold_zero_acc
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_fold_zero_acc(i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 0, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}
