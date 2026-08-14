; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — CallLowering emits real JAL_W with the call-preserved regmask.
;
; REGRESSION TEST: direct calls must not be PseudoCALL for ExpandPseudos to
; paper a missing regmask. Peer: AArch64CallLowering.cpp:1079,1192 (BL +
; addRegMask). HaydnCallLowering.cpp emits JAL_W + csr_haydn.
;
; If PseudoCALL returns, ISEL matches it and RA/post-RA cleanup can miss
; caller-saved clobbers (R12).

declare i32 @callee(i32)

define i32 @direct_call_jal_w(i32 %a) {
  %r = call i32 @callee(i32 %a)
  ret i32 %r
}

; ISEL-LABEL: name: direct_call_jal_w
; ISEL: JAL_W
; ISEL-SAME: csr_haydn
; ISEL-NOT: {{PseudoCALL[^I]}}
; ASM-LABEL: direct_call_jal_w:
; ASM: jal{{.*}}callee
