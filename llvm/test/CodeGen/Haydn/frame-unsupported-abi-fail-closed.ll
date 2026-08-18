; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/i128_formal.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=I128
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/interrupt.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ISR
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/naked.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=NAKED
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/ssp.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SSP
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/inreg.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=INREG
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/nest.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=NEST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftself.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWIFT
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/byref.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=BYREF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/half.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=HALF
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/control.ll
;
; Role: semantic — advertised frame/CC seats that have no product ABI
; fail closed at CallLowering (first firewall). i128 is not a CC type
; (reject before splitToValueTypes invents 2xi64). interrupt / naked /
; stack-protector have no ISR or protector ABI. inreg / nest / swift* /
; byref have no Haydn seat. half is storage/libcall, not a CC type.
; musttail stays fail-closed separately. Soft tail is ordinary JAL_W + RET.
;
; PEI last-line pin if those attributes still reach layout lives in
; frame-unsupported-abi-fail-closed.mir.

;--- i128_formal.ll
define i128 @i128_formal(i128 %x) {
  ; I128: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i128 %x
}

;--- interrupt.ll
define void @isr() "interrupt"="machine" {
  ; ISR: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- naked.ll
define void @naked_fn() naked {
  ; NAKED: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- ssp.ll
define void @ssp_fn() ssp {
  ; SSP: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret void
}

;--- inreg.ll
define i32 @inreg_formal(i32 inreg %x) {
  ; INREG: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i32 %x
}

;--- nest.ll
define i32 @nest_formal(ptr nest %p) {
  ; NEST: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  %v = load i32, ptr %p
  ret i32 %v
}

;--- swiftself.ll
define i32 @swiftself_formal(ptr swiftself %p) {
  ; SWIFT: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  %v = load i32, ptr %p
  ret i32 %v
}

;--- byref.ll
%T = type { i32 }
define i32 @byref_formal(ptr byref(%T) %p) {
  ; BYREF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  %v = load i32, ptr %p
  ret i32 %v
}

;--- half.ll
define half @half_formal(half %x) {
  ; HALF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret half %x
}

;--- control.ll
define i32 @i32_ok(i32 %x) {
  ret i32 %x
}
