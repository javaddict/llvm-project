; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — legalizer owns two-bank va_arg / va_start / va_copy.
;
; REGRESSION TEST: VASTART/VACOPY/VAARG_* must not survive as ExpandPseudos
; work. G_VASTART stores the 5×i32 list from save-area frame indices
; (RISCVLegalizerInfo.cpp:812). G_VAARG splits reg vs stack overflow
; (AArch64LegalizerInfo.cpp:2158 plus Haydn two-bank CFG). llvm.vacopy
; copies 5 words (AArch64LegalizerInfo.cpp:1706).
;
; If ExpandPseudos re-owns these, LEG will show G_VAARG/G_VASTART/vacopy
; or the VAARG_* / VASTART / VACOPY pseudos after ISel.

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

; LEG-LABEL: name: vaarg_i32_legalizer
; LEG-NOT: G_VAARG
; LEG-NOT: VAARG_I32
; LEG: G_LOAD
; LEG: G_ICMP
; LEG: G_SELECT
; ASM-LABEL: vaarg_i32_legalizer:
; ASM: ld32
define i32 @vaarg_i32_legalizer(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}

; LEG-LABEL: name: vaarg_i64_legalizer
; LEG-NOT: G_VAARG
; LEG-NOT: VAARG_I64
; LEG: G_LOAD
; LEG: G_SELECT
; ASM-LABEL: vaarg_i64_legalizer:
; ASM: {{ld64|ld32}}
define i64 @vaarg_i64_legalizer(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  ret i64 %v
}

; LEG-LABEL: name: vastart_legalizer
; LEG-NOT: G_VASTART
; LEG-NOT: VASTART
; LEG: G_FRAME_INDEX
; LEG: G_STORE
; ASM-LABEL: vastart_legalizer:
; ASM: st32
define void @vastart_legalizer(i32 %fixed, ...) {
  %ap = alloca [5 x i32]
  call void @llvm.va_start(ptr %ap)
  call void @llvm.va_end(ptr %ap)
  ret void
}

; LEG-LABEL: name: vacopy_legalizer
; LEG-NOT: vacopy
; LEG-NOT: VACOPY
; LEG: G_LOAD
; LEG: G_STORE
; ASM-LABEL: vacopy_legalizer:
; ASM: ld32
; ASM: st32
define void @vacopy_legalizer(ptr %dst, ptr %src) {
  call void @llvm.va_copy(ptr %dst, ptr %src)
  ret void
}
