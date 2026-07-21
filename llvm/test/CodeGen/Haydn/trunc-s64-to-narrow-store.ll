; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     < %s 2>&1 | FileCheck %s

; REGRESSION TEST: G_TRUNC s64 -> s8/s16 must materialize a GPR32 (MOVE32_DR_L)
; for the narrow result, NOT alias the narrow Dst to the DR64 source via
; replaceRegWith. Before the fix the G_TRUNC selector's sub-32-bit branch did
; constrainGenericRegister(Src, DR64); replaceRegWith(Dst s8/s16, Src s64)
; leaving the DR64 bank on the narrow result. When that result was stored via
; ST32 (whose data operand requires GPR32), the machine verifier aborted:
; *** Bad machine code: Illegal virtual register for instruction ***
; instruction: ST32 %..:dr64, %..:gpr32, 0 :: (store (s8) into...)
; operand 0: %..:dr64 Expected a GPR32 register, but got a DR64 register
; Surfaced by yarpgen differential seeds (store of a truncated 64-bit value).
;
; Test design: trunc i64 -> i8/i16 then store. The -verify-machineinstrs on the
; RUN line is the regression probe: if the DR64 leak returns, llc aborts with a
; non-zero exit and FileCheck sees no output -> test fails. The s64 -> s32 path
; already used MOVE32_DR_L; the fix extends the same extraction to s64 -> s8/s16.
; (Note: the lane-store fusion pass may fold MOVE32_DR_L + ST32 into a
; single lane-store, so we do NOT assert on move32_dr_l textually -- the verify
; pass is the real check.)

define void @trunc_s64_to_i8_store(i64 %v, ptr %p) nounwind {
; CHECK-LABEL: trunc_s64_to_i8_store:
  %t = trunc i64 %v to i8
  store i8 %t, ptr %p
  ret void
}

define void @trunc_s64_to_i16_store(i64 %v, ptr %p) nounwind {
; CHECK-LABEL: trunc_s64_to_i16_store:
  %t = trunc i64 %v to i16
  store i16 %t, ptr %p
  ret void
}

; Non-store consumer (arithmetic): the extracted GPR32 must also feed normal
; GPR32 uses, not leak a DR64.
define i8 @trunc_s64_to_i8_arith(i64 %v) nounwind {
; CHECK-LABEL: trunc_s64_to_i8_arith:
  %t = trunc i64 %v to i8
  %r = add i8 %t, 1
  ret i8 %r
}
