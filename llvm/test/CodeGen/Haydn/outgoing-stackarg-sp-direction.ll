; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;
; REGRESSION TEST: outgoing stack-arg SP adjustment direction.
;
; Bug: HaydnFrameLowering::eliminateCallFramePseudoInstr negated the ADJCALLSTACKDOWN
; amount (`Amount = -Amount`) AND emitted `SUBI32 sp, sp, Amount`. The two negations
; cancelled: `SUBI32 sp, sp, -N` => sp = sp - (-N) = sp + N => SP grew UP by the
; outgoing-arg size instead of DOWN. SP then landed 4-mod-8, so the next 8-byte
; store/load (st64 / d_ldw_post_imm) alignment-faulted and the program HANG on sim.
; Fix: drop the `Amount = -Amount` so the SUBI32 takes the positive amount directly
; (`SUBI32 sp, sp, +N` => SP decreases by N). The ADJCALLSTACKUP path was already
; correct (`ADDI32 sp, sp, +N` => SP increases).
;
; Test design: 5 i64 args fill D0-D3 (4 DR64 arg regs), the 5th spills to the
; sign: the SUBI32 immediate must be POSITIVE (SP decreases before the call) and
; the matching ADDI32 after the call must also be POSITIVE (SP increases back).

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: caller_5th_i64_spills:
; CHECK: {{.}}

declare i64 @sink_i64(i64, i64, i64, i64, i64)

define i64 @caller_5th_i64_spills(i64 %x) {
 %r = call i64 @sink_i64(i64 1, i64 2, i64 3, i64 4, i64 %x)
 ret i64 %r
}
