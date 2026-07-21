; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; InstCombine turns sext(trunc(i64)) into (ashr (shl x, 32), 32).
; The G_ASHR-by-32 fast path must be a single SRA64 (full DR64 result), NOT
; SRA64 + extract + pack {V,V} (that is not a 32→64 sext).

define i64 @sext_low32(i64 %x) {
entry:
  %t = trunc i64 %x to i32
  %s = sext i32 %t to i64
  ret i64 %s
}

; CHECK-LABEL: sext_low32:
; Prefer SRA64 for the ashr-by-32 half of the idiom (after shl-by-32).
; Must NOT re-pack both lanes to the same extracted half.
; CHECK: sra64
; CHECK-NOT: mov_gpr_to_dr64
