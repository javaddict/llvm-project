; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — CB-128: @llvm.returnaddress must select (depth 0 → lr; depth>0 → 0).

; CB-128: @llvm.returnaddress must select (depth 0 → lr; depth>0 → 0).

define ptr @ra_depth0() {
; CHECK-LABEL: ra_depth0:
; CHECK: {{move32|lr}}
; CHECK: jalr
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

define ptr @ra_depth1() {
; CHECK-LABEL: ra_depth1:
; CHECK: jalr
  %r = call ptr @llvm.returnaddress(i32 1)
  ret ptr %r
}

declare ptr @llvm.returnaddress(i32 immarg)
