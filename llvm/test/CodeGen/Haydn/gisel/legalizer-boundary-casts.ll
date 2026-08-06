; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - 2>&1 \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - 2>&1 \
; RUN:   | FileCheck %s

; Role: MIR — T7.1: after legalizer, nested cast chains must pass -verify-machineinstrs with no abort.

; T7.1: after legalizer, nested cast chains must pass -verify-machineinstrs
; with no abort. PostLegalizer no longer runs sanitizeCastCopies; producers
; (cast_combines / legalizer artifacts) own the type boundary.

; CHECK-LABEL: name: nested_bool_long_roundtrip

define i64 @nested_bool_long_roundtrip(i1 zeroext %b) nounwind {
entry:
  %z = zext i1 %b to i32
  %e = sext i32 %z to i64
  %t = trunc i64 %e to i32
  %e2 = zext i32 %t to i64
  ret i64 %e2
}

; CHECK-LABEL: name: double_zext_chain
define i64 @double_zext_chain(i8 zeroext %v) nounwind {
entry:
  %a = zext i8 %v to i16
  %b = zext i16 %a to i32
  %c = zext i32 %b to i64
  ret i64 %c
}
