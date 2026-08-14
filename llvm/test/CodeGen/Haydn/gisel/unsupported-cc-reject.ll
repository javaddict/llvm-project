; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/call.ll 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/formal.ll 2>&1 | FileCheck %s --check-prefix=FORMAL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/nest.ll 2>&1 | FileCheck %s --check-prefix=NEST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/nest_call.ll 2>&1 | FileCheck %s --check-prefix=NEST_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/return_cc.ll 2>&1 | FileCheck %s --check-prefix=RETCC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/byref.ll 2>&1 | FileCheck %s --check-prefix=BYREF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/byref_call.ll 2>&1 | FileCheck %s --check-prefix=BYREF_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftasync.ll 2>&1 | FileCheck %s --check-prefix=SWIFTASYNC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftasync_call.ll 2>&1 | FileCheck %s --check-prefix=SWIFTASYNC_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftself.ll 2>&1 | FileCheck %s --check-prefix=SWIFTSELF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swifterror.ll 2>&1 | FileCheck %s --check-prefix=SWIFTERROR
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/inalloca.ll 2>&1 | FileCheck %s --check-prefix=INALLOCA
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/inalloca_call.ll 2>&1 | FileCheck %s --check-prefix=INALLOCA_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/preallocated.ll 2>&1 | FileCheck %s --check-prefix=PREALLOC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/ghccc.ll 2>&1 | FileCheck %s --check-prefix=GHCCC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/anyregcc.ll 2>&1 | FileCheck %s --check-prefix=ANYREG
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/cxx_fast_tls.ll 2>&1 | FileCheck %s --check-prefix=CXXFAST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftself_call.ll 2>&1 | FileCheck %s --check-prefix=SWIFTSELF_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swifterror_call.ll 2>&1 | FileCheck %s --check-prefix=SWIFTERROR_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/preserve_all.ll 2>&1 | FileCheck %s --check-prefix=PRESERVE_ALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/preserve_most_call.ll 2>&1 | FileCheck %s --check-prefix=PRESERVE_MOST_CALL
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/preallocated_call.ll 2>&1 | FileCheck %s --check-prefix=PREALLOC_CALL
;
; Role: semantic — unsupported CCs and nest/byref/swift/inalloca ABI fail closed
; before formal/CALLSEQ/return mutation (no silent CC_Haydn reuse). Symmetric
; preflight on calls, definitions/formals, returns, and ABI-affecting flags.

;--- call.ll
declare swiftcc i32 @swift_callee(i32)
define i32 @swiftcc_caller(i32 %x) {
  ; CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  %r = call swiftcc i32 @swift_callee(i32 %x)
  ret i32 %r
}

;--- formal.ll
define swiftcc i32 @swiftcc_def(i32 %x) {
  ; FORMAL: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- nest.ll
define i32 @nest_formal(ptr nest %p, i32 %x) {
  ; NEST: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  %v = load i32, ptr %p
  %r = add i32 %v, %x
  ret i32 %r
}

;--- nest_call.ll
declare i32 @nest_sink(ptr nest, i32)
define i32 @nest_caller(ptr %p, i32 %x) {
  ; NEST_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  %r = call i32 @nest_sink(ptr nest %p, i32 %x)
  ret i32 %r
}

;--- return_cc.ll
define preserve_mostcc i32 @preserve_most_def(i32 %x) {
  ; RETCC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- byref.ll
define i32 @byref_formal(ptr byref([4 x i32]) %p) {
  ; BYREF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  %v = load i32, ptr %p
  ret i32 %v
}

;--- byref_call.ll
declare void @byref_sink(ptr byref([4 x i32]))
define void @byref_caller(ptr %p) {
  ; BYREF_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  call void @byref_sink(ptr byref([4 x i32]) %p)
  ret void
}

;--- swiftasync.ll
define void @swiftasync_formal(ptr swiftasync %ctx) {
  ; SWIFTASYNC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- swiftasync_call.ll
declare void @swiftasync_sink(ptr swiftasync)
define void @swiftasync_caller(ptr %ctx) {
  ; SWIFTASYNC_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  call void @swiftasync_sink(ptr swiftasync %ctx)
  ret void
}

;--- swiftself.ll
define void @swiftself_formal(ptr swiftself %self) {
  ; SWIFTSELF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- swifterror.ll
define void @swifterror_formal(ptr swifterror %err) {
  ; SWIFTERROR: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- inalloca.ll
define void @inalloca_formal(ptr inalloca(i32) %p) {
  ; INALLOCA: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- inalloca_call.ll
; Shared setArgFlags may assert on inalloca call attrs; assertion or lower failure is fail-closed.
declare void @inalloca_sink(ptr inalloca(i32))
define void @inalloca_caller(ptr %p) {
  ; INALLOCA_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate|Assertion|isByVal}}
  call void @inalloca_sink(ptr inalloca(i32) %p)
  ret void
}

;--- preallocated.ll
define void @preallocated_formal(ptr preallocated(i32) %p) {
  ; PREALLOC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- ghccc.ll
define ghccc i32 @ghccc_def(i32 %x) {
  ; GHCCC: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- anyregcc.ll
define anyregcc i32 @anyregcc_def(i32 %x) {
  ; ANYREG: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- cxx_fast_tls.ll
define cxx_fast_tlscc i32 @cxx_fast_tls_def(i32 %x) {
  ; CXXFAST: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- swiftself_call.ll
declare void @swiftself_sink(ptr swiftself)
define void @swiftself_caller(ptr %p) {
  ; SWIFTSELF_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  call void @swiftself_sink(ptr swiftself %p)
  ret void
}

;--- swifterror_call.ll
declare void @swifterror_sink(ptr swifterror)
define void @swifterror_caller(ptr swifterror %err) {
  ; SWIFTERROR_CALL: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower|unable to translate}}
  call void @swifterror_sink(ptr swifterror %err)
  ret void
}

;--- preserve_all.ll
define preserve_allcc i32 @preserve_all_def(i32 %x) {
  ; PRESERVE_ALL: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- preserve_most_call.ll
declare preserve_mostcc i32 @preserve_most_sink(i32)
define i32 @preserve_most_caller(i32 %x) {
  ; PRESERVE_MOST_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  %r = call preserve_mostcc i32 @preserve_most_sink(i32 %x)
  ret i32 %r
}

;--- preallocated_call.ll
; Generic setArgFlags may assert on preallocated call attrs; assertion or
; clean lower failure is fail-closed (no silent CC_Haydn reuse / partial CALLSEQ).
declare token @llvm.call.preallocated.setup(i32)
declare ptr @llvm.call.preallocated.arg(token, i32)
declare void @preallocated_sink(ptr preallocated(i32))
define void @preallocated_caller() {
  ; PREALLOC_CALL: {{unable to lower|failed to lower|Cannot select|unable to translate|Assertion|isByVal|preallocated}}
  %t = call token @llvm.call.preallocated.setup(i32 1)
  %a = call ptr @llvm.call.preallocated.arg(token %t, i32 0) preallocated(i32)
  call void @preallocated_sink(ptr preallocated(i32) %a) ["preallocated"(token %t)]
  ret void
}
