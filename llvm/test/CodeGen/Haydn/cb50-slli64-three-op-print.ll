; RUN: llc -mtriple=haydn-unknown-elf -global-isel -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — residual (.s path): SLLI64/SRLI64 must print the DB 3-operand form `slli64 dN, dN, imm` so BundleSim's haydn frontend binds CC_D_DI correctly.

; residual (.s path): SLLI64/SRLI64 must print the DB 3-operand form
; `slli64 dN, dN, imm` so BundleSim's haydn frontend binds CC_D_DI correctly.
; The old 2-op print `slli64 dN, imm` made ISS treat the immediate as rsd.
;
; This IR forces the schoolbook half-extract pattern (<<32 then >>32) used by
; u64 mul lowering / hash mix (cb50_mixhash_mul).

define i64 @half_zext_lo(i32 %lo) {
entry:
  %z = zext i32 %lo to i64
  ret i64 %z
}

; CHECK-LABEL: half_zext_lo:
; CHECK: slli64 {{d[0-9]+}}, {{d[0-9]+}}, 32
; CHECK: srli64 {{d[0-9]+}}, {{d[0-9]+}}, 32
; CHECK-NOT: slli64 {{d[0-9]+}}, 32
