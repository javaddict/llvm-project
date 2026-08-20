; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/i128_formal.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=I128F
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/i128_call.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=I128C
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
; RUN:     -verify-machineinstrs -o /dev/null %t/inreg_call.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=INREGC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/half_formal.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=HALF
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/half_call.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=HALFC
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/nest.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=NEST
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/swiftself.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWIFT
; RUN: not --crash llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/byref.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=BYREF
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - \
; RUN:     %t/control.ll | FileCheck %s --check-prefix=CTL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - \
; RUN:     %t/byval_formal.ll | FileCheck %s --check-prefix=BYVAL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - \
; RUN:     %t/frameaddress0.ll | FileCheck %s --check-prefix=FA0
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/frameaddress1.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=FA1
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/readcycle.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=CYCLE
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/read_register.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=RDREG
;
; Role: semantic — advertised ABI fail-closed residuals.
; i128 has no product CC (reject before splitToValueTypes invents 2×i64).
; half is not a CC type (soft-float product is float/double).
; interrupt / naked / ssp have no ISR or protector ABI.
; inreg / nest / swift* / byref are not Haydn seats (were silently ignored).
; Formal byval is the defined stack-indirect pointer path (not fail-closed).
; legal musttail sibcall is JAL_W_MSP (musttail-reject.ll);
; ineligible musttail stays fail-closed in tailcall-isr-fail-closed.ll.
; frameaddress depth 0 is FP; depth>0 and cycle/named-register I/O have
; no golden product (legal-then-unselectable abort closed).

;--- i128_formal.ll
define i128 @i128_formal(i128 %x) {
  ; I128F: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret i128 %x
}

;--- i128_call.ll
declare void @i128_sink(i128)
define void @i128_caller() {
  ; I128C: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  call void @i128_sink(i128 0)
  ret void
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

;--- inreg_call.ll
declare i32 @inreg_sink(i32 inreg)
define i32 @inreg_caller(i32 %x) {
  ; INREGC: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  %r = call i32 @inreg_sink(i32 inreg %x)
  ret i32 %r
}

;--- half_formal.ll
define half @half_formal(half %x) {
  ; HALF: {{unable to lower arguments|unable to lower function|unable to lower|failed to lower}}
  ret half %x
}

;--- half_call.ll
declare void @half_sink(half)
define void @half_caller(half %x) {
  ; HALFC: {{unable to lower|failed to lower|Cannot select|unable to translate}}
  call void @half_sink(half %x)
  ret void
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

;--- control.ll
define i64 @i64_ok(i64 %x) {
; CTL-LABEL: name: i64_ok
; CTL: RET
  ret i64 %x
}

;--- byval_formal.ll
%struct.two = type { i32, i32 }
define i32 @byval_formal_ok(ptr byval(%struct.two) %p) {
; BYVAL-LABEL: name: byval_formal_ok
; BYVAL: RET
  %v = load i32, ptr %p
  ret i32 %v
}

;--- frameaddress0.ll
define ptr @frameaddress0() {
; FA0-LABEL: name: frameaddress0
; FA0: COPY $r14
; FA0: RET
  %p = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %p
}
declare ptr @llvm.frameaddress.p0(i32)

;--- frameaddress1.ll
define ptr @frameaddress1() {
  ; FA1: {{unable to legalize|cannot select|no walkable frame chain}}
  %p = call ptr @llvm.frameaddress.p0(i32 1)
  ret ptr %p
}
declare ptr @llvm.frameaddress.p0(i32)

;--- readcycle.ll
define i64 @readcycle() {
  ; CYCLE: {{unable to legalize instruction: .*G_READCYCLECOUNTER|unable to legalize}}
  %t = call i64 @llvm.readcyclecounter()
  ret i64 %t
}
declare i64 @llvm.readcyclecounter()

;--- read_register.ll
define i32 @read_register() {
  ; RDREG: {{unable to legalize instruction: .*G_READ_REGISTER|unable to legalize}}
  %v = call i32 @llvm.read_register.i32(metadata !0)
  ret i32 %v
}
declare i32 @llvm.read_register.i32(metadata)
!0 = !{!"r1"}
