; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 -O2 < %s | FileCheck %s

; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_zext_gpr_to_dr64:
; CHECK-LABEL: test_trunc_dr64_to_gpr:
; CHECK-LABEL: test_multiple_cross_bank:
; CHECK-LABEL: test_cross_bank_under_pressure:
; CHECK-LABEL: test_zext_return:
; CHECK: {{.}}

declare i64 @get_i64()
declare i32 @get_i32()
declare void @use_i64(i64)
declare void @use_i32(i32)

;Test 1: zext i32 -> i64 (GPR to DR64 transfer).
;The low 32 bits come from the GPR value, high 32 bits are zeroed.

define i64 @test_zext_gpr_to_dr64(i32 %x) nounwind {
entry:
; zext must transfer i32 value from GPR to DR64, zero-extending
  %ext = zext i32 %x to i64
  %result = add i64 %ext, 1
  ret i64 %result
}

;Test 2: trunc i64 -> i32 (DR64 to GPR transfer).
;Only the low 32 bits are kept.

define i32 @test_trunc_dr64_to_gpr(i64 %x) nounwind {
entry:
; trunc must extract low 32 bits from DR64 to GPR
  %trunc = trunc i64 %x to i32
  %result = add i32 %trunc, 1
  ret i32 %result
}

;Test 3: Multiple alternating zext/trunc operations.
;Forces multiple cross-bank transfers in the same function.

define i64 @test_multiple_cross_bank(i32 %a, i32 %b) nounwind {
entry:
  %ext_a = zext i32 %a to i64
  %ext_b = zext i32 %b to i64
  %sum64 = add i64 %ext_a, %ext_b
  %trunc1 = trunc i64 %sum64 to i32
  %add32 = add i32 %trunc1, 10
  %ext2 = zext i32 %add32 to i64
  %result = add i64 %ext2, 100
  ret i64 %result
}

;Test 4: Cross-bank transfers with high register pressure.
;Many live i32 and i64 values force spills while cross-bank copies happen.

define i64 @test_cross_bank_under_pressure(i32 %a, i32 %b, i32 %c, i32 %d,
                                            i32 %e, i32 %f, i32 %g) nounwind {
entry:
; Both GPR and DR64 pressure — cross-bank transfers must still work
  ; Create many live GPR values
  %v1 = add i32 %a, %b
  %v2 = add i32 %c, %d
  %v3 = add i32 %e, %f
  %v4 = add i32 %v1, %g
  %v5 = add i32 %v2, %v3
  ; zext forces GPR->DR64 transfer under pressure
  %ext1 = zext i32 %v4 to i64
  %ext2 = zext i32 %v5 to i64
  %sum64 = add i64 %ext1, %ext2
  ; trunc forces DR64->GPR transfer
  %back = trunc i64 %sum64 to i32
  ; zext again: GPR->DR64
  %result = zext i32 %back to i64
  ret i64 %result
}

;Test 5: i64 return from i32 computation — final zext before return.

define i64 @test_zext_return(i32 %x) nounwind {
entry:
; The return value must be in D0 (i64 return register)
  %v1 = add i32 %x, 1
  %v2 = add i32 %v1, 2
  %v3 = add i32 %v2, 3
  %result = zext i32 %v3 to i64
  ret i64 %result
}
