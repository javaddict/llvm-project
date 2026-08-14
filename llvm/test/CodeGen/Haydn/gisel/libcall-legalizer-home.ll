; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — legalizer libcallFor / custom mul owns div-rem and s64 mul.
; ISel must not invent LIBCALL_* residual pseudos.
; FP libcall surface (the 13 former ICEs, half convert, G_IS_FPCLASS) lives in
; libcall-legalizer-fp-surface.ll — this file stays the integer home.

; ISEL-LABEL: name: sdiv_i32_legalizer
; ISEL-NOT: LIBCALL_
; ISEL: JAL_W
; ASM-LABEL: sdiv_i32_legalizer:
; ASM: jal{{.*}}__divsi3
define i32 @sdiv_i32_legalizer(i32 %a, i32 %b) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; ISEL-LABEL: name: udiv_i32_legalizer
; ISEL-NOT: LIBCALL_
; ISEL: JAL_W
; ASM-LABEL: udiv_i32_legalizer:
; ASM: jal{{.*}}__udivsi3
define i32 @udiv_i32_legalizer(i32 %a, i32 %b) {
  %r = udiv i32 %a, %b
  ret i32 %r
}

; ISEL-LABEL: name: srem_i32_legalizer
; ISEL-NOT: LIBCALL_
; ASM-LABEL: srem_i32_legalizer:
; ASM: jal{{.*}}__modsi3
define i32 @srem_i32_legalizer(i32 %a, i32 %b) {
  %r = srem i32 %a, %b
  ret i32 %r
}

; ISEL-LABEL: name: urem_i32_legalizer
; ISEL-NOT: LIBCALL_
; ASM-LABEL: urem_i32_legalizer:
; ASM: jal{{.*}}__umodsi3
define i32 @urem_i32_legalizer(i32 %a, i32 %b) {
  %r = urem i32 %a, %b
  ret i32 %r
}

; ISEL-LABEL: name: mul_i64_native
; ISEL-NOT: LIBCALL_MUL64
; ISEL-NOT: __muldi3
; ASM-LABEL: mul_i64_native:
; ASM-NOT: __muldi3
define i64 @mul_i64_native(i64 %a, i64 %b) {
  %r = mul i64 %a, %b
  ret i64 %r
}
