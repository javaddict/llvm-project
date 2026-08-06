; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - %s | FileCheck %s

; Role: semantic — G_TRUNC from s8 to s1 must be legal for _Bool operations.

; REGRESSION TEST: G_TRUNC from s8 to s1 must be legal for _Bool operations.
;
; Bug: Compiling functions with _Bool parameters caused an "unable to legalize
; instruction: %11:_(s1) = G_TRUNC %10:_(s8)" fatal error. The root cause
; was that the legalizer declared G_TRUNC legal for {s1, s32} but not {s1, s8}.
; When _Bool values are stored to memory (as i8 per C ABI) and loaded back
; the IRTranslator generates G_LOAD (s8) followed by G_TRUNC (s8 -> s1).
;
; Fix: Added {S1, S8} to G_TRUNC and {S8, S1} to G_ZEXT in HaydnLegalizerInfo.
; Added s1->s8 zero-extend (AND with 1) to the instruction selector.
;
; If G_TRUNC s8->s1 regresses, llc will crash with "unable to legalize instruction".
; Do NOT update CHECK lines without understanding the root cause.

; Basic _Bool operations (and, or, not)

define zeroext i1 @bool_and(i1 zeroext %a, i1 zeroext %b) {
; CHECK-LABEL: bool_and:
; CHECK: and32
  %result = and i1 %a, %b
  ret i1 %result
}

define zeroext i1 @bool_or(i1 zeroext %a, i1 zeroext %b) {
; CHECK-LABEL: bool_or:
; CHECK: or32
  %result = or i1 %a, %b
  ret i1 %result
}

define zeroext i1 @bool_not(i1 zeroext %a) {
; CHECK-LABEL: bool_not:
; Soft-zero uses xor32 r0,r0,r0; pin the real ones-complement not32 + mask.
; CHECK: not32
; CHECK: and32
  %result = xor i1 %a, true
  ret i1 %result
}

; Complex _Bool expression from the original bug report
define zeroext i1 @bool_complex(i1 zeroext %a, i1 zeroext %b) {
; CHECK-LABEL: bool_complex:
; i8 store/load + trunc s8->s1 path that used to legalize-fail.
; CHECK-DAG: st8
; CHECK-DAG: ldu8
; CHECK-DAG: and32
entry:
  %a.addr = alloca i8, align 1
  %b.addr = alloca i8, align 1
  %storedv = zext i1 %a to i8
  store i8 %storedv, ptr %a.addr, align 1
  %storedv1 = zext i1 %b to i8
  store i8 %storedv1, ptr %b.addr, align 1
  %0 = load i8, ptr %a.addr, align 1
  %loadedv = trunc i8 %0 to i1
  br i1 %loadedv, label %land.lhs.true, label %lor.rhs

land.lhs.true:
  %1 = load i8, ptr %b.addr, align 1
  %loadedv2 = trunc i8 %1 to i1
  br i1 %loadedv2, label %lor.end, label %lor.rhs

lor.rhs:
  %2 = load i8, ptr %a.addr, align 1
  %loadedv3 = trunc i8 %2 to i1
  %lnot = xor i1 %loadedv3, true
  br label %lor.end

lor.end:
  %3 = phi i1 [ true, %land.lhs.true ], [ %lnot, %lor.rhs ]
  ret i1 %3
}

; _Bool select
define zeroext i1 @bool_select(i1 zeroext %a, i1 zeroext %b, i1 zeroext %c) {
; CHECK-LABEL: bool_select:
; CHECK: movt32
  %result = select i1 %a, i1 %b, i1 %c
  ret i1 %result
}

; _Bool equality
define zeroext i1 @bool_eq(i1 zeroext %a, i1 zeroext %b) {
; CHECK-LABEL: bool_eq:
; CHECK: seq32
  %result = icmp eq i1 %a, %b
  ret i1 %result
}
