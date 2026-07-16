; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: MOVE32_DR_L / MOVE32_DR_H — DR64 half-lane -> GPR32 move.
;
; Context: these are the first *real* DR->GPR cross-bank moves in the Haydn
; ISA (added Rev 2,; haydn_instruction_db.json "MOVE32_DR_L"
; "MOVE32_DR_H"). Before this, the only cross-bank path was the MOV_DR64_TO_GPR
; pseudo, a SP-spill shim (Haydn has no native cross-bank move at that layer
; ISA-10). The new slot-0 ALU ops extract one 32-bit lane directly.
;
; Test design: each intrinsic takes a DR64 and returns a GPR32 half-lane.
; The result is returned, forcing the intrinsic to survive DCE. If selection
; regresses (falls back to the SP-spill shim or a libcall), the move32_dr_*
; mnemonics vanish.

declare i32 @llvm.haydn.move32.dr.l(i64)
declare i32 @llvm.haydn.move32.dr.h(i64)

; CHECK-LABEL: test_move32_dr_l:
; CHECK:       move32_dr_l
define i32 @test_move32_dr_l(i64 %x) {
  %r = call i32 @llvm.haydn.move32.dr.l(i64 %x)
  ret i32 %r
}

; CHECK-LABEL: test_move32_dr_h:
; CHECK:       move32_dr_h
define i32 @test_move32_dr_h(i64 %x) {
  %r = call i32 @llvm.haydn.move32.dr.h(i64 %x)
  ret i32 %r
}
