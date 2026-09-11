; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -o - %s \
; RUN:     | FileCheck %s --check-prefix=ASM
;
; AIE getCallOpcode is JAL vs JAL_IND (AIE1InstrInfo.cpp:688). Haydn ISel
; emits the general call (LOAD_ADDR HI12/LO20 + JALR_CALL). Short CallSImm20
; JAL is a later encoding relaxation when |disp| fits. Unlike RISC-V
; relaxCall (which deletes AUIPC and drops a fetch cycle), Haydn must keep
; the committed packet/cycle grid: vacated LUI/ADDI members become same-row
; NOPs. Not a subtarget feature and not an LLD veneer.

declare i32 @ext(i32)
declare void @callee(i32)

; ISEL-LABEL: name: call_ext
; ISEL: LOAD_ADDR
; ISEL: JALR_CALL
; ISEL-NOT: JAL_W
define i32 @call_ext(i32 %x) {
  %r = call i32 @ext(i32 %x)
  ret i32 %r
}

; ISEL-LABEL: name: musttail_ext
; ISEL: JAL_TCO
; ISEL-NOT: RET
define void @musttail_ext(i32 %x) nounwind {
  musttail call void @callee(i32 %x)
  ret void
}

; ASM-LABEL: call_ext:
; ASM: lui{{.*}}ext
; ASM: addi32{{.*}}ext
; ASM: jalr
