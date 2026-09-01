; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -O2 -stop-after=virtregrewriter < %s | FileCheck --check-prefix=RA %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -O2 < %s | FileCheck --check-prefix=ASM %s

; Role: M23 tied old-dest. Golden Behavior for MOVEI_H/L, MOVF64, MOVT64
; READS the destination (DR_Read_Port ∩ DR_Write_Port = rtd):
;   MOVT64  rtd = (SFR == 4'b1111) ? rsd : rtd
;   MOVF64  rtd = (SFR == 4'b0000) ? rsd : rtd
;   MOVEI_H rtd = {imm32, rtd[31:00]}
; The logical defs carry Constraints "$rd = $rd_old"; the IR intrinsics are
; unary/imm-only (public API unchanged), so the selector seeds $rd_old undef
; from the destination and TwoAddressInstructionPass rewrites the tie onto
; the def register. The machine-level RMW read is then material: chained
; same-register cmovs serialize (WAW + tied read), matching X2MOVT32/X4MOVT16
; which already model the tie.

declare i64 @llvm.haydn.movei.h(i32)
declare i64 @llvm.haydn.movei.l(i32)
declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

; MIR-LABEL: name: sel_unary_tie
; MIR: %[[D1:[0-9]+]]:dr64 = MOVT64 undef %[[D1]], %{{[0-9]+}}, implicit $sfr
; MIR: %[[D2:[0-9]+]]:dr64 = MOVF64 undef %[[D2]], %[[D1]], implicit $sfr
define i64 @sel_unary_tie(i64 %a) {
  %c1 = call i64 @llvm.haydn.movt64(i64 %a)
  %c2 = call i64 @llvm.haydn.movf64(i64 %c1)
  ret i64 %c2
}

; MIR-LABEL: name: sel_movei_tie
; MIR: %[[D1:[0-9]+]]:dr64 = MOVEI_H undef %[[D1]], 4660
; MIR: %[[D2:[0-9]+]]:dr64 = MOVEI_L undef %[[D2]], 22136
define i64 @sel_movei_tie() {
  %m1 = call i64 @llvm.haydn.movei.h(i32 4660)
  %m2 = call i64 @llvm.haydn.movei.l(i32 22136)
  %x = xor i64 %m1, %m2
  ret i64 %x
}

; Post-RA the tie rewrites onto the def register: the chained reads of old
; $d0 are explicit machine operands, not implicit-scheduler guesses.
; RA-LABEL: name: sel_unary_tie
; RA: $d0 = MOVT64 undef $d0, killed $d0, implicit $sfr
; RA: $d0 = MOVF64 undef $d0, killed $d0, implicit $sfr
; RA-LABEL: name: sel_movei_tie
; RA: $d{{[0-9]+}} = MOVEI_H undef $d{{[0-9]+}}, 4660
; RA: $d{{[0-9]+}} = MOVEI_L undef $d{{[0-9]+}}, 22136

; ASM keeps the golden 2-op spelling; the tie never changes the wire.
; ASM-LABEL: sel_unary_tie:
; ASM: movt64
; ASM: movf64
; ASM-LABEL: sel_movei_tie:
; ASM: movei_h
; ASM: movei_l
