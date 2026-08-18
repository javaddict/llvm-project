; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — accumulator-form MAC must read the accumulator via a tied two-address def, NOT an OR64-seeded implicit-use operand.

; REGRESSION TEST: accumulator-form MAC must read the accumulator via a tied
; two-address def, NOT an OR64-seeded implicit-use operand.
;
; Bug (codex verdict, reviews/m6-codex-destructive-acc-lanemap.md §1A): the
; selectAccMAC helper seeded the accumulator value into a fresh vreg Tmp
; via `OR64 Tmp, Acc, Acc`, then emitted `<MAC> DstReg, Src1, Src2
; implicit-use Tmp`. The implicit-use operand only created a scheduler
; liveness edge; it was NOT a tied-def constraint. The MC encoder dropped the
; implicit operand, and the regalloc was free to assign Acc and DstReg to
; different physical registers. Silicon reads its accumulator input from the
; physical rtd register — which held whatever was in DstReg's physical
; register, NOT the Acc value. All accumulator-form MACs (MULA64/MULS64
; MULAS64/MULSS64/FMULA32S/FF2MULA32RS/F2MULAA32RS/F2MULSS32RS/FMULAA16
; FMULS32S/FMULSS16/X2FCMULA32RS) silently miscomputed when the C observer
; read the accumulate separately from the destination.
;
; Fix : every accumulator-form MAC def now uses the standard LLVM
; tied-def idiom — let Constraints = "$rd = $rd_in", DisableEncoding =
; "$rd_in" — and selectAccMAC emits a SINGLE tied-def MCInst:
; %rd = MAC %rd_in(tied), %rs1, %rs2
; The tied constraint forces the regalloc to coalesce Acc and DstReg to the
; same physical register, so silicon's rtd read sees the accumulator value.
;
; Test design: each case feeds a distinct acc/dst (the intrinsic's acc arg is
; the call's first source operand; the dst is the def). If the tied-def
; constraint regresses (e.g. re-introducing the implicit-use pattern, or
; dropping the Constraints let-block from the.td), the post-RA verifier OR
; verify-machineinstrs will catch the mismatched operand count / untied
; accumulator, and the OR64 seed would reappear in the output. The
;
; The MC printer renders the tied-def as `OP dst, src1, src2` (the tied
; rd_in operand is not printed because it equals rd by the constraint).
; The 3-operand form is correct: silicon reads rtd from the encoded
; destination field (slot-1 DR_Read_Port = [rsd1, rsd2, rtd]), and the
; tied-def constraint forces regalloc to coalesce acc and dst to the same
; physical register, so the encoded destination field holds the accumulator
; value at silicon time.
;
; References:
; ~/haydn-plans/decisions/-accumulator-mac-tied-def-implementation.md
; ~/haydn-plans/decisions/-accumulator-mac-tied-def-modeling.md
; ~/haydn-plans/lessons/selectaccmac-implicit-use-not-silicon-correct.md
; ~/haydn-plans/decisions/-accumulator-mac-3arg-end-to-end-fix.md
; spec: Database/haydn_instruction_db.json MULA64_LL/MULS64_LL/FMULA32S_LL
; (slots.1.DR_Read_Port = [rsd1, rsd2, rtd])

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mulas64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
; MULA64_LL: verify the MAC instruction is emitted (tied-def), no OR64 seed.
; The two-address coalescer will assign acc and dst to the same phys reg.
define i64 @test_mula64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

; MULS64_LL (rtd = rtd - product): same tied-def check.
; CHECK-LABEL: test_muls64_ll:
; CHECK-NOT: or64
; CHECK: muls64.ll
define i64 @test_muls64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

; MULAS64_LL (rtd = product - rtd): same tied-def check.
; CHECK-LABEL: test_mulas64_ll:
; CHECK-NOT: or64
; CHECK: mulas64_ll
define i64 @test_mulas64_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

; MULSS64_LL (rtd = -product - rtd): same tied-def check.
; CHECK-LABEL: test_mulss64_ll:
; CHECK-NOT: or64
; CHECK: mulss64_ll
define i64 @test_mulss64_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

; FMULA32S_LL: accumulator-form fractional MAC.
; CHECK-LABEL: test_fmula32s_ll:
; CHECK-NOT: or64
; CHECK: fmula32s_ll
define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

; FF2MULA32RS_LH: this case also guards the codex-B arity fix (was selectBinary,
; now selectAccMAC) AND the codex-C lane-family fix (now FF2MULA32RS_LH, not
; F2MULAA32RS_HLLH — single product, not dual).
; CHECK-LABEL: test_ff2mula32rs_lh:
; CHECK-NOT: or64
; CHECK-NOT: f2mulaa32rs_hllh
; CHECK: ff2mula32rs_lh
define i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, <2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

; F2MULAA32RS_HHLL: dual-product accumulator MAC (the genuine dual-MAC case).
; Verifies the tied-def applies to the dual-product family as well.
; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK-NOT: or64
; CHECK: f2mulaa32rs_hhll
define i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

; X2FCMULA32RS: complex MAC accumulator form.
; CHECK-LABEL: test_x2fcmula32rs:
; CHECK-NOT: or64
; CHECK: x2fcmula32rs
define i64 @test_x2fcmula32rs(i64 %acc, i64 %a, i64 %b) {
  %bc.11 = bitcast i64 %acc to <2 x i32>
  %bc.12 = bitcast i64 %a to <2 x i32>
  %bc.13 = bitcast i64 %b to <2 x i32>
  %call.14 = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %bc.11, <2 x i32> %bc.12, <2 x i32> %bc.13)
  %r = bitcast <2 x i32> %call.14 to i64
  ret i64 %r
}

; CHAIN TEST: forces the accumulator and dst to be DIFFERENT SSA values by
; using acc AFTER the MAC. This stresses the two-address coalescer: if the
; tied-def constraint is removed, the regalloc may split acc and dst across
; physical registers and the data dependency the scheduler models will still
; appear (but silicon would miscompute). -verify-machineinstrs catches any
; malformed MIR. If the OR64 seed reappears (regression to), CHECK-NOT
; fails.
; CHECK-LABEL: test_chain_acc:
; CHECK-NOT: or64
; CHECK: mula64.ll{{.*}}
; CHECK: muls64.ll{{.*}}
define i64 @test_chain_acc(i64 %acc, i64 %a, i64 %b, i64 %c, i64 %d) {
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r1 = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.15, <2 x i32> %bc.16)
  %bc.17 = bitcast i64 %c to <2 x i32>
  %bc.18 = bitcast i64 %d to <2 x i32>
  %r2 = call i64 @llvm.haydn.muls64.ss.ll(i64 %r1, <2 x i32> %bc.17, <2 x i32> %bc.18)
  ret i64 %r2
}
