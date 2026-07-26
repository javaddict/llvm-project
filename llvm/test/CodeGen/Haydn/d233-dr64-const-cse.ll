; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST : MOV_GPR_TO_DR64 with identical constant sources
; (ADDI32 r0, C for both halves) must be CSE'd when built multiple times.
; The expansion of MOV_GPR_TO_DR64 is a 4-instruction stack round-trip
; (SUBI32+ST32+ST32+LD64); CSE eliminates the duplicate.
;
; Test: build the sign-extend of the same i32 value twice via zext to i64.
; The selector emits MOV_GPR_TO_DR64 for each; CSE replaces the second
; with OR64 (DR64 copy).

define i64 @cse_test(i32 %x) nounwind {
; CHECK-LABEL: cse_test:
; At most one "subi32 sp, sp, 8" from MOV_GPR_TO_DR64 expansion.
; (If CSE fires, duplicates are eliminated.)
; CHECK: { {{.*}}sext32t64 d0, r1{{.*}} }
; CHECK-NOT: subi32{{.*}} sp, sp, 8
entry:
  %a = zext i32 %x to i64
  %b = zext i32 %x to i64
  %c = add i64 %a, %b
  ret i64 %c
}
