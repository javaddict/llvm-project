; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: accumulator-form MAC builtins are 3-arg end-to-end.
;
; Bug : the spec defines MULA64_LL / MULS64_LL / FMULA32S_LL as
; `rtd = rtd OP product` (3-operand, rtd is read+written), but the end-to-end
; stack (BuiltinsHaydn.td, IntrinsicsHaydn.td, ISel) was 2-arg, causing
; "too many arguments to function call" when haydn_dsp.h's AE_MULA64_SS_*
; macros were invoked. Every MAC-heavy NatureDSP kernel was blocked.
;
; Fix : the 32 accumulator-form MAC intrinsics are now 3-arg
; (haydn_ternary_intrinsic). The selector emits an OR64 to seed the
; destination with the accumulator value, then the MAC instruction (silicon
; reads rtd as the implicit accumulator per spec slot 1 DR_Read_Port).
;
; Test design: invokes each 3-arg MAC intrinsic with (acc, a, b). Verifies
; the selected MIR contains the MAC mnemonic. If any MAC reverts to 2-arg
; this test fails with a "Callsite was not defined with variable arguments"
; verifier error. If the selector crashes (e.g., OptimizePHI assertion from
; multiple defs of DstReg — see), llc aborts.
;
; References:
; ~/haydn-plans/decisions/-accumulator-mac-3arg-end-to-end-fix.md
; ~/haydn-plans/decisions/-mula64-accumulator-form-3arg-spec.md
; ~/haydn-plans/lessons/isel-multiple-def-dst-reg.md
; spec: Database/haydn_instruction_db.json MULA64_LL/MULS64_LL/FMULA32S_LL

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mulas64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
; CHECK: 	.globl	test_mula64_ss_ll               // -- Begin function test_mula64_ss_ll
; CHECK: 	.type	test_mula64_ss_ll,@function
; CHECK-LABEL: test_mula64_ss_ll:                      // @test_mula64_ss_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}mula64.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_mula64_ss_ll, .Lfunc_end0-test_mula64_ss_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

; CHECK: 	.globl	test_muls64_ss_ll               // -- Begin function test_muls64_ss_ll
; CHECK: 	.type	test_muls64_ss_ll,@function
; CHECK-LABEL: test_muls64_ss_ll:                      // @test_muls64_ss_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}muls64.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_muls64_ss_ll, .Lfunc_end1-test_muls64_ss_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_muls64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

; CHECK: 	.globl	test_mulas64_ss_ll              // -- Begin function test_mulas64_ss_ll
; CHECK: 	.type	test_mulas64_ss_ll,@function
; CHECK-LABEL: test_mulas64_ss_ll:                     // @test_mulas64_ss_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}mulas64.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_mulas64_ss_ll, .Lfunc_end2-test_mulas64_ss_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_mulas64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

; CHECK: 	.globl	test_mulss64_ss_ll              // -- Begin function test_mulss64_ss_ll
; CHECK: 	.type	test_mulss64_ss_ll,@function
; CHECK-LABEL: test_mulss64_ss_ll:                     // @test_mulss64_ss_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}mulss64.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_mulss64_ss_ll, .Lfunc_end3-test_mulss64_ss_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_mulss64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

; CHECK: 	.globl	test_fmula32s_ll                // -- Begin function test_fmula32s_ll
; CHECK: 	.type	test_fmula32s_ll,@function
; CHECK-LABEL: test_fmula32s_ll:                       // @test_fmula32s_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}fmula32s.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_fmula32s_ll, .Lfunc_end4-test_fmula32s_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

; CHECK: 	.globl	test_ff2mula32rs_ll             // -- Begin function test_ff2mula32rs_ll
; CHECK: 	.type	test_ff2mula32rs_ll,@function
; CHECK-LABEL: test_ff2mula32rs_ll:                    // @test_ff2mula32rs_ll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}ff2mula32rs.ll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	test_ff2mula32rs_ll, .Lfunc_end5-test_ff2mula32rs_ll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_ff2mula32rs_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.ll(i64 %acc, <2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

; CHECK: 	.globl	test_f2mulaa32rs_hhll           // -- Begin function test_f2mulaa32rs_hhll
; CHECK: 	.type	test_f2mulaa32rs_hhll,@function
; CHECK-LABEL: test_f2mulaa32rs_hhll:                  // @test_f2mulaa32rs_hhll
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}f2mulaa32rs.hhll	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	test_f2mulaa32rs_hhll, .Lfunc_end6-test_f2mulaa32rs_hhll
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

; CHECK: 	.globl	test_x2fcmula32rs               // -- Begin function test_x2fcmula32rs
; CHECK: 	.type	test_x2fcmula32rs,@function
; CHECK-LABEL: test_x2fcmula32rs:                      // @test_x2fcmula32rs
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ {{.*}}xor32	r0, r0, r0{{.*}} }
; CHECK: 	{ {{.*}}x2fcmula32rs	d0, d1, d2{{.*}} }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	test_x2fcmula32rs, .Lfunc_end7-test_x2fcmula32rs
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
define i64 @test_x2fcmula32rs(i64 %acc, i64 %a, i64 %b) {
  %bc.11 = bitcast i64 %acc to <2 x i32>
  %bc.12 = bitcast i64 %a to <2 x i32>
  %bc.13 = bitcast i64 %b to <2 x i32>
  %call.14 = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %bc.11, <2 x i32> %bc.12, <2 x i32> %bc.13)
  %r = bitcast <2 x i32> %call.14 to i64
  ret i64 %r
}
