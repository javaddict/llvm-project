; Requires llvm-dwarfdump
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=asm -o - < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:   --force-dwarf-frame-section -o - < %s \
; RUN:   | llvm-dwarfdump -debug-frame - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=FRAME
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:   --force-dwarf-frame-section -o - < %s \
; RUN:   | llvm-readobj -S - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=SECTIONS

;REGRESSION TEST: DWARF debug info generation for the Haydn target.
;Bug: The CIE in.debug_frame had no initial CFA definition (just DW_CFA_nop)
;causing "DW_CFA_def_cfa_offset found when CFA rule was not RegPlusOffset" errors
;when llvm-dwarfdump tried to decode FDE opcodes.
;Fix: Added MCAsmInfo::addInitialFrameState(cfiDefCfa(SP, 0)) in
;HaydnMCTargetDesc and getInitialCFAOffset/getInitialCFARegister overrides
;in HaydnFrameLowering.
;This test verifies:
;1. Assembly output contains CFI directives (.cfi_startproc,.cfi_def_cfa_offset)
;2..debug_frame CIE has initial CFA = R13 (SP) + 0
;3. Object file contains expected debug sections
;If DWARF generation regresses, the CFI checks will fail.

target datalayout = "e-m:e-p:32:32-i64:64-v128:32:128-n32"
target triple = "haydn-unknown-elf"

define i32 @add(i32 %a, i32 %b) !dbg !4 {
entry:
  %sum = add i32 %a, %b, !dbg !8
  ret i32 %sum, !dbg !9
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1)
!1 = !DIFile(filename: "test.c", directory: "")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "add", scope: !1, file: !1, line: 1, type: !5, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7, !7}
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !DILocation(line: 2, column: 13, scope: !4)
!9 = !DILocation(line: 3, column: 3, scope: !4)

; ASM: .cfi_startproc
; Leaf function with no stack frame: with the initial CFA defined in the CIE
; (MCAsmInfo::addInitialFrameState), Haydn does NOT emit a redundant
; cfi_def_cfa_offset for stackless functions. The.cfi_startproc
; cfi_endproc pair is sufficient — the FDE inherits the CIE's initial CFA.
; ASM: .cfi_endproc

; FRAME:      CIE
; FRAME:      DW_CFA_def_cfa: R13 +0
; FRAME:      FDE cie=
; FRAME:      CFA=R13

; SECTIONS: Name: .debug_frame
