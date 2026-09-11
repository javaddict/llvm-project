; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=prologepilog -verify-machineinstrs < %s \
; RUN:     | FileCheck %s

; Role: MIR — PEI FrameLowering::eliminateCallFramePseudoInstr owns
; ADJCALLSTACKDOWN/UP. Peer: RISCVFrameLowering.cpp:1868.
;
; REGRESSION TEST: ExpandPseudos must not be a second owner. After PEI the
; outgoing stack-arg bracket is real SUBI32/ADDI32 of SP; ADJCALLSTACK* are
; gone. Zero-size adjustments are no-ops (no extra SP add/sub around the
; call besides the frame).

declare i32 @many_args(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @caller_stack_args_pei() {
  %r = call i32 @many_args(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
                           i32 8, i32 9)
  ret i32 %r
}

; CHECK-LABEL: name: caller_stack_args_pei
; CHECK-NOT: ADJCALLSTACK
; CHECK: SUBI32
; CHECK: JALR_CALL
; CHECK: ADDI32
